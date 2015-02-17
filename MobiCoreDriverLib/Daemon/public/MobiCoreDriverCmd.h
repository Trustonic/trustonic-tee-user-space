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
#ifndef MCDAEMON_H_
#define MCDAEMON_H_

#include <inttypes.h>      // ANSI C99

#include "mcUuid.h"
#include "mcSpid.h"
#include "mcVersionInfo.h"

#define SOCK_PATH "#mcdaemon"

typedef enum {
    MC_DRV_CMD_PING                 = 0,
    MC_DRV_CMD_GET_INFO             = 1,
    MC_DRV_CMD_OPEN_DEVICE          = 2,
    MC_DRV_CMD_CLOSE_DEVICE         = 3,
    MC_DRV_CMD_NQ_CONNECT           = 4,
    MC_DRV_CMD_OPEN_SESSION         = 5,
    MC_DRV_CMD_CLOSE_SESSION        = 6,
    MC_DRV_CMD_NOTIFY               = 7,
    MC_DRV_CMD_MAP_BULK_BUF         = 8,
    MC_DRV_CMD_UNMAP_BULK_BUF       = 9,
    MC_DRV_CMD_GET_VERSION          = 10,
    MC_DRV_CMD_GET_MOBICORE_VERSION = 11,
    MC_DRV_CMD_OPEN_TRUSTLET        = 12,
    MC_DRV_CMD_OPEN_TRUSTED_APP     = 13,

    // Registry Commands

    // Auth token OPS
    MC_DRV_REG_STORE_AUTH_TOKEN     = 0x100000,
    MC_DRV_REG_READ_AUTH_TOKEN      = 0x100001,
    MC_DRV_REG_DELETE_AUTH_TOKEN    = 0x100002,
    // Root container OPS
    MC_DRV_REG_READ_ROOT_CONT       = 0x100003,
    MC_DRV_REG_WRITE_ROOT_CONT      = 0x100004,
    MC_DRV_REG_DELETE_ROOT_CONT     = 0x100005,
    // Service Provider Container OPS
    MC_DRV_REG_READ_SP_CONT         = 0x100006,
    MC_DRV_REG_WRITE_SP_CONT        = 0x100007,
    MC_DRV_REG_DELETE_SP_CONT       = 0x100008,
    // Trustlet Container OPS
    MC_DRV_REG_READ_TL_CONT         = 0x100009,
    MC_DRV_REG_WRITE_TL_CONT        = 0x10000A,
    MC_DRV_REG_DELETE_TL_CONT       = 0x10000B,
    // Shared Object Data write
    MC_DRV_REG_WRITE_SO_DATA        = 0x10000C,
    // TA Blob store
    MC_DRV_REG_STORE_TA_BLOB        = 0x10000D,
    // Delete all TA objects
    MC_DRV_REG_DELETE_TA_OBJS       = 0x10000E,

} mcDrvCmd_t;

typedef struct {
    mcDrvCmd_t  commandId;
} mcDrvCommandHeader_t;

typedef struct {
    /* <t-base Daemon uses Client API return codes also in commands between Daemon and Client Library. */
    uint32_t  responseId;
} mcDrvResponseHeader_t;


#define MC_DEVICE_ID_DEFAULT    0 /**< The default device ID */

//--------------------------------------------------------------
struct MC_DRV_CMD_OPEN_DEVICE_struct {
    uint32_t  commandId;
    uint32_t  deviceId;
};

typedef struct {
    mcDrvResponseHeader_t        header;
} mcDrvRspOpenDevice_t;

//--------------------------------------------------------------
struct MC_DRV_CMD_CLOSE_DEVICE_struct {
    uint32_t  commandId;
};

typedef struct {
    mcDrvResponseHeader_t         header;
} mcDrvRspCloseDevice_t;

//--------------------------------------------------------------
struct MC_DRV_CMD_OPEN_SESSION_struct {
    uint32_t  commandId;
    uint32_t  deviceId;
    mcUuid_t  uuid;
    uint32_t  tci;
    uint32_t  handle;
    uint32_t  len;
};

typedef struct {
    uint32_t  sessionId;
    uint32_t  deviceSessionId;
    uint32_t  sessionMagic;
} mcDrvRspOpenSessionPayload_t, *mcDrvRspOpenSessionPayload_ptr;

typedef struct {
    mcDrvResponseHeader_t         header;
    mcDrvRspOpenSessionPayload_t  payload;
} mcDrvRspOpenSession_t;

//--------------------------------------------------------------
struct MC_DRV_CMD_OPEN_TRUSTLET_struct {
    uint32_t  commandId;
    uint32_t  deviceId;
    mcSpid_t  spid;
    uint32_t  trustlet_len;
    uint32_t  tci;
    uint32_t  handle;
    uint32_t  len;
};

typedef struct {
    uint32_t  sessionId;
    uint32_t  deviceSessionId;
    uint32_t  sessionMagic;
} mcDrvRspOpenTrustletPayload_t, *mcDrvRspOpenTrustletPayload_ptr;

typedef struct {
    mcDrvResponseHeader_t          header;
    mcDrvRspOpenTrustletPayload_t  payload;
} mcDrvRspOpenTrustlet_t;

//--------------------------------------------------------------
struct MC_DRV_CMD_OPEN_TRUSTED_APP_struct {
    uint32_t  commandId;
    uint32_t  deviceId;
    mcUuid_t  uuid;
    uint32_t  tci;
    uint32_t  handle;
    uint32_t  len;
};

//--------------------------------------------------------------
struct MC_DRV_CMD_CLOSE_SESSION_struct {
    uint32_t  commandId;
    uint32_t  sessionId;
};

typedef struct {
    mcDrvResponseHeader_t         header;
} mcDrvRspCloseSession_t;

//--------------------------------------------------------------
struct MC_DRV_CMD_NOTIFY_struct {
    uint32_t  commandId;
    uint32_t  sessionId;
};

// Notify does not have a response

//--------------------------------------------------------------
struct MC_DRV_CMD_MAP_BULK_BUF_struct {
    uint32_t  commandId;
    uint32_t  sessionId;
    uint32_t  handle;
    uint32_t  rfu;
    uint32_t  offsetPayload;
    uint32_t  lenBulkMem;
};

typedef struct {
    uint32_t  sessionId;
    uint32_t  secureVirtualAdr;
} mcDrvRspMapBulkMemPayload_t, *mcDrvRspMapBulkMemPayload_ptr;

typedef struct {
    mcDrvResponseHeader_t        header;
    mcDrvRspMapBulkMemPayload_t  payload;
} mcDrvRspMapBulkMem_t;


//--------------------------------------------------------------
struct MC_DRV_CMD_UNMAP_BULK_BUF_struct {
    uint32_t  commandId;
    uint32_t  sessionId;
    uint32_t  handle;
    uint32_t  secureVirtualAdr;
    uint32_t  lenBulkMem;
};

typedef struct {
    mcDrvResponseHeader_t          header;
} mcDrvRspUnmapBulkMem_t;


//--------------------------------------------------------------
struct MC_DRV_CMD_NQ_CONNECT_struct {
    uint32_t  commandId;
    uint32_t  deviceId;
    uint32_t  sessionId;
    uint32_t  deviceSessionId;
    uint32_t  sessionMagic; //Random data
};

typedef struct {
    mcDrvResponseHeader_t       header;
} mcDrvRspNqConnect_t;

//--------------------------------------------------------------
struct MC_DRV_CMD_GET_VERSION_struct {
    uint32_t commandId;
};

typedef struct {
    uint32_t responseId;
    uint32_t version;
} mcDrvRspGetVersion_t;

//--------------------------------------------------------------
struct MC_DRV_CMD_GET_MOBICORE_VERSION_struct {
    uint32_t  commandId;
};

typedef struct {
    mcVersionInfo_t versionInfo;
} mcDrvRspGetMobiCoreVersionPayload_t, *mcDrvRspGetMobiCoreVersionPayload_ptr;

typedef struct {
    mcDrvResponseHeader_t       header;
    mcDrvRspGetMobiCoreVersionPayload_t payload;
} mcDrvRspGetMobiCoreVersion_t;

//--------------------------------------------------------------
typedef union {
    mcDrvCommandHeader_t                header;
    MC_DRV_CMD_OPEN_DEVICE_struct       mcDrvCmdOpenDevice;
    MC_DRV_CMD_CLOSE_DEVICE_struct      mcDrvCmdCloseDevice;
    MC_DRV_CMD_OPEN_SESSION_struct      mcDrvCmdOpenSession;
    MC_DRV_CMD_OPEN_TRUSTLET_struct     mcDrvCmdOpenTrustlet;
    MC_DRV_CMD_OPEN_TRUSTED_APP_struct  mcDrvCmdOpenTrustedApp;
    MC_DRV_CMD_CLOSE_SESSION_struct     mcDrvCmdCloseSession;
    MC_DRV_CMD_NQ_CONNECT_struct        mcDrvCmdNqConnect;
    MC_DRV_CMD_NOTIFY_struct            mcDrvCmdNotify;
    MC_DRV_CMD_MAP_BULK_BUF_struct      mcDrvCmdMapBulkMem;
    MC_DRV_CMD_UNMAP_BULK_BUF_struct    mcDrvCmdUnmapBulkMem;
    MC_DRV_CMD_GET_VERSION_struct       mcDrvCmdGetVersion;
    MC_DRV_CMD_GET_MOBICORE_VERSION_struct  mcDrvCmdGetMobiCoreVersion;
} mcDrvCommand_t, *mcDrvCommand_ptr;

typedef union {
    mcDrvResponseHeader_t        header;
    mcDrvRspOpenDevice_t         mcDrvRspOpenDevice;
    mcDrvRspCloseDevice_t        mcDrvRspCloseDevice;
    mcDrvRspOpenSession_t        mcDrvRspOpenSession;
    mcDrvRspCloseSession_t       mcDrvRspCloseSession;
    mcDrvRspNqConnect_t          mcDrvRspNqConnect;
    mcDrvRspMapBulkMem_t         mcDrvRspMapBulkMem;
    mcDrvRspUnmapBulkMem_t       mcDrvRspUnmapBulkMem;
    mcDrvRspGetVersion_t         mcDrvRspGetVersion;
    mcDrvRspGetMobiCoreVersion_t mcDrvRspGetMobiCoreVersion;
} mcDrvResponse_t, *mcDrvResponse_ptr;

#endif /* MCDAEMON_H_ */

