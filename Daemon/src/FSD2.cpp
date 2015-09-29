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
 * FSD2 server.
 *
 * Handles incoming storage requests from TA through STH
 */

#include <unistd.h>
#include <string>
#include <cstring>
#include <errno.h>
#include <stdlib.h>
#include <signal.h>
#include <memory>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>
#include <log.h>

#include "MobiCoreDriverApi.h"
#include "sfs_error.h"
#include "FSD2.h"

#define PATH_SEPARATOR '/'

extern const std::string &getTbStoragePath();

using namespace std;

//------------------------------------------------------------------------------
const char * const FSD2::m_server_name = "McDaemon.FSD2";

class LockGuard {
    pthread_mutex_t* mutex_;
public:
    LockGuard(pthread_mutex_t* mutex): mutex_(mutex) {
        pthread_mutex_lock(mutex_);
    }
    ~LockGuard() {
        pthread_mutex_unlock(mutex_);
    }
};

FSD2::FSD2()
{
    memset(&m_sessionHandle, 0, sizeof(mcSessionHandle_t));
    pthread_mutex_init(&m_close_lock, NULL);

    g_pExchangeBuffer = NULL;
    g_pExchangeBufferNotAligned = NULL;
    g_pWorkspaceBuffer = NULL;
    g_nSectorSize = 0;
    for (int i = 0; i < 16; i++) {
        g_pPartitionNames[i] = NULL;
        g_pPartitionFiles[i] = NULL;
    }
}

FSD2::~FSD2()
{
    LOG_I("%s: Destroying File Storage Daemon object", __func__);
    FSD2_Close();
}

/*
 * main
 */
void FSD2::run()
{
    mcResult_t ret;
    const string baseDir = getTbStoragePath();
    uint8_t i;
    struct stat st;
    const char* tbstpath;

    sigset_t sigmask;
    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGUSR1);
    pthread_sigmask(SIG_UNBLOCK, &sigmask, (sigset_t *)0);

    LOG_I("%s: starts    ====", __func__);

    tbstpath = getTbStoragePath().c_str();

    /* Create Tbase storage directory */
    if (stat(tbstpath, &st) == -1) {
        LOG_I("%s: Creating <t-base storage Folder %s\n", __func__, tbstpath);
        if (mkdir(tbstpath, 0700) == -1) {
            LOG_E("%s: failed creating storage folder\n", __func__);
        }
    }

    /* Set default names to the partitions */
    for ( i=0; i<16 ;i++ ) {
    	g_pPartitionNames[i] = NULL;
	    g_pPartitionNames[i] = (char *)malloc(baseDir.size() + 1 /* separator */ + sizeof("Store_X.tf"));
    	if (g_pPartitionNames[i] != NULL) {
    	    sprintf(g_pPartitionNames[i], "%s%cStore_%1X.tf", baseDir.c_str(), PATH_SEPARATOR, i);
	    }
    	else {
    	    //free(baseDir);
	        i=0;
	        while(g_pPartitionNames[i] != NULL) free(g_pPartitionNames[i++]);
    	    LOG_E("%s: Out of memory", __func__);
	        ret = MC_DRV_ERR_NO_FREE_MEMORY;
	        return;
    	}
    }

    LOG_I("%s: starting File Storage Daemon 2", __func__);
    ret = FSD2_Open();
    if (ret != MC_DRV_OK)
        return;

    LOG_I("%s: Start listening for request from STH2", __func__);
    FSD2_listenDci();

    /* if we are here, something failed in the communication with the
     * Trusted Storage driver. We'll re-start and try again. Shutting down
     * isn't helpful here.
     * Optionally we could count and after 5 attempt abort, but that is also
     * not so ideal.
     */

    /* clean up first, ignore errors */
    FSD2_Close();

    LOG_E("%s: Exiting File Storage Daemon 2 0x%08X", __func__, ret);
}

/*
 * createSession()
 */
#define MAX_SECTOR_SIZE			            4096
#define SECTOR_NUM			                8
#define SFS_L2_CACHE_SLOT_SPACE		        12	// CPI: Dirty hard coded size. Cf sfs_internal.h
#define DEFAULT_WORKSPACE_SIZE              (SECTOR_NUM * (MAX_SECTOR_SIZE + SFS_L2_CACHE_SLOT_SPACE))

mcResult_t FSD2::FSD2_Open()
{
    mcResult_t   mcRet;
    const mcUuid_t sServiceId = SERVICE_DELEGATION_UUID;
    uint32_t nExchangeBufferSize = sizeof(STH2_delegation_exchange_buffer_t);
    uint32_t nWorkspaceLength = DEFAULT_WORKSPACE_SIZE;

    unsigned long reqAlignof = __alignof__(STH2_delegation_exchange_buffer_t);

    g_pExchangeBufferNotAligned = new uint8_t[nExchangeBufferSize+nWorkspaceLength+reqAlignof];
    if (g_pExchangeBufferNotAligned == NULL) {
        LOG_E("%s: allocation failed", __func__);
        return MC_DRV_ERR_NO_FREE_MEMORY;
    }

    // we ensure that the address is properly aligned for the structure
    g_pExchangeBufferNotAligned+=(reqAlignof-(((unsigned long)(g_pExchangeBufferNotAligned))%reqAlignof));

    g_pExchangeBuffer = (STH2_delegation_exchange_buffer_t*)g_pExchangeBufferNotAligned;

    memset(g_pExchangeBuffer, 0x00, nExchangeBufferSize+nWorkspaceLength);

    /* Open <t-base device */
    mcRet = mcOpenDevice(MC_DEVICE_ID_DEFAULT);
    if (MC_DRV_OK != mcRet) {
        LOG_E("%s: mcOpenDevice returned: 0x%08X\n", __func__, mcRet);
        delete[] g_pExchangeBufferNotAligned;
        return mcRet;
    }

    /* Open session to the SPT2 TA */
    memset(&m_sessionHandle, 0, sizeof(mcSessionHandle_t));
    m_sessionHandle.deviceId = MC_DEVICE_ID_DEFAULT;
    g_pExchangeBuffer->nWorkspaceLength = nWorkspaceLength;

    //LOG_I("%s: g_pExchangeBuffer->sWorkspace=0x%08X\n", __func__, g_pExchangeBuffer->sWorkspace);
    //LOG_I("%s: g_pExchangeBuffer=0x%08X\n", __func__, g_pExchangeBuffer);
    //LOG_I("%s: nExchangeBufferSize=%d\n", __func__, nExchangeBufferSize);

    mcRet = mcOpenSession(&m_sessionHandle,
                          &sServiceId,
                          (uint8_t *) g_pExchangeBuffer,
                          nExchangeBufferSize+nWorkspaceLength);
    if (MC_DRV_OK != mcRet) {
    	LOG_E("%s:  mcOpenSession returned: 0x%08X\n", __func__, mcRet);
	    mcCloseDevice(MC_DEVICE_ID_DEFAULT);
	    delete[] g_pExchangeBufferNotAligned;
	    memset(&m_sessionHandle, 0, sizeof(mcSessionHandle_t));
    } else {
        g_pWorkspaceBuffer = (uint8_t*)g_pExchangeBuffer->sWorkspace;
        memset(g_pPartitionFiles,0,16*sizeof(FILE*));
    }

    return mcRet;
}

mcResult_t FSD2::FSD2_Close()
{
    mcResult_t   mcRet = MC_DRV_OK;

    LockGuard lock(&m_close_lock);

    /* Close session to the debug driver trustlet */
    if(g_pExchangeBuffer != NULL) {

        mcRet = mcCloseSession(&m_sessionHandle);
        if (MC_DRV_OK != mcRet)
            LOG_E("%s: mcCloseSession returned: 0x%08X\n", __func__, mcRet);

        memset(&m_sessionHandle, 0, sizeof(mcSessionHandle_t));

        /* Close <t-base device */
        mcRet = mcCloseDevice(MC_DEVICE_ID_DEFAULT);
        if (MC_DRV_OK != mcRet)
            LOG_E("%s: mcCloseDevice returned: 0x%08X\n", __func__, mcRet);

        /* Clear DCI message buffer */
        uint32_t nWorkspaceLength = g_pExchangeBuffer->nWorkspaceLength;

        memset(g_pExchangeBuffer, 0, sizeof(STH2_delegation_exchange_buffer_t)+nWorkspaceLength);
        delete[] g_pExchangeBufferNotAligned;
        g_pExchangeBuffer = NULL;
    }

    LOG_I("%s: returning: 0x%08X\n", __func__, mcRet);
    return mcRet;
}

/*
 * runSession()
 */
void FSD2::FSD2_listenDci()
{
    mcResult_t mcRet = MC_DRV_OK;

    LOG_I("%s: DCI listener \n", __func__);

    while (mcRet == MC_DRV_OK && !shouldTerminate()) {
        LOG_D("%s: Waiting for notification\n", __func__);

        LockGuard lock(&m_close_lock);
        if(g_pExchangeBuffer == NULL)
            return;

        /* Wait for notification from SWd */
        mcRet = mcWaitNotification(&m_sessionHandle, MC_INFINITE_TIMEOUT);
        if (mcRet != MC_DRV_OK) {
            LOG_E("%s: mcWaitNotification failed, error=0x%08X\n", __func__, mcRet);
        }
        else {
            /* Received notification. */
            LOG_D("%s: Received Command from STH2\n", __func__);


            /* Read sector size (CPI TODO: every time ?) */
            g_nSectorSize = g_pExchangeBuffer->nSectorSize;
            LOG_D("%s: Sector Size: %d bytes", __func__, g_nSectorSize);

            /* Check sector size */
            if (!(g_nSectorSize == 512 || g_nSectorSize == 1024 || g_nSectorSize == 2048 || g_nSectorSize == 4096)) {
               LOG_E("%s: Incorrect sector size: terminating...", __func__);
               terminate();
               continue;
            }

            FSD2_ExecuteCommand();

            /* Set "listening" flag before notifying SPT2 */
            g_pExchangeBuffer->nDaemonState = STH2_DAEMON_LISTENING;
            mcRet = mcNotify(&m_sessionHandle);
            if (mcRet != MC_DRV_OK)
                LOG_E("%s: mcNotify() returned: 0x%08X\n", __func__, mcRet);
        }
    }
}

/*----------------------------------------------------------------------------
 * Utilities functions
 *----------------------------------------------------------------------------*/
TEEC_Result FSD2::errno2serror()
{
    switch (errno) {
       case EINVAL:
          return S_ERROR_BAD_PARAMETERS;
       case EMFILE:
          return S_ERROR_NO_MORE_HANDLES;
       case ENOENT:
          return S_ERROR_ITEM_NOT_FOUND;
       case EEXIST:
          return S_ERROR_ITEM_EXISTS;
       case ENOSPC:
          return S_ERROR_STORAGE_NO_SPACE;
       case ENOMEM:
          return S_ERROR_OUT_OF_MEMORY;
       case EBADF:
       case EACCES:
       default:
          return S_ERROR_STORAGE_UNREACHABLE;
    }
}

/*----------------------------------------------------------------------------
 * Instructions
 *----------------------------------------------------------------------------*/
/**
 * This function executes the DESTROY_PARTITION instruction
 *
 * @param nPartitionID: the partition identifier
 **/
TEEC_Result FSD2::partitionDestroy(uint32_t nPartitionID)
{
    TEEC_Result  nError = S_SUCCESS;

    if (g_pPartitionFiles[nPartitionID] != NULL) {
        /* The partition must not be currently opened */
        LOG_E("%s: g_pPartitionFiles not NULL", __func__);
        return S_ERROR_BAD_STATE;
    }

    /* Try to erase the file */
    if (unlink(g_pPartitionNames[nPartitionID]) != 0) {
        /* File in use or OS didn't allow the operation */
        nError = errno2serror();
    }

    return nError;
}

/**
 * This function executes the CREATE_PARTITION instruction. When successful,
 * it fills the g_pPartitionFiles[nPartitionID] slot.
 *
 * @param nPartitionID: the partition identifier
 **/
TEEC_Result FSD2::partitionCreate(uint32_t nPartitionID)
{
    uint32_t nError = S_SUCCESS;

    if (g_pPartitionFiles[nPartitionID] != NULL) {
        /* The partition is already opened */
        LOG_E("%s: g_pPartitionFiles not NULL", __func__);
        return S_ERROR_BAD_STATE;
    }

    /* Create the file unconditionnally */
    LOG_I("%s: Create storage file \"%s\"", __func__, g_pPartitionNames[nPartitionID]);
    g_pPartitionFiles[nPartitionID] = fopen(g_pPartitionNames[nPartitionID], "w+b");

    if (g_pPartitionFiles[nPartitionID] == NULL) {
        LOG_E("%s: Cannot create storage file \"%s\"", __func__, g_pPartitionNames[nPartitionID]);
        nError = errno2serror();
        return nError;
    }

    return nError;
}

/**
 * This function executes the OPEN_PARTITION instruction. When successful,
 * it fills the g_pPartitionFiles[nPartitionID] slot and writes the partition
 * size in hResultEncoder
 *
 * @param nPartitionID: the partition identifier
 * @param pnPartitionSize: filled with the number of sectors in the partition
 **/
TEEC_Result FSD2::partitionOpen(uint32_t nPartitionID, uint32_t* pnPartitionSize)
{
    uint32_t nError = S_SUCCESS;

    if (g_pPartitionFiles[nPartitionID] != NULL) {
        /* No partition must be currently opened in the session */
        LOG_E("%s: g_pPartitionFiles not NULL", __func__);
        return S_ERROR_BAD_STATE;
    }

    /* Open the file */
    g_pPartitionFiles[nPartitionID] = fopen(g_pPartitionNames[nPartitionID], "r+b");
    if (g_pPartitionFiles[nPartitionID] == NULL) {
        if (errno == ENOENT) {
            /* File does not exist */
            LOG_E("%s: Storage file \"%s\" does not exist", __func__, g_pPartitionNames[nPartitionID]);
            nError = S_ERROR_ITEM_NOT_FOUND;
            return nError;
        }
        else {
            LOG_E("%s: cannot open storage file \"%s\"", __func__, g_pPartitionNames[nPartitionID]);
            nError = errno2serror();
            return nError;
        }
    }
    /* Determine the current number of sectors */
    fseek(g_pPartitionFiles[nPartitionID], 0L, SEEK_END);
    *pnPartitionSize = ftell(g_pPartitionFiles[nPartitionID]) / g_nSectorSize;

    LOG_I("%s: storage file \"%s\" successfully opened (size = %d KB (%d bytes))",
        __func__,
        g_pPartitionNames[nPartitionID],
        ((*pnPartitionSize) * g_nSectorSize) / 1024,
        ((*pnPartitionSize) * g_nSectorSize));

    return nError;
}

/**
 * This function executes the CLOSE_PARTITION instruction.
 * It closes the partition file.
 *
 * @param nPartitionID: the partition identifier
 **/
TEEC_Result FSD2::partitionClose(uint32_t nPartitionID)
{
    if (g_pPartitionFiles[nPartitionID] == NULL) {
        /* The partition is currently not opened */
        return S_ERROR_BAD_STATE;
    }
    fclose(g_pPartitionFiles[nPartitionID]);
    g_pPartitionFiles[nPartitionID] = NULL;
    return S_SUCCESS;
}

/**
 * This function executes the READ instruction.
 *
 * @param nPartitionID: the partition identifier
 * @param nSectorIndex: the index of the sector to read
 * @param nWorkspaceOffset: the offset in the workspace where the sector must be written
 **/
TEEC_Result FSD2::partitionRead(uint32_t nPartitionID, uint32_t nSectorIndex, uint32_t nWorkspaceOffset)
{
    FILE* pFile;

    LOG_D("%s: >Partition %1X: read sector 0x%08X into workspace at offset 0x%08X",
        __func__, nPartitionID, nSectorIndex, nWorkspaceOffset);

    pFile = g_pPartitionFiles[nPartitionID];

    if (pFile == NULL) {
        /* The partition is not opened */
        return S_ERROR_BAD_STATE;
    }

    if (fseek(pFile, nSectorIndex*g_nSectorSize, SEEK_SET) != 0) {
        LOG_E("%s: fseek error: %s", __func__, strerror(errno));
        return errno2serror();
    }

    if (fread(g_pWorkspaceBuffer + nWorkspaceOffset,
                g_nSectorSize, 1,
                pFile) != 1) {
        if (feof(pFile)) {
            LOG_E("%s: fread error: End-Of-File detected", __func__);
            return S_ERROR_ITEM_NOT_FOUND;
        }
        LOG_E("%s: fread error: %s", __func__, strerror(errno));
        return errno2serror();
    }

    return S_SUCCESS;
}

/**
 * This function executes the WRITE instruction.
 *
 * @param nPartitionID: the partition identifier
 * @param nSectorIndex: the index of the sector to read
 * @param nWorkspaceOffset: the offset in the workspace where the sector must be read
 **/
TEEC_Result FSD2::partitionWrite(uint32_t nPartitionID, uint32_t nSectorIndex, uint32_t nWorkspaceOffset)
{
    FILE* pFile;

    LOG_D("%s: >Partition %1X: write sector 0x%X from workspace at offset 0x%X",
        __func__, nPartitionID, nSectorIndex, nWorkspaceOffset);

    pFile = g_pPartitionFiles[nPartitionID];

    if (pFile == NULL) {
        /* The partition is not opened */
        return S_ERROR_BAD_STATE;
    }

    if (fseek(pFile, nSectorIndex*g_nSectorSize, SEEK_SET) != 0) {
        LOG_E("%s: fseek error: %s", __func__, strerror(errno));
        return errno2serror();
    }

    if (fwrite(g_pWorkspaceBuffer + nWorkspaceOffset,
               g_nSectorSize, 1,
               pFile) != 1) {
        LOG_E("%s: fwrite error: %s", __func__, strerror(errno));
        return errno2serror();
    }
    return S_SUCCESS;
}

/**
 * This function executes the SET_SIZE instruction.
 *
 * @param nPartitionID: the partition identifier
 * @param nNewSectorCount: the new sector count
 **/
TEEC_Result FSD2::partitionSetSize(uint32_t nPartitionID, uint32_t nNewSectorCount)
{
    FILE* pFile;
    uint32_t nCurrentSectorCount;

    pFile = g_pPartitionFiles[nPartitionID];

    if (pFile == NULL) {
        /* The partition is not opened */
        return S_ERROR_BAD_STATE;
    }

    /* Determine the current size of the partition */
    if (fseek(pFile, 0, SEEK_END) != 0) {
        LOG_E("%s: fseek error: %s", __func__, strerror(errno));
        return errno2serror();
    }
    nCurrentSectorCount = ftell(pFile) / g_nSectorSize;

    if (nNewSectorCount > nCurrentSectorCount) {
        uint32_t nAddedBytesCount;
        /* Enlarge the partition file. Make sure we actually write
           some non-zero data into the new sectors. Otherwise, some file-system
           might not really reserve the storage space but use a
           sparse representation. In this case, a subsequent write instruction
           could fail due to out-of-space, which we want to avoid. */
        nAddedBytesCount = (nNewSectorCount-nCurrentSectorCount)*g_nSectorSize;
        while (nAddedBytesCount) {
            if (fputc(0xA5, pFile)!=0xA5) {
                LOG_E("%s: fputc error: %s", __func__, strerror(errno));
                return errno2serror();
            }
            nAddedBytesCount--;
        }
    }
    else if (nNewSectorCount < nCurrentSectorCount) {
        int result = 0;
        /* Truncate the partition file */
        result = ftruncate(fileno(pFile),nNewSectorCount * g_nSectorSize);

        if (result) {
            return errno2serror();
        }
    }
    return S_SUCCESS;
}

/**
 * This function executes the SYNC instruction.
 *
 * @param pPartitionID: the partition identifier
 **/
TEEC_Result FSD2::partitionSync(uint32_t nPartitionID)
{
    TEEC_Result nError = S_SUCCESS;
    int result;

    FILE* pFile = g_pPartitionFiles[nPartitionID];

    if (pFile == NULL) {
        /* The partition is not currently opened */
        return S_ERROR_BAD_STATE;
    }

    /* First make sure that the data in the stdio buffers
       is flushed to the file descriptor */
    result=fflush(pFile);
    if (result) {
        nError=errno2serror();
        goto end;
    }
    /* Then synchronize the file descriptor with the file-system */
    result=fdatasync(fileno(pFile));

    if (result) {
        nError=errno2serror();
    }
end:
   return nError;
}

/**
 * This function executes the NOTIFY instruction.
 *
 * @param pMessage the message string
 * @param nMessageType the type of messages
 **/
void FSD2::notify(const wchar_t* pMessage, uint32_t nMessageType)
{
    switch (nMessageType) {
        case DELEGATION_NOTIFY_TYPE_ERROR:
            LOG_E("%s: %ls", __func__, pMessage);
            break;
        case DELEGATION_NOTIFY_TYPE_WARNING:
            LOG_W("%s: %ls", __func__, pMessage);
            break;
        case DELEGATION_NOTIFY_TYPE_DEBUG:
            LOG_D("%s: DEBUG: %ls", __func__, pMessage);
            break;
        case DELEGATION_NOTIFY_TYPE_INFO:
        default:
            LOG_D("%s: %ls", __func__, pMessage);
            break;
    }
}

/*----------------------------------------------------------------------------
 * Command dispatcher function
 *----------------------------------------------------------------------------*/
/* Debug function to show the command name */
const char* FSD2::getCommandtypeString(uint32_t nInstructionID)
{
    switch (nInstructionID) {
        case DELEGATION_INSTRUCTION_PARTITION_CREATE:
            return "PARTITION_CREATE";
        case DELEGATION_INSTRUCTION_PARTITION_OPEN:
            return "PARTITION_OPEN";
        case DELEGATION_INSTRUCTION_PARTITION_READ:
            return "PARTITION_READ";
        case DELEGATION_INSTRUCTION_PARTITION_WRITE:
            return "PARTITION_WRITE";
        case DELEGATION_INSTRUCTION_PARTITION_SET_SIZE:
            return "PARTITION_SET_SIZE";
        case DELEGATION_INSTRUCTION_PARTITION_SYNC:
            return "PARTITION_SYNC";
        case DELEGATION_INSTRUCTION_PARTITION_CLOSE:
            return "PARTITION_CLOSE";
        case DELEGATION_INSTRUCTION_PARTITION_DESTROY:
            return "PARTITION_DESTROY";
        case DELEGATION_INSTRUCTION_SHUTDOWN:
            return "SHUTDOWN";
        case DELEGATION_INSTRUCTION_NOTIFY:
            return "NOTIFY";
        default:
            return "UNKNOWN";
    }

}

void FSD2::FSD2_ExecuteCommand()
{
    TEEC_Result    nError;
    uint32_t       nInstructionsIndex;
    uint32_t       nInstructionsBufferSize = g_pExchangeBuffer->nInstructionsBufferSize;


    LOG_D("%s: nInstructionsBufferSize=%d", __func__, nInstructionsBufferSize);

    /* Reset the operation results */
    nError = TEEC_SUCCESS;
    g_pExchangeBuffer->sAdministrativeData.nSyncExecuted = 0;
    memset(g_pExchangeBuffer->sAdministrativeData.nPartitionErrorStates, 0x00, sizeof(g_pExchangeBuffer->sAdministrativeData.nPartitionErrorStates));
    memset(g_pExchangeBuffer->sAdministrativeData.nPartitionOpenSizes, 0x00, sizeof(g_pExchangeBuffer->sAdministrativeData.nPartitionOpenSizes));

    /* Execute the instructions */
    nInstructionsIndex = 0;
    while (true) {
        DELEGATION_INSTRUCTION * pInstruction;
	    uint32_t nInstructionID;
	    pInstruction = (DELEGATION_INSTRUCTION *)(&g_pExchangeBuffer->sInstructions[nInstructionsIndex/4]);
	    if (nInstructionsIndex + 4 <= nInstructionsBufferSize) {
	        nInstructionID = pInstruction->sGeneric.nInstructionID;
	        nInstructionsIndex+=4;
	    } else {
	        LOG_D("%s: Instruction buffer end, size = %i", __func__, nInstructionsBufferSize);
	        goto instruction_parse_end;
	    }

	    LOG_D("%s: nInstructionID=0x%02X [%s], nInstructionsIndex=%d", __func__, nInstructionID, getCommandtypeString(nInstructionID), nInstructionsIndex);

	    if ((nInstructionID & 0x0F) == 0) {
            /* Partition-independent instruction */
            switch (nInstructionID) {
                case DELEGATION_INSTRUCTION_SHUTDOWN: {
                    terminate(); /* Flag the thread for termination */
                    break;
                }
                case DELEGATION_INSTRUCTION_NOTIFY: {
                    /* Parse the instruction parameters */
                    wchar_t  pMessage[100];
                    uint32_t nMessageType;
                    uint32_t nMessageSize;
                    memset(pMessage, 0, 100*sizeof(wchar_t));

                    if (nInstructionsIndex + 8 <= nInstructionsBufferSize) {
                        nMessageType = pInstruction->sNotify.nMessageType;
                        nMessageSize = pInstruction->sNotify.nMessageSize;
                        nInstructionsIndex+=8;
                    } else {
                        goto instruction_parse_end;
                    }
                    if (nMessageSize > (99)*sizeof(wchar_t)) {
                        /* How to handle the error correctly in this case ? */
                        goto instruction_parse_end;
                    }
                    if (nInstructionsIndex + nMessageSize <= nInstructionsBufferSize) {
                        memcpy(pMessage, &pInstruction->sNotify.nMessage[0], nMessageSize);
                        nInstructionsIndex+=nMessageSize;
                    }
                    else {
                        goto instruction_parse_end;
                    }
                    /* Align the pInstructionsIndex on 4 bytes */
                    nInstructionsIndex = (nInstructionsIndex+3)&~3;
                    notify(pMessage, nMessageType);
                    break;
                }
                default: {
                   LOG_E("%s: Unknown instruction identifier: %02X", __func__, nInstructionID);
                   nError = S_ERROR_BAD_PARAMETERS;
                   break;
                }
            }
	    } else {
            /* Partition-specific instruction */
            uint32_t nPartitionID = (nInstructionID & 0xF0) >> 4;
            if (g_pExchangeBuffer->sAdministrativeData.nPartitionErrorStates[nPartitionID] == S_SUCCESS) {
                /* Execute the instruction only if there is currently no error on the partition */
                switch (nInstructionID & 0x0F) {
                    case DELEGATION_INSTRUCTION_PARTITION_CREATE: {
                        nError = partitionCreate(nPartitionID);
                        LOG_D("%s: INSTRUCTION: ID=0x%x pid=%d err=0x%08X", __func__, (nInstructionID & 0x0F), nPartitionID, nError);
                        break;
                    }
                    case DELEGATION_INSTRUCTION_PARTITION_OPEN: {
                        uint32_t nPartitionSize = 0;
                        nError = partitionOpen(nPartitionID, &nPartitionSize);
                        LOG_D("%s: INSTRUCTION: ID=0x%x pid=%d pSize=%d err=0x%08X", __func__, (nInstructionID & 0x0F), nPartitionID, nPartitionSize, nError);
                        if (nError == S_SUCCESS) {
                            g_pExchangeBuffer->sAdministrativeData.nPartitionOpenSizes[nPartitionID] = nPartitionSize;
                        }
                        break;
                    }
                    case DELEGATION_INSTRUCTION_PARTITION_READ: {
                        /* Parse parameters */
                        uint32_t nSectorID;
                        uint32_t nWorkspaceOffset;
                        if (nInstructionsIndex + 8 <= nInstructionsBufferSize) {
                            nSectorID = pInstruction->sReadWrite.nSectorID;
                            nWorkspaceOffset = pInstruction->sReadWrite.nWorkspaceOffset;
                            nInstructionsIndex+=8;
                        } else {
                            goto instruction_parse_end;
                        }
                        nError = partitionRead(nPartitionID, nSectorID, nWorkspaceOffset);
                        LOG_D("%s: INSTRUCTION: ID=0x%x pid=%d sid=%d woff=%d err=0x%08X", __func__, (nInstructionID & 0x0F), nPartitionID, nSectorID, nWorkspaceOffset, nError);
                        break;
                    }
                    case DELEGATION_INSTRUCTION_PARTITION_WRITE:{
                        /* Parse parameters */
                        uint32_t nSectorID;
                        uint32_t nWorkspaceOffset;
                        if (nInstructionsIndex + 8 <= nInstructionsBufferSize) {
                            nSectorID = pInstruction->sReadWrite.nSectorID;
                            nWorkspaceOffset = pInstruction->sReadWrite.nWorkspaceOffset;
                            nInstructionsIndex+=8;
                        } else {
                            goto instruction_parse_end;
                        }
                        nError = partitionWrite(nPartitionID, nSectorID, nWorkspaceOffset);
                        LOG_D("%s: INSTRUCTION: ID=0x%x pid=%d sid=%d woff=%d err=0x%08X", __func__, (nInstructionID & 0x0F), nPartitionID, nSectorID, nWorkspaceOffset, nError);
                        break;
                    }
                    case DELEGATION_INSTRUCTION_PARTITION_SYNC: {
                        nError = partitionSync(nPartitionID);
                        LOG_D("%s: INSTRUCTION: ID=0x%x pid=%d err=0x%08X", __func__, (nInstructionID & 0x0F), nPartitionID, nError);
                        if (nError == S_SUCCESS) {
                            g_pExchangeBuffer->sAdministrativeData.nSyncExecuted++;
                        }
                        break;
                    }
                    case DELEGATION_INSTRUCTION_PARTITION_SET_SIZE: {
                        uint32_t nNewSize;
                        if (nInstructionsIndex + 4 <= nInstructionsBufferSize) {
                            nNewSize = pInstruction->sSetSize.nNewSize;
                            nInstructionsIndex+=4;
                        } else {
                            goto instruction_parse_end;
                        }
                        nError = partitionSetSize(nPartitionID, nNewSize);
                        LOG_D("%s: INSTRUCTION: ID=0x%x pid=%d nNewSize=%d err=0x%08X", __func__, (nInstructionID & 0x0F), nPartitionID, nNewSize, nError);
                        break;
                    }
                    case DELEGATION_INSTRUCTION_PARTITION_CLOSE: {
                        nError = partitionClose(nPartitionID);
                        LOG_D("%s: INSTRUCTION: ID=0x%x pid=%d err=0x%08X", __func__, (nInstructionID & 0x0F), nPartitionID, nError);
                        break;
                    }
                    case DELEGATION_INSTRUCTION_PARTITION_DESTROY: {
                        nError = partitionDestroy(nPartitionID);
                        LOG_D("%s: INSTRUCTION: ID=0x%x pid=%d err=0x%08X", __func__, (nInstructionID & 0x0F), nPartitionID, nError);
                        break;
                    }
                    default: {
                        LOG_E("%s: Unknown instruction identifier: %02X", __func__, nInstructionID);
                        nError = S_ERROR_BAD_PARAMETERS;
                        break;
                    }
                }
                g_pExchangeBuffer->sAdministrativeData.nPartitionErrorStates[nPartitionID] = nError;
            } else {
                /* Skip the instruction if there is currently error on the partition */
                switch (nInstructionID & 0x0F) {
                    case DELEGATION_INSTRUCTION_PARTITION_CREATE:
                    case DELEGATION_INSTRUCTION_PARTITION_OPEN:
                    case DELEGATION_INSTRUCTION_PARTITION_SYNC:
                    case DELEGATION_INSTRUCTION_PARTITION_CLOSE:
                    case DELEGATION_INSTRUCTION_PARTITION_DESTROY:
                        break;
                    case DELEGATION_INSTRUCTION_PARTITION_READ: {
                        if (nInstructionsIndex + 8 <= nInstructionsBufferSize) {
                            nInstructionsIndex+=8;
                        } else {
                            goto instruction_parse_end;
                        }
                        break;
                    }
                    case DELEGATION_INSTRUCTION_PARTITION_WRITE:{
                        if (nInstructionsIndex + 8 <= nInstructionsBufferSize) {
                            nInstructionsIndex+=8;
                        } else {
                            goto instruction_parse_end;
                        }
                        break;
                    }
                    case DELEGATION_INSTRUCTION_PARTITION_SET_SIZE: {
                        if (nInstructionsIndex + 4 <= nInstructionsBufferSize) {
                            nInstructionsIndex+=4;
                        } else {
                            goto instruction_parse_end;
                        }
                        break;
                    }
                    default: {
                        LOG_E("%s: Unknown instruction identifier: %02X", __func__, nInstructionID);
                        /* OMS: update partition error with BAD PARAM ? */
                        break;
                    }
                }
            }
        }
    }
instruction_parse_end:
//      memset(pOperation, 0, sizeof(TEEC_Operation));
	return;
}
