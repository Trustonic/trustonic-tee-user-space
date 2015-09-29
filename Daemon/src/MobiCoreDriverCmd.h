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

#ifndef MOBICORE_REGISTRY_API_H_
#define MOBICORE_REGISTRY_API_H_

#include "mcUuid.h"
#include "mcSpid.h"

#define SOCK_PATH "#mcdaemon"

typedef enum {
    // Auth token OPS
    MC_DRV_REG_READ_AUTH_TOKEN     = 0,
    MC_DRV_REG_WRITE_AUTH_TOKEN,
    MC_DRV_REG_DELETE_AUTH_TOKEN,
    // Root container OPS
    MC_DRV_REG_READ_ROOT_CONT,
    MC_DRV_REG_WRITE_ROOT_CONT,
    MC_DRV_REG_DELETE_ROOT_CONT,
    // Service Provider Container OPS
    MC_DRV_REG_READ_SP_CONT,
    MC_DRV_REG_WRITE_SP_CONT,
    MC_DRV_REG_DELETE_SP_CONT,
    // Trustlet Container OPS
    MC_DRV_REG_READ_TL_CONT,
    MC_DRV_REG_WRITE_TL_CONT,
    MC_DRV_REG_DELETE_TL_CONT,
    // Shared Object Data write
    MC_DRV_REG_WRITE_SO_DATA,
    // TA Blob store
    MC_DRV_REG_STORE_TA_BLOB,
    // Delete all TA objects
    MC_DRV_REG_DELETE_TA_OBJS,

    MC_DRV_REG_END
} mcDrvCmd_t;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	/* id - free run counter managed by driver, initialized by
	 * initial_cmd_counter, incremented for each command.
	 * If difference between current value and value from previous command
	 * is greater than 1, it means daemon and driver are out of synch. */
	uint32_t id;
	uint32_t cmd;
	uint32_t data_size;
} CommandHeader;

typedef struct {
	/* id - must be same value as it was in the corresponding command,
	 * but managed by daemon */
	uint32_t id;
	uint32_t result;
	uint32_t data_size;
} ResponseHeader;

typedef struct {
	uint32_t len;
	uint32_t tl_start_off;
	uint8_t value[];
} regObject_t;

typedef struct {
	mcUuid_t uuid;
	mcSpid_t spid;
	uint8_t blob[];
} TlBlob;

typedef struct {
	mcSpid_t spid;
	uint8_t blob[];
} TaBlob;

#ifdef __cplusplus
}
#endif

#endif // MOBICORE_REGISTRY_API_H_
