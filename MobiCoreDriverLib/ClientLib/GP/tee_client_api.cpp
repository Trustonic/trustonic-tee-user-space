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
#undef LOG_TAG
#define LOG_TAG "GpClient"
#include "tee_client_api.h"
#include "log.h"
#include "MobiCoreDriverApi.h"
#include "Mci/mcinq.h"
#include <sys/mman.h>
#include "GpTci.h"
#include "../Session.h"

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

//------------------------------------------------------------------------------
//Local satic functions
static void _libUuidToArray(
    const TEEC_UUID *uuid,
    uint8_t         *uuid_str);


static TEEC_Result _TEEC_UnwindOperation(
    _TEEC_TCI           *tci,
    mcSessionHandle_t   *handle,
    TEEC_Operation      *operation,
    bool                copyValues,
    uint32_t            *returnOrigin);

static TEEC_Result _TEEC_SetupOperation(
    _TEEC_TCI           *tci,
    mcSessionHandle_t   *handle,
    TEEC_Operation      *operation,
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
    _TEEC_TCI           *tci,
    mcSessionHandle_t   *handle,
    TEEC_Operation      *operation,
    uint32_t            *returnOrigin)
{
    uint32_t                    i;
    _TEEC_ParameterInternal     *imp;
    TEEC_Parameter              *ext;
    mcResult_t                  mcRet = MC_DRV_OK;
    TEEC_Result                 teecResult = TEEC_SUCCESS;

    LOG_I(" %s()", __func__);

    tci->operation.isCancelled = false;
    tci->operation.paramTypes = 0;

    //operation can be NULL
    if (operation != NULL) {

        operation->started = 1;

        //This design allows a non-NULL buffer with a size of 0 bytes to allow trivial integration with any
        //implementations of the C library malloc, in which is valid to allocate a zero byte buffer and receive a non-
        //NULL pointer which may not be de-referenced in return.

        for (i = 0; i < _TEEC_PARAMETER_NUMBER; i++) {
            uint8_t paramType = _TEEC_GET_PARAM_TYPE(operation->paramTypes, i);

            imp = &tci->operation.params[i];
            ext = &operation->params[i];

            switch (paramType) {
            case TEEC_VALUE_OUTPUT:
                LOG_I("  cycle %d, TEEC_VALUE_OUTPUT", i);
                break;
            case TEEC_NONE:
                LOG_I("  cycle %d, TEEC_NONE", i);
                break;
            case TEEC_VALUE_INPUT:
            case TEEC_VALUE_INOUT: {
                LOG_I("  cycle %d, TEEC_VALUE_IN*", i);
                imp->value.a = ext->value.a;
                imp->value.b = ext->value.b;
                break;
            }
            case TEEC_MEMREF_TEMP_INPUT:
            case TEEC_MEMREF_TEMP_OUTPUT:
            case TEEC_MEMREF_TEMP_INOUT: {
                //TODO: A Temporary Memory Reference may be null, which can be used to denote a special case for the
                //parameter. Output Memory References that are null are typically used to request the required output size.
                LOG_I("  cycle %d, TEEC_TEMP_IN*", i);
                imp->memref.mapInfo.sVirtualLen = 0;
                if ((ext->tmpref.size) && (ext->tmpref.buffer)) {
                    mcRet = mcMap(handle, ext->tmpref.buffer, ext->tmpref.size, &imp->memref.mapInfo);
                    if (mcRet != MC_DRV_OK) {
                        LOG_E("mcMap failed, mcRet=0x%08X", mcRet);
                        *returnOrigin = TEEC_ORIGIN_COMMS;
                        i = _TEEC_PARAMETER_NUMBER;
                    }
                } else {
                    LOG_I("  cycle %d, TEEC_TEMP_IN*  - zero pointer or size", i);
                }
                break;
            }
            case TEEC_MEMREF_WHOLE: {
                LOG_I("  cycle %d, TEEC_MEMREF_WHOLE", i);
                imp->memref.mapInfo.sVirtualLen = 0;
                if (ext->memref.parent->size) {
                    mcRet = mcMap(handle, ext->memref.parent->buffer, ext->memref.parent->size, &imp->memref.mapInfo);
                    if (mcRet != MC_DRV_OK) {
                        LOG_E("mcMap failed, mcRet=0x%08X", mcRet);
                        *returnOrigin = TEEC_ORIGIN_COMMS;
                        i = _TEEC_PARAMETER_NUMBER;
                    }
                }
                /* We don't transmit that the mem ref is the whole shared mem */
                /* Magic number 4 means that it is a mem ref */
                paramType = ext->memref.parent->flags | 4;
                break;
            }
            case TEEC_MEMREF_PARTIAL_INPUT:
            case TEEC_MEMREF_PARTIAL_OUTPUT:
            case TEEC_MEMREF_PARTIAL_INOUT: {
                LOG_I("  cycle %d, TEEC_PARTIAL_IN*", i);
                //Check data flow consistency
                if ((((ext->memref.parent->flags & (TEEC_MEM_INPUT | TEEC_MEM_OUTPUT)) == TEEC_MEM_INPUT) &&
                        (paramType == TEEC_MEMREF_PARTIAL_OUTPUT)) ||
                        (((ext->memref.parent->flags & (TEEC_MEM_INPUT | TEEC_MEM_OUTPUT)) == TEEC_MEM_OUTPUT) &&
                         (paramType == TEEC_MEMREF_PARTIAL_INPUT))) {
                    LOG_E("PARTIAL data flow inconsistency");
                    *returnOrigin = TEEC_ORIGIN_API;
                    teecResult = TEEC_ERROR_BAD_PARAMETERS;
                    i = _TEEC_PARAMETER_NUMBER;
                    break;
                }
                /* We don't transmit that the mem ref is partial */
                paramType &= TEEC_MEMREF_TEMP_INOUT;

                if (ext->memref.offset + ext->memref.size > ext->memref.parent->size) {
                    LOG_E("PARTIAL offset/size error");
                    *returnOrigin = TEEC_ORIGIN_API;
                    teecResult = TEEC_ERROR_BAD_PARAMETERS;
                    i = _TEEC_PARAMETER_NUMBER;
                    break;
                }
                imp->memref.mapInfo.sVirtualLen = 0;
                if (ext->memref.size) {
                    mcRet = mcMap(handle, (uint8_t *)ext->memref.parent->buffer + ext->memref.offset, ext->memref.size, &imp->memref.mapInfo);
                    if (mcRet != MC_DRV_OK) {
                        LOG_E("mcMap failed, mcRet=0x%08X", mcRet);
                        *returnOrigin = TEEC_ORIGIN_COMMS;
                        i = _TEEC_PARAMETER_NUMBER;
                    }
                }
                break;
            }
            default:
                LOG_E("cycle %d, default", i);
                *returnOrigin = TEEC_ORIGIN_API;
                teecResult = TEEC_ERROR_BAD_PARAMETERS;
                i = _TEEC_PARAMETER_NUMBER;
                break;
            }
            tci->operation.paramTypes |= (paramType<<i*4);
        }

        if (tci->operation.isCancelled) {
            LOG_E("the operation has been cancelled in COMMS");
            *returnOrigin = TEEC_ORIGIN_COMMS;
            teecResult = TEEC_ERROR_CANCEL;
        }

        if ((mcRet != MC_DRV_OK) || (teecResult != TEEC_SUCCESS)) {
            uint32_t retOrigIgnored;
            _TEEC_UnwindOperation(tci, handle, operation, false, &retOrigIgnored);
            //Zeroing out tci->operation
            memset(&tci->operation, 0, sizeof(tci->operation));
            if (teecResult != TEEC_SUCCESS) return teecResult;
            return TEEC_ERROR_GENERIC;
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
    _TEEC_TCI           *tci,
    mcSessionHandle_t   *handle,
    TEEC_Operation      *operation,
    bool                copyValues,
    uint32_t            *returnOrigin)
{
    uint32_t                    i;
    _TEEC_ParameterInternal     *imp;
    TEEC_Parameter              *ext;
    uint8_t                     *buffer;

    //operation can be NULL
    if (operation == NULL) return  TEEC_SUCCESS;

    LOG_I(" %s()", __func__);

    operation->started = 2;

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
        buffer = NULL;

        switch (_TEEC_GET_PARAM_TYPE(operation->paramTypes, i)) {
        case TEEC_VALUE_INPUT:
            LOG_I("  cycle %d, TEEC_VALUE_INPUT", i);
            break;
        case TEEC_NONE:
            LOG_I("  cycle %d, TEEC_NONE", i);
            break;
        case TEEC_VALUE_OUTPUT:
        case TEEC_VALUE_INOUT: {
            LOG_I("  cycle %d, TEEC_VALUE_*OUT", i);
            if (copyValues) {
                ext->value.a = imp->value.a;
                ext->value.b = imp->value.b;
            }
            break;
        }
        case TEEC_MEMREF_TEMP_OUTPUT:
        case TEEC_MEMREF_TEMP_INPUT:
        case TEEC_MEMREF_TEMP_INOUT: {
            LOG_I("  cycle %d, TEEC_TEMP*", i);
            if ((copyValues) && (_TEEC_GET_PARAM_TYPE(operation->paramTypes, i) != TEEC_MEMREF_TEMP_INPUT)) {
                ext->tmpref.size = imp->memref.outputSize;
            }
            buffer = (uint8_t *)ext->tmpref.buffer;
            break;
        }
        case TEEC_MEMREF_WHOLE: {
            LOG_I("  cycle %d, TEEC_MEMREF_WHOLE", i);
            if ((copyValues) && (ext->memref.parent->flags != TEEC_MEM_INPUT)) {
                ext->memref.size = imp->memref.outputSize;
            }
            buffer = (uint8_t *)ext->memref.parent->buffer;
            break;
        }

        case TEEC_MEMREF_PARTIAL_OUTPUT:
        case TEEC_MEMREF_PARTIAL_INOUT:
        case TEEC_MEMREF_PARTIAL_INPUT: {
            LOG_I("  cycle %d, TEEC_MEMREF_PARTIAL*", i);
            if ((copyValues) && (_TEEC_GET_PARAM_TYPE(operation->paramTypes, i) != TEEC_MEMREF_PARTIAL_INPUT)) {
                ext->memref.size = imp->memref.outputSize;
            }
            buffer = (uint8_t *)ext->memref.parent->buffer + ext->memref.offset;
            break;
        }
        default:
            LOG_E("cycle %d, bad parameter", i);
            break;
        }

        if ((buffer != NULL) && (imp->memref.mapInfo.sVirtualLen != 0)) {
            // This function assumes that we cannot handle error of mcUnmap
            (void)mcUnmap(handle, buffer, &imp->memref.mapInfo);
        }
    }

    return tci->returnStatus;
}

//------------------------------------------------------------------------------
//TEEC_InitializeContext: TEEC_SUCCESS, Another error code from Table 4-2.
//MC_DRV_OK, MC_DRV_ERR_INVALID_OPERATION, MC_DRV_ERR_DAEMON_UNREACHABLE, MC_DRV_ERR_UNKNOWN_DEVICE, MC_DRV_ERR_INVALID_DEVICE_FILE
TEEC_Result TEEC_InitializeContext(
    const char   *name,
    TEEC_Context *context)
{
    LOG_I("== %s() ==============", __func__);

    if (context == NULL) return TEEC_ERROR_BAD_PARAMETERS;
    context->imp.reserved = MC_DEVICE_ID_DEFAULT;

    switch (mcOpenDevice(MC_DEVICE_ID_DEFAULT)) {
    case MC_DRV_OK:
        return TEEC_SUCCESS;
    case MC_DRV_ERR_DAEMON_UNREACHABLE:
        return TEEC_ERROR_COMMUNICATION;
    case MC_DRV_ERR_UNKNOWN_DEVICE:
        return TEEC_ERROR_BAD_PARAMETERS;
    case MC_DRV_ERR_INVALID_DEVICE_FILE:
        return TEEC_ERROR_COMMUNICATION;
    }

    return TEEC_ERROR_GENERIC;
}

//------------------------------------------------------------------------------
//mcCloseDevice: MC_DRV_OK, MC_DRV_ERR_UNKNOWN_DEVICE, MC_DRV_ERR_SESSION_PENDING, MC_DRV_ERR_DAEMON_UNREACHABLE
//TEEC_FinalizeContext: void

//TODO: The implementation of this function MUST NOT be able to fail: after this function returns the Client
//Application must be able to consider that the Context has been closed.

void TEEC_FinalizeContext(TEEC_Context *context)
{
    mcResult_t      mcRet;

    LOG_I("== %s() ==============", __func__);

    //The parameter context MUST point to an initialized TEE Context.
    //Just realized: The function implementation MUST do nothing if context is NULL.
    if (context == NULL) {
        LOG_E("context is NULL");
        return;
    }

    //The implementation of this function MUST NOT be able to fail: after this function returns the Client
    //Application must be able to consider that the Context has been closed.
    mcRet = mcCloseDevice(context->imp.reserved);
    if (mcRet != MC_DRV_OK) {
        LOG_E("mcCloseDevice failed (%08x)", mcRet);
        /* continue even in case of error */;
    }
}

//------------------------------------------------------------------------------
static TEEC_Result _TEEC_CallTA(
    TEEC_Session    *session,
    TEEC_Operation  *operation,
    uint32_t        *returnOrigin)
{
    mcResult_t      mcRet;
    TEEC_Result     teecRes;
    TEEC_Result     teecError = TEEC_SUCCESS;

    LOG_I(" %s()", __func__);

    // Phase 1: start the operation and wait for the result
    teecRes = _TEEC_SetupOperation((_TEEC_TCI *)session->imp.tci, &session->imp.handle, operation, returnOrigin);
    if (teecRes != TEEC_SUCCESS ) {
        LOG_E("_TEEC_SetupOperation failed (%08x)", teecRes);
        return teecRes;
    }

    // Signal the Trusted App
    mcRet = mcNotify(&session->imp.handle);
    if (MC_DRV_OK != mcRet) {
        LOG_E("Notify failed (%08x)", mcRet);
        teecError = TEEC_ERROR_COMMUNICATION;
        goto end;
    }

    // -------------------------------------------------------------
    // Wait for the Trusted App response
    mcRet = mcWaitNotification(&session->imp.handle, MC_INFINITE_TIMEOUT);
    if (mcRet != MC_DRV_OK) {
        teecError = TEEC_ERROR_COMMUNICATION;
        if (mcRet == MC_DRV_INFO_NOTIFICATION) {
            int32_t lastErr = SESSION_ERR_NO;
            mcGetSessionErrorCode(&session->imp.handle, &lastErr);
            switch (lastErr) {
            case TA_EXIT_CODE_FINISHED:
                // We may get here if the TA_OpenSessionEntryPoint returns an error and TA goes fast through DestroyEntryPoint and exits the TA.
                teecError = TEEC_SUCCESS;
                break;
            case ERR_SESSION_KILLED:
                teecError = TEEC_ERROR_TARGET_KILLED;
                break;
            case ERR_INVALID_SID:
            case ERR_SID_NOT_ACTIVE:
                LOG_E("mcWaitNotification failed (%08x)", mcRet);
                LOG_E("mcGetSessionErrorCode returned %d", lastErr);
                break;
            default:
                LOG_E("Target is DEAD");
                *returnOrigin = TEEC_ORIGIN_TEE;
                teecError = TEEC_ERROR_TARGET_DEAD;
                break;
            }
        }
    }
    // Phase 2: Return values and cleanup
end:
    // unmap memory and copy values if no error
    teecRes = _TEEC_UnwindOperation((_TEEC_TCI *)session->imp.tci, &session->imp.handle, operation,
                                    (teecError == TEEC_SUCCESS), returnOrigin);
    if (teecRes != TEEC_SUCCESS ) {
        LOG_E("_TEEC_UnwindOperation (%08x)", teecRes);
        /* continue even in case of error */;
    }

    // Cleanup
    if (teecError != TEEC_SUCCESS) {
        // Previous interactions failed, either TA is dead or communication error
        mcRet = mcCloseSession(&session->imp.handle);
        if (mcRet != MC_DRV_OK) {
            LOG_E("mcCloseSession failed (%08x)", mcRet);
            /* continue even in case of error */;
        }
        session->imp.active = false;
        if (teecError == TEEC_ERROR_COMMUNICATION) {
            *returnOrigin = TEEC_ORIGIN_COMMS;
        }
        munmap(session->imp.tci, sysconf(_SC_PAGESIZE));
        session->imp.tci = NULL;
    }
    return teecError;
}

//------------------------------------------------------------------------------
__MC_CLIENT_LIB_API mcResult_t mcOpenGPTA(
    mcSessionHandle_t  *session,
    const mcUuid_t     *uuid,
    uint8_t            *tci,
    uint32_t           len
);
//------------------------------------------------------------------------------
//TEEC_OpenSession: if the returnOrigin is different from TEEC_ORIGIN_TRUSTED_APP, an error code from Table 4-2
// If the returnOrigin is equal to TEEC_ORIGIN_TRUSTED_APP, a return code defined by the
//protocol between the Client Application and the Trusted Application.
TEEC_Result TEEC_OpenSession (
    TEEC_Context    *context,
    TEEC_Session    *session,
    const TEEC_UUID *destination,
    uint32_t        connectionMethod,
    void            *connectionData,
    TEEC_Operation  *operation,
    uint32_t        *returnOrigin)
{
    mcResult_t      mcRet;
    TEEC_Result     teecRes;
    uint32_t        returnOrigin_local = TEEC_ORIGIN_API;
    mcUuid_t        tauuid;

    LOG_I("== %s() ==============", __func__);
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

    if (connectionMethod != TEEC_LOGIN_PUBLIC) {
        //JACKET: Client authorization is not supported. The connectionMethod parameter
        //must be TEEC LOGIN PUBLIC, otherwise return TEEC ERROR NOT IMPLEMENTED.
        LOG_E("connectionMethod != TEEC_LOGIN_PUBLIC");
        if (returnOrigin != NULL) *returnOrigin = TEEC_ORIGIN_API;
        return TEEC_ERROR_NOT_IMPLEMENTED;
    }

    // -------------------------------------------------------------
    session->imp.active = false;

    _libUuidToArray((TEEC_UUID *)destination, (uint8_t *)tauuid.value);

    if (operation) operation->imp.session = &session->imp;

    //Allocate a 4kB page with mmap, zero it out, and set session->imp.tci to its address.
    session->imp.tci = NULL;
    void *bulkBuf = (void *)mmap(0, sysconf(_SC_PAGESIZE), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (bulkBuf == MAP_FAILED) {
        LOG_E("mmap failed on tci buffer allocation");
        if (returnOrigin != NULL) *returnOrigin = TEEC_ORIGIN_API;
        return TEEC_ERROR_OUT_OF_MEMORY;
    }

    session->imp.tci = bulkBuf;
    memset(session->imp.tci, 0, sysconf(_SC_PAGESIZE));

    pthread_mutex_init(&session->imp.mutex_tci, NULL);
    pthread_mutex_lock(&session->imp.mutex_tci);

    //Fill the TCI buffer session.tci with the destination UUID.
    memcpy(&(((_TEEC_TCI *)session->imp.tci)->destination), destination, sizeof(TEEC_UUID));
    // -------------------------------------------------------------
    memset(&session->imp.handle, 0, sizeof(mcSessionHandle_t));
    session->imp.handle.deviceId = context->imp.reserved ; // The device ID (default device is used)
    mcRet = mcOpenGPTA(
                &session->imp.handle,
                &tauuid,
                (uint8_t *)session->imp.tci,
                sizeof(_TEEC_TCI));
    if (mcRet != MC_DRV_OK) {
        LOG_E("mcOpenGPTA failed (%08x)", mcRet);
        if (returnOrigin != NULL) *returnOrigin = TEEC_ORIGIN_COMMS;
        switch (mcRet) {
        case MC_DRV_ERR_TRUSTED_APPLICATION_NOT_FOUND:
            teecRes = TEEC_ERROR_ITEM_NOT_FOUND;
            break;
        case MC_DRV_ERR_SERVICE_BLOCKED:
            teecRes = TEEC_ERROR_SD_BLOCKED;
            break;
        case MC_DRV_ERR_SERVICE_LOCKED:
            teecRes = TEEC_ERROR_TA_LOCKED;
            break;
        case MC_DRV_ERR_SERVICE_KILLED:
            teecRes = TEEC_ERROR_TARGET_KILLED;
            break;
        case MC_DRV_ERR_NO_FREE_INSTANCES:
            teecRes = TEEC_ERROR_BUSY;
            break;
        default:
            //TODO: Improve the error codes
            teecRes = TEEC_ERROR_GENERIC;
        }
        goto error;
    }

    session->imp.active = true;

    // Let TA go through entry points
    LOG_I(" let TA go through entry points");
    ((_TEEC_TCI *)session->imp.tci)->operation.type = _TA_OPERATION_OPEN_SESSION;
    teecRes = _TEEC_CallTA(session, operation, &returnOrigin_local);

    // Check for error on communication level
    if (teecRes != TEEC_SUCCESS ) {
        LOG_E("_TEEC_CallTA failed(%08x)", teecRes);
        // Nothing to do here because _TEEC_CallTA closes broken sessions
        if (returnOrigin != NULL) *returnOrigin = returnOrigin_local;
        goto error;
    }
    LOG_I(" no errors in com layer");

    // Check for error from TA
    if (returnOrigin != NULL) *returnOrigin = ((_TEEC_TCI *)session->imp.tci)->returnOrigin;
    teecRes = ((_TEEC_TCI *)session->imp.tci)->returnStatus;
    if (teecRes != TEEC_SUCCESS ) {
        LOG_E("TA OpenSession EP failed(%08x)", teecRes);
        goto error;
    }

    LOG_I(" %s() = TEEC_SUCCESS ", __func__);
    pthread_mutex_unlock(&session->imp.mutex_tci);

    if (returnOrigin != NULL) *returnOrigin = TEEC_ORIGIN_TRUSTED_APP;
    return TEEC_SUCCESS;

    // -------------------------------------------------------------
error:
    if (session->imp.active) {
        // After notifying us, TA went to Destry EP, so close session now
        mcRet = mcCloseSession(&session->imp.handle);
        if (mcRet != MC_DRV_OK) {
            LOG_E("mcCloseSession failed (%08x)", mcRet);
            /* continue even in case of error */;
        }
        session->imp.active = false;
    }

    pthread_mutex_unlock(&session->imp.mutex_tci);
    pthread_mutex_destroy(&session->imp.mutex_tci);
    if (session->imp.tci) {
        munmap(session->imp.tci, sysconf(_SC_PAGESIZE));
        session->imp.tci = NULL;
    }

    LOG_I(" %s() = 0x%x", __func__, teecRes);
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

    LOG_I("== %s() ==============", __func__);

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
    if (operation) operation->imp.session = &session->imp;

    pthread_mutex_lock(&session->imp.mutex_tci);

    // Call TA
    ((_TEEC_TCI *)session->imp.tci)->operation.commandId = commandID;
    ((_TEEC_TCI *)session->imp.tci)->operation.type = _TA_OPERATION_INVOKE_COMMAND;
    teecRes = _TEEC_CallTA(session, operation, &returnOrigin_local);
    if (teecRes != TEEC_SUCCESS ) {
        LOG_E("_TEEC_CallTA failed(%08x)", teecRes);
        if (returnOrigin != NULL) *returnOrigin = returnOrigin_local;
    } else {
        if (returnOrigin != NULL) *returnOrigin = ((_TEEC_TCI *)session->imp.tci)->returnOrigin;
        teecRes                                 = ((_TEEC_TCI *)session->imp.tci)->returnStatus;
    }

    pthread_mutex_unlock(&session->imp.mutex_tci);
    LOG_I(" %s() = 0x%x", __func__, teecRes);
    return teecRes;
}

//------------------------------------------------------------------------------
void TEEC_CloseSession(TEEC_Session *session)
{
    mcResult_t      mcRet;
    TEEC_Result     teecRes = TEEC_SUCCESS;
    uint32_t        returnOrigin;

    LOG_I("== %s() ==============", __func__);

    // -------------------------------------------------------------
    //The Implementation MUST do nothing if the session parameter is NULL.
    if (session == NULL) {
        LOG_E("session is NULL");
        return;
    }

    // -------------------------------------------------------------
    if (session->imp.active) {
        // Let TA go through CloseSession and Destroy entry points
        LOG_I(" let TA go through close entry points");
        pthread_mutex_lock(&session->imp.mutex_tci);
        ((_TEEC_TCI *)session->imp.tci)->operation.type = _TA_OPERATION_CLOSE_SESSION;
        teecRes = _TEEC_CallTA(session, NULL, &returnOrigin);
        if (teecRes != TEEC_SUCCESS ) {
            /* continue even in case of error */;
            LOG_E("_TEEC_CallTA failed(%08x)", teecRes);
        }

        if (session->imp.active) {
            // Close Session
            mcRet = mcCloseSession(&session->imp.handle);
            if (mcRet != MC_DRV_OK) {
                LOG_E("mcCloseSession failed (%08x)", mcRet);
                /* ignore error and also there shouldn't be one */
            }
        }
        pthread_mutex_unlock(&session->imp.mutex_tci);
    }

    pthread_mutex_destroy(&session->imp.mutex_tci);
    if (session->imp.tci) {
        munmap(session->imp.tci, sysconf(_SC_PAGESIZE));
        session->imp.tci = NULL;
    }
    session->imp.active = false;

    LOG_I(" %s() = 0x%x", __func__, teecRes);
}

//------------------------------------------------------------------------------
TEEC_Result TEEC_RegisterSharedMemory(
    TEEC_Context      *context,
    TEEC_SharedMemory *sharedMem)
{
    LOG_I("== %s() ==============", __func__);

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
    if ((sharedMem->flags & ~(TEEC_MEM_INPUT | TEEC_MEM_OUTPUT)) != 0) {
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
    LOG_I("== %s() ==============", __func__);

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
    if ((sharedMem->flags & ~(TEEC_MEM_INPUT | TEEC_MEM_OUTPUT)) != 0) {
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
    LOG_I("== %s() ==============", __func__);

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

    //TODO: Attempting to release Shared Memory which is used by a pending operation.

}

//------------------------------------------------------------------------------
void TEEC_RequestCancellation(
    TEEC_Operation *operation)
{
    LOG_I("== %s() ==============", __func__);

    while (operation->started == 0);

    LOG_I("while(operation->started ==0) passed");

    if (operation->started > 1) {
        LOG_I("The operation has finished");
        return;
    }

    TEEC_Session_IMP *session = operation->imp.session;
    operation->started = 2;

    if (!session->active)  {
        LOG_I("Corresponding session is not active");
        return;
    }
    ((_TEEC_TCI *)session->tci)->operation.isCancelled = true;

    // Step 4.3: signal the Trustlet
    mcResult_t mcRet = mcNotify(&session->handle);
    if (MC_DRV_OK != mcRet) {
        LOG_E("Notify failed (%08x)", mcRet);
    }
}

//------------------------------------------------------------------------------
