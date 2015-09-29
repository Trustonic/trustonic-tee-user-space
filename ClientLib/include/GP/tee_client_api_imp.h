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

/*
 * This header file defines the implementation-dependent types,
 * constants and macros for all the Trusted Foundations implementations
 * of the TEE Client API
 */
#ifndef   __TEE_CLIENT_API_IMP_H__
#define   __TEE_CLIENT_API_IMP_H__

#if TBASE_API_LEVEL >= 3

#include <pthread.h>

#define TEEC_MEM_INOUT (TEEC_MEM_INPUT | TEEC_MEM_OUTPUT)

typedef struct {
    uint32_t                    reserved;
} TEEC_Context_IMP;

typedef struct {
    uint32_t                    sessionId;
    TEEC_Context_IMP            context;
    void                        *tci;
    bool                        active;
    pthread_mutex_t             mutex_tci;  //mutex to serialize CA requests
} TEEC_Session_IMP;

typedef struct {
    bool                        implementation_allocated;
} TEEC_SharedMemory_IMP;

typedef struct {
    TEEC_Session_IMP            *session;
} TEEC_Operation_IMP;

/* There is no natural, compile-time limit on the shared memory, but a specific
   implementation may introduce a limit (in particular on TrustZone) */
#define TEEC_CONFIG_SHAREDMEM_MAX_SIZE ((size_t)0xFFFFFFFF)

#define TEEC_PARAM_TYPES(entry0Type, entry1Type, entry2Type, entry3Type) \
   ((entry0Type) | ((entry1Type) << 4) | ((entry2Type) << 8) | ((entry3Type) << 12))

#endif /* TBASE_API_LEVEL >= 3 */

#endif /* __TEE_CLIENT_API_IMP_H__ */
