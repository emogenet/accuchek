
#include <log.h>
#include <regex>
#include <fcntl.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <execinfo.h>
#include <inttypes.h>
#include <sys/time.h>

static bool startTimeReady;
static struct timeval startTime;
static void initTime() {
    if(false==startTimeReady) {
        gettimeofday(&startTime, 0);
        startTimeReady = true;
    }
}
struct PreMainInit {
    PreMainInit() {
        initTime();
    }
} preMainInit;

bool gQuiet;
uint64_t gProgIdHi;
uint64_t gProgIdLo;
uint8_t gMode = ' ';
#define GLOBAL(x, y)                                                                                        \
    x g##y;                                                                                                 \
    static pthread_mutex_t y##Lock = PTHREAD_MUTEX_INITIALIZER;                                             \
    void lock##y()   { LOG_FTL(0!=pthread_mutex_lock(&(y##Lock)),   "failed to acquire lock for " #y); }    \
    void unlock##y() { LOG_FTL(0!=pthread_mutex_unlock(&(y##Lock)), "failed to release lock for " #y); }    \

    GLOBAL(    bool, Logging)
    GLOBAL(    bool, InitDone)
    GLOBAL(uint64_t, TIDPool)

#undef GLOBAL

#define LOG_OUT stdout

static void initLog() {

    if(gInitDone) {
        return;
    }

    lockInitDone();

        if(false==gInitDone) {

            gInitDone = true;

            initTime();
            setvbuf(stdout, (char *) NULL, _IONBF, 0);
            setvbuf(stderr, (char *) NULL, _IONBF, 0);

            ssize_t sz = sizeof(gProgIdHi);
            int f = open("/dev/urandom", O_RDONLY);
                LOG_FTL(sz!=read(f, &gProgIdHi, sz), "rng fails");
                LOG_FTL(sz!=read(f, &gProgIdLo, sz), "rng fails");
                gProgIdLo <<= 16; // make room for thread id
            close(f);
        }

        LOG_DBG(
            "progId = 0x%" PRIX64 "%" PRIX64 "",
            gProgIdHi,
            gProgIdLo
        );

    unlockInitDone();
}

int Log::threadId() {
    static __thread int gTID = -1;
    if(gTID<0) {
        lockTIDPool();
            gTID = ++gTIDPool;
        unlockTIDPool();
    }
    return gTID;
}

static inline int getStackDepth() {
    #if defined(NDEBUG)
        return 1;
    #else
        void *buffer[1024];
        return backtrace(buffer, 1024);
    #endif
}

static const char *logMsgNames[] = {
    "log",
    "nfo",
    "wrn",
    "ftl",
    0
};

static void vMsg(
    Log::Mode   mode,
    const char  *fileName,
    const char  *functionName,
    int         lineNumber,
    const char  *format,
    va_list     argPtr
) {
    initLog();

    if(gQuiet) {
        return;
    }

    const char *logMsg = logMsgNames[mode];
    if(Log::kDbg==mode) {

        static const char *filter;
        static auto initDone = false;
        static std::regex *regexp = 0;
        if(!initDone) {
            filter = getenv("LOG");
            if(filter) {
                std::string r(filter);
                r = ".*" + r + ".*";
                regexp = new std::regex(
                    r.c_str(),
                    std::regex_constants::optimize |
                    std::regex_constants::icase
                );
            }
        }

        bool matches = false;
        if(0!=regexp) {
            auto m0 = std::regex_match(logMsg, *regexp);
            auto m1 = std::regex_match(fileName, *regexp);
            auto m2 = std::regex_match(functionName, *regexp);
            auto m3 = format ? std::regex_match(format, *regexp) : false;
            matches = (m0 || m1 || m2 || m3);
        }
        if(!matches) {
            return;
        }
    }

    struct timeval t;
    gettimeofday(&t, 0);

    const char *fName = strlen(fileName) + fileName;
    while(fileName<fName && fName[0]!='/') {
        --fName;
    }
    if(fName[0]=='/') {
        ++fName;
    }

    lockLogging();

        static auto prevTime = -1.0;

        auto deltaTime = 0.0;
        auto dTime = (t.tv_sec + 1e-6*t.tv_usec);
        if(0.0<=prevTime) {
            deltaTime = (dTime - prevTime);
        }
        prevTime = dTime;

        fprintf(
            LOG_OUT,
            "%3d.%06d(%+12.6f):%s:%c%08X:T%2d:D%3d:L%4d:%s:%s: ",
            (int)(t.tv_sec - startTime.tv_sec),
            (int)t.tv_usec,
            deltaTime,
            logMsg,
            gMode,
            (int)gProgIdHi,
            Log::threadId(),
            getStackDepth()-1,
            lineNumber,
            fName,
            functionName
        );

        if(0!=format) {
            vfprintf(LOG_OUT, format, argPtr);
        }

        if(Log::kFatal==mode && 0!=errno) {
            perror(" ====> system error = ");
        }

        fputc('\n', LOG_OUT);
        fflush(LOG_OUT);

        if(Log::kFatal==mode) {
            abort();
        }

    unlockLogging();
}

void Log::msg(
    Log::Mode mode,
    const char  *fileName,
    const char  *functionName,
    int         lineNumber,
    const char  *format,
    ...
) {
    va_list arg;
    va_start(arg, format);
        vMsg(
            mode,
            fileName,
            functionName,
            lineNumber,
            format,
            arg
        );
    va_end(arg);
}

void Log::assrt(
    const char *fileName,
    const char *functionName,
    int        lineNumber,
    bool       bCondition,
    const char *condition,
    const char *format,
    ...
) {
    if(bCondition) {
        return;
    }

    va_list arg;
    va_start(arg, format);

        Log::msg(
            Log::kInfo,
            fileName,
            functionName,
            lineNumber,
            "assertion failing: %s",
            condition
        );

        vMsg(
            Log::kFatal,
            fileName,
            functionName,
            lineNumber,
            format,
            arg
        );

    va_end(arg);
}

