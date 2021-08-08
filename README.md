# Roche Accu-Chek Guide sample download utility

## **TL;DR:**

Linux C++-17 code to download samples from a Roche Accu-Chek Guide
blood glucose monitor using libusb

## **To compile:**

+ install libusb-1.0-dev
+ install build-essential
+ in a shell, type:

    `make`

## **To run:**

+ in a shell, type:

    `./accuchek > samples.json`

+ blood glucose levels are in file samples.json

## **What it does:**

+ scans all USB devices in the system
+ finds an Accu-Chek Guide device if there's one
+ connects to it
+ downloads all blood glucose samples
+ dumps them as JSON on stdout
+ hopefully exit gracefully

## **Of interest:**

+ This has been tested on Ubuntu 20.04. On other *nixes, YMMV.

+ This is a rough first cut, improvements via PRs are welcome.

+ Unless you enjoy futzing around with udev and the like, you
  should run the utility as root

+ Produced JSON has glucose levels in both mg/dL and mmol/L units

+ The ascii timestamps in JSON are expressed in the local device time

+ The epoch timestamps in JSON are GMT, assuming the computer running
  the utility is set to the same timezone as the Accu-Chek device.

+ The proprietary USB protocol needed to talk to the device was
  reverse-engineered from the Javascript code found here the author
  of which likely had access to the vendor documentation:

    https://github.com/tidepool-org/uploader/tree/master/lib/drivers/roche

+ The JS code has a little more functionality (eg it can set the device
  time), but as much as I can ascertain, it's not particularly portable:
  it only runs on top of Chrome, and even there, I have never really
  managed to get it to run on anything but windoze: the amount of depenencies
  you have to install to ever hope to see it run is simply frightening.

+ A number of things might go wrong with this code. When that happens:

    + disconnect device USB cable
    + kill the utility
    + re-connect device USB cable
    + make sure it says "**data transfer / transferring data**" on the device screen
    + type in a root shell: `export ACCUCHEK_DBG=1`
    + from the same shell, run the utility again to see what the problem is

