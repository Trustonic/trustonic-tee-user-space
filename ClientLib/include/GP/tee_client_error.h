/*
 * Copyright (c) 2013-2014 TRUSTONIC LIMITED
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

#ifndef __TEE_CLIENT_ERROR_H__
#define __TEE_CLIENT_ERROR_H__

#define TEEC_SUCCESS                      ((TEEC_Result)0x00000000)

/**
 * Generic error code : Generic error
 **/
#define TEEC_ERROR_GENERIC                ((TEEC_Result)0xFFFF0000)

/**
 * Generic error code : The underlying security system denies the access to the
 * object
 **/
#define TEEC_ERROR_ACCESS_DENIED          ((TEEC_Result)0xFFFF0001)

/**
 * Generic error code : The pending operation is cancelled.
 **/
#define TEEC_ERROR_CANCEL                 ((TEEC_Result)0xFFFF0002)

/**
 * Generic error code : The underlying system detects a conflict
 **/
#define TEEC_ERROR_ACCESS_CONFLICT        ((TEEC_Result)0xFFFF0003)

/**
 * Generic error code : Too much data for the operation or some data remain
 * unprocessed by the operation.
 **/
#define TEEC_ERROR_EXCESS_DATA            ((TEEC_Result)0xFFFF0004)

/**
 * Generic error code : Error of data format
 **/
#define TEEC_ERROR_BAD_FORMAT             ((TEEC_Result)0xFFFF0005)

/**
 * Generic error code : The specified parameters are invalid
 **/
#define TEEC_ERROR_BAD_PARAMETERS         ((TEEC_Result)0xFFFF0006)

/**
 * Generic error code : Illegal state for the operation.
 **/
#define TEEC_ERROR_BAD_STATE              ((TEEC_Result)0xFFFF0007)

/**
 * Generic error code : The item is not found
 **/
#define TEEC_ERROR_ITEM_NOT_FOUND         ((TEEC_Result)0xFFFF0008)

/**
 * Generic error code : The specified operation is not implemented
 **/
#define TEEC_ERROR_NOT_IMPLEMENTED        ((TEEC_Result)0xFFFF0009)

/**
 * Generic error code : The specified operation is not supported
 **/
#define TEEC_ERROR_NOT_SUPPORTED          ((TEEC_Result)0xFFFF000A)

/**
 * Generic error code : Insufficient data is available for the operation.
 **/
#define TEEC_ERROR_NO_DATA                ((TEEC_Result)0xFFFF000B)

/**
 * Generic error code : Not enough memory to perform the operation
 **/
#define TEEC_ERROR_OUT_OF_MEMORY          ((TEEC_Result)0xFFFF000C)

/**
 * Generic error code : The service is currently unable to handle the request;
 * try later
 **/
#define TEEC_ERROR_BUSY                   ((TEEC_Result)0xFFFF000D)

/**
 * Generic communication error
 **/
#define TEEC_ERROR_COMMUNICATION          ((TEEC_Result)0xFFFF000E)

/**
 * Generic error code : security violation
 **/
#define TEEC_ERROR_SECURITY               ((TEEC_Result)0xFFFF000F)

/**
 * Generic error code : the buffer is too short
 **/
#define TEEC_ERROR_SHORT_BUFFER           ((TEEC_Result)0xFFFF0010)

/**
 * Error of communication: The target of the connection is dead
 **/
#define TEEC_ERROR_TARGET_DEAD            ((TEEC_Result)0xFFFF3024)

/**
 * File system error code: not enough space to complete the operation.
 **/
#define TEEC_ERROR_STORAGE_NO_SPACE       ((TEEC_Result)0xFFFF3041)

/*------------------------------------------------------------------------------
   Implementation-specific errors 
------------------------------------------------------------------------------*/
#define TEEC_TBASE_ERROR_STORAGE_ITEM_EXISTS        ((TEEC_Result)0x80000000)
#define TEEC_TBASE_ERROR_STORAGE_CORRUPTED          ((TEEC_Result)0x80000001)
#define TEEC_TBASE_ERROR_STORAGE_UNREACHABLE        ((TEEC_Result)0x80000002)
#define TEEC_TBASE_ERROR_NO_MORE_HANDLES            ((TEEC_Result)0x80000003)
#define TEEC_TBASE_ERROR_ITEM_EXISTS                ((TEEC_Result)0x80000004)

#endif /* __TEE_CLIENT_ERROR_H__ */

