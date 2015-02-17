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
/**
 * <t-base Driver API.
 *
 * Functions for accessing <t-base functionality from the normal world.
 * Handles sessions and notifications via MCI buffer.
 */
#include <stdint.h>
#ifndef WIN32
#include <stdbool.h>
#include <list>
#include <errno.h>
#include "assert.h"
#endif

#include "public/MobiCoreDriverApi.h"

#ifndef WIN32
#include "mc_linux.h"
#include "Connection.h"
#include "CMutex.h"
#include "Device.h"
#include "mcVersionHelper.h"

#include "Daemon/public/MobiCoreDriverCmd.h"
#include "Daemon/public/mcVersion.h"

#include "log.h"

#include "Mci/mcimcp.h"

MC_CHECK_VERSION(DAEMON, 0, 2);

/** Notification data structure. */
typedef struct {
    uint32_t sessionId; /**< Session ID. */
    int32_t payload; /**< Additional notification information. */
} notification_t;

using namespace std;

static list<Device *> devices;

// Forward declarations.
uint32_t getDaemonVersion(Connection *devCon, uint32_t *version);

static CMutex devMutex;
//------------------------------------------------------------------------------
Device *resolveDeviceId(uint32_t deviceId)
{
    for (list<Device *>::iterator iterator = devices.begin();
            iterator != devices.end();
            ++iterator) {
        Device  *device = (*iterator);

        if (device->deviceId == deviceId) {
            return device;
        }
    }
    return NULL;
}


//------------------------------------------------------------------------------
void addDevice(Device *device)
{
    devices.push_back(device);
}


//------------------------------------------------------------------------------
bool removeDevice(uint32_t deviceId)
{
    for (list<Device *>::iterator iterator = devices.begin();
            iterator != devices.end();
            ++iterator) {
        Device  *device = (*iterator);

        if (device->deviceId == deviceId) {
            devices.erase(iterator);
            delete device;
            return true;
        }
    }
    return false;
}

//------------------------------------------------------------------------------
// Parameter checking functions
// Note that android-ndk renames __func__ to __PRETTY_FUNCTION__
// see also /prebuilt/ndk/android-ndk-r4/platforms/android-8/arch-arm/usr/include/sys/cdefs.h

#define CHECK_DEVICE(device) \
    if (NULL == device) \
    { \
        LOG_E("Device has not been found"); \
        mcResult = MC_DRV_ERR_UNKNOWN_DEVICE; \
        break; \
    }

#define CHECK_DEVICE_CLOSED(device,  deviceId) \
    if (NULL == device && MC_DEVICE_ID_DEFAULT == deviceId) \
    { \
        LOG_E("Device not open"); \
        mcResult = MC_DRV_ERR_DAEMON_DEVICE_NOT_OPEN; \
        break; \
    } else \
        CHECK_DEVICE(device);



#define CHECK_NOT_NULL(X) \
    if (NULL == X) \
    { \
        LOG_E("Parameter \""#X "\" is NULL"); \
        mcResult = MC_DRV_ERR_NULL_POINTER; \
        break; \
    }

#define CHECK_SESSION(S,SID) \
    if (NULL == S) \
    { \
        LOG_E("Session %03x not found", SID); \
        mcResult = MC_DRV_ERR_UNKNOWN_SESSION; \
        break; \
    }

//------------------------------------------------------------------------------
// Socket marshaling and checking functions
#define SEND_TO_DAEMON(CONNECTION, COMMAND, ...) \
{ \
    COMMAND ##_struct x = { \
        COMMAND, \
        __VA_ARGS__ \
    }; \
    int ret = CONNECTION->writeData(&x, sizeof x); \
    if(ret < 0) { \
        LOG_E("sending to Daemon failed."); \
        mcResult = MC_DRV_ERR_SOCKET_WRITE; \
        break; \
    } \
}

#define RECV_FROM_DAEMON(CONNECTION, RSP_STRUCT) \
{ \
    int rlen = CONNECTION->readData( \
            RSP_STRUCT, \
            sizeof(*RSP_STRUCT)); \
    if (rlen <= 0) { \
        LOG_E("reading from Daemon failed"); \
        mcResult = MC_DRV_ERR_SOCKET_READ; \
        break; \
    } \
    if (rlen != sizeof(*RSP_STRUCT) && rlen != sizeof(mcDrvResponseHeader_t)) {\
        LOG_E("wrong buffer length %i received from Daemon", rlen); \
        mcResult = MC_DRV_ERR_SOCKET_LENGTH; \
        break; \
    } \
}
#endif /* WIN32 */

//------------------------------------------------------------------------------
__MC_CLIENT_LIB_API mcResult_t mcOpenDevice(uint32_t deviceId)
{
    mcResult_t mcResult = MC_DRV_OK;
#ifndef WIN32

    Connection *devCon = NULL;

    devMutex.lock();
    LOG_I("===%s(%i)===", __FUNCTION__, deviceId);

    do {
        Device *device = resolveDeviceId(deviceId);
        if (device != NULL) {
            LOG_E("Device %d already opened", deviceId);
            mcResult = MC_DRV_OK;
            device->openCount++;
            break;
        }

        // Handle SIGPIPE inside write()
        //  If Daemon crashes and ClientLib writes to named socket,
        //  a sigpipe is send to ClientLib/TLC and kills it.
        signal(SIGPIPE, SIG_IGN);

        // Open new connection to device
        devCon = new Connection();
        if (!devCon->connect(SOCK_PATH)) {
            LOG_W(" Could not connect to %s socket", SOCK_PATH);
            mcResult = MC_DRV_ERR_SOCKET_CONNECT;
            break;
        }

        // Runtime check of Daemon version.
        char *errmsg;
        uint32_t version = 0;
        mcResult = getDaemonVersion(devCon, &version);
        if (mcResult != MC_DRV_OK) {
            break;
        }
        if (!checkVersionOkDAEMON(version, &errmsg)) {
            LOG_E("%s", errmsg);
            mcResult = MC_DRV_ERR_DAEMON_VERSION;
            break;
        }
        LOG_I(" %s", errmsg);

        // Forward device open to the daemon and read result
        SEND_TO_DAEMON(devCon, MC_DRV_CMD_OPEN_DEVICE, deviceId);

        RECV_FROM_DAEMON(devCon, &mcResult);

        if (mcResult != MC_DRV_OK) {
            LOG_W(" %s(): Request at Daemon failed, respId=%x ", __FUNCTION__, mcResult);
            break;
        }

        // there is no payload to read

        device = new Device(deviceId, devCon);
        mcResult = device->open("/dev/" MC_USER_DEVNODE);
        if (mcResult != MC_DRV_OK) {
            delete device;
            // devCon is freed in the Device destructor
            devCon = NULL;
            LOG_E("Could not open device file: /dev/%s", MC_USER_DEVNODE);
            break;
        }

        addDevice(device);
        device->openCount++;

    } while (false);

    devMutex.unlock();
    if (mcResult != MC_DRV_OK) {
        if (devCon != NULL)
            delete devCon;
        LOG_I(" Device not opened.");
    } else {
        LOG_I(" Successfully opened the device.");
    }

#endif /* WIN32 */
    return mcResult;
}


//------------------------------------------------------------------------------
__MC_CLIENT_LIB_API mcResult_t mcCloseDevice(
    uint32_t deviceId
)
{
    mcResult_t mcResult = MC_DRV_OK;
#ifndef WIN32

	devMutex.lock();
    LOG_I("===%s(%i)===", __FUNCTION__, deviceId);
    do {
        Device *device = resolveDeviceId(deviceId);
        CHECK_DEVICE_CLOSED(device, deviceId);

        Connection *devCon = device->connection;

        if (device->openCount != 1) {
            device->openCount--;
            break;
        }

        // Check if daemon is still alive
        if (!devCon->isConnectionAlive()) {
            removeDevice(deviceId);
            LOG_E("Daemon is  dead removing device");
            mcResult = MC_DRV_ERR_DAEMON_UNREACHABLE;
            break;
        }

        // Return if not all sessions have been closed
        // TODO-2012-08-31-haenellu: improve check, if device connection is dead, this makes no more sense.
        if (device->hasSessions()) {
            LOG_E("Trying to close device while sessions are still pending.");
            mcResult = MC_DRV_ERR_SESSION_PENDING;
            break;
        }

        SEND_TO_DAEMON(devCon, MC_DRV_CMD_CLOSE_DEVICE);

        RECV_FROM_DAEMON(devCon, &mcResult);

        if (mcResult != MC_DRV_OK) {
            LOG_W(" %s(): Request at Daemon failed, respId=%d ", __FUNCTION__, mcResult);
            break;
        }

        removeDevice(deviceId);

    } while (false);

    devMutex.unlock();

#endif /* WIN32 */
	return mcResult;
}


//------------------------------------------------------------------------------
__MC_CLIENT_LIB_API mcResult_t mcOpenSession(
    mcSessionHandle_t  *session,
    const mcUuid_t     *uuid,
    uint8_t            *tci,
    uint32_t           len
)
{
    mcResult_t mcResult = MC_DRV_OK;
#ifndef WIN32

    devMutex.lock();
    LOG_I("===%s()===", __FUNCTION__);

    BulkBufferDescriptor *bulkBuf = NULL;

    do {
        uint32_t handle = 0;
        CHECK_NOT_NULL(session);
        CHECK_NOT_NULL(uuid);

        if (len > MC_MAX_TCI_LEN) {
            LOG_E("TCI length is longer than %d", MC_MAX_TCI_LEN);
            mcResult = MC_DRV_ERR_TCI_TOO_BIG;
            break;
        }

        // Get the device associated with the given session
        Device *device = resolveDeviceId(session->deviceId);
        CHECK_DEVICE(device);

        Connection *devCon = device->connection;

        // First assume the TCI is a contiguous buffer
        // Get the physical address of the given TCI
        CWsm_ptr pWsm = device->findContiguousWsm(tci);
        if (pWsm == NULL) {
            if (tci != NULL && len != 0) {
                // Then assume it's a normal buffer that needs to be mapped
                mcResult = device->mapBulkBuf(tci, len, &bulkBuf);
                if (mcResult != MC_DRV_OK) {
                    LOG_E("Registering buffer failed. ret=%x", mcResult);
                    mcResult = MC_DRV_ERR_WSM_NOT_FOUND;
                    break;
                }
                handle = bulkBuf->handle;
            } else if ( len != 0 ) {
                LOG_E("mcOpenSession(): length is more than allocated TCI");
                mcResult = MC_DRV_ERR_TCI_GREATER_THAN_WSM;
                break;
            }
        } else {
            if (pWsm->len < len) {
                LOG_E("mcOpenSession(): length is more than allocated TCI");
                mcResult = MC_DRV_ERR_TCI_GREATER_THAN_WSM;
                break;
            }
            handle = pWsm->handle;
        }

        SEND_TO_DAEMON(devCon, MC_DRV_CMD_OPEN_SESSION,
                       session->deviceId,
                       *uuid,
                       (uint32_t)((uintptr_t)tci & 0xFFF),
                       handle,
                       len);

        // Read command response
        RECV_FROM_DAEMON(devCon, &mcResult);

        if (mcResult != MC_DRV_OK) {
            // TODO-2012-09-06-haenellu: Remove this code once tests can handle it

            if (MC_DRV_ERROR_MAJOR(mcResult) != MC_DRV_ERR_MCP_ERROR) {
                LOG_E("Daemon could not open session, responseId %d.", mcResult);
            } else {
                uint32_t mcpResult = MC_DRV_ERROR_MCP(mcResult);
                LOG_E("<t-base reported failing of MC_MCP_CMD_OPEN_SESSION command, mcpResult %d.", mcpResult);

                // IMPROVEMENT-2012-09-03-haenellu: Remove this switch case and use MCP code in tests.
                switch (mcpResult) {
                case MC_MCP_RET_ERR_WRONG_PUBLIC_KEY:
                    mcResult = MC_DRV_ERR_WRONG_PUBLIC_KEY;
                    break;
                case MC_MCP_RET_ERR_CONTAINER_TYPE_MISMATCH:
                    mcResult = MC_DRV_ERR_CONTAINER_TYPE_MISMATCH;
                    break;
                case MC_MCP_RET_ERR_CONTAINER_LOCKED:
                    /* this is not a perfect match, but close enough */
                case MC_MCP_RET_ERR_SERVICE_BLOCKED:
                    /* this is not a perfect match, but close enough */
                case MC_MCP_RET_ERR_SERVICE_LOCKED:
                    mcResult = MC_DRV_ERR_CONTAINER_LOCKED;
                    break;
                case MC_MCP_RET_ERR_SP_NO_CHILD:
                    mcResult = MC_DRV_ERR_SP_NO_CHILD;
                    break;
                case MC_MCP_RET_ERR_TL_NO_CHILD:
                    mcResult = MC_DRV_ERR_TL_NO_CHILD;
                    break;
                case MC_MCP_RET_ERR_UNWRAP_ROOT_FAILED:
                    mcResult = MC_DRV_ERR_UNWRAP_ROOT_FAILED;
                    break;
                case MC_MCP_RET_ERR_UNWRAP_SP_FAILED:
                    mcResult = MC_DRV_ERR_UNWRAP_SP_FAILED;
                    break;
                case MC_MCP_RET_ERR_UNWRAP_TRUSTLET_FAILED:
                    mcResult = MC_DRV_ERR_UNWRAP_TRUSTLET_FAILED;
                    break;
                case MC_MCP_RET_ERR_UNKNOWN_UUID:
                    mcResult = MC_DRV_ERR_TRUSTED_APPLICATION_NOT_FOUND;
                    break;
                default:
                    // TODO-2012-09-06-haenellu: Remove line and adapt codes in tests.
                    mcResult = MC_DRV_ERR_MCP_ERROR;
                    break;
                }
            }
            break; // loading of Trustlet failed, unlock mutex and return
        }

        // read payload
        mcDrvRspOpenSessionPayload_t rspOpenSessionPayload;
        RECV_FROM_DAEMON(devCon, &rspOpenSessionPayload);

        // Register session with handle
        session->sessionId = rspOpenSessionPayload.sessionId;

        LOG_I(" Service is started. Setting up channel for notifications.");

        // Set up second channel for notifications
        Connection *sessionConnection = new Connection();
        if (!sessionConnection->connect(SOCK_PATH)) {
            LOG_E("Could not connect to %s", SOCK_PATH);
            delete sessionConnection;
            // Here we know we couldn't connect to the Daemon.
            // Maybe we should use existing connection to close Trustlet.
            mcResult = MC_DRV_ERR_SOCKET_CONNECT;
            break;
        }

        do {
            SEND_TO_DAEMON(sessionConnection, MC_DRV_CMD_NQ_CONNECT,
                           session->deviceId,
                           session->sessionId,
                           rspOpenSessionPayload.deviceSessionId,
                           rspOpenSessionPayload.sessionMagic);

            RECV_FROM_DAEMON(sessionConnection, &mcResult);

            if (mcResult != MC_DRV_OK) {
                LOG_E("CMD_NQ_CONNECT failed, respId=%d", mcResult);
                break;
            }

        } while (0);
        if (mcResult != MC_DRV_OK) {
            delete sessionConnection;
            // Here we know we couldn't communicate well with the Daemon.
            // Maybe we should use existing connection to close Trustlet.
            break; // unlock mutex and return
        }

        // there is no payload.

        // Session has been established, new session object must be created
        Session *sessionObj = device->createNewSession(session->sessionId, sessionConnection);
        // If the session tci was a mapped buffer then register it
        if (bulkBuf)
            sessionObj->addBulkBuf(bulkBuf);

        LOG_I(" Successfully opened session %03x.", session->sessionId);

    } while (false);

    if (mcResult != MC_DRV_OK && bulkBuf) {
        delete bulkBuf;
    }

// TODO: enable as soon as there are more error codes
//    if (mcResult == MC_DRV_ERR_SOCKET_WRITE || mcResult == MC_DRV_ERR_SOCKET_READ) {
//        LOG_E("Connection is dead, removing device.");
//        removeDevice(session->deviceId);
//    }

    devMutex.unlock();

#endif /* WIN32 */
    return mcResult;
}

//------------------------------------------------------------------------------
__MC_CLIENT_LIB_API mcResult_t mcOpenTrustlet(
    mcSessionHandle_t  *session,
    mcSpid_t           spid,
    uint8_t            *trustlet,
    uint32_t           tlen,
    uint8_t            *tci,
    uint32_t           len
)
{
    mcResult_t mcResult = MC_DRV_OK;
#ifndef WIN32

    devMutex.lock();
    LOG_I("===%s()===", __FUNCTION__);

    BulkBufferDescriptor *bulkBuf = NULL;

    do {
        uint32_t handle = 0;
        CHECK_NOT_NULL(session);
        CHECK_NOT_NULL(trustlet);
        CHECK_NOT_NULL(tci);

        if (len > MC_MAX_TCI_LEN) {
            LOG_E("TCI length is longer than %d", MC_MAX_TCI_LEN);
            mcResult = MC_DRV_ERR_TCI_TOO_BIG;
            break;
        }

        // Get the device associated with the given session
        Device *device = resolveDeviceId(session->deviceId);
        CHECK_DEVICE(device);

        Connection *devCon = device->connection;

        // First assume the TCI is a contiguous buffer
        // Get the physical address of the given TCI
        CWsm_ptr pWsm = device->findContiguousWsm(tci);
        if (pWsm == NULL) {
            // Then assume it's a normal buffer that needs to be mapped
            mcResult = device->mapBulkBuf(tci, len, &bulkBuf);
            if (mcResult != MC_DRV_OK) {
                LOG_E("Registering buffer failed. ret=%x", mcResult);
                mcResult = MC_DRV_ERR_WSM_NOT_FOUND;;
                break;
            }
            handle = bulkBuf->handle;
        } else {
            if (pWsm->len < len) {
                LOG_E("mcOpenSession(): length is more than allocated TCI");
                mcResult = MC_DRV_ERR_TCI_GREATER_THAN_WSM;
                break;
            }
            handle = pWsm->handle;
        }

        SEND_TO_DAEMON(devCon, MC_DRV_CMD_OPEN_TRUSTLET,
                       session->deviceId,
                       spid,
                       (uint32_t)tlen,
                       (uint32_t)((uintptr_t)tci & 0xFFF),
                       handle,
                       len);

        // Send the full trustlet data
        int ret = devCon->writeData(trustlet, tlen);
        if (ret < 0) {
            LOG_E("sending to Daemon failed.");
            \
            mcResult = MC_DRV_ERR_SOCKET_WRITE;
            \
            break;
        }

        // Read command response
        RECV_FROM_DAEMON(devCon, &mcResult);

        if (mcResult != MC_DRV_OK) {
            // TODO-2012-09-06-haenellu: Remove this code once tests can handle it

            if (MC_DRV_ERROR_MAJOR(mcResult) != MC_DRV_ERR_MCP_ERROR) {
                LOG_E("Daemon could not open session, responseId %d.", mcResult);
            } else {
                uint32_t mcpResult = MC_DRV_ERROR_MCP(mcResult);
                LOG_E("<t-base reported failing of MC_MCP_CMD_OPEN_SESSION command, mcpResult %d.", mcpResult);

                // IMPROVEMENT-2012-09-03-haenellu: Remove this switch case and use MCP code in tests.
                switch (mcpResult) {
                case MC_MCP_RET_ERR_WRONG_PUBLIC_KEY:
                    mcResult = MC_DRV_ERR_WRONG_PUBLIC_KEY;
                    break;
                case MC_MCP_RET_ERR_CONTAINER_TYPE_MISMATCH:
                    mcResult = MC_DRV_ERR_CONTAINER_TYPE_MISMATCH;
                    break;
                case MC_MCP_RET_ERR_CONTAINER_LOCKED:
                    mcResult = MC_DRV_ERR_CONTAINER_LOCKED;
                    break;
                case MC_MCP_RET_ERR_SP_NO_CHILD:
                    mcResult = MC_DRV_ERR_SP_NO_CHILD;
                    break;
                case MC_MCP_RET_ERR_TL_NO_CHILD:
                    mcResult = MC_DRV_ERR_TL_NO_CHILD;
                    break;
                case MC_MCP_RET_ERR_UNWRAP_ROOT_FAILED:
                    mcResult = MC_DRV_ERR_UNWRAP_ROOT_FAILED;
                    break;
                case MC_MCP_RET_ERR_UNWRAP_SP_FAILED:
                    mcResult = MC_DRV_ERR_UNWRAP_SP_FAILED;
                    break;
                case MC_MCP_RET_ERR_UNWRAP_TRUSTLET_FAILED:
                    mcResult = MC_DRV_ERR_UNWRAP_TRUSTLET_FAILED;
                    break;
                default:
                    // TODO-2012-09-06-haenellu: Remove line and adapt codes in tests.
                    mcResult = MC_DRV_ERR_MCP_ERROR;
                    break;
                }
            }
            break; // loading of Trustlet failed, unlock mutex and return
        }

        // read payload
        mcDrvRspOpenSessionPayload_t rspOpenSessionPayload;
        RECV_FROM_DAEMON(devCon, &rspOpenSessionPayload);

        // Register session with handle
        session->sessionId = rspOpenSessionPayload.sessionId;

        LOG_I(" Service is started. Setting up channel for notifications.");

        // Set up second channel for notifications
        Connection *sessionConnection = new Connection();
        if (!sessionConnection->connect(SOCK_PATH)) {
            LOG_E("Could not connect to %s", SOCK_PATH);
            delete sessionConnection;
            // Here we know we couldn't connect to the Daemon.
            // Maybe we should use existing connection to close Trustlet.
            mcResult = MC_DRV_ERR_SOCKET_CONNECT;
            break;
        }

        do {
            SEND_TO_DAEMON(sessionConnection, MC_DRV_CMD_NQ_CONNECT,
                           session->deviceId,
                           session->sessionId,
                           rspOpenSessionPayload.deviceSessionId,
                           rspOpenSessionPayload.sessionMagic);

            RECV_FROM_DAEMON(sessionConnection, &mcResult);

            if (mcResult != MC_DRV_OK) {
                LOG_E("CMD_NQ_CONNECT failed, respId=%d", mcResult);
                break;
            }

        } while (0);

        if (mcResult != MC_DRV_OK) {
            delete sessionConnection;
            // Here we know we couldn't communicate well with the Daemon.
            // Maybe we should use existing connection to close Trustlet.
            break; // unlock mutex and return
        }

        // there is no payload.

        // Session has been established, new session object must be created
        Session *sessionObj = device->createNewSession(session->sessionId, sessionConnection);
        // If the session tci was a mapped buffer then register it
        if (bulkBuf)
            sessionObj->addBulkBuf(bulkBuf);

        LOG_I(" Successfully opened session %03x.", session->sessionId);

    } while (false);

    if (mcResult != MC_DRV_OK && bulkBuf) {
        delete bulkBuf;
    }

// TODO: enable as soon as there are more error codes
//    if (mcResult == MC_DRV_ERR_SOCKET_WRITE || mcResult == MC_DRV_ERR_SOCKET_READ) {
//        LOG_E("Connection is dead, removing device.");
//        removeDevice(session->deviceId);
//    }

    devMutex.unlock();

#endif /* WIN32 */
    return mcResult;
}

//------------------------------------------------------------------------------
__MC_CLIENT_LIB_API mcResult_t mcOpenGPTA(
    mcSessionHandle_t  *session,
    const mcUuid_t     *uuid,
    uint8_t            *tci,
    uint32_t           len
)
{
    mcResult_t mcResult = MC_DRV_OK;

#ifndef WIN32
    devMutex.lock();
    LOG_I("===%s()===", __FUNCTION__);

    BulkBufferDescriptor *bulkBuf = NULL;

    do {
        uint32_t handle = 0;
        CHECK_NOT_NULL(session);
        CHECK_NOT_NULL(uuid);

        if (len > MC_MAX_TCI_LEN) {
            LOG_E("TCI length is longer than %d", MC_MAX_TCI_LEN);
            mcResult = MC_DRV_ERR_TCI_TOO_BIG;
            break;
        }

        // Get the device associated with the given session
        Device *device = resolveDeviceId(session->deviceId);
        CHECK_DEVICE(device);

        Connection *devCon = device->connection;

        // First assume the TCI is a contiguous buffer
        // Get the physical address of the given TCI
        CWsm_ptr pWsm = device->findContiguousWsm(tci);
        if (pWsm == NULL) {
            if (tci != NULL && len != 0) {
                // Then assume it's a normal buffer that needs to be mapped
                mcResult = device->mapBulkBuf(tci, len, &bulkBuf);
                if (mcResult != MC_DRV_OK) {
                    LOG_E("Registering buffer failed. ret=%x", mcResult);
                    mcResult = MC_DRV_ERR_WSM_NOT_FOUND;
                    break;
                }
                handle = bulkBuf->handle;
            } else if ( len != 0 ) {
                LOG_E("mcOpenSession(): length is more than allocated TCI");
                mcResult = MC_DRV_ERR_TCI_GREATER_THAN_WSM;
                break;
            }
        } else {
            if (pWsm->len < len) {
                LOG_E("mcOpenSession(): length is more than allocated TCI");
                mcResult = MC_DRV_ERR_TCI_GREATER_THAN_WSM;
                break;
            }
            handle = pWsm->handle;
        }

        SEND_TO_DAEMON(devCon, MC_DRV_CMD_OPEN_TRUSTED_APP,
                       session->deviceId,
                       *uuid,
                       (uint32_t)((uintptr_t)tci & 0xFFF),
                       handle,
                       len);

        // Read command response
        RECV_FROM_DAEMON(devCon, &mcResult);

        if (mcResult != MC_DRV_OK) {
            // TODO-2012-09-06-haenellu: Remove this code once tests can handle it

            if (MC_DRV_ERROR_MAJOR(mcResult) != MC_DRV_ERR_MCP_ERROR) {
                LOG_E("Daemon could not open session, responseId %d.", mcResult);
            } else {
                uint32_t mcpResult = MC_DRV_ERROR_MCP(mcResult);
                LOG_E("<t-base reported failing of MC_MCP_CMD_OPEN_SESSION command, mcpResult %d.", mcpResult);

                // IMPROVEMENT-2012-09-03-haenellu: Remove this switch case and use MCP code in tests.
                switch (mcpResult) {
                case MC_MCP_RET_ERR_WRONG_PUBLIC_KEY:
                    mcResult = MC_DRV_ERR_WRONG_PUBLIC_KEY;
                    break;
                case MC_MCP_RET_ERR_CONTAINER_TYPE_MISMATCH:
                    mcResult = MC_DRV_ERR_CONTAINER_TYPE_MISMATCH;
                    break;
                case MC_MCP_RET_ERR_CONTAINER_LOCKED:
                    mcResult = MC_DRV_ERR_CONTAINER_LOCKED;
                    break;
                case MC_MCP_RET_ERR_SP_NO_CHILD:
                    mcResult = MC_DRV_ERR_SP_NO_CHILD;
                    break;
                case MC_MCP_RET_ERR_TL_NO_CHILD:
                    mcResult = MC_DRV_ERR_TL_NO_CHILD;
                    break;
                case MC_MCP_RET_ERR_UNWRAP_ROOT_FAILED:
                    mcResult = MC_DRV_ERR_UNWRAP_ROOT_FAILED;
                    break;
                case MC_MCP_RET_ERR_UNWRAP_SP_FAILED:
                    mcResult = MC_DRV_ERR_UNWRAP_SP_FAILED;
                    break;
                case MC_MCP_RET_ERR_UNWRAP_TRUSTLET_FAILED:
                    mcResult = MC_DRV_ERR_UNWRAP_TRUSTLET_FAILED;
                    break;
                case MC_MCP_RET_ERR_UNKNOWN_UUID:
                    mcResult = MC_DRV_ERR_TRUSTED_APPLICATION_NOT_FOUND;
                    break;
                case MC_MCP_RET_ERR_SERVICE_BLOCKED:
                    mcResult = MC_DRV_ERR_SERVICE_BLOCKED;
                    break;
                case MC_MCP_RET_ERR_SERVICE_LOCKED:
                    mcResult = MC_DRV_ERR_SERVICE_LOCKED;
                    break;
                case MC_MCP_RET_ERR_SERVICE_KILLED:
                    /* because a session can execute as part of the Open Session
                     * command, it can also be killed during that phase.
                     * or can it?
                     */
                    mcResult = MC_DRV_ERR_SERVICE_KILLED;
                    break;
                case MC_MCP_RET_ERR_NO_MORE_SESSIONS:
                    mcResult = MC_DRV_ERR_NO_FREE_INSTANCES;
                    break;
                default:
                    // TODO-2012-09-06-haenellu: Remove line and adapt codes in tests.
                    mcResult = MC_DRV_ERR_MCP_ERROR;
                    break;
                }
            }
            break; // loading of Trustlet failed, unlock mutex and return
        }

        // read payload
        mcDrvRspOpenSessionPayload_t rspOpenSessionPayload;
        RECV_FROM_DAEMON(devCon, &rspOpenSessionPayload);

        // Register session with handle
        session->sessionId = rspOpenSessionPayload.sessionId;

        LOG_I(" Service is started. Setting up channel for notifications.");

        // Set up second channel for notifications
        Connection *sessionConnection = new Connection();
        if (!sessionConnection->connect(SOCK_PATH)) {
            LOG_E("Could not connect to %s", SOCK_PATH);
            delete sessionConnection;
            // Here we know we couldn't connect to the Daemon.
            // Maybe we should use existing connection to close Trustlet.
            mcResult = MC_DRV_ERR_SOCKET_CONNECT;
            break;
        }

        do {
            SEND_TO_DAEMON(sessionConnection, MC_DRV_CMD_NQ_CONNECT,
                           session->deviceId,
                           session->sessionId,
                           rspOpenSessionPayload.deviceSessionId,
                           rspOpenSessionPayload.sessionMagic);

            RECV_FROM_DAEMON(sessionConnection, &mcResult);

            if (mcResult != MC_DRV_OK) {
                LOG_E("CMD_NQ_CONNECT failed, respId=%d", mcResult);
                break;
            }

        } while (0);
        if (mcResult != MC_DRV_OK) {
            delete sessionConnection;
            // Here we know we couldn't communicate well with the Daemon.
            // Maybe we should use existing connection to close Trustlet.
            break; // unlock mutex and return
        }

        // there is no payload.

        // Session has been established, new session object must be created
        Session *sessionObj = device->createNewSession(session->sessionId, sessionConnection);
        // If the session tci was a mapped buffer then register it
        if (bulkBuf)
            sessionObj->addBulkBuf(bulkBuf);

        LOG_I(" Successfully opened session %03x.", session->sessionId);

    } while (false);

    if (mcResult != MC_DRV_OK && bulkBuf) {
        delete bulkBuf;
    }

// TODO: enable as soon as there are more error codes
//    if (mcResult == MC_DRV_ERR_SOCKET_WRITE || mcResult == MC_DRV_ERR_SOCKET_READ) {
//        LOG_E("Connection is dead, removing device.");
//        removeDevice(session->deviceId);
//    }

    devMutex.unlock();

#endif /* WIN32 */
    return mcResult;
}

//------------------------------------------------------------------------------
__MC_CLIENT_LIB_API mcResult_t mcCloseSession(mcSessionHandle_t *session)
{
    mcResult_t mcResult = MC_DRV_OK;
#ifndef WIN32

    LOG_I("===%s()===", __FUNCTION__);
    devMutex.lock();
    do {
        CHECK_NOT_NULL(session);
        LOG_I(" Closing session %03x.", session->sessionId);

        Device *device = resolveDeviceId(session->deviceId);
        CHECK_DEVICE(device);

        Connection *devCon = device->connection;

        Session *nqSession = device->resolveSessionId(session->sessionId);

        CHECK_SESSION(nqSession, session->sessionId);

        SEND_TO_DAEMON(devCon, MC_DRV_CMD_CLOSE_SESSION, session->sessionId);

        RECV_FROM_DAEMON(devCon, &mcResult);

        if (mcResult != MC_DRV_OK) {
            LOG_E("CMD_CLOSE_SESSION failed, respId=%d", mcResult);
            // TODO-2012-08-03-haenellu: Remove once tests can handle it.
            mcResult = MC_DRV_ERR_UNKNOWN_DEVICE;
            break;
        }

        bool r = device->removeSession(session->sessionId);
        if (!r)
        {
            LOG_E("removeSession failed");
            assert(0);
        }



    } while (false);

    if (mcResult == MC_DRV_ERR_SOCKET_WRITE || mcResult == MC_DRV_ERR_SOCKET_READ) {
        LOG_E("Connection is dead, removing device.");
        removeDevice(session->deviceId);
    }

    devMutex.unlock();

#endif /* WIN32 */
    return mcResult;
}


//------------------------------------------------------------------------------
__MC_CLIENT_LIB_API mcResult_t mcNotify(
    mcSessionHandle_t   *session
)
{
    mcResult_t mcResult = MC_DRV_OK;
#ifndef WIN32

	devMutex.lock();
    LOG_I("===%s()===", __FUNCTION__);

    do {
        CHECK_NOT_NULL(session);
        LOG_I(" Notifying session %03x.", session->sessionId);

        Device *device = resolveDeviceId(session->deviceId);
        CHECK_DEVICE(device);

        Connection *devCon = device->connection;

        Session *nqsession = device->resolveSessionId(session->sessionId);
        CHECK_SESSION(nqsession, session->sessionId);

        SEND_TO_DAEMON(devCon, MC_DRV_CMD_NOTIFY, session->sessionId);
        // Daemon will not return a response
    } while (false);

    if (mcResult == MC_DRV_ERR_SOCKET_WRITE) {
        LOG_E("Connection is dead, removing device.");
        removeDevice(session->deviceId);
    }

    devMutex.unlock();

#endif /* WIN32 */
    return mcResult;
}


//------------------------------------------------------------------------------
__MC_CLIENT_LIB_API mcResult_t mcWaitNotification(
    mcSessionHandle_t  *session,
    int32_t            timeout
)
{
    mcResult_t mcResult = MC_DRV_OK;
#ifndef WIN32

    // TODO-2012-11-02-gurel: devMutex locking and unlocking had to be commented out
    // below. Otherwise, when there are multiple threads in Nwd TLC side, we endup a
    // deadlock situation, e.g. one thread waits for notification and another one sends
    // notification.

    //devMutex.lock();
    LOG_I("===%s()===", __FUNCTION__);

    do {
        CHECK_NOT_NULL(session);
        LOG_I(" Waiting for notification of session %03x.", session->sessionId);

        Device *device = resolveDeviceId(session->deviceId);
        CHECK_DEVICE(device);

        Session  *nqSession = device->resolveSessionId(session->sessionId);
        CHECK_SESSION(nqSession, session->sessionId);

        Connection *nqconnection = nqSession->notificationConnection;
        uint32_t count = 0;

        // Read notification queue till it's empty
        for (;;) {
            notification_t notification;
            ssize_t numRead = nqconnection->readData(
                                  &notification,
                                  sizeof(notification_t),
                                  timeout);
            // Check for interrupted system call and loop, but only if timeout is infinite
            if ((numRead == -1) && (errno == EINTR)) {
                if (timeout == MC_INFINITE_TIMEOUT) {
                    continue;
                } else if (timeout == MC_INFINITE_TIMEOUT_INTERRUPTIBLE) {
                    mcResult = MC_DRV_ERR_INTERRUPTED_BY_SIGNAL;
                    break;
                }
            }
            //Exit on timeout in first run
            //Later runs have timeout set to 0. -2 means, there is no more data.
            if (count == 0 && numRead == -2 ) {
                LOG_W("Timeout hit at %s", __FUNCTION__);
                mcResult = MC_DRV_ERR_TIMEOUT;
                break;
            }
            if (count == 0 && numRead == 0 ) {
                LOG_E("Connection is dead, removing device.");
                removeDevice(session->deviceId);
                mcResult = MC_DRV_ERR_NOTIFICATION;
                break;
            }
            // After first notification the queue will be drained, Thus we set
            // no timeout for the following reads
            timeout = 0;

            if (numRead != sizeof(notification_t)) {
                if (count == 0) {
                    //failure in first read, notify it
                    mcResult = MC_DRV_ERR_NOTIFICATION;
                    LOG_E("read notification failed, %i bytes received", (int)numRead);
                    break;
                } else {
                    // Read of the n-th notification failed/timeout. We don't tell the
                    // caller, as we got valid notifications before.
                    mcResult = MC_DRV_OK;
                    break;
                }
            }

            count++;
            LOG_I(" Received notification %d for session %03x, payload=%d",
                  count, notification.sessionId, notification.payload);

            if (notification.payload != 0) {
                // Session end point died -> store exit code
                nqSession->setErrorInfo(notification.payload);

                mcResult = MC_DRV_INFO_NOTIFICATION;
                break;
            }
        } // for(;;)

    } while (false);

    //devMutex.unlock();

#endif /* WIN32 */
    return mcResult;
}


//------------------------------------------------------------------------------
__MC_CLIENT_LIB_API mcResult_t mcMallocWsm(
    uint32_t    deviceId,
    uint32_t    align,
    uint32_t    len,
    uint8_t     **wsm,
    uint32_t    wsmFlags)
{
    mcResult_t mcResult = MC_DRV_ERR_UNKNOWN;
#ifndef WIN32

    LOG_I("===%s(len=%i)===", __FUNCTION__, len);

    devMutex.lock();

    do {
        Device *device = resolveDeviceId(deviceId);

        // Is the device known
        // CHECK_DEVICE(device);

        // Is the device opened.
        CHECK_DEVICE_CLOSED(device, deviceId)

        CHECK_NOT_NULL(wsm);

        CWsm_ptr pWsm;
        mcResult = device->allocateContiguousWsm(len, &pWsm);
        if (mcResult != MC_DRV_OK) {
            LOG_W(" Allocation of WSM failed");
            break;
        }

        *wsm = (uint8_t *)pWsm->virtAddr;
        mcResult = MC_DRV_OK;

    } while (false);

    devMutex.unlock();

#endif /* WIN32 */
    return mcResult;
}


//------------------------------------------------------------------------------
__MC_CLIENT_LIB_API mcResult_t mcFreeWsm(
    uint32_t    deviceId,
    uint8_t     *wsm
)
{
    mcResult_t mcResult = MC_DRV_ERR_UNKNOWN;
#ifndef WIN32

    Device *device;

    devMutex.lock();

    LOG_I("===%s(%p)===", __FUNCTION__, wsm);

    do {

        // Get the device associated wit the given session
        device = resolveDeviceId(deviceId);

        // Is the device known
        CHECK_DEVICE(device);

        // Is the device opened.
        CHECK_DEVICE_CLOSED(device, deviceId)

        // find WSM object
        CWsm_ptr pWsm = device->findContiguousWsm(wsm);
        if (pWsm == NULL) {
            LOG_E("address is unknown to mcFreeWsm");
            mcResult = MC_DRV_ERR_WSM_NOT_FOUND;
            break;
        }

        // Free the given virtual address
        mcResult = device->freeContiguousWsm(pWsm);
        if (mcResult != MC_DRV_OK) {
            LOG_E("Free of virtual address failed");
            break;
        }
        mcResult = MC_DRV_OK;

    } while (false);

    devMutex.unlock();

#endif /* WIN32 */
    return mcResult;
}

//------------------------------------------------------------------------------
__MC_CLIENT_LIB_API mcResult_t mcMap(
    mcSessionHandle_t  *sessionHandle,
    void               *buf,
    uint32_t           bufLen,
    mcBulkMap_t        *mapInfo
)
{
    mcResult_t mcResult = MC_DRV_ERR_UNKNOWN;
#ifndef WIN32

    static CMutex mutex;

    LOG_I("===%s()===", __FUNCTION__);

    devMutex.lock();

    do {
        CHECK_NOT_NULL(sessionHandle);
        CHECK_NOT_NULL(mapInfo);
        CHECK_NOT_NULL(buf);

        // Determine device the session belongs to
        Device *device = resolveDeviceId(sessionHandle->deviceId);
        // Is the device known
        CHECK_DEVICE(device);

        // Is the device opened.
        CHECK_DEVICE_CLOSED(device, sessionHandle->deviceId)

        Connection *devCon = device->connection;

        // Get session
        Session *session = device->resolveSessionId(sessionHandle->sessionId);
        CHECK_SESSION(session, sessionHandle->sessionId);

        LOG_I(" Mapping %p to session %03x.", buf, sessionHandle->sessionId);

        // Register mapped bulk buffer to Kernel Module and keep mapped bulk buffer in mind
        BulkBufferDescriptor *bulkBuf;
        mcResult = session->addBulkBuf(buf, bufLen, &bulkBuf);
        if (mcResult != MC_DRV_OK) {
            LOG_E("Registering buffer failed. ret=%x", mcResult);
            break;
        }

        SEND_TO_DAEMON(devCon, MC_DRV_CMD_MAP_BULK_BUF,
                       session->sessionId,
                       bulkBuf->handle,
                       0,
                       (uint32_t)((uintptr_t)bulkBuf->virtAddr & 0xFFF),
                       bulkBuf->len);

        // Read command response
        RECV_FROM_DAEMON(devCon, &mcResult);

        if (mcResult != MC_DRV_OK) {
            LOG_E("CMD_MAP_BULK_BUF failed, respId=%d", mcResult);
            // TODO-2012-09-06-haenellu: Remove once tests can handle it.
            mcResult = MC_DRV_ERR_DAEMON_UNREACHABLE;

            // Unregister mapped bulk buffer from Kernel Module and remove mapped
            // bulk buffer from session maintenance
            if (session->removeBulkBuf(buf) != MC_DRV_OK) {
                // Removing of bulk buffer not possible
                LOG_E("Unregistering of bulk memory from Kernel Module failed");
            }
            break;
        }

        mcDrvRspMapBulkMemPayload_t rspMapBulkMemPayload;
        RECV_FROM_DAEMON(devCon, &rspMapBulkMemPayload);

        // Set mapping info for internal structures
        bulkBuf->sVirtualAddr = (uint32_t)rspMapBulkMemPayload.secureVirtualAdr;
        // Set mapping info for Trustlet
        // NOTE: The "nice" piece of code is needed because sVirtualAddr has 2 different
        // types to accomodate backward compatibility but also keep the length to 32bits
        // so on 32 bit systems it's void* but on 64 bit systems it's uint32_t
        *(uint32_t*)&mapInfo->sVirtualAddr = bulkBuf->sVirtualAddr;
        mapInfo->sVirtualLen = bufLen;
        mcResult = MC_DRV_OK;

    } while (false);

//    // TODO: enable as soon as there are more error codes
//    if (mcResult == MC_DRV_ERR_SOCKET_WRITE || mcResult == MC_DRV_ERR_SOCKET_READ) {
//        LOG_E("Connection is dead, removing device.");
//        removeDevice(sessionHandle->deviceId);
//    }

    devMutex.unlock();

#endif /* WIN32 */
    return mcResult;
}

//------------------------------------------------------------------------------
__MC_CLIENT_LIB_API mcResult_t mcUnmap(
    mcSessionHandle_t  *sessionHandle,
    void               *buf,
    mcBulkMap_t        *mapInfo
)
{
    mcResult_t mcResult = MC_DRV_ERR_UNKNOWN;
#ifndef WIN32

    static CMutex mutex;

    LOG_I("===%s()===", __FUNCTION__);

    devMutex.lock();

    do {
        CHECK_NOT_NULL(sessionHandle);
        CHECK_NOT_NULL(mapInfo);
        CHECK_NOT_NULL(buf);
        if (mapInfo->sVirtualAddr == 0) {
            LOG_E("Invalid secure virtual address %u.", (uint32_t)mapInfo->sVirtualAddr);
            mcResult = MC_DRV_ERR_NULL_POINTER;
            break;
        }

        // Determine device the session belongs to
        Device *device = resolveDeviceId(sessionHandle->deviceId);
        // Is the device known
        CHECK_DEVICE(device);

        // Is the device opened.
        CHECK_DEVICE_CLOSED(device, sessionHandle->deviceId)

        Connection  *devCon = device->connection;

        // Get session
        Session *session = device->resolveSessionId(sessionHandle->sessionId);
        CHECK_SESSION(session, sessionHandle->sessionId);

        uint32_t handle = session->getBufHandle((uint32_t)mapInfo->sVirtualAddr, mapInfo->sVirtualLen);
        if (handle == 0) {
            LOG_E("Unable to find internal handle for buffer %u.", (uint32_t)mapInfo->sVirtualAddr);
            mcResult = MC_DRV_ERR_BLK_BUFF_NOT_FOUND;
            break;
        }

        LOG_I(" Unmapping %p(handle=%u) from session %03x.", buf, handle, sessionHandle->sessionId);

        SEND_TO_DAEMON(devCon, MC_DRV_CMD_UNMAP_BULK_BUF,
                       session->sessionId,
                       handle,
                       (uint32_t)mapInfo->sVirtualAddr,
                       mapInfo->sVirtualLen);

        RECV_FROM_DAEMON(devCon, &mcResult);

        if (mcResult != MC_DRV_OK) {
            LOG_E("Daemon reported failing of UNMAP BULK BUF command, responseId %d.", mcResult);
            // TODO-2012-09-06-haenellu: Remove once tests can handle it.
            mcResult = MC_DRV_ERR_DAEMON_UNREACHABLE;
            break;
        }

        // Unregister mapped bulk buffer from Kernel Module and remove mapped
        // bulk buffer from session maintenance
        mcResult = session->removeBulkBuf(buf);
        if (mcResult != MC_DRV_OK) {
            LOG_E("Unregistering of bulk memory from Kernel Module failed.");
            break;
        }

        mcResult = MC_DRV_OK;

    } while (false);

    if (mcResult == MC_DRV_ERR_SOCKET_WRITE || mcResult == MC_DRV_ERR_SOCKET_READ) {
        LOG_E("Connection is dead, removing device.");
        removeDevice(sessionHandle->deviceId);
    }

    devMutex.unlock();

#endif /* WIN32 */
    return mcResult;
}


//------------------------------------------------------------------------------
__MC_CLIENT_LIB_API mcResult_t mcGetSessionErrorCode(
    mcSessionHandle_t   *session,
    int32_t             *lastErr
)
{
    mcResult_t mcResult = MC_DRV_OK;
#ifndef WIN32

    devMutex.lock();
    LOG_I("===%s()===", __FUNCTION__);

    do {
        CHECK_NOT_NULL(session);
        CHECK_NOT_NULL(lastErr);

        // Get device
        Device *device = resolveDeviceId(session->deviceId);
        // Is the device known
        CHECK_DEVICE(device);

        // Is the device opened.
        CHECK_DEVICE_CLOSED(device, session->deviceId)

        // Get session
        Session *nqsession = device->resolveSessionId(session->sessionId);
        CHECK_SESSION(nqsession, session->sessionId);

        // get session error code from session
        *lastErr = nqsession->getLastErr();

    } while (false);

    devMutex.unlock();

#endif /* WIN32 */
	return mcResult;
}

//------------------------------------------------------------------------------
__MC_CLIENT_LIB_API mcResult_t mcGetMobiCoreVersion(
    uint32_t  deviceId,
    mcVersionInfo_t *versionInfo
)
{
    mcResult_t mcResult = MC_DRV_OK;
#ifndef WIN32

    devMutex.lock();
    LOG_I("===%s()===", __FUNCTION__);

    do {
        Device *device = resolveDeviceId(deviceId);

        // Is the device known
        CHECK_DEVICE(device);

        // Is the device opened.
        CHECK_DEVICE_CLOSED(device, deviceId)

        CHECK_NOT_NULL(versionInfo);

        Connection *devCon = device->connection;

        SEND_TO_DAEMON(devCon, MC_DRV_CMD_GET_MOBICORE_VERSION);

        // Read GET MOBICORE VERSION response.

        RECV_FROM_DAEMON(devCon, &mcResult);

        if (mcResult != MC_DRV_OK) {
            LOG_E("MC_DRV_CMD_GET_MOBICORE_VERSION bad response, respId=%d", mcResult);
            // TODO-2012-09-06-haenellu: Remove once tests can handle it.
            mcResult = MC_DRV_ERR_DAEMON_UNREACHABLE;
            break;
        }

        // Read payload.
        mcVersionInfo_t versionInfo_socket;
        RECV_FROM_DAEMON(devCon, &versionInfo_socket);

        *versionInfo = versionInfo_socket;

    } while (0);

    devMutex.unlock();

#endif /* WIN32 */
    return mcResult;
}

#ifndef WIN32
//------------------------------------------------------------------------------
// Only called by mcOpenDevice()
// Must be taken with devMutex locked.
uint32_t getDaemonVersion(Connection *devCon, uint32_t *version)
{
    mcResult_t mcResult = MC_DRV_OK;
    uint32_t v = 0;

    LOG_I("===%s()===", __FUNCTION__);

    do {
        SEND_TO_DAEMON(devCon, MC_DRV_CMD_GET_VERSION);

        RECV_FROM_DAEMON(devCon, &mcResult);

        if (mcResult != MC_DRV_OK) {
            LOG_E("MC_DRV_CMD_GET_VERSION bad response, respId=%d", mcResult);
            // version is still 0, we don't further analyze response here.
            break;
        }

        RECV_FROM_DAEMON(devCon, &v);

    } while (0);

    if (mcResult == MC_DRV_OK) {
        *version = v;
    }

    return mcResult;
}
#endif /* WIN32 */

