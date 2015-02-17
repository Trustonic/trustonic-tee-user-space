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
/** Mobicore Driver Registry Interface
 *
 * Implements the MobiCore registry interface for the ROOT-PA
 *
 * @file
 * @ingroup MCD_MCDIMPL_DAEMON_REG
 */

#include <stdlib.h>
#include <dirent.h>
#include <stdio.h>
#include <sys/stat.h>
#include <assert.h>
#include <string>
#include <cstring>
#include <cstddef>
#include "mcLoadFormat.h"
#include "mcSpid.h"
#include "mcVersionHelper.h"

#include "log.h"

#include "MobiCoreRegistry.h"
#include "MobiCoreDriverCmd.h"

#include "Connection.h"

#define DAEMON_TIMEOUT 30000

using namespace std;


static mcResult_t writeBlobData(void *buff, uint32_t len)
{
    Connection con;
mcDrvResponseHeader_t rsp = { responseId :
                                  MC_DRV_ERR_INVALID_PARAMETER
                                };
    if (!con.connect(SOCK_PATH)) {
        LOG_E("Failed to connect to daemon!");
        return MC_DRV_ERR_DAEMON_SOCKET;
    }

    if (con.writeData(buff, len) <= 0) {
        LOG_E("Failed to send daemon to data!");
        return MC_DRV_ERR_DAEMON_SOCKET;
    }

    if (con.readData(&rsp, sizeof(rsp), DAEMON_TIMEOUT) <= 0) {
        LOG_E("Failed to get answer from daemon!");
        return MC_DRV_ERR_DAEMON_SOCKET;
    }

    return rsp.responseId;
}

static mcResult_t readBlobData(void *buff, uint32_t len, void *rbuff, uint32_t *rlen)
{
    Connection con;
    int32_t size;
mcDrvResponseHeader_t rsp = { responseId :
                                  MC_DRV_ERR_INVALID_PARAMETER
                                };
    if (*rlen == 0) {
        LOG_E("Invalid buffer length!");
        return MC_DRV_ERR_DAEMON_SOCKET;
    }

    if (!con.connect(SOCK_PATH)) {
        LOG_E("Failed to connect to daemon!");
        return MC_DRV_ERR_DAEMON_SOCKET;
    }

    if (con.writeData(buff, len) <= 0) {
        LOG_E("Failed to send daemon to data!");
        return MC_DRV_ERR_DAEMON_SOCKET;
    }

    // First read the response
    if (con.readData(&rsp, sizeof(rsp), DAEMON_TIMEOUT) <= 0) {
        LOG_E("Failed to get answer from daemon!");
        return MC_DRV_ERR_DAEMON_SOCKET;
    }

    //Then read the actual data
    size = con.readData(rbuff, *rlen, DAEMON_TIMEOUT);
    if (size <= 0) {
        LOG_E("Failed to get answer from daemon!");
        return MC_DRV_ERR_DAEMON_SOCKET;
    }
    // Return also the read buf size
    *rlen = (uint32_t)size;

    return rsp.responseId;
}

//------------------------------------------------------------------------------
mcResult_t mcRegistryStoreAuthToken(void *so, uint32_t size)
{
    typedef struct __attribute ((packed)) {
        uint32_t commandId;
        uint32_t soSize;
        uint8_t so;
    } storeCmd;

    mcResult_t ret;
    storeCmd *cmd = (storeCmd *)malloc(sizeof(storeCmd) + size - 1);
    if (cmd == NULL) {
        LOG_E("Allocation failure");
        return MC_DRV_ERR_NO_FREE_MEMORY;
    }

    cmd->commandId = MC_DRV_REG_STORE_AUTH_TOKEN;
    cmd->soSize = size;
    memcpy(&cmd->so, so, size);
    ret = writeBlobData(cmd, sizeof(storeCmd) + size - 1);
    free(cmd);
    return ret;
}


//------------------------------------------------------------------------------
mcResult_t mcRegistryReadAuthToken(void *so, uint32_t *size)
{
mcDrvCommandHeader_t cmd = { commandId :
                                 MC_DRV_REG_READ_AUTH_TOKEN
                               };
    uint32_t rsize;
    mcResult_t ret;
    // we expect to max read what the user has allocated
    rsize = *size;
    ret = readBlobData(&cmd, sizeof(cmd), so, &rsize);
    // return the actual read size
    *size = rsize;
    return ret;
}

//------------------------------------------------------------------------------
mcResult_t mcRegistryDeleteAuthToken(void)
{
mcDrvCommandHeader_t cmd = { commandId :
                                 MC_DRV_REG_DELETE_AUTH_TOKEN
                               };
    return writeBlobData(&cmd, sizeof(cmd));
}


//------------------------------------------------------------------------------
mcResult_t mcRegistryStoreRoot(void *so, uint32_t size)
{
    typedef struct __attribute ((packed)) {
        uint32_t commandId;
        uint32_t soSize;
        uint8_t so;
    } storeCmd;
    mcResult_t ret;
    storeCmd *cmd = (storeCmd *)malloc(sizeof(storeCmd) + size - 1);
    if (cmd == NULL) {
        LOG_E("Allocation failure");
        return MC_DRV_ERR_NO_FREE_MEMORY;
    }

    cmd->commandId = MC_DRV_REG_WRITE_ROOT_CONT;
    cmd->soSize = size;
    memcpy(&cmd->so, so, size);
    ret = writeBlobData(cmd, sizeof(storeCmd) + size - 1);
    free(cmd);
    return ret;
}


//------------------------------------------------------------------------------
mcResult_t mcRegistryReadRoot(void *so, uint32_t *size)
{
mcDrvCommandHeader_t cmd = { commandId :
                                 MC_DRV_REG_READ_ROOT_CONT
                               };
    uint32_t rsize;
    mcResult_t ret;

    rsize = *size;
    ret = readBlobData(&cmd, sizeof(cmd), so, &rsize);
    *size = rsize;
    return ret;
}

//------------------------------------------------------------------------------
mcResult_t mcRegistryCleanupRoot(void)
{
mcDrvCommandHeader_t cmd = { commandId :
                                 MC_DRV_REG_DELETE_ROOT_CONT
                               };
    return writeBlobData(&cmd, sizeof(cmd));
}

//------------------------------------------------------------------------------
mcResult_t mcRegistryStoreSp(mcSpid_t spid, void *so, uint32_t size)
{
    typedef struct __attribute ((packed)) {
        uint32_t commandId;
        uint32_t soSize;
        mcSpid_t spid;
        uint8_t so;
    } storeCmd;

    mcResult_t ret;
    storeCmd *cmd = (storeCmd *)malloc(sizeof(storeCmd) + size - 1);
    if (cmd == NULL) {
        LOG_E("Allocation failure");
        return MC_DRV_ERR_NO_FREE_MEMORY;
    }

    cmd->commandId = MC_DRV_REG_WRITE_SP_CONT;
    cmd->soSize = size;
    cmd->spid = spid;
    memcpy(&cmd->so, so, size);

    ret = writeBlobData(cmd, sizeof(storeCmd) + size - 1);
    free(cmd);
    return ret;
}


//------------------------------------------------------------------------------
mcResult_t mcRegistryReadSp(mcSpid_t spid, void *so, uint32_t *size)
{
    struct {
        uint32_t commandId;
        mcSpid_t spid;
    } cmd;
    uint32_t rsize;
    mcResult_t ret;
    cmd.commandId = MC_DRV_REG_READ_SP_CONT;
    cmd.spid = spid;

    rsize = *size;
    ret = readBlobData(&cmd, sizeof(cmd), so, &rsize);
    *size = rsize;

    return ret;
}

//------------------------------------------------------------------------------
mcResult_t mcRegistryCleanupSp(mcSpid_t spid)
{
    struct {
        uint32_t commandId;
        mcSpid_t spid;
    } cmd;

    cmd.commandId = MC_DRV_REG_DELETE_SP_CONT;
    cmd.spid = spid;

    return writeBlobData(&cmd, sizeof(cmd));
}

//------------------------------------------------------------------------------
mcResult_t mcRegistryStoreTrustletCon(const mcUuid_t *uuid, mcSpid_t spid, void *so, uint32_t size)
{
    typedef struct __attribute ((packed)) {
        uint32_t commandId;
        uint32_t soSize;
        mcUuid_t uuid;
        mcSpid_t spid;
        uint8_t so;
    } storeCmd;

    mcResult_t ret;
    storeCmd *cmd = (storeCmd *)malloc(sizeof(storeCmd) + size - 1);
    if (cmd == NULL) {
        LOG_E("Allocation failure");
        return MC_DRV_ERR_NO_FREE_MEMORY;
    }

    cmd->commandId = MC_DRV_REG_WRITE_TL_CONT;
    cmd->soSize = size;
    cmd->spid = spid;
    memcpy(&cmd->uuid, uuid, sizeof(mcUuid_t));
    memcpy(&cmd->so, so, size);

    ret = writeBlobData(cmd, sizeof(storeCmd) + size - 1);
    free(cmd);
    return ret;
}

//------------------------------------------------------------------------------
mcResult_t mcRegistryStoreTABlob(mcSpid_t spid, void *blob, uint32_t size)
{
    typedef struct {
        uint32_t commandId;
        uint32_t blobSize;
        mcSpid_t spid;
        uint8_t blob[];
    } storeCmd;

    mcResult_t ret;
    storeCmd *cmd = (storeCmd *)malloc(sizeof(storeCmd) + size);
    if (cmd == NULL) {
        LOG_E("Allocation failure");
        return MC_DRV_ERR_NO_FREE_MEMORY;
    }

    cmd->commandId = MC_DRV_REG_STORE_TA_BLOB;
    cmd->blobSize = size;
    cmd->spid = spid;
    memcpy(&cmd->blob, blob, size);

    ret = writeBlobData(cmd, sizeof(storeCmd) + size);
    free(cmd);
    return ret;
}

//------------------------------------------------------------------------------
mcResult_t mcRegistryReadTrustletCon(const mcUuid_t *uuid, mcSpid_t spid, void *so, uint32_t *size)
{
    struct {
        uint32_t commandId;
        mcUuid_t uuid;
        mcSpid_t spid;
    } cmd;
    mcResult_t ret;
    uint32_t rsize;
    cmd.commandId = MC_DRV_REG_READ_TL_CONT;
    cmd.spid = spid;
    memcpy(&cmd.uuid, uuid, sizeof(mcUuid_t));

    rsize = *size;
    ret = readBlobData(&cmd, sizeof(cmd), so, &rsize);
    *size = rsize;
    return ret;
}

//------------------------------------------------------------------------------
mcResult_t mcRegistryCleanupTrustlet(const mcUuid_t *uuid, const mcSpid_t spid)
{
    struct {
        uint32_t commandId;
        mcUuid_t uuid;
        mcSpid_t spid;
    } cmd;

    if (uuid == NULL) {
        return MC_DRV_ERR_INVALID_PARAMETER;
    }

    cmd.commandId = MC_DRV_REG_DELETE_TL_CONT;
    cmd.spid = spid;
    memcpy(&cmd.uuid, uuid, sizeof(mcUuid_t));

    return writeBlobData(&cmd, sizeof(cmd));
}

//------------------------------------------------------------------------------
mcResult_t mcRegistryCleanupTA(const mcUuid_t *uuid)
{
    struct {
        uint32_t commandId;
        mcUuid_t uuid;
    } cmd;

    if (uuid == NULL) {
        return MC_DRV_ERR_INVALID_PARAMETER;
    }

    cmd.commandId = MC_DRV_REG_DELETE_TA_OBJS;
    memcpy(&cmd.uuid, uuid, sizeof(mcUuid_t));

    return writeBlobData(&cmd, sizeof(cmd));
}

//------------------------------------------------------------------------------
mcResult_t mcRegistryStoreData(void *so, uint32_t size)
{
    return MC_DRV_ERR_INVALID_PARAMETER;
}


//------------------------------------------------------------------------------
mcResult_t mcRegistryReadData(uint32_t context, const mcCid_t *cid, mcPid_t pid,
                              mcSoDataCont_t *so, uint32_t maxLen)
{
    return MC_DRV_ERR_INVALID_PARAMETER;
}

