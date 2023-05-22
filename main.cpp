/*

     download samples from a Roche accuchek device using libusb

     compile with something along the lines of:

         c++ -std=c++17 -I. -o accuchek main.cpp log.cpp -lusb-1.0

 */

// stuff we need
#include <log.h>
#include <string>
#include <thread>
#include <time.h>
#include <vector>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <algorithm>
#include <inttypes.h>
#include <unordered_map>
#include <libusb-1.0/libusb.h>

// config key value pair map
using Config = std::unordered_map<
    std::string,
    std::string
>;

// globals
static Config g_config;
static FILE *g_output = 0;
static auto g_lineCount = 0;
static auto g_firstLine = true;

/*
    proprietary roche protocol constants, copied from:

        https://github.com/tidepool-org/uploader/tree/master/lib/drivers/roche

    these seem to be from the "Continua Health Alliance standard (ISO/IEEE 11073)"

    for the morbidly curious, see:
        https://en.wikipedia.org/wiki/Continua_Health_Alliance
        https://github.com/signove/antidote
        http://11073.org

 */

static constexpr uint16_t kAPDU_TYPE_ASSOCIATION_REQUEST =             0xE200;
static constexpr uint16_t kAPDU_TYPE_ASSOCIATION_RESPONSE =            0xE300;
static constexpr uint16_t kAPDU_TYPE_ASSOCIATION_RELEASE_REQUEST =     0xE400;
static constexpr uint16_t kAPDU_TYPE_ASSOCIATION_RELEASE_RESPONSE =    0xE500;
static constexpr uint16_t kAPDU_TYPE_ASSOCIATION_ABORT =               0xE600;
static constexpr uint16_t kAPDU_TYPE_PRESENTATION_APDU =               0xE700;

static constexpr uint16_t kDATA_ADPU_INVOKE_GET =                      0x0103;
static constexpr uint16_t kDATA_ADPU_INVOKE_CONFIRMED_ACTION =         0x0107;
static constexpr uint16_t kDATA_ADPU_RESPONSE_CONFIRMED_EVENT_REPORT = 0x0201;
static constexpr uint16_t kDATA_ADPU_RESPONSE_GET =                    0x0203;
static constexpr uint16_t kDATA_ADPU_RESPONSE_CONFIRMED_ACTION =       0x0207;

static constexpr uint16_t kEVENT_TYPE_MDC_NOTI_CONFIG =                0x0D1C;
static constexpr uint16_t kEVENT_TYPE_MDC_NOTI_SEGMENT_DATA =          0x0D21;

static constexpr uint16_t kACTION_TYPE_MDC_ACT_SEG_GET_INFO =          0x0C0D;
static constexpr uint16_t kACTION_TYPE_MDC_ACT_SEG_GET_ID_LIST =       0x0C1E;
static constexpr uint16_t kACTION_TYPE_MDC_ACT_SEG_TRIG_XFER =         0x0C1C;
static constexpr uint16_t kACTION_TYPE_MDC_ACT_SEG_SET_TIME =          0x0C17;

#define MDC_LIST                                \
  x(MDC_MOC_VMO_METRIC, 4)                      \
  x(MDC_MOC_VMO_METRIC_ENUM, 5)                 \
  x(MDC_MOC_VMO_METRIC_NU, 6)                   \
  x(MDC_MOC_VMO_METRIC_SA_RT, 9)                \
  x(MDC_MOC_SCAN, 16)                           \
  x(MDC_MOC_SCAN_CFG, 17)                       \
  x(MDC_MOC_SCAN_CFG_EPI, 18)                   \
  x(MDC_MOC_SCAN_CFG_PERI, 19)                  \
  x(MDC_MOC_VMS_MDS_SIMP, 37)                   \
  x(MDC_MOC_VMO_PMSTORE, 61)                    \
  x(MDC_MOC_PM_SEGMENT, 62)                     \
  x(MDC_ATTR_CONFIRM_MODE, 2323)                \
  x(MDC_ATTR_CONFIRM_TIMEOUT, 2324)             \
  x(MDC_ATTR_TRANSPORT_TIMEOUT, 2694)           \
  x(MDC_ATTR_ID_HANDLE, 2337)                   \
  x(MDC_ATTR_ID_INSTNO, 2338)                   \
  x(MDC_ATTR_ID_LABEL_STRING, 2343)             \
  x(MDC_ATTR_ID_MODEL, 2344)                    \
  x(MDC_ATTR_ID_PHYSIO, 2347)                   \
  x(MDC_ATTR_ID_PROD_SPECN, 2349)               \
  x(MDC_ATTR_ID_TYPE, 2351)                     \
  x(MDC_ATTR_METRIC_STORE_CAPAC_CNT, 2369)      \
  x(MDC_ATTR_METRIC_STORE_SAMPLE_ALG, 2371)     \
  x(MDC_ATTR_METRIC_STORE_USAGE_CNT, 2372)      \
  x(MDC_ATTR_MSMT_STAT, 2375)                   \
  x(MDC_ATTR_NU_ACCUR_MSMT, 2378)               \
  x(MDC_ATTR_NU_CMPD_VAL_OBS, 2379)             \
  x(MDC_ATTR_NU_VAL_OBS, 2384)                  \
  x(MDC_ATTR_NUM_SEG, 2385)                     \
  x(MDC_ATTR_OP_STAT, 2387)                     \
  x(MDC_ATTR_POWER_STAT, 2389)                  \
  x(MDC_ATTR_SA_SPECN, 2413)                    \
  x(MDC_ATTR_SCALE_SPECN_I16, 2415)             \
  x(MDC_ATTR_SCALE_SPECN_I32, 2416)             \
  x(MDC_ATTR_SCALE_SPECN_I8, 2417)              \
  x(MDC_ATTR_SCAN_REP_PD, 2421)                 \
  x(MDC_ATTR_SEG_USAGE_CNT, 2427)               \
  x(MDC_ATTR_SYS_ID, 2436)                      \
  x(MDC_ATTR_SYS_TYPE, 2438)                    \
  x(MDC_ATTR_TIME_ABS, 2439)                    \
  x(MDC_ATTR_TIME_BATT_REMAIN, 2440)            \
  x(MDC_ATTR_TIME_END_SEG, 2442)                \
  x(MDC_ATTR_TIME_PD_SAMP, 2445)                \
  x(MDC_ATTR_TIME_REL, 2447)                    \
  x(MDC_ATTR_TIME_STAMP_ABS, 2448)              \
  x(MDC_ATTR_TIME_STAMP_REL, 2449)              \
  x(MDC_ATTR_TIME_START_SEG, 2450)              \
  x(MDC_ATTR_TX_WIND, 2453)                     \
  x(MDC_ATTR_UNIT_CODE, 2454)                   \
  x(MDC_ATTR_UNIT_LABEL_STRING, 2457)           \
  x(MDC_ATTR_VAL_BATT_CHARGE, 2460)             \
  x(MDC_ATTR_VAL_ENUM_OBS, 2462)                \
  x(MDC_ATTR_TIME_REL_HI_RES, 2536)             \
  x(MDC_ATTR_TIME_STAMP_REL_HI_RES, 2537)       \
  x(MDC_ATTR_DEV_CONFIG_ID, 2628)               \
  x(MDC_ATTR_MDS_TIME_INFO, 2629)               \
  x(MDC_ATTR_METRIC_SPEC_SMALL, 2630)           \
  x(MDC_ATTR_SOURCE_HANDLE_REF, 2631)           \
  x(MDC_ATTR_SIMP_SA_OBS_VAL, 2632)             \
  x(MDC_ATTR_ENUM_OBS_VAL_SIMP_OID, 2633)       \
  x(MDC_ATTR_ENUM_OBS_VAL_SIMP_STR, 2634)       \
  x(MDC_REG_CERT_DATA_LIST, 2635)               \
  x(MDC_ATTR_NU_VAL_OBS_BASIC, 2636)            \
  x(MDC_ATTR_PM_STORE_CAPAB, 2637)              \
  x(MDC_ATTR_PM_SEG_MAP, 2638)                  \
  x(MDC_ATTR_PM_SEG_PERSON_ID, 2639)            \
  x(MDC_ATTR_SEG_STATS, 2640)                   \
  x(MDC_ATTR_SEG_FIXED_DATA, 2641)              \
  x(MDC_ATTR_SCAN_HANDLE_ATTR_VAL_MAP, 2643)    \
  x(MDC_ATTR_SCAN_REP_PD_MIN, 2644)             \
  x(MDC_ATTR_ATTRIBUTE_VAL_MAP, 2645)           \
  x(MDC_ATTR_NU_VAL_OBS_SIMP, 2646)             \
  x(MDC_ATTR_PM_STORE_LABEL_STRING, 2647)       \
  x(MDC_ATTR_PM_SEG_LABEL_STRING, 2648)         \
  x(MDC_ATTR_TIME_PD_MSMT_ACTIVE, 2649)         \
  x(MDC_ATTR_SYS_TYPE_SPEC_LIST, 2650)          \
  x(MDC_ATTR_METRIC_ID_PART, 2655)              \
  x(MDC_ATTR_ENUM_OBS_VAL_PART, 2656)           \
  x(MDC_ATTR_SUPPLEMENTAL_TYPES, 2657)          \
  x(MDC_ATTR_TIME_ABS_ADJUST, 2658)             \
  x(MDC_ATTR_CLEAR_TIMEOUT, 2659)               \
  x(MDC_ATTR_TRANSFER_TIMEOUT, 2660)            \
  x(MDC_ATTR_ENUM_OBS_VAL_SIMP_BIT_STR, 2661)   \
  x(MDC_ATTR_ENUM_OBS_VAL_BASIC_BIT_STR, 2662)  \
  x(MDC_ATTR_METRIC_STRUCT_SMALL, 2675)         \
  x(MDC_ATTR_NU_CMPD_VAL_OBS_SIMP, 2676)        \
  x(MDC_ATTR_NU_CMPD_VAL_OBS_BASIC, 2677)       \
  x(MDC_ATTR_ID_PHYSIO_LIST, 2678)              \
  x(MDC_ATTR_SCAN_HANDLE_LIST, 2679)            \
  x(MDC_ATTR_TIME_BO, 2689)                     \
  x(MDC_ATTR_TIME_STAMP_BO, 2690)               \
  x(MDC_ATTR_TIME_START_SEG_BO, 2691)           \
  x(MDC_ATTR_TIME_END_SEG_BO, 2692)             \

// all the MDC_* constants in one big enum
enum MDC_ENUM {
    #define x(a, b) k##a = b,
        MDC_LIST
    #undef x
};

// load config file
static auto loadConfig() {
auto fp = fopen("config.txt", "r");
  if(0!=fp) {
    size_t len = 0;
    char *line = 0;
    while(1) {

      auto nbRead = getline(&line, &len, fp);
      if(nbRead<0) {
        break;
      }
      if(0==nbRead) {
        continue;
      }

      auto p = line;
      char *firstSep = 0;
      char *secondSep = 0;
      char *secondFirst = 0;
      auto end = (nbRead + p);
      while(p<end) {
        auto c = p[0];
        auto validChar = (
          ('0'<=c && c<='9')  ||
          ('A'<=c && c<='Z')  ||
          ('a'<=c && c<='z')  ||
          ('_'==c)
        );
        if(false==validChar) {
          if(0==firstSep) {
            firstSep = p;
          } else {
            if(0==secondSep && 0!=secondFirst) {
              secondSep = p;
            }
          }
        } else {
          if(0!=firstSep) {
            if(0==secondFirst) {
              secondFirst = p;
            }
          }
        }
        ++p;
      }
      if(0==firstSep || 0==secondFirst || 0==secondSep) {
        continue;
      }

      auto key = std::string(line, firstSep);
      auto val = std::string(secondFirst, secondSep);
      g_config[key] = val;
    }
    if(0!=line) {
      free(line);
    }
    fclose(fp);
  }
}

// get the name of a specific MDC_* constant as a string
static auto findKeyByValue(
    uint16_t value
) {
    #define x(a, b) if((b)==value) return #a;
        MDC_LIST
    #undef x
    return (const char *)0;
};

// canonical hexdump of a buffer
static auto hexDump(
    const uint8_t *buffer,
    uint32_t size
) {
    auto i = 0;
    while(i<size) {
        auto e = (16 +i );
        for(int j=i; j<e; ++j) {
            if(j<size) {
                printf("%02X ", buffer[j]);
            } else {
                putchar(' ');
                putchar(' ');
                putchar(' ');
            }
        }
        putchar(' ');
        putchar(' ');
        putchar(' ');
        for(int j=i; j<e; ++j) {
            if(j<size) {
                auto c = buffer[j];
                putchar(isprint(c) ? c : '.');
            }
        }
        putchar('\n');
        i = e;
    }
}

// canonical hexdump of a buffer with header
static auto hexDumpWithHeader(
    const char *bufferName,
    const uint8_t *buffer,
    uint32_t size
) {
    LOG_NFO(
        "hexdump of buffer:\n\nBUFFER START \"%s\" size=%d (0x%x) ===============================================",
        bufferName,
        (int)size,
        (int)size
    );
    hexDump(buffer, size);
    printf("BUFFER END ============================================================================================\n\n");
}

// write big endian 16bit int to buffer and shift ptr
static auto be16(
    uint8_t *&p,
    uint16_t v
) {
    p[0] = (v >> 8) & 0xFF;
    p[1] = (v >> 0) & 0xFF;
    p += 2;
}

// read big endian 16bit int to buffer and shift ptr
static auto be16r(
    const uint8_t *p,
    size_t &offset
) {
    auto hi = p[0 + offset];
    auto lo = p[1 + offset];
    offset += 2;
    return (((uint16_t)hi)<<8) | lo;
}

// write big endian 32bit int to buffer and shift ptr
static auto be32(
    uint8_t *&p,
    uint32_t v
) {
    p[0] = (v >> 24) & 0xFF;
    p[1] = (v >> 16) & 0xFF;
    p[2] = (v >>  8) & 0xFF;
    p[3] = (v >>  0) & 0xFF;
    p += 4;
}

// read big endian 32bit int to buffer and shift ptr
static auto be32r(
    const uint8_t *p,
    size_t &offset
) {
    uint32_t p0 = p[0 + offset];
    uint32_t p1 = p[1 + offset];
    uint32_t p2 = p[2 + offset];
    uint32_t p3 = p[3 + offset];
    offset += 4;

    return (
        (p0 << 24)  |
        (p1 << 16)  |
        (p2 <<  8)  |
        (p3 <<  0)
    );
}

// a usb device (only things about the device we actually need)
struct USBDevice {

    // data
    libusb_device *dev;
    uint16_t vendorId;
    uint16_t productId;
    std::string vendor;
    std::string product;
    uint8_t sndEndPoint;
    uint8_t rcvEndPoint;
    uint8_t configValue;
    uint8_t interfaceNumber;
    uint8_t alternateSetting;
    libusb_device_handle *devHandle;

    // constructor
    USBDevice(
        libusb_device *_dev,
        uint16_t _vendorId,
        uint16_t _productId,
        const char *_vendor,
        const char *_product,
        uint8_t _sndEndPoint,   // NB: used to write _to_ device _from_ host
        uint8_t _rcvEndPoint,   // NB: used to read _from_ device _to_ host
        const libusb_config_descriptor *cfg,
        const libusb_interface_descriptor *altSetting
    )
        :   dev(_dev),
            vendorId(_vendorId),
            productId(_productId),
            vendor(_vendor),
            product(_product),
            sndEndPoint(_sndEndPoint),
            rcvEndPoint(_rcvEndPoint),
            configValue(cfg->bConfigurationValue),
            interfaceNumber(altSetting->bInterfaceNumber),
            alternateSetting(altSetting->bAlternateSetting),
            devHandle(0)
    {
        // increase refcount on libusb device handle
        libusb_ref_device(dev);
    }

    // copy constructor
    USBDevice(
        const USBDevice &rhs
    )
        :   dev(rhs.dev),
            vendorId(rhs.vendorId),
            productId(rhs.productId),
            vendor(rhs.vendor),
            product(rhs.product),
            sndEndPoint(rhs.sndEndPoint),
            rcvEndPoint(rhs.rcvEndPoint),
            configValue(rhs.configValue),
            interfaceNumber(rhs.interfaceNumber),
            alternateSetting(rhs.alternateSetting),
            devHandle(rhs.devHandle)
    {
        // increase refcount on libusb device handle
        libusb_ref_device(dev);
    }

    // destructor
    ~USBDevice() {
        // decrease refcount on libusb device handle
        libusb_unref_device(dev);
    }

    // show device specs
    auto show(
        const char *msg
    ) {
        LOG_NFO(
            "%s:\n"
            "\n"
            "    bus number:    %d\n"
            "    dev address:   %d\n"
            "    cfg value:     %d\n"
            "    alt setting:   %d\n"
            "    alt interface number: %d\n"
            "    vendor:        (0x%04x) %s\n"
            "    product:       (0x%04x) %s\n"
            "    sndEndPnt:     %d\n"
            "    rcvEndPnt:     %d\n"
            ,
            msg,
            libusb_get_bus_number(dev),
            libusb_get_device_address(dev),
            (int)configValue,
            (int)alternateSetting,
            (int)interfaceNumber,
            (int)vendorId,
            vendor.c_str(),
            (int)productId,
            product.c_str(),
            sndEndPoint,
            rcvEndPoint
        );
    }
};

/*


    syslog excerpt from an actual accuchek connecting:

        May 26 14:35:34 machine kernel: [598051.987118] usb 3-1: new full-speed USB device number 59 using xhci_hcd
        May 26 14:35:34 machine kernel: [598052.141001] usb 3-1: New USB device found, idVendor=173a, idProduct=21d5, bcdDevice= 1.00
        May 26 14:35:34 machine kernel: [598052.141005] usb 3-1: New USB device strings: Mfr=1, Product=2, SerialNumber=0
        May 26 14:35:34 machine kernel: [598052.141007] usb 3-1: Product: ACCU-CHEK Guide
        May 26 14:35:34 machine kernel: [598052.141008] usb 3-1: Manufacturer: Roche
        May 26 14:35:34 machine mtp-probe: checking bus 3, device 59: "/sys/devices/pci0000:00/0000:00:14.0/usb3/3-1"
        May 26 14:35:34 machine mtp-probe: bus: 3, device: 59 was not an MTP device
        May 26 14:35:34 machine mtp-probe: checking bus 3, device 59: "/sys/devices/pci0000:00/0000:00:14.0/usb3/3-1"
        May 26 14:35:34 machine mtp-probe: bus: 3, device: 59 was not an MTP device

    dump from a generic usb prober on the accuchek:

        /dev/char/189:314
        /dev/bus/usb/003/059
        idVendor=173a
        idProduct=21d5

        examining USB device
            device has 1 configs
                examining config 0
                config has 1 interfaces
                    examining interface 0
                    interface has 1 altSettings
                        examining altsetting 0
                        altSetting has 2 endpoints
                            examining endpoint 0
                            endpoint has packet size 64
                            endpoint isInterruptType=0
                            examining endpoint 1
                            endpoint has packet size 64
                            endpoint isInterruptType=0

        ==> found valid accuchek device:
            bus number:    3
            dev address:   60
            cfg value:     1
            alt setting:   0
            alt interface number: 0
            vendor:        (0x173a) Roche
            product:       (0x21d5) ACCU-CHEK Guide
            sndEndPnt:     1
            rcvEndPnt:     129

    lsusb -v excerpt:

        Bus 003Device 118: ID 173a:21d5 Roche
        Device Descriptor:
          bLength                18
          bDescriptorType         1
          bcdUSB               2.00
          bDeviceClass            0
          bDeviceSubClass         0
          bDeviceProtocol         0
          bMaxPacketSize0        64
          idVendor           0x173a Roche
          idProduct          0x21d5
          bcdDevice            1.00
          iManufacturer           1 Roche
          iProduct                2 ACCU-CHEK Guide
          iSerial                 0
          bNumConfigurations      1
          Configuration Descriptor:
            bLength                 9
            bDescriptorType         2
            wTotalLength       0x0052
            bNumInterfaces          1
            bConfigurationValue     1
            iConfiguration          3 FS configuration
            bmAttributes         0x80
              (Bus Powered)
            MaxPower              100mA
            Interface Descriptor:
              bLength                 9
              bDescriptorType         4
              bInterfaceNumber        0
              bAlternateSetting       0
              bNumEndpoints           2
              bInterfaceClass        15
              bInterfaceSubClass      0
              bInterfaceProtocol      0
              iInterface              4 Personal Healthcare Device Class
              ** UNRECOGNIZED:  04 20 02 00
              ** UNRECOGNIZED:  06 30 00 01 11 10
              Endpoint Descriptor:
                bLength                 7
                bDescriptorType         5
                bEndpointAddress     0x81  EP 1 IN
                bmAttributes            2
                  Transfer Type            Bulk
                  Synch Type               None
                  Usage Type               Data
                wMaxPacketSize     0x0040  1x 64 bytes
                bInterval               0
                DEVICE CLASS:  04 21 01 08
                ** UNRECOGNIZED:  10 22 52 6f 63 68 65 20 50 48 44 43 20 54 78 00
              Endpoint Descriptor:
                bLength                 7
                bDescriptorType         5
                bEndpointAddress     0x01  EP 1 OUT
                bmAttributes            2
                  Transfer Type            Bulk
                  Synch Type               None
                  Usage Type               Data
                wMaxPacketSize     0x0040  1x 64 bytes
                bInterval               0
                DEVICE CLASS:  04 21 01 08
                ** UNRECOGNIZED:  10 22 52 6f 63 68 65 20 50 48 44 43 20 52 78 00

    endpoints:

        bEndpointAddress     0x81  EP 1 IN      (device to host)
        bEndpointAddress     0x01  EP 1 OUT     (host to device)
        rcvEndPnt:     129
        sndEndPnt:     1


*/

// open an accuchek USB device and download data from it
static auto operateDevice(
    USBDevice &usbDevice
) {
    /*
       much of what follows was directly reverse-engineered from the highly
       unportable (only works in effing Chrome) javascript code found here:

           https://github.com/tidepool-org/uploader/tree/master/lib/drivers/roche

       and backported to raw libusb. The original author of the tidepool code
       likely had access to a manual documenting the protocol.

    */

    // open device
    auto dev = usbDevice.dev;
    libusb_device_handle *devHandle = 0;
    auto fail0 = libusb_open(dev, &devHandle);
    if(fail0) {
        LOG_WRN("libusb_open failed on selected device -- giving up");
        exit(1);
    }
    usbDevice.devHandle = devHandle;

    // detach whatever kernel driver may have been attached to it
    libusb_detach_kernel_driver(
        devHandle,
        usbDevice.interfaceNumber
    );

    // load the configuration chosen during detection phase
    {
        auto fail =libusb_set_configuration(
            devHandle,
            usbDevice.configValue
        );
        if(fail<0) {
            LOG_WRN("failed to configure selected device -- giving up");
            exit(1);
        }
    }

    // claim interface
    {
        auto fail = libusb_claim_interface(
            devHandle,
            usbDevice.interfaceNumber
        );
        if(fail<0) {
            LOG_WRN("failed to claim interface -- giving up");
            exit(1);
        }
    }

    // set alt setting chosen during detection phase on interface
    {
        auto fail = libusb_set_interface_alt_setting(
            devHandle,
            usbDevice.interfaceNumber,
            usbDevice.alternateSetting
        );
        if(fail<0) {
            LOG_WRN("failed to set alt setting -- giving up");
            exit(1);
        }
    }

    // make some noise
    LOG_NFO("using device snd endpoint = %d", usbDevice.sndEndPoint);
    LOG_NFO("using device rcv endpoint = %d\n", usbDevice.rcvEndPoint);

    // things we're going to need whole talking to the device
    #define BUFFER_SIZE size_t(1024)
    uint8_t buffer[BUFFER_SIZE];
    uint16_t invokeId = -1;
    int phaseIndex = 1;

    // lambda to send out a message via bulk transfer
    auto bulkOut = [&](
        const char *msgName,
        size_t len
    ) {

        // make some noise
        printf("\n");
        LOG_NFO(
            "phase %d: sending message %s",
            (int)phaseIndex,
            msgName
        );

        // show hex dump of the outgoing message content
        hexDumpWithHeader(
            msgName,
            buffer,
            len
        );

        // send the message via a bulk transfer on the send endpoint
        int bytesWritten = -1;
        auto fail = libusb_bulk_transfer(
            devHandle,              // device
            usbDevice.sndEndPoint,  // endpoint
            buffer,                 // content
            len,                    // content size
            &bytesWritten,          // actual number of bytes written out
            5000                    // timeout in ms
        );
        if(0!=fail || len!=bytesWritten) {
            LOG_WRN("failed to send message %s -- giving up", msgName);
            LOG_WRN("libusb error was :%s", libusb_strerror(fail));
            exit(1);
        }
        LOG_NFO(
            "successfully wrote message %s, size=%d (0x%x):",
            msgName,
            (int)len,
            (int)len
        );

        // move on to next phase
        ++phaseIndex;
    };

    // lambda to receive a message via bulk transfer
    auto bulkIn = [&](
        const char *msgName,
        size_t maxLen = BUFFER_SIZE
    ) {
        // make some noise
        printf("\n");
        LOG_NFO(
            "phase %d: receiving message %s",
            (int)phaseIndex,
            msgName
        );

        // read data
        int bytesRead = 0;
        auto fail = libusb_bulk_transfer(
            devHandle,              // device
            usbDevice.rcvEndPoint,  // endpoint
            buffer,                 // content
            maxLen,                 // max content length
            &bytesRead,             // actual number of bytes read in
            5000                    // timeout in ms
        );
        if(0!=fail) {
            LOG_WRN("failed to receive message %s -- giving up", msgName);
            LOG_WRN("libusb error was :%s", libusb_strerror(fail));
            exit(1);
        }

        // we don't care about the content, but dump it anyways
        LOG_NFO(
            "successfully read message \"%s\" from device",
            msgName
        );

        // show hex dump of the message content
        hexDumpWithHeader(
            msgName,
            buffer,
            bytesRead
        );

        // move on to next phase
        ++phaseIndex;

        // return number of bytes read
        return bytesRead;
    };

    // read and update invokeId from response buffer
    auto updateInvokeId = [&](
        size_t offset = 6
    ) {
        invokeId = be16r(buffer, offset);
        LOG_NFO(
            "invokeId after phase %d is: %d",
            (int)phaseIndex,
            (int)invokeId
        );
    };

    // protocol step: do a control transfer in
    {
        #define PHASE_1 "initial control transfer in"
        LOG_NFO("phase 1: " PHASE_1);

        auto bytesRead = libusb_control_transfer(
            devHandle,
            (
                LIBUSB_REQUEST_TYPE_STANDARD |
                LIBUSB_RECIPIENT_DEVICE      |
                LIBUSB_ENDPOINT_IN
            ),
            LIBUSB_REQUEST_GET_STATUS,
            0,
            0,
            buffer,
            2,
            5000
        );
        if(bytesRead<=0) {
            LOG_WRN("failed " PHASE_1 " -- giving up");
            LOG_WRN("libusb error was :%s", libusb_strerror(bytesRead));
            exit(1);
        }
        LOG_NFO(PHASE_1 " succeeded");
        hexDumpWithHeader(
            PHASE_1,
            buffer,
            bytesRead
        );
        ++phaseIndex;
    }

    // protocol step: wait for pairing request from the device
    {
        bulkIn(
            "pairing request",
            64
        );
    }

    // protocol step: send a pairing confirmation to the device
    {
        // the message the device expects
        auto p = buffer;
        memset(buffer, 0, sizeof(buffer));
        be16(p, kAPDU_TYPE_ASSOCIATION_RESPONSE); // msg type
        be16(p,         44);                      // length (p, excludes initial 4 bytes)
        be16(p,     0x0003);                      // accepted-unknown-config
        be16(p,      20601);                      // data-proto-id
        be16(p,         38);                      // data-proto-info length
        be32(p, 0x80000002);                      // protocolVersion
        be16(p,     0x8000);                      // encoding-rules = MDER
        be32(p, 0x80000000);                      // nomenclatureVersion
        be32(p,          0);                      // functionalUnits = normal association
        be32(p, 0x80000000);                      // systemType = sys-type-manager
        be16(p,          8);                      // system-id length
        be32(p, 0x12345678);                      // system-id high
        be32(p, 0x00000000);                      // zero
        be32(p, 0x00000000);                      // zero
        be32(p, 0x00000000);                      // zero
        be16(p,     0x0000);                      // zero

        bulkOut(
            "pairing confirmation",
            (p-buffer)
        );
    };

    // protocol step: wait for config info (as a response to pairing confirm)
    {
        auto bytesRead = bulkIn("config info");
        updateInvokeId();
    }

    // lambda: find object of a given "class" in a config info response buffer
    auto getObj = [&](
        const uint8_t *buffer,
        uint16_t objRequestedClass,
        uint16_t &_objHandle
    ) {
        auto offset = size_t(24);
        auto count = be16r(buffer, offset);
        auto dummy = be16r(buffer, offset);
        LOG_NFO("got %d object in config info response", (int)count);
        for(int i=0; i<count; ++i) {
            auto objClass = be16r(buffer, offset);
            auto objHandle = be16r(buffer, offset);
            auto objAttrCount = be16r(buffer, offset);
            auto objSize = be16r(buffer, offset);
            if(0) {
                LOG_NFO(
                    "obj %d, size=%d, class=%d (%s), handle=%d",
                    (int)i,
                    (int)objSize,
                    (int)objClass,
                    findKeyByValue(objClass),
                    (int)objHandle
                );
                hexDump((offset + buffer), objSize);
            }
            if(objRequestedClass==objClass) {
                _objHandle = objHandle;
                return std::pair(
                    (offset + buffer),
                    objAttrCount
                );
            }
            offset += objSize;
        }
        return std::pair((const uint8_t*)0, 0);
    };

    // lambda: find attribute of a given "class" in a response buffer object
    auto getAttr = [&](
        const uint8_t *buffer,
        uint16_t attributeCount,
        uint16_t attrRequestedClass
    ) {

        LOG_NFO(
            "looking for attribute of class %d among %d attributes",
            (int)attrRequestedClass,
            (int)attributeCount
        );

        auto offset = size_t(0);
        for(int i=0; i<attributeCount; ++i) {
            auto attrClass = be16r(buffer, offset);
            auto attrSize = be16r(buffer, offset);
            if(0) {
                LOG_NFO(
                    "attr %d, size=%d, class=%d (%s)",
                    (int)i,
                    (int)attrSize,
                    (int)attrClass,
                    findKeyByValue(attrClass)
                );
                hexDump((offset + buffer), attrSize);
            }
            if(attrRequestedClass==attrClass) {
                return std::pair(
                    (offset + buffer),
                    attrSize
                );
            }
            offset += attrSize;
        }
        return std::pair((const uint8_t*)0, 0);
    };

    // get attribute list from a buffer
    auto getAttrList = [&](
        const uint8_t *buffer,
        uint16_t &attrCnt,
        uint16_t &attrLen,
        size_t &offset
    ) {

        LOG_NFO("parsing attr list");

        offset = 14;
        attrCnt = be16r(buffer, offset);
        attrLen = be16r(buffer, offset);

        LOG_NFO(
            "got attrcnt=%d, len=%d, offset=%d",
            (int)attrCnt,
            (int)attrLen,
            (int)offset
        );
    };

    // use lambdas to parse config and extract useful info
    LOG_NFO("parsing config info response");
    uint16_t pmStoreHandle = 0;
    auto pmStore = getObj(buffer, kMDC_MOC_VMO_PMSTORE, pmStoreHandle);
    if(0==pmStore.first) {
        LOG_WRN("failed to parse config buffer for pmStore -- giving up");
        exit(1);
    }
    LOG_NFO(
        "found pmStore of size %d, handle = %d",
        (int)pmStore.second,
        (int)pmStoreHandle
    );

    auto nbSegments = getAttr(
        pmStore.first,
        pmStore.second,
        kMDC_ATTR_NUM_SEG
    );
    if(0==nbSegments.first) {
        LOG_WRN("failed to parse pmStore for nbSegments -- giving up");
        exit(1);
    }
    LOG_NFO("successfully found \"nbSegments\" oject");

    uint16_t nbSegs = -1;
    {
        size_t o = 0;
        nbSegs = be16r(nbSegments.first, o);
        LOG_NFO("data is split into %d segments", (int)nbSegs);
    }

    // protocol step: send "config well received" response
    {
        auto p = buffer;
        memset(buffer, 0, sizeof(buffer));
        be16(p, kAPDU_TYPE_PRESENTATION_APDU);               // msg type
        be16(p,     22);                                     // length
        be16(p,     20);                                     // octet stringlength
        be16(p, invokeId);                                   // invoke-id read from config
        be16(p, kDATA_ADPU_RESPONSE_CONFIRMED_EVENT_REPORT); //
        be16(p,     14);                                     // length
        be16(p,      0);                                     // obj-handle = 0
        be32(p,      0);                                     // currentTime = 0
        be16(p, kEVENT_TYPE_MDC_NOTI_CONFIG);                // event-type
        be16(p,      4);                                     // length
        be16(p, 0x4000);                                     // config-report-id = extended-config-start
        be16(p,      0);                                     // config-result = accepted-config

        bulkOut(
            "config received confirmation",
            (p-buffer)
        );
    }

    // protocol step: send MDS attribute request
    {
        auto p = buffer;
        memset(buffer, 0, sizeof(buffer));
        be16(p, kAPDU_TYPE_PRESENTATION_APDU); // msg type
        be16(p,     14);                       // length
        be16(p,     12);                       // octet stringlength
        be16(p, (1+invokeId));                 // invoke-id from config
        be16(p, kDATA_ADPU_INVOKE_GET);        //
        be16(p,      6);                       // length
        be16(p,      0);                       // obj-handle = 0
        be32(p,      0);                       // currentTime = 0

        bulkOut(
            "MDS attribute request",
            (p-buffer)
        );
    }

    // protocol step: read MDS attr answer
    {
        auto bytesRead = bulkIn("MDS attribute answer");
        updateInvokeId();

        // check for abort
        size_t o = 0;
        auto retCode = be16r(buffer, o);
        if(kAPDU_TYPE_ASSOCIATION_ABORT==retCode) {
            LOG_WRN("received association abort request -- giving up");
            exit(1);
        }
    }

    // parse device information out of MDS attr answer
    if(0) {

        // we don't really need any of this
        size_t offset = -1;
        uint16_t attrLen = -1;
        uint16_t attrCount = -1;
        getAttrList(
            buffer,
            attrCount,
            attrLen,
            offset
        );

        // get device exact model
        getAttr(
            offset + buffer,
            attrCount,
            kMDC_ATTR_ID_MODEL
        );

        // get device productions specs
        getAttr(
            offset + buffer,
            attrCount,
            kMDC_ATTR_ID_PROD_SPECN
        );

        // get device internal time
        getAttr(
            offset + buffer,
            attrCount,
            kMDC_ATTR_TIME_ABS
        );
    }

    // protocol step: send action request
    {
        LOG_NFO("8: send action request");
        auto p = buffer;
        memset(buffer, 0, sizeof(buffer));
        be16(p, kAPDU_TYPE_PRESENTATION_APDU); // msg type
        be16(p,     20);                       // length
        be16(p,     18);                       // octet stringlength
        be16(p, (1+invokeId));                 // invoke-id from prev answer
        be16(p, kDATA_ADPU_INVOKE_CONFIRMED_ACTION);
        be16(p,     12);                       // length of what follows (could also be zero)
        be16(p, pmStoreHandle);                // store handle
        be16(p, kACTION_TYPE_MDC_ACT_SEG_GET_INFO);
        be16(p,      6);                       // length
        be16(p,      1);                       // all segments
        be16(p,      2);                       // length
        be16(p,      0);                       // something

        bulkOut(
            "action request",
            (p-buffer)
        );
    }

    // protocol step: read action request response
    {
        auto bytesRead = bulkIn("action request response");
        updateInvokeId();
    }

    // ----> here, the original js code sets the device time ... skip for now

    // protocol step: start request for data segments
    {
        auto p = buffer;
        memset(buffer, 0, sizeof(buffer));
        be16(p, kAPDU_TYPE_PRESENTATION_APDU); // msg type
        be16(p,     16);                       // length
        be16(p,     14);                       // octet stringlength
        be16(p, (1+invokeId));                 // invoke-id from prev answer
        be16(p, kDATA_ADPU_INVOKE_CONFIRMED_ACTION);
        be16(p,      8);                       // length of what follows (could also be zero)
        be16(p, pmStoreHandle);                // store handle
        be16(p, kACTION_TYPE_MDC_ACT_SEG_TRIG_XFER);
        be16(p,      2);                       // length
        be16(p,      0);                       // segment

        bulkOut(
            "request segments",
            (p-buffer)
        );
    }

    // step: read segment stream header answer
    {
        auto bytesRead = bulkIn("segment headers");
        updateInvokeId();

        uint16_t dataResponse = 0;
        if(22<=bytesRead) {
            size_t o = 20;
            dataResponse = be16r(buffer, o);
        }

        if(22==bytesRead && 0!=dataResponse) {
            if(3==dataResponse) {
                LOG_WRN("empty data segment -- giving up");
            } else {
                LOG_NFO(
                    "error retrieving data, code = %d",
                    (int)dataResponse
                );
            }
            exit(1);
        }

        uint16_t headerValue = -1;
        if(16<=bytesRead) {
            size_t o = 14;
            headerValue = be16r(buffer, o);
        }

        if(
            (bytesRead < 22) ||
            (kACTION_TYPE_MDC_ACT_SEG_TRIG_XFER!=headerValue)
        ) {
            LOG_WRN("unexpected / incorrect answer packet -- giving up");
            exit(1);
        }
    }

    // step: read segments one by one
    int segIndex = 0;
    while(true) {

        // get data and update invokeId
        auto bytesRead = bulkIn("data segment");
        auto status = buffer[32];
        updateInvokeId();

        // fish some data we need to send back in the "confirm" message
        size_t o = 22;
        auto u0 = be32r(buffer, o);
        auto u1 = be32r(buffer, o);
        auto u2 = be16r(buffer, o);

        // lambda to parse samples out of each segment
        auto parseData = [&]() {

            size_t o = 30;
            auto nbEntries = be16r(buffer, o);
            LOG_NFO("segment has %d entries", (int)nbEntries);
            o -= 2;

            for(int i=0; i<nbEntries; ++i) {

                // decode weird-ass encoding of datetime values
                auto cvt = [](
                    uint8_t x
                ) {
                    int v = -1;
                    char buf[8];
                    sprintf(buf, "%02X", x);
                    sscanf(buf, "%d", &v);
                    return v;
                };

                // load date
                auto cc = cvt(buffer[ 6 + o]);
                auto yy = cvt(buffer[ 7 + o]);
                auto mm = cvt(buffer[ 8 + o]);
                auto dd = cvt(buffer[ 9 + o]);
                auto hh = cvt(buffer[10 + o]);
                auto mn = cvt(buffer[11 + o]);

                // load value and status
                auto ro = (14 + o);
                auto vv = be16r(buffer, ro);
                auto ss = be16r(buffer, ro);
                o += 12;

                // dump sample
                LOG_NFO(
                    "sample: %02d%02d/%02d/%02d %02d:%02d => (mg/dL=%2d, mmol/L=%7.3f, status=0x%02x)",
                    cc,
                    yy,
                    mm,
                    dd,
                    hh,
                    mn,
                    vv,
                    (vv / 18.0),
                    ss
                );

                // compute epoch
                struct tm t;
                memset(&t, 0, sizeof(t));
                t.tm_min = mn;
                t.tm_hour = hh;
                t.tm_mday = dd;
                t.tm_mon = (mm-1);
                t.tm_year = ((cc*100 + yy) - 1900);
                //auto epoch = timegm(&t);
                auto epoch = timelocal(&t);

                // write sample as JSON
                if(0==ss) {
                    fprintf(
                        g_output,
                        "%s\n    { \"id\":%6d, \"epoch\":%11" PRIu64 ", \"timestamp\":\"%02d%02d/%02d/%02d %02d:%02d\", \"mg/dL\":%3d, \"mmol/L\":%10.6f }",
                        (g_firstLine ? "" : ","),
                        (int)(g_lineCount++),
                        (uint64_t)epoch,
                        (int)cc,
                        (int)yy,
                        (int)mm,
                        (int)dd,
                        (int)hh,
                        (int)mn,
                        (int)vv,
                        (vv / 18.0)
                    );
                    g_firstLine = false;
                }
            }
        };

        // parse received data segment
        parseData();

        // send "data received" ack
        {
            auto p = buffer;
            memset(buffer, 0, sizeof(buffer));
            be16(p, kAPDU_TYPE_PRESENTATION_APDU); // msg type
            be16(p,     30);                       // length
            be16(p,     28);                       // octet stringlength
            be16(p, invokeId);                     // invoke-id from prev answer
            be16(p, kDATA_ADPU_RESPONSE_CONFIRMED_EVENT_REPORT);
            be16(p,     22);                       // length of what follows (could also be zero)
            be16(p, pmStoreHandle);                // store handle
            be32(p, 0xFFFFFFFF);                   // relative time
            be16(p, kEVENT_TYPE_MDC_NOTI_SEGMENT_DATA);
            be16(p,     12);
            be32(p,     u0);
            be32(p,     u1);
            be16(p,     u2);
            be16(p, 0x0080);

            bulkOut(
                "data segment received ACK",
                (p-buffer)
            );
        }

        // bail if segment was flagged as last one in the stream
        if(0x40 & status) {
            break;
        }
    }

    // protocol step: disconnect cleanly from device
    {
        auto p = buffer;
        memset(buffer, 0, sizeof(buffer));
        be16(p, kAPDU_TYPE_ASSOCIATION_RELEASE_REQUEST); // msg type
        be16(p,      2);                       // length = 2
        be16(p, 0x0000);                       // normal release
        bulkOut("release request", (p-buffer));
        bulkIn("release confirmation");
    }

    // protocol step: close device
    LOG_NFO("closing usb device");
    libusb_close(devHandle);
}

// process one USB device and add it to the list if it matches requirements
static auto addDeviceIfAccuChek(
    std::vector<USBDevice> &validDevices,
    libusb_device *dev
) {

    // get USB device description
    libusb_device_descriptor dsc;
    auto fail = libusb_get_device_descriptor(dev, &dsc);
    if(0!=fail) {
        LOG_WRN("libusb_get_device_descriptor failed");
        return;
    }

    // ugly trick to "goto done" over declarations using a break
    struct libusb_config_descriptor *cfg = 0;
    do {

        // accuchek has one config, anything with more or less is a bust
        if(1!=dsc.bNumConfigurations) {
            LOG_NFO("not a match, too many configs to be an accuchek");
            break;
        }

        // load first config
        auto confIndex = 0;
        auto fail0 = libusb_get_config_descriptor(dev, confIndex, &cfg);
        if(0!=fail0) {
            LOG_WRN("libusb_get_device_descriptor failed");
            break;
        }

        // accuchek single config has one interface, anything with more or less is a bust
        if(1!=cfg->bNumInterfaces) {
            LOG_NFO("not a match, too many interfaces to be an accuchek");
            break;
        }

        // look at all "alt settings" for first interface
        auto interfaceIndex = 0;
        auto interface = &(cfg->interface[interfaceIndex]);

        // accuchek single config has one alt setting, anything with more or less is a bust
        if(1!=interface->num_altsetting) {
            LOG_NFO("not a match, too many alt settings to be an accuchek");
            break;
        }

        // accuchek has two endpoints, anything with more or less is a bust
        int altSettingIndex = 0;
        auto altSetting = &(interface->altsetting[altSettingIndex]);
        if(2!=altSetting->bNumEndpoints) {
            LOG_NFO("not a match, too many endpoints to be an accuchek");
            break;
        }

        // look for endpoints on altsetting that match our criteria
        auto in = 0;    // 0 is an invalid endpoint
        auto out = 0;   // 0 is an invalid endpoint
        for(auto endPointIndex=0; endPointIndex<altSetting->bNumEndpoints; ++endPointIndex) {
            // accuchek endpoints should both have a max packet size of 64
            auto endPoint = &(altSetting->endpoint[endPointIndex]);
            if(64==endPoint->wMaxPacketSize) {
                // device must be non-interrupt type
                auto attributes = (endPoint->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK);
                auto isInterrupt = (LIBUSB_TRANSFER_TYPE_INTERRUPT==attributes);
                if(false==isInterrupt) {
                    // LIBUSB_ENDPOINT_IN means endpoint is used to transfer date from the device to the host
                    auto isInput = (LIBUSB_ENDPOINT_IN & endPoint->bEndpointAddress);
                    if(isInput) {
                        in = endPoint->bEndpointAddress;
                    } else {
                        out = endPoint->bEndpointAddress;
                    }
                }
            }
        }

        // skip devices that dont have at least one input and one output
        auto ok = (0!=in && 0!=out);
        if(false==ok) {
            LOG_NFO("not a match, need at least one input endpoint and one output endpoint");
            break;
        }

        // we found a device seems to fit the bill, open it
        LOG_NFO("found a usb device that looks good, checking further by opening it");
        libusb_device_handle *devHandle = 0;
        auto fail1 = libusb_open(dev, &devHandle);
        if(fail1) {
            LOG_WRN("libusb_open failed, giving up");
            break;
        }

        // get device vendor
        char vendor[512];
        memset(vendor, 0, sizeof(vendor));
        auto r0 = libusb_get_string_descriptor_ascii(
            devHandle,
            dsc.iManufacturer,
            (uint8_t*)vendor,
            (-1+sizeof(vendor))
        );
        if(r0<0) {
            LOG_NFO("not a match, vendorId unreadable");
            libusb_close(devHandle);
            break;
        }

        // get product id
        char product[512];
        memset(product, 0, sizeof(product));
        auto r1 = libusb_get_string_descriptor_ascii(
            devHandle,
            dsc.iProduct,
            (uint8_t*)product,
            (-1+sizeof(product))
        );
        if(r1<0) {
            LOG_NFO("not a match, productId unreadable");
            libusb_close(devHandle);
            break;
        }

        auto isKeyValid = [&](
          const std::string &key
        ) {
            static const auto valid = std::string("1");
            return (0!=g_config.count(key) && valid==g_config[key]);
        };

        auto isDeviceValid = [&](
          uint32_t vendorId,
          uint32_t deviceId
        ) {
          char buffer[1024];
          snprintf(
            buffer,
            sizeof(buffer),
            "vendor_0x%04x_device_0x%04x",
            vendorId,
            deviceId
          );
          return isKeyValid(buffer);
        };

        // check that device and vendor is in list of known devices
        if(isDeviceValid(dsc.idVendor, dsc.idProduct)) {
            // we have a new valid device, add it to the list
            LOG_NFO("========> found a matching USB device");
            validDevices.emplace_back(
                dev,
                dsc.idVendor,
                dsc.idProduct,
                vendor,
                product,
                out,
                in,
                cfg,
                altSetting
            );
        } else {
            LOG_NFO(
                "nope: looks like it, but thats not the one. this device has mfgr=%s device=%s\n",
                vendor,
                product
            );
        }
        libusb_close(devHandle);
    } while(0);

    // free config data structure
done:
    libusb_free_config_descriptor(cfg);
}

// find all possible accuchek devices, pick one and download data from it
static auto findAndOperateAccuChek(
    libusb_context *libUSBContext,
    int ix = -1
) {

    // obtain a list of all USB devices in the system
    libusb_device **devices = 0;
    LOG_NFO("getting list of all USB devices in system from libusb");
    auto count = libusb_get_device_list(libUSBContext, &devices);
    LOG_NFO("found %d USB devices in system", (int)count);

    // check them one by one and add to the list if specs are a match
    std::vector<USBDevice> validDevices;
    LOG_NFO("searching for valid accuchek devices");
    for(int i=0; i<count; ++i) {
        LOG_NFO("checking if device %d is an accuchek", (int)i);
        addDeviceIfAccuChek(validDevices, devices[i]);
    }

    // clean up device list
    libusb_free_device_list(devices, 1);

    // if no devices found, bail
    if(0==validDevices.size()) {
        LOG_WRN("found no accuchek device whatsoever -- giving up");
        exit(1);
    }

    // make some noise
    LOG_NFO(
        "found altogether %d accuchek devices",
        (int)validDevices.size()
    );

    // make sure we user referes to a valid device
    if(int(validDevices.size())<=int(ix)) {
        LOG_WRN(
            "user selected device %d but only %d devices were found -- aborting",
            ix,
            (int)validDevices.size()
        );
        exit(1);
    }

    // select a specific device (first seen or as specified by user)
    auto selectedIndex = std::max(0, ix);
    auto &selectedDevice = validDevices[selectedIndex];

    // show details of selected device as gathered from libusb
    char buf[256];
    sprintf(
        buf,
        "selecting accuchek device #%d:",
        (int)selectedIndex
    );
    selectedDevice.show(buf);

    // talk to device to download data from it
    operateDevice(selectedDevice);
}

// open libusb, return handle
static auto openLibUSB() {

    LOG_NFO("opening libusb");

    // init libusb
    libusb_context *libUSBContext = 0;
    auto fail = libusb_init(&libUSBContext);
    if(0!=fail || 0==libUSBContext) {
        LOG_WRN("libusb init failure");
        exit (1);
    }

    // turn libusb debug on
    #if 0
        #if 0
            enum libusb_log_level {
                LIBUSB_LOG_LEVEL_NONE = 0,
                LIBUSB_LOG_LEVEL_ERROR = 1,
                LIBUSB_LOG_LEVEL_WARNING = 2,
                LIBUSB_LOG_LEVEL_INFO = 3,
                LIBUSB_LOG_LEVEL_DEBUG = 4,
            };
            enum libusb_option {
                    LIBUSB_OPTION_LOG_LEVEL = 0,
                    LIBUSB_OPTION_USE_USBDK = 1,
            };
        #endif
        libusb_set_option(
            libUSBContext,
            LIBUSB_OPTION_LOG_LEVEL,
            LIBUSB_LOG_LEVEL_DEBUG
        );
    #endif

    LOG_NFO("libusb opened OK");
    return libUSBContext;
}

// close libusb
static auto closeLibUSB(
    libusb_context *libUSBContext
) {
    LOG_NFO("closing libusb");
    libusb_exit(libUSBContext);
}

// entry point
int main(
    int argc,
    char *argv[]
) {

    // load config file
    loadConfig();

    // be silent unless asked to talk
    if(0!=getenv("ACCUCHEK_DBG")) {
        // unbuffer stdout/stderr
        setvbuf(stdout, 0, _IONBF, 0);
        setvbuf(stderr, 0, _IONBF, 0);
        g_output = stdout;
    } else {

        // dup stdout
        int newFD = dup(1);

        // batten down the hatches
        close(1);
        close(2);

        // fdopen dup'd stdout
        g_output = fdopen(newFD, "wb");
        fprintf(g_output, "[");
    }

    // make some noise
    LOG_NFO("starting");

    // open libusb
    auto libUSBContext = openLibUSB();

    // find and talk to one accuchek device
    findAndOperateAccuChek(
        libUSBContext,
        (1<argc ? atoi(argv[1]) : -1)
    );

    // clean up and bail
    fprintf(g_output, "\n]\n");
    closeLibUSB(libUSBContext);
    LOG_NFO("done");
    return 0;
}

