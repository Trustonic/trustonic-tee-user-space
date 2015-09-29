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

/**
 * @file   sfs_error.h
 * @brief  error codes used for levels 0-2
 *
 */

#ifndef __SFS_ERROR_H__
#define __SFS_ERROR_H__

#include "sfs_type.h"

#if defined (DRIVER)

#include "tee_error.h"

/* Existing TEE codes */
#define S_SUCCESS                       TEE_SUCCESS
#define S_ERROR_GENERIC                 TEE_ERROR_GENERIC
#define S_ERROR_CANCEL                  TEE_ERROR_CANCEL
#define S_ERROR_ACCESS_CONFLICT         TEE_ERROR_ACCESS_CONFLICT
#define S_ERROR_BAD_PARAMETERS          TEE_ERROR_BAD_PARAMETERS
#define S_ERROR_BAD_STATE               TEE_ERROR_BAD_STATE
#define S_ERROR_ITEM_NOT_FOUND          TEE_ERROR_ITEM_NOT_FOUND
#define S_ERROR_NOT_SUPPORTED           TEE_ERROR_NOT_SUPPORTED
#define S_ERROR_OUT_OF_MEMORY           TEE_ERROR_OUT_OF_MEMORY
#define S_ERROR_COMMUNICATION           TEE_ERROR_COMMUNICATION
#define S_ERROR_SHORT_BUFFER            TEE_ERROR_SHORT_BUFFER
#define S_ERROR_STORAGE_NO_SPACE        TEE_ERROR_STORAGE_NO_SPACE

/* Implementation-specific errors  */
#define S_ERROR_STORAGE_ITEM_EXISTS     ((S_RESULT)TEE_TBASE_ERROR_STORAGE_ITEM_EXISTS)
#define S_ERROR_STORAGE_CORRUPTED       ((S_RESULT)TEE_TBASE_ERROR_STORAGE_CORRUPTED)
#define S_ERROR_STORAGE_UNREACHABLE     ((S_RESULT)TEE_TBASE_ERROR_STORAGE_UNREACHABLE)
#define S_ERROR_NO_MORE_HANDLES         ((S_RESULT)TEE_TBASE_ERROR_NO_MORE_HANDLES)
#define S_ERROR_ITEM_EXISTS             ((S_RESULT)TEE_TBASE_ERROR_ITEM_EXISTS)

#else

#include "tee_client_error.h"

/* Existing TEEC codes */
#define S_SUCCESS                       TEEC_SUCCESS
#define S_ERROR_GENERIC                 TEEC_ERROR_GENERIC
#define S_ERROR_CANCEL                  TEEC_ERROR_CANCEL
#define S_ERROR_ACCESS_CONFLICT         TEEC_ERROR_ACCESS_CONFLICT
#define S_ERROR_BAD_PARAMETERS          TEEC_ERROR_BAD_PARAMETERS
#define S_ERROR_BAD_STATE               TEEC_ERROR_BAD_STATE
#define S_ERROR_ITEM_NOT_FOUND          TEEC_ERROR_ITEM_NOT_FOUND
#define S_ERROR_NOT_SUPPORTED           TEEC_ERROR_NOT_SUPPORTED
#define S_ERROR_OUT_OF_MEMORY           TEEC_ERROR_OUT_OF_MEMORY
#define S_ERROR_COMMUNICATION           TEEC_ERROR_COMMUNICATION
#define S_ERROR_SHORT_BUFFER            TEEC_ERROR_SHORT_BUFFER
#define S_ERROR_STORAGE_NO_SPACE        TEEC_ERROR_STORAGE_NO_SPACE

/* Implementation-specific errors  */
#define S_ERROR_STORAGE_ITEM_EXISTS     ((S_RESULT)TEEC_TBASE_ERROR_STORAGE_ITEM_EXISTS)
#define S_ERROR_STORAGE_CORRUPTED       ((S_RESULT)TEEC_TBASE_ERROR_STORAGE_CORRUPTED)
#define S_ERROR_STORAGE_UNREACHABLE     ((S_RESULT)TEEC_TBASE_ERROR_STORAGE_UNREACHABLE)
#define S_ERROR_NO_MORE_HANDLES         ((S_RESULT)TEEC_TBASE_ERROR_NO_MORE_HANDLES)
#define S_ERROR_ITEM_EXISTS             ((S_RESULT)TEEC_TBASE_ERROR_ITEM_EXISTS)

#endif /* #if defined (DRIVER) */


#endif //__SFS_ERROR_H__
