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
#ifndef _GP_TCI_H_
#define _GP_TCI_H_

typedef struct {
    uint32_t   a;
    uint32_t   b;
} TEE_Value;

typedef struct {
    uint32_t    sVirtualAddr;         /**< The virtual address of the Bulk buffer regarding the address space of the Trustlet, already includes a possible offset! */
    uint32_t    sVirtualLen;       /**< Length of the mapped Bulk buffer */
    uint32_t    outputSize;
} _TEEC_MemoryReferenceInternal;

typedef union {
    TEE_Value                      value;
    _TEEC_MemoryReferenceInternal   memref;
} _TEEC_ParameterInternal;

typedef enum {
    _TA_OPERATION_OPEN_SESSION =    1,
    _TA_OPERATION_INVOKE_COMMAND =  2,
    _TA_OPERATION_CLOSE_SESSION =   3,
} _TEEC_TCI_type;

typedef struct {
    _TEEC_TCI_type          type;
    uint32_t                commandId;
    uint32_t                paramTypes;
    _TEEC_ParameterInternal params[4];
    bool                    isCancelled;
} _TEEC_OperationInternal;

typedef struct {
    char header[8];// = "TCIGP000"`: version indicator (to support future format changes)
    TEEC_UUID destination;
    _TEEC_OperationInternal operation; //the data of the ongoing operation (if any)
    uint32_t ready;
    // The following fields are set by the secure world (in a future version, they may also be set by the normal world communication layer):
    uint32_t returnOrigin;
    uint32_t returnStatus;
} _TEEC_TCI;

/**
 * Termination codes
 */
#define TA_EXIT_CODE_PANIC  (300)
#define TA_EXIT_CODE_TCI    (301)
#define TA_EXIT_CODE_PARAMS (302)
#define TA_EXIT_CODE_FINISHED       (303)
#define TA_EXIT_CODE_SESSIONSTATE   (304)
#define TA_EXIT_CODE_CREATEFAILED   (305)

#endif // _GP_TCI_H_
