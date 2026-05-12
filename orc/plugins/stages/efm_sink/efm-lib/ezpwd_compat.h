#ifndef EZPWD_COMPAT_H
#define EZPWD_COMPAT_H

#ifdef _WIN32
    #include <cstdint>

    #ifndef _SSIZE_T_DEFINED
        using ssize_t = intptr_t;
        #define _SSIZE_T_DEFINED
    #endif
#endif

#ifdef _MSC_VER
    #define and &&
    #define or ||
    #define not !
#endif

#include "ezpwd/rs_base"
#include "ezpwd/rs"

#ifdef _MSC_VER
    #undef and
    #undef or
    #undef not
#endif

#endif // EZPWD_COMPAT_H
