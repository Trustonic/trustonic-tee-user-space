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

#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>

#define LOG_TAG "McClient"
#include "log.h"
#include "MobiCoreDriverApi.h"
#include "common_client.h"
#include "buildTag.h"

static const __attribute__((used)) char* buildtag = MOBICORE_COMPONENT_BUILD_TAG;

//------------------------------------------------------------------------------

/*
 * On mcCloseDevice(), all mappings need to be removed, which means we need to
 * keep them somewhere.  We keep them in an array, resized when needed.
 * Both map and length must be kept in user-space, as after a fork another
 * mapping with same address may have appeared from the other process(es).
 */
struct maplen {
    uint8_t* wsm;
    uint32_t len;
};

static struct maplen* wsms;
static size_t wsms_capacity, wsms_length;
static pthread_mutex_t wsms_mutex = PTHREAD_MUTEX_INITIALIZER;

static CommonClient& client = CommonClient::getInstance();

static inline int wsms_add(uint8_t* wsm, uint32_t len)
{
    int ret = 0;
    pthread_mutex_lock(&wsms_mutex);
    if (wsms_length == wsms_capacity) {
        size_t new_wsms_capacity = wsms_capacity;
        /* Need some room */
        if (new_wsms_capacity == 0) {
            new_wsms_capacity = 16;
        } else {
            new_wsms_capacity *= 2;
        }
        struct maplen* new_wsms = (struct maplen*)realloc(wsms, new_wsms_capacity * sizeof(*wsms));
        if (!new_wsms) {
            LOG_ERRNO("resize WSMs array");
            ret = -1;
        } else {
            wsms = new_wsms;
            wsms_capacity = new_wsms_capacity;
        }
    }
    if (ret == 0) {
        wsms[wsms_length].wsm = wsm;
        wsms[wsms_length].len = len;
        wsms_length++;
    }
    pthread_mutex_unlock(&wsms_mutex);
    return ret;
}

static inline int wsms_remove(uint8_t* wsm)
{
    int ret = 0;
    pthread_mutex_lock(&wsms_mutex);
    /* Find position of WSM */
    size_t i;
    for (i = 0; i < wsms_length; i++) {
        if (wsms[i].wsm == wsm) {
            break;
        }
    }
    if (i == wsms_length) {
        LOG_E("WSM %p not found in array", wsm);
        ret = -1;
    } else {
        /* Replace WSM with last so the first free element remains wsms[wsms_length] */
        wsms[i] = wsms[--wsms_length];
    }
    pthread_mutex_unlock(&wsms_mutex);
    return ret;
}

static inline uint32_t wsms_len(uint8_t* wsm)
{
    uint32_t len = 0;
    pthread_mutex_lock(&wsms_mutex);
    for (size_t i = 0; i < wsms_length; i++) {
        if (wsms[i].wsm == wsm) {
            len = wsms[i].len;
            break;
        }
    }
    pthread_mutex_unlock(&wsms_mutex);
    return len;
}

static inline struct maplen wsms_removeLast(void)
{
    struct maplen wsm;
    pthread_mutex_lock(&wsms_mutex);
    if (wsms_length > 0) {
        wsm = wsms[--wsms_length];
    } else {
        wsm.wsm = NULL;
        // To make Coverity happy
        wsm.len = 0;
    }
    pthread_mutex_unlock(&wsms_mutex);
    return wsm;
}

/* Only 1 device is supported */
static inline bool isValidDevice(uint32_t deviceId)
{
    return (MC_DEVICE_ID_DEFAULT == deviceId);
}

/* Convert driver errors into tbase set */
static mcResult_t convert_syscall_error(int32_t ret)
{
    switch (ret) {
    case 0:
        return MC_DRV_OK;
    case -1:
        switch (errno) {
            case ENOMSG:
                return MC_DRV_NO_NOTIFICATION;
            case EBADMSG:
                return MC_DRV_ERR_NOTIFICATION;
            case ENOSPC:
                return MC_DRV_ERR_OUT_OF_RESOURCES;
            case EHOSTDOWN:
                return MC_DRV_ERR_INIT;
            case ENODEV:
                return MC_DRV_ERR_UNKNOWN_DEVICE;
            case ENXIO:
                return MC_DRV_ERR_UNKNOWN_SESSION;
            case EPERM:
                return MC_DRV_ERR_INVALID_OPERATION;
            case EBADE:
                return MC_DRV_ERR_INVALID_RESPONSE;
            case ETIME:
                return MC_DRV_ERR_TIMEOUT;
            case ENOMEM:
                return MC_DRV_ERR_NO_FREE_MEMORY;
            case EUCLEAN:
                return MC_DRV_ERR_FREE_MEMORY_FAILED;
            case ENOTEMPTY:
                return MC_DRV_ERR_SESSION_PENDING;
            case EHOSTUNREACH:
                return MC_DRV_ERR_DAEMON_UNREACHABLE;
            case ENOENT:
                return MC_DRV_ERR_INVALID_DEVICE_FILE;
            case EINVAL:
                return MC_DRV_ERR_INVALID_PARAMETER;
            case EPROTO:
                return MC_DRV_ERR_KERNEL_MODULE;
            case EADDRINUSE:
                return MC_DRV_ERR_BULK_MAPPING;
            case EADDRNOTAVAIL:
                return MC_DRV_ERR_BULK_UNMAPPING;
            case ECOMM:
                return MC_DRV_INFO_NOTIFICATION;
            case EUNATCH:
                return MC_DRV_ERR_NQ_FAILED;
            case EBADF:
                return MC_DRV_ERR_DAEMON_DEVICE_NOT_OPEN;
            case EINTR:
                return MC_DRV_ERR_INTERRUPTED_BY_SIGNAL;
            case EKEYREJECTED:
                return MC_DRV_ERR_WRONG_PUBLIC_KEY;
            case ECONNREFUSED:
                return MC_DRV_ERR_SERVICE_BLOCKED;
            case ECONNABORTED:
                return MC_DRV_ERR_SERVICE_LOCKED;
            case ECONNRESET:
                return MC_DRV_ERR_SERVICE_KILLED;
            default:
                LOG_ERRNO("syscall");
                return MC_DRV_ERR_UNKNOWN;
        }
    default:
        LOG_E("Unknown code %d", ret);
        return MC_DRV_ERR_UNKNOWN;
    }
}

//------------------------------------------------------------------------------
//		CLIENT API
//------------------------------------------------------------------------------
__MC_CLIENT_LIB_API mcResult_t mcOpenDevice(
    uint32_t deviceId
)
{
    LOG_D("===%s(%i)===", __FUNCTION__, deviceId);

    // Check parameters
    if (!isValidDevice(deviceId)) {
        return MC_DRV_ERR_UNKNOWN_DEVICE;
    }

    if (client.open()) {
        return convert_syscall_error(-1);
    }

    return MC_DRV_OK;
}


//------------------------------------------------------------------------------
__MC_CLIENT_LIB_API mcResult_t mcCloseDevice(
    uint32_t deviceId
)
{
    LOG_D("===%s(%i)===", __FUNCTION__, deviceId);

    // Check parameters
    if (!isValidDevice(deviceId)) {
        return MC_DRV_ERR_UNKNOWN_DEVICE;
    }

    // Check whether there are still some 'devices open'
    if (client.closeCheck() == 0) {
        return MC_DRV_OK;
    }

    // Check that all sessions are closed and freeze client
    mcResult_t mcResult = convert_syscall_error(client.freeze());
    if (mcResult != MC_DRV_OK) {
        return mcResult;
    }

    // Free all remaining WSMs
    while (true) {
        struct maplen wsm = wsms_removeLast();
        if (wsm.wsm == NULL) {
            break;
        }
        mcFreeWsm(deviceId, wsm.wsm);
    }

    // Close the device
    client.close();
    return MC_DRV_OK;
}


//------------------------------------------------------------------------------
__MC_CLIENT_LIB_API mcResult_t mcOpenSession(
    mcSessionHandle_t  *session,
    const mcUuid_t     *uuid,
    uint8_t            *tci,
    uint32_t           len
)
{
    mcResult_t mcResult = MC_DRV_ERR_UNKNOWN;
    struct mc_ioctl_open_session sess;
    int ret;

    LOG_D("===%s()===", __FUNCTION__);

    do {
        // Check parameters
        if (!session) {
            mcResult = MC_DRV_ERR_INVALID_PARAMETER;
            break;
        }
        if (!isValidDevice(session->deviceId)) {
            mcResult = MC_DRV_ERR_UNKNOWN_DEVICE;
            break;
        }
        if (!uuid) {
            mcResult = MC_DRV_ERR_INVALID_PARAMETER;
            break;
        }

        // Call ioctl
        sess.sid = 0;
        memcpy( (void *)&sess.uuid, uuid, sizeof(sess.uuid) );
        sess.tci = (uintptr_t)tci;
        sess.tcilen = len;
        sess.is_gp_uuid = false;
        sess.identity.login_type = TEEC_LOGIN_PUBLIC;
        ret = client.openSession(sess);
        mcResult = convert_syscall_error(ret);
        if (MC_DRV_OK != mcResult) {
            break;
        }

        // Fill in return parameters
        session->sessionId = sess.sid;
    } while (false);

    /* Legacy error matching */
    if (MC_DRV_OK != mcResult) {
        if (MC_DRV_ERR_UNKNOWN == mcResult)
            mcResult = MC_DRV_ERR_INVALID_PARAMETER;

        LOG_E("return 0x%x", mcResult);
    }

    return mcResult;
}

//------------------------------------------------------------------------------
__MC_CLIENT_LIB_API mcResult_t mcOpenTrustlet(
    mcSessionHandle_t  *session,
    mcSpid_t           spid,
    uint8_t            *trustlet,
    uint32_t           tLen,
    uint8_t            *tci,
    uint32_t           tciLen
)
{
    mcResult_t mcResult = MC_DRV_ERR_UNKNOWN;
    struct mc_ioctl_open_trustlet trus;
    int ret;

    LOG_D("===%s()===", __FUNCTION__);

    do {
        // Check parameters
        if (!session) {
            LOG_E("Session handle is null");
            mcResult = MC_DRV_ERR_INVALID_PARAMETER;
            break;
        }
        if (!isValidDevice(session->deviceId)) {
            mcResult = MC_DRV_ERR_UNKNOWN_DEVICE;
            break;
        }

        // Call ioctl
        trus.sid = 0;
        trus.spid = spid;
        trus.buffer = (uintptr_t)trustlet;
        trus.tlen = tLen;
        trus.tci = (uintptr_t)tci;
        trus.tcilen = tciLen;
        ret = client.openTrustlet(trus);
        mcResult = convert_syscall_error(ret);

        // Fill in return parameters
        if (MC_DRV_OK == mcResult) {
            session->sessionId = trus.sid;
        }

    } while (false);

    /* Legacy error matching */
    if (MC_DRV_OK != mcResult) {
        if (MC_DRV_ERR_UNKNOWN == mcResult)
            mcResult = MC_DRV_ERR_INVALID_PARAMETER;

        LOG_E("return 0x%x", mcResult);
    }

    return mcResult;
}


//------------------------------------------------------------------------------
__MC_CLIENT_LIB_API mcResult_t mcCloseSession(
    mcSessionHandle_t *session
)
{
    mcResult_t mcResult = MC_DRV_ERR_UNKNOWN;
    int ret;

    LOG_D("===%s()===", __FUNCTION__);

    do {
        // Check parameters
        if (!session) {
            LOG_E("Session handle is null");
            mcResult = MC_DRV_ERR_INVALID_PARAMETER;
            break;
        }
        if (!isValidDevice(session->deviceId)) {
            mcResult = MC_DRV_ERR_UNKNOWN_DEVICE;
            break;
        }

        // Call ioctl
        ret = client.closeSession(session->sessionId);
        mcResult = convert_syscall_error(ret);

    } while (false);

    return mcResult;
}


//------------------------------------------------------------------------------
__MC_CLIENT_LIB_API mcResult_t mcNotify(
    mcSessionHandle_t   *session
)
{
    mcResult_t mcResult = MC_DRV_ERR_UNKNOWN;
    int ret;

    LOG_D("===%s()===", __FUNCTION__);

    do {
        // Check parameters
        if (!session) {
            LOG_E("Session handle is null");
            mcResult = MC_DRV_ERR_INVALID_PARAMETER;
            break;
        }
        if (!isValidDevice(session->deviceId)) {
            mcResult = MC_DRV_ERR_UNKNOWN_DEVICE;
            break;
        }

        // Call ioctl
        ret = client.notify(session->sessionId);
        mcResult = convert_syscall_error(ret);

    } while (false);

    return mcResult;
}


//------------------------------------------------------------------------------
__MC_CLIENT_LIB_API mcResult_t mcWaitNotification(
    mcSessionHandle_t  *session,
    int32_t            timeout
)
{
    mcResult_t mcResult = MC_DRV_ERR_UNKNOWN;
    struct mc_ioctl_wait wait;
    int ret;

    LOG_D("===%s()===", __FUNCTION__);

    do {
        // Check parameters
        if (!session) {
            LOG_E("Session handle is null");
            mcResult = MC_DRV_ERR_INVALID_PARAMETER;
            break;
        }
        if (!isValidDevice(session->deviceId)) {
            mcResult = MC_DRV_ERR_UNKNOWN_DEVICE;
            break;
        }

        // Call ioctl
        wait.sid = session->sessionId;
        wait.timeout = timeout;
        do {
            ret = client.waitNotification(wait);
            mcResult = convert_syscall_error(ret);
        } while (MC_INFINITE_TIMEOUT == timeout &&
                MC_DRV_ERR_INTERRUPTED_BY_SIGNAL == mcResult);

    } while (false);

    return mcResult;
}


//------------------------------------------------------------------------------
__MC_CLIENT_LIB_API mcResult_t mcMallocWsm(
    uint32_t    deviceId,
    uint32_t    align,
    uint32_t    len,
    uint8_t     **wsm,
    uint32_t    wsmFlags
)
{
    (void) align;
    (void) wsmFlags;
    mcResult_t mcResult = MC_DRV_ERR_UNKNOWN;
    LOG_D("===%s(len=%i)===", __FUNCTION__, len);

    do {
        // Check parameters
        if (!isValidDevice(deviceId)) {
            mcResult = MC_DRV_ERR_UNKNOWN_DEVICE;
            break;
        }
        if (!wsm) {
            LOG_E("Wsm pointer is null");
            mcResult = MC_DRV_ERR_INVALID_PARAMETER;
            break;
        }
        // Alloc and map kernel buffer into user space
        if (client.malloc(wsm, len)) {
            mcResult = convert_syscall_error(-1);
            break;
        }
        wsms_add(*wsm, len);
        mcResult = MC_DRV_OK;

    } while (false);

    return mcResult;
}


//------------------------------------------------------------------------------
// Note: mcFreeWsm() only succeeds if wsm is as returned by mcMallocWsm().
__MC_CLIENT_LIB_API mcResult_t mcFreeWsm(
    uint32_t    deviceId,
    uint8_t     *wsm
)
{
    mcResult_t mcResult = MC_DRV_ERR_UNKNOWN;

    LOG_D("===%s(%p)===", __FUNCTION__, wsm);

    do {
            // Check parameters
            if (!isValidDevice(deviceId)) {
                mcResult = MC_DRV_ERR_UNKNOWN_DEVICE;
                break;
            }

            // Get mapping length
            uint32_t len = wsms_len(wsm);
            if (len == 0) {
                    LOG_E("wsm %p length not available", wsm);
                    mcResult = MC_DRV_ERR_INVALID_PARAMETER;
            } else {
                    // Unmap and free kernel buffer
                    int ret = client.free(wsm, len);
                    mcResult = convert_syscall_error(ret);
                    if (ret == 0) {
                        wsms_remove(wsm);
                    }
            }
    } while (false);

    return mcResult;
}

//------------------------------------------------------------------------------
__MC_CLIENT_LIB_API mcResult_t mcMap(
    mcSessionHandle_t  *session,
    void               *buf,
    uint32_t           bufLen,
    mcBulkMap_t        *mapInfo
)
{
    mcResult_t mcResult = MC_DRV_ERR_UNKNOWN;
    struct mc_ioctl_map map;
    int ret;

    LOG_D("===%s()===", __FUNCTION__);

    do {
        // Check parameters
        if (!session) {
            LOG_E("Session handle is null");
            mcResult = MC_DRV_ERR_INVALID_PARAMETER;
            break;
        }
        if (!isValidDevice(session->deviceId)) {
            mcResult = MC_DRV_ERR_UNKNOWN_DEVICE;
            break;
        }
        if (!mapInfo) {
            LOG_E("mapInfo pointer is null");
            mcResult = MC_DRV_ERR_INVALID_PARAMETER;
            break;
        }

        // Call ioctl
        map.sid = session->sessionId;
        map.bufs[0].va = (uintptr_t)buf;
        map.bufs[0].len = bufLen;
        map.bufs[0].flags = MC_IO_MAP_INPUT | MC_IO_MAP_OUTPUT;
        for (int i = 1; i < MC_MAP_MAX; i++) {
            map.bufs[i].va = 0;
        }
        ret = client.map(map);
        mcResult = convert_syscall_error(ret);
        if (MC_DRV_OK != mcResult) {
            break;
        }
        LOG_D("map'd va=%llx len=%u sva=%llx",
                map.bufs[0].va, map.bufs[0].len, map.bufs[0].sva);

        // Fill in return parameters
#if ( __WORDSIZE != 64 )
        mapInfo->sVirtualAddr = (void *)(uintptr_t)map.bufs[0].sva;
#else
        mapInfo->sVirtualAddr = (uint32_t)map.bufs[0].sva;
#endif
        mapInfo->sVirtualLen = map.bufs[0].len;
    } while (false);

    /* Legacy error matching */
    if (MC_DRV_OK != mcResult) {
        if (MC_DRV_ERR_UNKNOWN == mcResult)
            mcResult = MC_DRV_ERR_BULK_MAPPING;

        LOG_E("return 0x%x", mcResult);
    }

    return mcResult;
}

//------------------------------------------------------------------------------
__MC_CLIENT_LIB_API mcResult_t mcUnmap(
    mcSessionHandle_t  *session,
    void               *buf,
    mcBulkMap_t        *mapInfo
)
{
    mcResult_t mcResult = MC_DRV_ERR_UNKNOWN;
    struct mc_ioctl_map map;
    int ret;

    LOG_D("===%s()===", __FUNCTION__);

    do {
        // Check parameters
        if (!session) {
            LOG_E("Session handle is null");
            mcResult = MC_DRV_ERR_INVALID_PARAMETER;
            break;
        }
        if (!isValidDevice(session->deviceId)) {
            mcResult = MC_DRV_ERR_UNKNOWN_DEVICE;
            break;
        }
        if (!mapInfo) {
            LOG_E("mapInfo pointer is null");
            mcResult = MC_DRV_ERR_INVALID_PARAMETER;
            break;
        }

        // Call ioctl
        map.sid = session->sessionId;
        map.bufs[0].va = (uintptr_t)buf;
        map.bufs[0].sva = (uintptr_t)mapInfo->sVirtualAddr;
        map.bufs[0].len = mapInfo->sVirtualLen;
        LOG_D("unmap va=%llx len=%u sva=%llx",
                map.bufs[0].va, map.bufs[0].len, map.bufs[0].sva);
        for (int i = 1; i < MC_MAP_MAX; i++) {
            map.bufs[i].va = 0;
        }
        ret = client.unmap(map);
        mcResult = convert_syscall_error(ret);

    } while (false);

    if (MC_DRV_OK != mcResult) {
        if (MC_DRV_ERR_UNKNOWN == mcResult)
            mcResult = MC_DRV_ERR_BULK_UNMAPPING;

        LOG_E("return 0x%x", mcResult);
    }

    return mcResult;
}


//------------------------------------------------------------------------------
__MC_CLIENT_LIB_API mcResult_t mcGetSessionErrorCode(
    mcSessionHandle_t   *session,
    int32_t             *lastErr
)
{
    mcResult_t mcResult = MC_DRV_ERR_UNKNOWN;
    struct mc_ioctl_geterr err;
    int ret;

    LOG_D("===%s()===", __FUNCTION__);

    do {
        // Check parameters
        if (!session) {
            LOG_E("Session handle is null");
            mcResult = MC_DRV_ERR_INVALID_PARAMETER;
            break;
        }
        if (!isValidDevice(session->deviceId)) {
            mcResult = MC_DRV_ERR_UNKNOWN_DEVICE;
            break;
        }
        if (!lastErr) {
            mcResult = MC_DRV_ERR_INVALID_PARAMETER;
            break;
        }

        // Call ioctl
        err.sid = session->sessionId;
        ret = client.getError(err);
        mcResult = convert_syscall_error(ret);
        if (MC_DRV_OK != mcResult) {
            break;
        }

        // Fill in return parameters
        *lastErr = err.value;

    } while (false);

    return mcResult;
}

//------------------------------------------------------------------------------
__MC_CLIENT_LIB_API mcResult_t mcGetMobiCoreVersion(
    uint32_t  deviceId,
    mcVersionInfo_t *versionInfo
)
{
    mcResult_t mcResult = MC_DRV_ERR_UNKNOWN;
    int ret;

    LOG_D("===%s()===", __FUNCTION__);

    do {
        // Check parameters
        if (!isValidDevice(deviceId)) {
            mcResult = MC_DRV_ERR_UNKNOWN_DEVICE;
            break;
        }
        if (!versionInfo) {
            LOG_E("versionInfo pointer is null");
            mcResult = MC_DRV_ERR_INVALID_PARAMETER;
            break;
        }

        struct mc_version_info version_info;
        ret = client.getVersion(version_info);
        if (!ret) {
            memcpy(versionInfo, &version_info, sizeof(*versionInfo));
        }
        mcResult = convert_syscall_error(ret);
    } while (false);

    return mcResult;
}
