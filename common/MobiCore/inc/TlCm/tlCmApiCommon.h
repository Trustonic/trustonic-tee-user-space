/** @addtogroup CMP_COMMON
 * @{
 * @file
 * Common interface definitions to content management trustlet (TlCm).
 *
 * The TlCm is responsible for implementing content management protocol (CMP)
 * commands and generating approriate CMP responses.
 *
 * Copyright © Trustonic Limited 2013.
 *
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. Neither the name of the Trustonic Limited nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef TL_CM_API_COMMON_H_
#define TL_CM_API_COMMON_H_

#include "tlCmError.h"

/** TlCm command ids supported.
 * @note All command ids must be in range 0 to 0x1D.
 */
//lint -esym(756, cmpCommands_t) cmpCommands_t type by itself not used.
typedef enum cmpCommands_t {
    MC_CMP_CMD_AUTHENTICATE = 0,
    MC_CMP_CMD_BEGIN_ROOT_AUTHENTICATION = 1,
    MC_CMP_CMD_BEGIN_SOC_AUTHENTICATION = 2,
    MC_CMP_CMD_BEGIN_SP_AUTHENTICATION = 3, 
    MC_CMP_CMD_GENERATE_AUTH_TOKEN = 4,
    MC_CMP_CMD_GET_VERSION = 5,
    //MC_CMP_CMD_ROOT_CONT_ACTIVATE = 6,
    MC_CMP_CMD_ROOT_CONT_LOCK_BY_ROOT = 7,
    //MC_CMP_CMD_ROOT_CONT_REGISTER = 8,
    MC_CMP_CMD_ROOT_CONT_REGISTER_ACTIVATE = 9,
    MC_CMP_CMD_ROOT_CONT_UNLOCK_BY_ROOT = 10,
    MC_CMP_CMD_ROOT_CONT_UNREGISTER = 11,
    MC_CMP_CMD_SP_CONT_ACTIVATE = 12,
    MC_CMP_CMD_SP_CONT_LOCK_BY_ROOT = 13,
    MC_CMP_CMD_SP_CONT_LOCK_BY_SP = 14,
    MC_CMP_CMD_SP_CONT_REGISTER = 15,
    MC_CMP_CMD_SP_CONT_REGISTER_ACTIVATE = 16,
    MC_CMP_CMD_SP_CONT_UNLOCK_BY_ROOT = 17,
    MC_CMP_CMD_SP_CONT_UNLOCK_BY_SP = 18,
    MC_CMP_CMD_SP_CONT_UNREGISTER = 19,
    MC_CMP_CMD_TLT_CONT_ACTIVATE = 20,
    MC_CMP_CMD_TLT_CONT_LOCK_BY_SP = 21,
    MC_CMP_CMD_TLT_CONT_PERSONALIZE = 22,
    MC_CMP_CMD_TLT_CONT_REGISTER = 23,
    MC_CMP_CMD_TLT_CONT_REGISTER_ACTIVATE = 24,
    MC_CMP_CMD_TLT_CONT_UNLOCK_BY_SP = 25,
    MC_CMP_CMD_TLT_CONT_UNREGISTER = 26,
    MC_CMP_CMD_GET_SUID = 27,
    MC_CMP_CMD_AUTHENTICATE_TERMINATE = 28,
    MC_CMP_CMD_LAST_ = MC_CMP_CMD_AUTHENTICATE_TERMINATE,
} cmpCommands_t;

/** TlCm exit code: TlCm exited with error. */
#define EXIT_ERROR  ((uint32_t)(-1))

#endif // TL_CM_API_COMMON_H_

/** @} */
