/*
 * Copyright (c) 2013-2015 TRUSTONIC LIMITED
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the TRUSTONIC LIMITED nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __SFS_TYPES_H__
#define __SFS_TYPES_H__

#if defined (DRIVER)
#include "tee_type.h"
#endif

/* These definitions are for common port */
typedef uint32_t                S_RESULT;
typedef uint32_t                S_HANDLE;
typedef S_HANDLE                SM_HANDLE;
#define S_HANDLE_NULL           ((S_HANDLE)0)
#define SM_HANDLE_INVALID       S_HANDLE_NULL

/** Definition of an UUID (from RFC 4122 http://www.ietf.org/rfc/rfc4122.txt) */
typedef struct S_UUID
{
   uint32_t timeLow;
   uint16_t timeMid;
   uint16_t timeHiAndVersion;
   uint8_t clockSeqAndNode[8];
} S_UUID;

/* Static SFS configuration */
typedef struct
{

   uint32_t nFileSystemCacheSize;        /* filesystem.cache.size */
   uint32_t nFileSystemSectorSize;       /* filesystem.sector.size */
   uint32_t nFileSystemSizeMax;          /* filesystem.size.max in KB */
}
SYSTEM_STATIC_CFG;


extern SYSTEM_STATIC_CFG g_sSystemStaticCfg;

/**
 * Return the difference {p1}-{p2} in bytes
 **/
#define SPointerDiff(p1, p2)    ((int32_t)(((uint32_t)p1) - ((uint32_t)p2)))

/**
 * Add an offset of {n} bytes to the pointer {p}
 **/
#define SPointerAdd(p, n)       ((void*)(((uint8_t*)(p)) + (int32_t)(n)))


#if defined (DRIVER)
    #include "DrApi/DrApi.h"

/* These definitions are for TDriver port */
    #define _SLogTrace(...)         drDbgPrintLnf(__VA_ARGS__)
    #define SLogTrace(...)          drDbgPrintLnf(__VA_ARGS__)
    #define SLogError(...)          drDbgPrintLnf(__VA_ARGS__)
    //#define SLogError(...)          drApiPrintLnf(__VA_ARGS__)
    #define SLogWarning(...)        drDbgPrintLnf(__VA_ARGS__)
    #define __INCLUDE_DEBUG
    _EXTERN_C _NORETURN void _doAssert(
        const char      *expr,
        const char      *file,
        const uint32_t  line
    );
    #define SAssert(cond) \
            do{if (!(cond)){_doAssert(NULL,__FILE__, __LINE__);}} while(FALSE)

    _DRAPI_EXTERN_C _DRAPI_NORETURN void drApiExit(uint32_t exitCode);
    #define SPanic(exitCode)        drApiExit(exitCode)

    #define SMemAllocEx(param,size) drApiMalloc(size,0)
    #define SMemAlloc(size)         drApiMalloc(size,0)
    #define SMemFree(ptr)           drApiFree(ptr)
    #define SMemMove(a,b,c)         memmove(a,b,c)
    #define SMemCompare(a,b,c)      memcmp(a,b,c)
    #define SMemFill(a,b,c)         memset(a,b,c)
    #define SMemRealloc(a, b)       drApiRealloc(a, b)

    #define setError(...)           drDbgPrintLnf(__VA_ARGS__)
    #define exosTraceError(...)     drDbgPrintLnf(__VA_ARGS__)

    #define Trace(...)              drDbgPrintLnf(__VA_ARGS__)

    #define Error(...)              drDbgPrintLnf(__VA_ARGS__)

#elif defined (TEST_SUITE_NAME)
/* These definitions are for Test Suite port */
/* Olivier, this is your part to update */
    #include <stdlib.h>
    #include <stdio.h>
//    #include "ssdi.h"

    #ifndef LOG_TAG
    #define LOG_TAG "sfs"
    #endif

    #include <log.h>
//    #undef LOG_I
//    #define LOG_I(fmt, args...) DUMMY_FUNCTION()

    #ifdef NDEBUG
    #define LOG(fmt, args...) DUMMY_FUNCTION()
    #else
    #define LOG(...) \
        (void) fprintf(stdout, __VA_ARGS__); (void) fflush(stdout)
    #endif

    #define setError(...)           LOG(__VA_ARGS__)
    #define Trace(...)              LOG(__VA_ARGS__)

#endif /* #if defined (DRIVER) */

#endif //__SFS_TYPES_H__
