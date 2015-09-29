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
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "Mci/mcinq.h"          /* TA termination codes */

#define LOG_TAG "GpClient"
#include "log.h"
#include "common_client.h"
#include "tee_client_api.h"
#include "GpTci.h"      /* Needs stuff from tee_client_api.h or its includes */

//------------------------------------------------------------------------------
// Macros
#define _TEEC_GET_PARAM_TYPE(t, i) (((t) >> (4*i)) & 0xF)

//Parameter number
#define _TEEC_PARAMETER_NUMBER      4

/**
 * These error codes are still to be decided by GP and as we do not wish to
 * expose any part of the GP TAF as of yet, for now they will have to live here
 * until we decide what to do about them.
 */
#define TEEC_ERROR_TA_LOCKED        0xFFFF0257
#define TEEC_ERROR_SD_BLOCKED       0xFFFF0258
#define TEEC_ERROR_TARGET_KILLED    0xFFFF0259

static CommonClient& client = CommonClient::getInstance();

//------------------------------------------------------------------------------
// Local functions
static TEEC_Result _TEEC_UnwindOperation(
    TEEC_Session_IMP    *session,
    _TEEC_TCI           *tci,
    TEEC_Operation      *operation,
    bool                copyValues,
    uint32_t            *returnOrigin);

//------------------------------------------------------------------------------
static void _libUuidToArray(
    const TEEC_UUID *uuid,
    uint8_t         *uuidArr)
{
    uint8_t *pIdentifierCursor = (uint8_t *)uuid;
    /* offsets and syntax constants. See explanations above */
#ifdef S_BIG_ENDIAN
    uint32_t offsets = 0;
#else
    uint32_t offsets = 0xF1F1DF13;
#endif
    uint32_t i;

    for (i = 0; i < sizeof(TEEC_UUID); i++) {
        /* Two-digit hex number */
        uint8_t number;
        int32_t offset = ((int32_t)((offsets & 0xF) << 28)) >> 28;
        number = pIdentifierCursor[offset];
        offsets >>= 4;
        pIdentifierCursor++;

        uuidArr[i] = number;
    }
}

//------------------------------------------------------------------------------
static TEEC_Result _TEEC_SetupOperation(
    TEEC_Session_IMP    *session,
    _TEEC_TCI           *tci,
    TEEC_Operation      *operation,
    uint32_t            *returnOrigin)
{
    uint32_t                    i;
    _TEEC_ParameterInternal     *imp;
    TEEC_Parameter              *ext;
    TEEC_Result                 teecResult = TEEC_SUCCESS;

    LOG_D(" %s()", __func__);

    tci->operation.isCancelled = false;
    tci->operation.paramTypes = 0;

    //operation can be NULL
    if (operation != NULL) {
        uint32_t n_buf = 0;
        operation->started = 1;
        // Buffers to map to SWd
        struct mc_ioctl_map map;
        map.sid = session->sessionId;
        // Operation parameters for the buffers above
        _TEEC_ParameterInternal *imps[_TEEC_PARAMETER_NUMBER] = { NULL };

        //This design allows a non-NULL buffer with a size of 0 bytes to allow trivial integration with any
        //implementations of the C library malloc, in which is valid to allocate a zero byte buffer and receive a non-
        //NULL pointer which may not be de-referenced in return.

        for (i = 0; i < _TEEC_PARAMETER_NUMBER && teecResult == TEEC_SUCCESS; i++) {
            uint8_t paramType = _TEEC_GET_PARAM_TYPE(operation->paramTypes, i);

            imp = &tci->operation.params[i];
            ext = &operation->params[i];

            switch (paramType) {
            case TEEC_VALUE_OUTPUT:
                LOG_D("  cycle %d, TEEC_VALUE_OUTPUT", i);
                break;
            case TEEC_NONE:
                LOG_D("  cycle %d, TEEC_NONE", i);
                break;
            case TEEC_VALUE_INPUT:
            case TEEC_VALUE_INOUT: {
                LOG_D("  cycle %d, TEEC_VALUE_IN*", i);
                imp->value.a = ext->value.a;
                imp->value.b = ext->value.b;
                break;
            }
            case TEEC_MEMREF_TEMP_INPUT:
            case TEEC_MEMREF_TEMP_OUTPUT:
            case TEEC_MEMREF_TEMP_INOUT: {
                // A Temporary Memory Reference may be null, which can be used
                // to denote a special case for the parameter. Output Memory
                // References that are null are typically used to request the
                // required output size.
                LOG_D("  cycle %d, TEEC_MEMREF_TEMP_*", i);
                if ((ext->tmpref.size) && (ext->tmpref.buffer)) {
                    map.bufs[n_buf].va = (uintptr_t)ext->tmpref.buffer;
                    map.bufs[n_buf].len = (uint32_t)ext->tmpref.size;
                    map.bufs[n_buf].flags = paramType & TEEC_MEM_INOUT;
                    imps[n_buf] = imp;
                    n_buf++;
                } else {
                    LOG_D("  cycle %d, TEEC_TEMP_IN*  - zero pointer or size", i);
                }
                break;
            }
            case TEEC_MEMREF_WHOLE: {
                LOG_D("  cycle %d, TEEC_MEMREF_WHOLE", i);
                if (ext->memref.parent->size) {
                    map.bufs[n_buf].va = (uintptr_t)ext->memref.parent->buffer;
                    map.bufs[n_buf].len = (uint32_t)ext->memref.parent->size;
                    map.bufs[n_buf].flags = ext->memref.parent->flags & TEEC_MEM_INOUT;
                    imps[n_buf] = imp;
                    n_buf++;
                }
                /* We don't transmit that the mem ref is the whole shared mem */
                /* Magic number 4 means that it is a mem ref */
                paramType = (uint8_t)ext->memref.parent->flags | 4;
                break;
            }
            case TEEC_MEMREF_PARTIAL_INPUT:
            case TEEC_MEMREF_PARTIAL_OUTPUT:
            case TEEC_MEMREF_PARTIAL_INOUT: {
                LOG_D("  cycle %d, TEEC_MEMREF_PARTIAL_*", i);
                //Check data flow consistency
                if ((((ext->memref.parent->flags & TEEC_MEM_INOUT) == TEEC_MEM_INPUT) &&
                        (paramType == TEEC_MEMREF_PARTIAL_OUTPUT)) ||
                        (((ext->memref.parent->flags & TEEC_MEM_INOUT) == TEEC_MEM_OUTPUT) &&
                         (paramType == TEEC_MEMREF_PARTIAL_INPUT))) {
                    LOG_E("PARTIAL data flow inconsistency");
                    *returnOrigin = TEEC_ORIGIN_API;
                    teecResult = TEEC_ERROR_BAD_PARAMETERS;
                    break;
                }
                /* We don't transmit that the mem ref is partial */
                paramType &= TEEC_MEMREF_TEMP_INOUT;

                if (ext->memref.offset + ext->memref.size > ext->memref.parent->size) {
                    LOG_E("PARTIAL offset/size error");
                    *returnOrigin = TEEC_ORIGIN_API;
                    teecResult = TEEC_ERROR_BAD_PARAMETERS;
                    break;
                }
                if (ext->memref.size) {
                    map.bufs[n_buf].va = (uintptr_t)ext->memref.parent->buffer + ext->memref.offset;
                    map.bufs[n_buf].len = (uint32_t)ext->memref.size;
                    map.bufs[n_buf].flags = paramType & TEEC_MEM_INOUT;
                    imps[n_buf] = imp;
                    n_buf++;
                }
                break;
            }
            default:
                LOG_E("cycle %d, default", i);
                *returnOrigin = TEEC_ORIGIN_API;
                teecResult = TEEC_ERROR_BAD_PARAMETERS;
                break;
            }
            tci->operation.paramTypes |= (uint32_t)(paramType << i * 4);
        }

        if (n_buf > MC_MAP_MAX) {
                LOG_E("too many buffers: %s", strerror(errno));
                teecResult = TEEC_ERROR_EXCESS_DATA;
        }

        if ((teecResult == TEEC_SUCCESS) && (tci->operation.isCancelled)) {
            LOG_E("the operation has been cancelled in COMMS");
            *returnOrigin = TEEC_ORIGIN_COMMS;
            teecResult = TEEC_ERROR_CANCEL;
        }

        // Map buffers
        if ((teecResult == TEEC_SUCCESS) && (n_buf > 0)) {
            for (i = n_buf; i < MC_MAP_MAX; i++) {
                map.bufs[i].va = 0;
            }
            if (client.map(map) == 0) {
                for (i = 0; i < MC_MAP_MAX; i++) {
                    if (map.bufs[i].va != 0) {
                        imps[i]->memref.sVirtualAddr = (uint32_t)map.bufs[i].sva;
                        imps[i]->memref.sVirtualLen = map.bufs[i].len;
                    }
                }
            } else {
                LOG_E("client map failed: %s", strerror(errno));
                *returnOrigin = TEEC_ORIGIN_COMMS;
                teecResult = TEEC_ERROR_GENERIC;
            }
        }

        if (teecResult != TEEC_SUCCESS) {
            uint32_t retOrigIgnored;
            _TEEC_UnwindOperation(session, tci, operation, false, &retOrigIgnored);
            //Zeroing out tci->operation
            memset(&tci->operation, 0, sizeof(tci->operation));
            return teecResult;
        }
    }

    //Copy version indicator field
    memcpy(tci->header, "TCIGP000", sizeof(tci->header));

    // Fill in invalid values for secure world to overwrite
    tci->returnStatus = TEEC_ERROR_BAD_STATE;

    // Signal completion of request writing
    tci->ready = 1;

    return teecResult;
}

//------------------------------------------------------------------------------
static TEEC_Result _TEEC_UnwindOperation(
    TEEC_Session_IMP    *session,
    _TEEC_TCI           *tci,
    TEEC_Operation      *operation,
    bool                copyValues,
    uint32_t            *returnOrigin)
{
    uint32_t                    i;
    _TEEC_ParameterInternal     *imp;
    TEEC_Parameter              *ext;
    uint32_t                    n_buf = 0;

    //operation can be NULL
    if (operation == NULL) {
        return  TEEC_SUCCESS;
    }

    LOG_D(" %s()", __func__);

    operation->started = 2;
    // Buffers to unmap from SWd
    struct mc_ioctl_map map;
    map.sid = session->sessionId;
    // Some sanity checks
    if (tci->returnOrigin == 0 ||
            ((tci->returnOrigin != TEEC_ORIGIN_TRUSTED_APP) && (tci->returnStatus != TEEC_SUCCESS))) {
        *returnOrigin = TEEC_ORIGIN_COMMS;
        return TEEC_ERROR_COMMUNICATION;
    }
    *returnOrigin = tci->returnOrigin;

    //Clear sVirtualLen to unMap further
    for (i = 0; i < _TEEC_PARAMETER_NUMBER; i++) {
        imp = &tci->operation.params[i];
        ext = &operation->params[i];

        switch (_TEEC_GET_PARAM_TYPE(operation->paramTypes, i)) {
        case TEEC_VALUE_INPUT:
            LOG_D("  cycle %d, TEEC_VALUE_INPUT", i);
            break;
        case TEEC_NONE:
            LOG_D("  cycle %d, TEEC_NONE", i);
            break;
        case TEEC_VALUE_OUTPUT:
        case TEEC_VALUE_INOUT: {
            LOG_D("  cycle %d, TEEC_VALUE_*OUT", i);
            if (copyValues) {
                ext->value.a = imp->value.a;
                ext->value.b = imp->value.b;
            }
            break;
        }
        case TEEC_MEMREF_TEMP_OUTPUT:
        case TEEC_MEMREF_TEMP_INPUT:
        case TEEC_MEMREF_TEMP_INOUT: {
            LOG_D("  cycle %d, TEEC_TEMP*", i);
            if ((copyValues) && (_TEEC_GET_PARAM_TYPE(operation->paramTypes, i) != TEEC_MEMREF_TEMP_INPUT)) {
                ext->tmpref.size = imp->memref.outputSize;
            }
            if (imp->memref.sVirtualLen > 0) {
                map.bufs[n_buf].va = (uintptr_t)ext->tmpref.buffer;
                map.bufs[n_buf].sva = imp->memref.sVirtualAddr;
                map.bufs[n_buf].len = imp->memref.sVirtualLen;
                n_buf++;
            }
            break;
        }
        case TEEC_MEMREF_WHOLE: {
            LOG_D("  cycle %d, TEEC_MEMREF_WHOLE", i);
            if ((copyValues) && (ext->memref.parent->flags != TEEC_MEM_INPUT)) {
                ext->memref.size = imp->memref.outputSize;
            }
            if (imp->memref.sVirtualLen > 0) {
                map.bufs[n_buf].va = (uintptr_t)ext->memref.parent->buffer;
                map.bufs[n_buf].sva = imp->memref.sVirtualAddr;
                map.bufs[n_buf].len = imp->memref.sVirtualLen;
                n_buf++;
            }
            break;
        }

        case TEEC_MEMREF_PARTIAL_OUTPUT:
        case TEEC_MEMREF_PARTIAL_INOUT:
        case TEEC_MEMREF_PARTIAL_INPUT: {
            LOG_D("  cycle %d, TEEC_MEMREF_PARTIAL*", i);
            if ((copyValues) && (_TEEC_GET_PARAM_TYPE(operation->paramTypes, i) != TEEC_MEMREF_PARTIAL_INPUT)) {
                ext->memref.size = imp->memref.outputSize;
            }
            if (imp->memref.sVirtualLen > 0) {
                map.bufs[n_buf].va = (uintptr_t)ext->memref.parent->buffer + ext->memref.offset;
                map.bufs[n_buf].sva = imp->memref.sVirtualAddr;
                map.bufs[n_buf].len = imp->memref.sVirtualLen;
                n_buf++;
            }
            break;
        }
        default:
            LOG_E("cycle %d, bad parameter", i);
            break;
        }
    }

    if (n_buf > MC_MAP_MAX) {
        LOG_E("too many buffers: %s", strerror(errno));
        return TEEC_ERROR_EXCESS_DATA;
    }

    for (i = n_buf; i < MC_MAP_MAX; i++) {
        map.bufs[i].va = 0;
    }

    if (n_buf > 0) {
        // This function assumes that we cannot handle errors
        if (client.unmap(map) < 0) {
            LOG_E("client unmap failed: %s", strerror(errno));
        }
    }

    return tci->returnStatus;
}

//------------------------------------------------------------------------------
//TEEC_InitializeContext: TEEC_SUCCESS, Another error code from Table 4-2.
TEEC_Result TEEC_InitializeContext(
    const char   *name,
    TEEC_Context *context)
{
    (void) name;
    LOG_D("== %s() ==============", __func__);

    if (context == NULL) {
        LOG_E("context is NULL");
        return TEEC_ERROR_BAD_PARAMETERS;
    }

    // In fact MC_DRV_OK
    if (client.open()) {
        switch (errno) {
            case ENOENT:
                return TEEC_ERROR_COMMUNICATION;
            case EINVAL:
                return TEEC_ERROR_BAD_PARAMETERS;
            default:
                return TEEC_ERROR_GENERIC;
        }
    }

    return TEEC_SUCCESS;
}

//------------------------------------------------------------------------------
//TEEC_FinalizeContext: void

// The implementation of this function MUST NOT be able to fail: after this function returns the Client
// Application must be able to consider that the Context has been closed.

void TEEC_FinalizeContext(
    TEEC_Context *context)
{
    LOG_D("== %s() ==============", __func__);

    // The parameter context MUST point to an initialized TEE Context.
    if (context == NULL) {
        LOG_E("context is NULL");
        return;
    }

    // The implementation of this function MUST NOT be able to fail: after this function returns the Client
    // Application must be able to consider that the Context has been closed.
    if (client.close() != 0) {
        LOG_E("mcCloseDevice failed: %s", strerror(errno));
        /* continue even in case of error */;
    }
}

static void _TEEC_CloseSession(
    TEEC_Session_IMP    *session)
{
    if (client.closeSession(session->sessionId) != 0) {
        LOG_E("%s failed: %s", __func__, strerror(errno));
    }
}

//------------------------------------------------------------------------------
static TEEC_Result _TEEC_CallTA(
    TEEC_Session_IMP    *session,
    TEEC_Operation      *operation,
    uint32_t            *returnOrigin)
{
    TEEC_Result     teecRes;
    TEEC_Result     teecError = TEEC_SUCCESS;
    int             ret = 0;

    LOG_D(" %s()", __func__);

    // Phase 1: start the operation and wait for the result
    _TEEC_TCI *tci = static_cast<_TEEC_TCI *>(session->tci);
    teecRes = _TEEC_SetupOperation(session, tci, operation, returnOrigin);
    if (teecRes != TEEC_SUCCESS ) {
        LOG_E("_TEEC_SetupOperation failed (%08x)", teecRes);
        return teecRes;
    }

    // Signal the Trusted App
    ret = client.notify(session->sessionId);
    if (ret != 0) {
        LOG_E("Notify failed: %s", strerror(errno));
        teecError = TEEC_ERROR_COMMUNICATION;
    } else {
        // -------------------------------------------------------------
        // Wait for the Trusted App response
        struct mc_ioctl_wait wait;
        wait.sid = session->sessionId;
        wait.timeout = -1;
        ret = client.waitNotification(wait);
        if (ret != 0) {
            teecError = TEEC_ERROR_COMMUNICATION;
            if (errno == ECOMM) {
                struct mc_ioctl_geterr err;
                err.sid = session->sessionId;

                ret = client.getError(err);
                switch (err.value) {
                case TA_EXIT_CODE_FINISHED:
                    // We may get here if the TA_OpenSessionEntryPoint returns an error and TA goes fast through DestroyEntryPoint and exits the TA.
                    teecError = TEEC_SUCCESS;
                    break;
                case ERR_SESSION_KILLED:
                    teecError = TEEC_ERROR_TARGET_KILLED;
                    break;
                case ERR_INVALID_SID:
                case ERR_SID_NOT_ACTIVE:
                    LOG_E("mcWaitNotification failed: %s", strerror(errno));
                    LOG_E("mcGetSessionErrorCode returned %d", err.value);
                    break;
                default:
                    LOG_E("Target is DEAD");
                    *returnOrigin = TEEC_ORIGIN_TEE;
                    teecError = TEEC_ERROR_TARGET_DEAD;
                    break;
                }
            }
        }
    }
    // Phase 2: Return values and cleanup
    // unmap memory and copy values if no error
    teecRes = _TEEC_UnwindOperation(session, tci, operation,
                                    (teecError == TEEC_SUCCESS), returnOrigin);
    if (teecRes != TEEC_SUCCESS ) {
        LOG_E("_TEEC_UnwindOperation (%08x)", teecRes);
        /* continue even in case of error */;
    }

    // Cleanup
    if (teecError != TEEC_SUCCESS) {
        // Previous interactions failed, either TA is dead or communication error
        _TEEC_CloseSession(session);
        session->active = false;
        if (teecError == TEEC_ERROR_COMMUNICATION) {
            *returnOrigin = TEEC_ORIGIN_COMMS;
        }
        munmap(session->tci, (size_t)sysconf(_SC_PAGESIZE));
        session->tci = NULL;
    }
    return teecError;
}

//------------------------------------------------------------------------------
//TEEC_OpenSession: if the returnOrigin is different from TEEC_ORIGIN_TRUSTED_APP, an error code from Table 4-2
// If the returnOrigin is equal to TEEC_ORIGIN_TRUSTED_APP, a return code defined by the
//protocol between the Client Application and the Trusted Application.
TEEC_Result TEEC_OpenSession (
    TEEC_Context        *context,
    TEEC_Session        *session,
    const TEEC_UUID     *destination,
    uint32_t            connectionMethod,
    const void          *connectionData,
    TEEC_Operation      *operation,
    uint32_t            *returnOrigin)
{
    TEEC_Result         teecRes;
    uint32_t            returnOrigin_local = TEEC_ORIGIN_API;
    struct mc_uuid_t    tauuid;
    int                 ret = 0;

    LOG_D("== %s() ==============", __func__);
    // -------------------------------------------------------------
    //The parameter context MUST point to an initialized TEE Context.
    if (context == NULL) {
        LOG_E("context is NULL");
        if (returnOrigin != NULL) *returnOrigin = TEEC_ORIGIN_API;
        return TEEC_ERROR_BAD_PARAMETERS;
    }

    if (session == NULL) {
        LOG_E("session is NULL");
        if (returnOrigin != NULL) *returnOrigin = TEEC_ORIGIN_API;
        return TEEC_ERROR_BAD_PARAMETERS;
    }

    if ((connectionMethod != TEEC_LOGIN_PUBLIC) &&
        (connectionMethod != TEEC_LOGIN_USER) &&
        (connectionMethod != TEEC_LOGIN_GROUP) &&
        (connectionMethod != TEEC_LOGIN_APPLICATION) &&
        (connectionMethod != TEEC_LOGIN_USER_APPLICATION) &&
        (connectionMethod != TEEC_LOGIN_GROUP_APPLICATION)) {
         LOG_E("connectionMethod not supported");
         if (returnOrigin != NULL)
             *returnOrigin = TEEC_ORIGIN_API;
         return TEEC_ERROR_NOT_IMPLEMENTED;
    }

    if ((TEEC_LOGIN_GROUP == connectionMethod) ||
        (TEEC_LOGIN_GROUP_APPLICATION == connectionMethod)) {
        if (NULL == connectionData) {
            LOG_E("connectionData is NULL");
            if (returnOrigin != NULL)
                *returnOrigin = TEEC_ORIGIN_API;
            return TEEC_ERROR_BAD_PARAMETERS;
        }
    }

    // -------------------------------------------------------------
    session->imp.active = false;

    _libUuidToArray(destination, tauuid.value);

    if (operation) {
        operation->imp.session = &session->imp;
    }

    //Allocate a 4kB page with mmap, zero it out, and set session->imp.tci to its address.
    session->imp.tci = NULL;
    void *bulkBuf = (void *)mmap(0, (size_t)sysconf(_SC_PAGESIZE), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (bulkBuf == MAP_FAILED) {
        LOG_E("mmap failed on tci buffer allocation");
        if (returnOrigin != NULL) *returnOrigin = TEEC_ORIGIN_API;
        return TEEC_ERROR_OUT_OF_MEMORY;
    }

    session->imp.tci = bulkBuf;
    memset(session->imp.tci, 0, (size_t)sysconf(_SC_PAGESIZE));

    pthread_mutex_init(&session->imp.mutex_tci, NULL);
    pthread_mutex_lock(&session->imp.mutex_tci);

    //Fill the TCI buffer session.tci with the destination UUID.
    _TEEC_TCI* tci = static_cast<_TEEC_TCI*>(session->imp.tci);
    memcpy(&tci->destination, destination, sizeof(tci->destination));
    // -------------------------------------------------------------
    struct mc_ioctl_open_session sess;
    sess.sid = 0;
    sess.tci = (uintptr_t)tci;
    sess.tcilen = sizeof(_TEEC_TCI);
    sess.uuid = tauuid;
    sess.is_gp_uuid = 1;
    sess.identity.login_type = static_cast<mc_login_type>(connectionMethod);

    if (NULL != connectionData) {
        memcpy(&sess.identity.login_data, connectionData, sizeof(sess.identity.login_data));
    }

    ret = client.openSession(sess);
    if (ret != 0) {
        LOG_E("%s failed: %s", __func__, strerror(errno));
        if (returnOrigin != NULL) {
            *returnOrigin = TEEC_ORIGIN_COMMS;
        }
        switch (errno) {
        case ENOENT:
            teecRes = TEEC_ERROR_ITEM_NOT_FOUND;
            break;
        case EACCES:
            teecRes = TEEC_ERROR_ACCESS_DENIED;
            break;
        case EINVAL:
            teecRes = TEEC_ERROR_NOT_IMPLEMENTED;
            break;
        case ECONNREFUSED:
            teecRes = TEEC_ERROR_SD_BLOCKED;
            break;
        case ECONNABORTED:
            teecRes = TEEC_ERROR_TA_LOCKED;
            break;
        case ECONNRESET:
            teecRes = TEEC_ERROR_TARGET_KILLED;
            break;
        case EBUSY:
            teecRes = TEEC_ERROR_BUSY;
            break;
        default:
            teecRes = TEEC_ERROR_GENERIC;
        }
        goto error;
    }

    session->imp.context = context->imp;
    session->imp.sessionId = sess.sid;
    session->imp.active = true;
    LOG_I(" created session ID %x", session->imp.sessionId);

    // Let TA go through entry points
    LOG_D(" let TA go through entry points");
    tci->operation.type = _TA_OPERATION_OPEN_SESSION;
    teecRes = _TEEC_CallTA(&session->imp, operation, &returnOrigin_local);

    // Check for error on communication level
    if (teecRes != TEEC_SUCCESS ) {
        LOG_E("_TEEC_CallTA failed(%08x)", teecRes);
        // Nothing to do here because _TEEC_CallTA closes broken sessions
        if (returnOrigin != NULL) *returnOrigin = returnOrigin_local;
        goto error;
    }
    LOG_D(" no errors in com layer");

    // Check for error from TA
    if (returnOrigin != NULL) *returnOrigin = tci->returnOrigin;
    teecRes = tci->returnStatus;
    if (teecRes != TEEC_SUCCESS ) {
        LOG_E("TA OpenSession EP failed(%08x)", teecRes);
        goto error;
    }

    LOG_D(" %s() = TEEC_SUCCESS ", __func__);
    pthread_mutex_unlock(&session->imp.mutex_tci);

    if (returnOrigin != NULL) *returnOrigin = TEEC_ORIGIN_TRUSTED_APP;
    return TEEC_SUCCESS;

    // -------------------------------------------------------------
error:
    if (session->imp.active) {
        // After notifying us, TA went to Destry EP, so close session now
        _TEEC_CloseSession(&session->imp);
        session->imp.active = false;
    }

    pthread_mutex_unlock(&session->imp.mutex_tci);
    pthread_mutex_destroy(&session->imp.mutex_tci);
    if (session->imp.tci) {
        munmap(session->imp.tci, (size_t)sysconf(_SC_PAGESIZE));
        session->imp.tci = NULL;
    }

    LOG_D(" %s() = 0x%x", __func__, teecRes);
    return teecRes;
}

//------------------------------------------------------------------------------
TEEC_Result TEEC_InvokeCommand(
    TEEC_Session     *session,
    uint32_t         commandID,
    TEEC_Operation   *operation,
    uint32_t         *returnOrigin)
{
    TEEC_Result teecRes;
    uint32_t returnOrigin_local = TEEC_ORIGIN_API;

    LOG_D("== %s() ==============", __func__);

    // -------------------------------------------------------------
    if (session == NULL) {
        LOG_E("session is NULL");
        if (returnOrigin != NULL) *returnOrigin = TEEC_ORIGIN_API;
        return TEEC_ERROR_BAD_PARAMETERS;
    }

    if (!session->imp.active) {
        LOG_E("session is inactive");
        if (returnOrigin != NULL) *returnOrigin = TEEC_ORIGIN_API;
        return TEEC_ERROR_BAD_STATE;
    }
    // -------------------------------------------------------------
    if (operation) {
        operation->imp.session = &session->imp;
    }

    pthread_mutex_lock(&session->imp.mutex_tci);

    // Call TA
    _TEEC_TCI* tci = static_cast<_TEEC_TCI*>(session->imp.tci);
    tci->operation.commandId = commandID;
    tci->operation.type = _TA_OPERATION_INVOKE_COMMAND;
    teecRes = _TEEC_CallTA(&session->imp, operation, &returnOrigin_local);
    if (teecRes != TEEC_SUCCESS ) {
        LOG_E("_TEEC_CallTA failed(%08x)", teecRes);
        if (returnOrigin != NULL) {
            *returnOrigin = returnOrigin_local;
        }
    } else {
        if (returnOrigin != NULL) {
            *returnOrigin = tci->returnOrigin;
        }
        teecRes = tci->returnStatus;
    }

    pthread_mutex_unlock(&session->imp.mutex_tci);
    LOG_D(" %s() = 0x%x", __func__, teecRes);
    return teecRes;
}

//------------------------------------------------------------------------------
void TEEC_CloseSession(TEEC_Session *session)
{
    TEEC_Result     teecRes = TEEC_SUCCESS;
    uint32_t        returnOrigin;

    LOG_D("== %s() ==============", __func__);

    // -------------------------------------------------------------
    //The Implementation MUST do nothing if the session parameter is NULL.
    if (session == NULL) {
        LOG_E("session is NULL");
        return;
    }

    // -------------------------------------------------------------
    if (session->imp.active) {
        // Let TA go through CloseSession and Destroy entry points
        LOG_D(" let TA go through close entry points");
        pthread_mutex_lock(&session->imp.mutex_tci);
        _TEEC_TCI* tci = static_cast<_TEEC_TCI*>(session->imp.tci);
        tci->operation.type = _TA_OPERATION_CLOSE_SESSION;
        teecRes = _TEEC_CallTA(&session->imp, NULL, &returnOrigin);
        if (teecRes != TEEC_SUCCESS ) {
            /* continue even in case of error */;
            LOG_E("_TEEC_CallTA failed(%08x)", teecRes);
        }

        if (session->imp.active) {
            _TEEC_CloseSession(&session->imp);
        }
        pthread_mutex_unlock(&session->imp.mutex_tci);
    }

    pthread_mutex_destroy(&session->imp.mutex_tci);
    if (session->imp.tci) {
        munmap(session->imp.tci, (size_t)sysconf(_SC_PAGESIZE));
        session->imp.tci = NULL;
    }
    session->imp.active = false;

    LOG_D(" %s() = 0x%x", __func__, teecRes);
}

//------------------------------------------------------------------------------
TEEC_Result TEEC_RegisterSharedMemory(
    TEEC_Context      *context,
    TEEC_SharedMemory *sharedMem)
{
    LOG_D("== %s() ==============", __func__);

    //The parameter context MUST point to an initialized TEE Context.
    if (context == NULL) {
        LOG_E("context is NULL");
        return TEEC_ERROR_BAD_PARAMETERS;
    }
    //The parameter sharedMem MUST point to the Shared Memory structure defining
    //the memory region to register.
    if (sharedMem == NULL) {
        LOG_E("sharedMem is NULL");
        return TEEC_ERROR_BAD_PARAMETERS;
    }
    //The buffer field MUST point to the memory region to be shared, and MUST not be NULL.
    if (sharedMem->buffer == NULL) {
        LOG_E("sharedMem->buffer is NULL");
        return TEEC_ERROR_BAD_PARAMETERS;
    }
    if (((int)sharedMem->flags & ~TEEC_MEM_INOUT) != 0) {
        LOG_E("sharedMem->flags is incorrect");
        return TEEC_ERROR_BAD_PARAMETERS;
    }
    if (sharedMem->flags == 0) {
        LOG_E("sharedMem->flags is incorrect");
        return TEEC_ERROR_BAD_PARAMETERS;
    }

    sharedMem->imp.implementation_allocated = false;
    return TEEC_SUCCESS;
}

//------------------------------------------------------------------------------
TEEC_Result TEEC_AllocateSharedMemory(
    TEEC_Context      *context,
    TEEC_SharedMemory *sharedMem)
{
    //No connection to "context"?
    LOG_D("== %s() ==============", __func__);

    //The parameter context MUST point to an initialized TEE Context.
    if (context == NULL) {
        LOG_E("context is NULL");
        return TEEC_ERROR_BAD_PARAMETERS;
    }
    //The parameter sharedMem MUST point to the Shared Memory structure defining
    //the memory region to register.
    if (sharedMem == NULL) {
        LOG_E("sharedMem is NULL");
        return TEEC_ERROR_BAD_PARAMETERS;
    }
    if (((int)sharedMem->flags & ~TEEC_MEM_INOUT) != 0) {
        LOG_E("sharedMem->flags is incorrect");
        return TEEC_ERROR_BAD_PARAMETERS;
    }
    if (sharedMem->flags == 0) {
        LOG_E("sharedMem->flags is incorrect");
        return TEEC_ERROR_BAD_PARAMETERS;
    }

    sharedMem->buffer = malloc(sharedMem->size);
    if (sharedMem->buffer == NULL) {
        LOG_E("malloc failed");
        return TEEC_ERROR_OUT_OF_MEMORY;
    }
    sharedMem->imp.implementation_allocated = true;

    return TEEC_SUCCESS;
}

//------------------------------------------------------------------------------
void TEEC_ReleaseSharedMemory (
    TEEC_SharedMemory *sharedMem)
{
    //No connection to "context"?
    LOG_D("== %s() ==============", __func__);

    //The Implementation MUST do nothing if the sharedMem parameter is NULL
    if (sharedMem == NULL) {
        LOG_E("sharedMem is NULL");
        return;
    }

    //For a memory buffer allocated using TEEC_AllocateSharedMemory the Implementation
    //MUST free the underlying memory
    if (sharedMem->imp.implementation_allocated) {
        if (sharedMem->buffer) {
            free(sharedMem->buffer);
            sharedMem->buffer = NULL;
            sharedMem->size = 0;
        }
    }
}

//------------------------------------------------------------------------------
void TEEC_RequestCancellation(
    TEEC_Operation *operation)
{
    LOG_D("== %s() ==============", __func__);

    while (operation->started == 0);

    LOG_D("while(operation->started ==0) passed");

    if (operation->started > 1) {
        LOG_D("The operation has finished");
        return;
    }

    TEEC_Session_IMP *session = operation->imp.session;
    operation->started = 2;

    if (!session->active)  {
        LOG_D("Corresponding session is not active");
        return;
    }
    _TEEC_TCI* tci = static_cast<_TEEC_TCI*>(session->tci);
    tci->operation.isCancelled = true;

    // Step 4.3: signal the Trustlet
    int ret = client.notify(session->sessionId);
    if (ret != 0) {
        LOG_E("Notify failed: %s", strerror(errno));
    }
}

//------------------------------------------------------------------------------
