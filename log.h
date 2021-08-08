#ifndef __LOG_H__
    #define __LOG_H__

    // export LOG='*' to see debug messages

    #include <stdio.h>
    #include <assert.h>
    #include <stdlib.h>

    #define GCC_DIAG_STR(s)         #s
    #define GCC_DIAG_DO_PRAGMA(x)   _Pragma (#x)
    #define GCC_DIAG_JOINSTR(x,y)   GCC_DIAG_STR(x ## y)
    #define GCC_DIAG_PRAGMA(x)      GCC_DIAG_DO_PRAGMA(GCC diagnostic x)
    #define GCC_DIAG_PUSH(x)        GCC_DIAG_PRAGMA(push) GCC_DIAG_PRAGMA(ignored GCC_DIAG_JOINSTR(-W,x))
    #define GCC_DIAG_POP()          GCC_DIAG_PRAGMA(pop)

    struct Log {

        enum Mode {
            kDbg = 0,
            kInfo,
            kWarning,
            kFatal,
            kNbModes
        };

        static void msg(
            Mode       mode,
            const char *fileName,
            const char *functionName,
            int        lineNumber,
            const char *format = 0,
            ...
        );

        static void assrt(
            const char *fileName,
            const char *functionName,
            int        lineNumber,
            bool       bCondition,
            const char *condition,
            const char *format = 0,
            ...
        );

        static int threadId();
    };

    #if defined(LOG_OFF)

        #define LOG_MSG(x, ...)
        #define LOG_ASSERT(x, ...)

    #else

        #define LOG_MSG(x, ...)     \
            do {                    \
                Log::msg(           \
                    (x),            \
                    __FILE__,       \
                    __FUNCTION__,   \
                    __LINE__,       \
                    ##__VA_ARGS__   \
                );                  \
            } while(0)              \

        #define LOG_ASSERT(x, ...)  \
            do {                    \
                Log::assrt(         \
                    __FILE__,       \
                    __FUNCTION__,   \
                    __LINE__,       \
                    (x),            \
                    #x,             \
                    ##__VA_ARGS__   \
                );                  \
            } while(0)              \

    #endif

    #define LOG_DBG(...) LOG_MSG(Log::kDbg,     ##__VA_ARGS__)
    #define LOG_NFO(...) LOG_MSG(Log::kInfo,    ##__VA_ARGS__)
    #define LOG_WRN(...) LOG_MSG(Log::kWarning, ##__VA_ARGS__)

    #define LOG_FTL(x, ...) LOG_ASSERT(!(x), ##__VA_ARGS__)
    #define LOG_IMPLY(x, y, ...) LOG_ASSERT(((x)==false) || (y), ##__VA_ARGS__)
    #define LOG_EQUIV(x, y, ...) {          \
        LOG_IMPLY((x), (y), ##__VA_ARGS__); \
        LOG_IMPLY((y), (x), ##__VA_ARGS__); \
    }                                       \

#endif // __LOG_H__

