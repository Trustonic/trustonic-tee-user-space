/*
 * Copyright (c) 2013 TRUSTONIC LIMITED
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
/**
 * Definition of the machine-specific integer types
 **/
#ifndef __TEE_TYPE_H__
#define __TEE_TYPE_H__

/* C99 integer types */
#if (!defined(__STDC_VERSION__) || __STDC_VERSION__ < 199901L) &&(!defined(ANDROID))

#include <limits.h>

/* Figure out if a 64-bit integer types is available */
#if \
    defined(_MSC_VER) || \
    defined(__SYMBIAN32__) || \
    defined(_WIN32_WCE) || \
    (defined(ULLONG_MAX) && ULLONG_MAX == 0xFFFFFFFFFFFFFFFFULL) || \
    (defined(ULONG_LONG_MAX) && ULONG_LONG_MAX == 0xFFFFFFFFFFFFFFFFULL)
typedef unsigned long long uint64_t;
typedef long long int64_t;
#else
#define __S_TYPE_INT64_UNDEFINED
#endif

#if UINT_MAX == 0xFFFFFFFF
typedef unsigned int uint32_t;
typedef int int32_t;
#elif ULONG_MAX == 0xFFFFFFFF
typedef unsigned long uint32_t;
typedef long int32_t;
#else
#error This compiler is not supported.
#endif

#if USHRT_MAX == 0xFFFF
typedef unsigned short uint16_t;
typedef short  int16_t;
#else
#error This compiler is not supported.
#endif

#if UCHAR_MAX == 0xFF
typedef unsigned char uint8_t;
typedef signed char   int8_t;
#else
#error This compiler is not supported.
#endif

#if !defined(__cplusplus)
typedef unsigned char bool;
#define false ( (bool)0 )
#define true  ( (bool)1 )
#endif

#else  /* !defined(__STDC_VERSION__) || __STDC_VERSION__ < 199901L */

#include <stdbool.h>
#include <stdint.h>

#endif  /* !(!defined(__STDC_VERSION__) || __STDC_VERSION__ < 199901L) */

#include <stddef.h>

#ifndef NULL
#  ifdef __cplusplus
#     define NULL  0
#  else
#     define NULL  ((void *)0)
#  endif
#endif

#define IN
#define OUT

typedef uint32_t TEEC_Result;

/** Definition of an UUID (from RFC 4122 http://www.ietf.org/rfc/rfc4122.txt) */
typedef struct TEE_UUID {
    uint32_t timeLow;
    uint16_t timeMid;
    uint16_t timeHiAndVersion;
    uint8_t clockSeqAndNode[8];
} TEE_UUID;
typedef TEE_UUID TEEC_UUID;

#endif /* __TEE_TYPE_H__ */
