#include <lib.h>

#define uassert(x)                                                             \
    do {                                                                       \
        if (!(x)) {                                                            \
            user_halt("assertion failed: %s", #x);                             \
        }                                                                      \
    } while (0)

static void accepted() { user_halt("OSTEST_OK"); }

#define tot 22
