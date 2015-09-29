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
/** Mobicore Driver Registry Interface
 *
 * Implements the MobiCore registry interface for the ROOT-PA
 *
 * @file
 * @ingroup MCD_MCDIMPL_DAEMON_REG
 */

#include <stdint.h>
#include <string.h>
#include <sys/uio.h>
#include <fcntl.h>
#include "log.h"

#include "mcSpid.h"
#include "MobiCoreRegistry.h"
#include "MobiCoreDriverCmd.h"
#include "Connection.h"
#include "buildTag.h"

static const __attribute__((used)) char* buildtag = MOBICORE_COMPONENT_BUILD_TAG;

#define DAEMON_TIMEOUT  30000
#define ARRAY_SIZE(a)   (sizeof(a)/sizeof((a)[0]))

struct cmd_t: public CommandHeader
{
	cmd_t(const uint32_t c)
	{
		id = 0;
		cmd = c;
		data_size = 0;
	}
};

static
mcResult_t check_iov(const struct iovec *iov, size_t iovcnt, size_t *pay_size)
{
    size_t sz = 0;
    if(iov == NULL || iov[0].iov_len != sizeof(cmd_t))
	return MC_DRV_ERR_INVALID_PARAMETER;

    for(size_t i = 0; i < iovcnt; i++) {
	if(iov[i].iov_base == NULL || iov[i].iov_len == 0)
	    return MC_DRV_ERR_INVALID_PARAMETER;
	sz += iov[i].iov_len;
    }
    *pay_size = sz - iov[0].iov_len;
    return MC_DRV_OK;
}


static mcResult_t send_cmd_recv_data(struct iovec *out_iov,
        size_t out_iovcnt, void *rbuff = NULL, uint32_t *rlen = NULL)
{
    uint32_t nsegs;
    ResponseHeader rsp;
    size_t payload_sz;
    ssize_t length;

    if( (rbuff == NULL && rlen != NULL) ||
        (rbuff != NULL && rlen == NULL) ||
         out_iov == NULL || out_iovcnt == 0) {
	LOG_E("Invalid buffer length!");
	return MC_DRV_ERR_INVALID_PARAMETER;
    }

    memset(&rsp, 0, sizeof(rsp));
    rsp.result = check_iov(out_iov, out_iovcnt, &payload_sz);
    if(rsp.result != MC_DRV_OK)
	    return rsp.result;

    struct iovec in_iov[2] = {
            { &rsp, sizeof(rsp) }
    };

    if(rbuff != NULL) {
	    nsegs = 2;
	    in_iov[1].iov_base = rbuff;
	    in_iov[1].iov_len  = *rlen;
    } else {
	    nsegs = 1;
    }

    cmd_t &cmd = *static_cast<cmd_t *>(out_iov[0].iov_base);
    cmd.data_size = static_cast<uint32_t>(payload_sz);
    LOG_D("Sending command %d", cmd.cmd);

    Connection con;

    if (!con.connect(SOCK_PATH)) {
        LOG_E("Failed to connect to daemon!");
        return MC_DRV_ERR_DAEMON_SOCKET;
    }

    if (con.writeMsg(out_iov, static_cast<uint32_t>(out_iovcnt)) <= 0) {
	LOG_E("Failed to send data to daemon");
        return MC_DRV_ERR_DAEMON_SOCKET;
    }

    length = con.readMsg(in_iov, nsegs, DAEMON_TIMEOUT);
    if (length <= 0) {
        LOG_E("Failed to get answer from daemon!");
        return MC_DRV_ERR_DAEMON_SOCKET;
    }
    if (length < (ssize_t)sizeof(ResponseHeader)) {
        LOG_E("Invalid length received from daemon!");
        return MC_DRV_ERR_DAEMON_SOCKET;
    }
    length -= sizeof(ResponseHeader);

    LOG_D("result is %x", rsp.result);

    // Return also the read size
    if(rsp.result == MC_DRV_OK && rlen != NULL)
	*rlen = length;

    return rsp.result;
}

//------------------------------------------------------------------------------
mcResult_t mcRegistryStoreAuthToken(void *so, uint32_t size)
{
    cmd_t cmd(MC_DRV_REG_WRITE_AUTH_TOKEN);
    struct iovec iov[] = {
            {&cmd, sizeof(cmd)},
            {so, size}
    };
    LOG_D("execute MC_DRV_REG_WRITE_AUTH_TOKEN");
    return send_cmd_recv_data(iov, ARRAY_SIZE(iov));
}

//------------------------------------------------------------------------------
mcResult_t mcRegistryReadAuthToken(void *so, uint32_t *size)
{
    cmd_t cmd(MC_DRV_REG_READ_AUTH_TOKEN);
    struct iovec iov[] = {
            {&cmd, sizeof(cmd)}
    };
    //__android_log_print(ANDROID_LOG_INFO, LOG_TAG, "%s", MOBICORE_COMPONENT_BUILD_TAG);
    LOG_D("execute MC_DRV_REG_READ_AUTH_TOKEN");
    return send_cmd_recv_data(iov, ARRAY_SIZE(iov), so, size);
}

//------------------------------------------------------------------------------
mcResult_t mcRegistryDeleteAuthToken(void)
{
    cmd_t cmd(MC_DRV_REG_DELETE_AUTH_TOKEN);
    struct iovec iov[] = {
            {&cmd, sizeof(cmd)}
    };
    LOG_D("execute MC_DRV_REG_DELETE_AUTH_TOKEN");
    return send_cmd_recv_data(iov, ARRAY_SIZE(iov));
}

//------------------------------------------------------------------------------
mcResult_t mcRegistryStoreRoot(void *so, uint32_t size)
{
    cmd_t cmd(MC_DRV_REG_WRITE_ROOT_CONT);
    struct iovec iov[] = {
            {&cmd, sizeof(cmd)},
            {so, size}
    };
    LOG_D("execute MC_DRV_REG_WRITE_ROOT_CONT");
    return send_cmd_recv_data(iov, ARRAY_SIZE(iov));
}

//------------------------------------------------------------------------------
mcResult_t mcRegistryReadRoot(void *so, uint32_t *size)
{
    cmd_t cmd(MC_DRV_REG_READ_ROOT_CONT);
    struct iovec iov[] = {
            {&cmd, sizeof(cmd)}
    };
    LOG_D("execute MC_DRV_REG_READ_ROOT_CONT");
    return send_cmd_recv_data(iov, ARRAY_SIZE(iov), so, size);
}

//------------------------------------------------------------------------------
mcResult_t mcRegistryCleanupRoot(void)
{
    cmd_t cmd(MC_DRV_REG_DELETE_ROOT_CONT);
    struct iovec iov[] = {
            {&cmd, sizeof(cmd)}
    };
    LOG_D("execute MC_DRV_REG_DELETE_ROOT_CONT");
    return send_cmd_recv_data(iov, ARRAY_SIZE(iov));
}

//------------------------------------------------------------------------------
mcResult_t mcRegistryStoreSp(mcSpid_t spid, void *so, uint32_t size)
{
    cmd_t cmd(MC_DRV_REG_WRITE_SP_CONT);
    struct iovec iov[] = {
            {&cmd, sizeof(cmd)},
            {&spid, sizeof(spid)},
            {so, size}
    };
    LOG_D("execute MC_DRV_REG_WRITE_SP_CONT");
    return send_cmd_recv_data(iov, ARRAY_SIZE(iov));
}

//------------------------------------------------------------------------------
mcResult_t mcRegistryReadSp(mcSpid_t spid, void *so, uint32_t *size)
{
    cmd_t cmd(MC_DRV_REG_READ_SP_CONT);
    struct iovec iov[] = {
            {&cmd, sizeof(cmd)},
            {&spid, sizeof(spid)}
    };
    LOG_D("execute MC_DRV_REG_READ_SP_CONT");
    return send_cmd_recv_data(iov, ARRAY_SIZE(iov), so, size);
}

//------------------------------------------------------------------------------
mcResult_t mcRegistryCleanupSp(mcSpid_t spid)
{
    cmd_t cmd(MC_DRV_REG_DELETE_SP_CONT);
    struct iovec iov[] = {
            {&cmd, sizeof(cmd)},
            {&spid, sizeof(spid)},
    };
    LOG_D("execute MC_DRV_REG_DELETE_SP_CONT");
    return send_cmd_recv_data(iov, ARRAY_SIZE(iov));
}

//------------------------------------------------------------------------------
mcResult_t mcRegistryStoreTrustletCon(const mcUuid_t *uuid,
        mcSpid_t spid, void *so, uint32_t size)
{
    cmd_t cmd(MC_DRV_REG_WRITE_TL_CONT);
    struct iovec iov[] = {
            {&cmd,  sizeof(cmd)},
            {const_cast<mcUuid_t *>(uuid),  sizeof(*uuid)},
            {&spid, sizeof(spid)},
            {so,    size}
    };
    LOG_D("execute MC_DRV_REG_WRITE_TL_CONT");
    return send_cmd_recv_data(iov, ARRAY_SIZE(iov));
}

//------------------------------------------------------------------------------
mcResult_t mcRegistryStoreTABlob(mcSpid_t spid, void *blob, uint32_t size)
{
    cmd_t cmd(MC_DRV_REG_STORE_TA_BLOB);
    struct iovec iov[] = {
            {&cmd,  sizeof(cmd)},
            {&spid, sizeof(spid)},
            {blob,  size}
    };
    LOG_D("execute MC_DRV_REG_STORE_TA_BLOB");
    return send_cmd_recv_data(iov, ARRAY_SIZE(iov));
}

//------------------------------------------------------------------------------
mcResult_t mcRegistryReadTrustletCon(const mcUuid_t *uuid, mcSpid_t spid,
        void *so, uint32_t *size)
{
    cmd_t cmd(MC_DRV_REG_READ_TL_CONT);
    struct iovec iov[] = {
            {&cmd, sizeof(cmd)},
            {const_cast<mcUuid_t *>(uuid),  sizeof(*uuid)},
            {&spid, sizeof(spid)}
    };
    LOG_D("execute MC_DRV_REG_READ_TL_CONT");
    return send_cmd_recv_data(iov, ARRAY_SIZE(iov), so, size);
}

//------------------------------------------------------------------------------
mcResult_t mcRegistryCleanupTrustlet(const mcUuid_t *uuid, const mcSpid_t spid)
{
    cmd_t cmd(MC_DRV_REG_DELETE_TL_CONT);
    struct iovec iov[] = {
            {&cmd,  sizeof(cmd)},
            {const_cast<mcUuid_t *>(uuid),  sizeof(*uuid)},
            {const_cast<mcSpid_t *>(&spid), sizeof(spid)},
    };
    LOG_D("execute MC_DRV_REG_DELETE_TL_CONT");
    return send_cmd_recv_data(iov, ARRAY_SIZE(iov));
}

//------------------------------------------------------------------------------
mcResult_t mcRegistryCleanupTA(const mcUuid_t *uuid)
{
    cmd_t cmd(MC_DRV_REG_DELETE_TA_OBJS);
    struct iovec iov[] = {
	    {&cmd,  sizeof(cmd)},
	    {const_cast<mcUuid_t *>(uuid),  sizeof(*uuid)},
    };
    LOG_D("execute MC_DRV_REG_DELETE_TA_OBJS");
    return send_cmd_recv_data(iov, ARRAY_SIZE(iov));
}

//------------------------------------------------------------------------------
mcResult_t mcRegistryStoreData(void *, uint32_t)
{
    return MC_DRV_ERR_INVALID_PARAMETER;
}

//------------------------------------------------------------------------------
mcResult_t mcRegistryReadData(uint32_t, const mcCid_t *, mcPid_t,
                              mcSoDataCont_t *, uint32_t)
{
    return MC_DRV_ERR_INVALID_PARAMETER;
}
