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
 * FSD server.
 *
 * Handles incoming storage requests from TA through STH
 */

#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <signal.h>
#include <memory>
#include <stdio.h>
#include <assert.h>
#include <pthread.h>

#include "PrivateRegistry.h"
#include "FSD.h"
#include <log.h>

/* The following definitions are not exported in the header files of the
   client API. */
#define TEE_DATA_FLAG_EXCLUSIVE           0x00000400
#define TEE_ERROR_STORAGE_NO_SPACE       ((TEEC_Result)0xFFFF3041)
#define TEE_ERROR_CORRUPT_OBJECT         ((TEEC_Result)0xF0100001)

//------------------------------------------------------------------------------
const char * const FSD::m_server_name = "McDaemon.FSD";

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

FSD::FSD(size_t dci_msg_size):
        m_dci(NULL),
        m_dci_msg_size(dci_msg_size)
{
    memset(&m_sessionHandle, 0, sizeof(mcSessionHandle_t));
    pthread_mutex_init(&m_close_lock, NULL);
}

FSD::~FSD(void)
{
    LOG_D("Destroying File Storage Daemon object");
    FSD_Close();
}

void FSD::run(void)
{
    struct stat st;
    mcResult_t ret;
    const char* tbstpath;

    sigset_t sigmask;
    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGUSR1);
    pthread_sigmask(SIG_UNBLOCK, &sigmask, (sigset_t *)0);

    LOG_D("FSD::run()====");

    tbstpath = getTbStoragePath().c_str();

    /* Create Tbase storage directory */
    if (stat(tbstpath, &st) == -1) {
        LOG_D("%s: Creating <t-base storage Folder %s", TAG_LOG, tbstpath);
        if (mkdir(tbstpath, 0700) == -1) {
            LOG_E("%s: failed creating storage folder", TAG_LOG);
        }
    }

    do {
        LOG_D("%s: starting File Storage Daemon", TAG_LOG);
        ret = FSD_Open();
        if (ret != MC_DRV_OK)
            break;
        LOG_D("%s: Start listening for request from STH", TAG_LOG);
        FSD_listenDci();

        /* if we are here, something failed in the communication with the
         * Trusted Storage driver. We'll re-start and try again. Shutting down
         * isn't helpful here.
         * Optionally we could count and after 5 attempt abort, but that is also
         * not so ideal.
         */

        /* clean up first, ignore errors */
        (void) FSD_Close();

        LOG_W("Restarting communications with <t-base STH");

    } while(!shouldTerminate());
    LOG_I("Exiting File Storage Daemon 0x%08X", ret);
}

mcResult_t FSD::FSD_Open(void)
{
    dciMessage_t *dci;
    mcResult_t   mcRet;
    const mcUuid_t uuid = DRV_STH_UUID;

    dci = (dciMessage_t*)new uint8_t[m_dci_msg_size];
    if (dci == NULL) {
        LOG_E("FSD_Open(): allocation failed");
        return MC_DRV_ERR_NO_FREE_MEMORY;
    }

    /* Open <t-base device */
    mcRet = mcOpenDevice(MC_DEVICE_ID_DEFAULT);
    if (MC_DRV_OK != mcRet)
    {
        LOG_E("FSD_Open(): mcOpenDevice returned: %d", mcRet);
        goto error;
    }

    /* Open session to the sth driver */
    mcSessionHandle_t sessionHandle;
    memset(&sessionHandle, 0, sizeof(sessionHandle));
    sessionHandle.deviceId = MC_DEVICE_ID_DEFAULT;
    mcRet = mcOpenSession(&sessionHandle,
                          &uuid,
                          (uint8_t *) dci,
                          static_cast<uint32_t>(m_dci_msg_size));
    if (MC_DRV_OK != mcRet)
    {
        LOG_E("FSD_Open(): mcOpenSession returned: %d", mcRet);
        goto close_device;
    }

    /* Wait for notification from SWd */
    mcRet = mcWaitNotification(&sessionHandle, MC_INFINITE_TIMEOUT);
    if (MC_DRV_OK != mcRet)
        goto close_session;

    /**
     * The following notification is required for initial sync up
     * with the driver
     */
    dci->command.header.commandId = CMD_ST_SYNC;
    mcRet = mcNotify(&sessionHandle);
    if (MC_DRV_OK != mcRet)
    {
        LOG_E("FSD_Open(): mcNotify returned: %d", mcRet);
        goto close_session;
    }

    /* Wait for notification from SWd */
    mcRet = mcWaitNotification(&sessionHandle, MC_INFINITE_TIMEOUT);
    if (MC_DRV_OK != mcRet)
        goto close_session;

    LOG_D("FSD_Open(): received first notification");
    LOG_D("FSD_Open(): send notification  back");
    mcRet = mcNotify(&sessionHandle);
    if (mcRet == MC_DRV_OK) {
        LockGuard lock(&m_close_lock);
        if(!shouldTerminate()) {
            m_dci = dci;
            m_sessionHandle = sessionHandle;
            LOG_D("FSD_Open(): returning success");
            return mcRet;
        } else
            LOG_E("FSD_Open(): thread should be terminated, bailing out");
    } else
        LOG_E("FSD_Open(): mcNotify returned: %d", mcRet);

close_session:
    mcCloseSession(&sessionHandle);

close_device:
    mcCloseDevice(MC_DEVICE_ID_DEFAULT);

error:
    delete[] dci;

    return mcRet;
}

mcResult_t FSD::FSD_Close(void)
{
    mcResult_t   mcRet = MC_DRV_OK;

    LockGuard lock(&m_close_lock);

    /* Close session to the debug driver trustlet */
    if(m_dci != NULL) {

        mcRet = mcCloseSession(&m_sessionHandle);
        if (MC_DRV_OK != mcRet)
            LOG_E("FSD_Close(): mcCloseSession returned: %d", mcRet);

        memset(&m_sessionHandle, 0, sizeof(mcSessionHandle_t));

        /* Close <t-base device */
        mcRet = mcCloseDevice(MC_DEVICE_ID_DEFAULT);
        if (MC_DRV_OK != mcRet)
            LOG_E("FSD_Close(): mcCloseDevice returned: %d", mcRet);

        /* Clear DCI message buffer */
        memset(m_dci, 0, m_dci_msg_size);
        delete[] m_dci;
        m_dci = NULL;
    }

    LOG_D("FSD_Close(): returning: 0x%.8x", mcRet);
    return mcRet;
}

void FSD::FSD_listenDci(void)
{
    mcResult_t mcRet = MC_DRV_OK;
    LOG_D("FSD_listenDci(): DCI listener");

    while (mcRet == MC_DRV_OK && !shouldTerminate()) {
        LOG_D("FSD_listenDci(): Waiting for notification");

        LockGuard lock(&m_close_lock);
        if(m_dci == NULL) {
            break;
        }

        /* Wait for notification from SWd */
        mcRet = mcWaitNotification(&m_sessionHandle,
                                   MC_INFINITE_TIMEOUT_INTERRUPTIBLE);
        if (mcRet != MC_DRV_OK) {
            if (mcRet == MC_DRV_ERR_INTERRUPTED_BY_SIGNAL) {
                LOG_D("Giving up on signal");
                terminate();
            } else {
                LOG_ERRNO("FSD_listenDci(): mcWaitNotification failed");
            }
        } else {
            /* Received notification. */
            LOG_D("FSD_listenDci(): Received Command (0x%.8x) from STH",
                  m_dci->sth_request.type);
            FSD_ExecuteCommand();

            /* notify the STH */
            mcRet = mcNotify(&m_sessionHandle);
            if (mcRet != MC_DRV_OK) {
                LOG_E("mcNotify() returned: %d", mcRet);
            }
        }
    }
}

// Output FileName is guaranteed to be 0 ended.
void FSD_HexFileName(
				unsigned char*		fn,
				char*				FileName,
				uint32_t 			fnSize,
				uint32_t 			FileNameSize
){
	assert (FileNameSize == fnSize *2 +1);
	char tmp[fnSize * 2 + 1];
	uint32_t i=0;

	// tmp is size-variabled array, have to init it with memset
	memset(tmp, 0, fnSize * 2 + 1);
	for (i = 0; i < fnSize; i++) {
        // the implementation of snprintf counts also the trailing "\0"
		snprintf(&tmp[i * 2], 2 + sizeof("\0"), "%02x", fn[i]);
	}
	tmp[fnSize * 2 + 1] = '\0';
	strncpy(FileName,tmp,FileNameSize);
}


// Output DirName is guaranteed to be 0 ended.
void FSD_CreateTaDirName(
				TEE_UUID*			ta_uuid,
				char*				DirName,
				uint32_t 			uuidSize,
				uint32_t            DirNameSize
){

	assert (DirNameSize == uuidSize *2 +1);
	char tmp[uuidSize * 2 + 1];
	unsigned char*		fn;
	uint32_t i=0;

	memset(tmp, 0, uuidSize * 2 + 1);
	fn = (unsigned char*)ta_uuid;
	for (i = 0; i < uuidSize; i++) {
		// the implementation of snprintf counts also the trailing "\0"
		snprintf(&tmp[i * 2], 2 + sizeof("\0"), "%02x", fn[i]);
	}
	tmp[uuidSize * 2 + 1] = '\0';
	strncpy(DirName,tmp,DirNameSize);
}

void FSD_CreateTaDirPath(
				std::string          storage,
				STH_FSD_message_t    *sth_request,
				char                 *TAdirpath,
				size_t
){
	const char* tbstpath = storage.c_str();
	size_t tbstpathSize = storage.length();
	char tadirname[TEE_UUID_STRING_SIZE+1] = {0};

	FSD_CreateTaDirName(&sth_request->uuid,tadirname,sizeof(TEE_UUID),sizeof(tadirname));

	strncpy(TAdirpath, tbstpath, tbstpathSize);
	strncat(TAdirpath, "/", strlen(("/")));
	strncat(TAdirpath, tadirname, strlen(tadirname));

	LOG_D("%s: Storage    %s", __func__, tbstpath);
	LOG_D("%s: TA dirname %s", __func__, tadirname);

}

void FSD_CreateFilePath(
				STH_FSD_message_t    *sth_request,
				char                 *Filepath,
				size_t               ,
				char                 *TAdirpath,
				size_t               TAdirpathSize // sizeof(TAdirpath)

){
	char filename[2*FILENAMESIZE+1] = {0};
	FSD_HexFileName(sth_request->filename,filename,FILENAMESIZE,sizeof(filename));

	strncpy(Filepath, TAdirpath, TAdirpathSize-1);
	strncat(Filepath, "/", strlen("/"));
	strncat(Filepath, filename, strlen(filename));

	LOG_D("%s: filename   %s", __func__, filename);
	LOG_D("%s: fullpath   %s", __func__, Filepath);

}

//------------------------------------------------------------------------------
void FSD::FSD_ExecuteCommand(void)
{
	switch (m_dci->sth_request.type) {

	case STH_MESSAGE_TYPE_LOOK:
		LOG_D("FSD_ExecuteCommand(): Looking for file");
		m_dci->sth_request.status = FSD_LookFile();
		break;

	case STH_MESSAGE_TYPE_READ:
		LOG_D("FSD_ExecuteCommand(): Reading file");
		m_dci->sth_request.status = FSD_ReadFile();
		break;

	case STH_MESSAGE_TYPE_WRITE:
		LOG_D("FSD_ExecuteCommand(): Writing file");
		m_dci->sth_request.status = FSD_WriteFile();
		break;

	case STH_MESSAGE_TYPE_DELETE:
		LOG_D("FSD_ExecuteCommand(): Deleting file");
		m_dci->sth_request.status = FSD_DeleteFile();
		LOG_D("FSD_ExecuteCommand(): file deleted status is 0x%08x",
			m_dci->sth_request.status);
		break;

	case STH_MESSAGE_TYPE_DELETE_ALL:
		LOG_D("FSD_ExecuteCommand(): Deleting directory");
		m_dci->sth_request.status = FSD_DeleteDir();
		LOG_D("FSD_ExecuteCommand(): Directory deleted status is "
		  "0x%08x", m_dci->sth_request.status);
		break;

	default:
		LOG_E("FSD_ExecuteCommand(): Ignoring unknown command %x",
			m_dci->sth_request.type);
		break;
	}
}


/****************************  File operations  *******************************/

uint32_t FSD::FSD_LookFile(void)
{
	FILE * pFile=NULL;
	STH_FSD_message_t* sth_request=NULL;
	size_t res;
	std::string storage = getTbStoragePath();
	char TAdirpath[storage.length()+1+TEE_UUID_STRING_SIZE+1];
	char Filepath[storage.length()+1+TEE_UUID_STRING_SIZE+1+2*FILENAMESIZE+1];

	memset(TAdirpath, 0, storage.length()+1+TEE_UUID_STRING_SIZE+1);
	memset(Filepath, 0, storage.length()+1+TEE_UUID_STRING_SIZE+1+2*FILENAMESIZE+1);
	sth_request= &m_dci->sth_request;
	FSD_CreateTaDirPath(
					storage,
					sth_request,
					TAdirpath,
					sizeof(TAdirpath));

	FSD_CreateFilePath(
					sth_request,
					Filepath,
					sizeof(Filepath),
					TAdirpath,
					sizeof(TAdirpath));
	pFile = fopen(Filepath, "r");
	if (pFile==NULL)
	{
		LOG_E("%s: Error looking for file 0x%.8x",__func__,TEEC_ERROR_ITEM_NOT_FOUND);
		return TEEC_ERROR_ITEM_NOT_FOUND;
	}

	res = fread(sth_request->payload,sizeof(char),sth_request->payloadLen,pFile);

	if (ferror(pFile))
	{
		LOG_E("%s: Error reading file res is %zu and errno is %s",__func__,res,strerror(errno));
		fclose(pFile);
		return TEEC_ERROR_ITEM_NOT_FOUND;
	}

    if (res < sth_request->payloadLen)
    {
        //File is shorter than expected
        if (feof(pFile)) {
            LOG_D("%s: EOF reached: res is %zu, payloadLen is %d",__func__,res, sth_request->payloadLen);
        }
    }

	fclose(pFile);

	return TEEC_SUCCESS;
}

uint32_t FSD::FSD_ReadFile(void)
{
	FILE * pFile=NULL;
	STH_FSD_message_t* sth_request=NULL;
	size_t res;
	std::string storage = getTbStoragePath();
	char TAdirpath[storage.length()+1+TEE_UUID_STRING_SIZE+1];
	char Filepath[storage.length()+1+TEE_UUID_STRING_SIZE+1+2*FILENAMESIZE+1];

	memset(TAdirpath, 0, storage.length()+1+TEE_UUID_STRING_SIZE+1);
	memset(Filepath, 0, storage.length()+1+TEE_UUID_STRING_SIZE+1+2*FILENAMESIZE+1);
	sth_request= &m_dci->sth_request;
	FSD_CreateTaDirPath(
					storage,
					sth_request,
					TAdirpath,
					sizeof(TAdirpath));

	FSD_CreateFilePath(
					sth_request,
					Filepath,
					sizeof(Filepath),
					TAdirpath,
					sizeof(TAdirpath));

	pFile = fopen(Filepath, "r");
	if (pFile==NULL)
	{
		LOG_E("%s: Error looking for file 0x%.8x", __func__,TEEC_ERROR_ITEM_NOT_FOUND);
		return TEEC_ERROR_ITEM_NOT_FOUND;
	}
	res = fread(sth_request->payload,sizeof(char),sth_request->payloadLen,pFile);

	if (ferror(pFile))
	{
		LOG_E("%s: Error reading file res is %zu and errno is %s",__func__,res,strerror(errno));
		fclose(pFile);
		return TEE_ERROR_CORRUPT_OBJECT;
	}

    if (res < sth_request->payloadLen)
    {
       //File is shorter than expected
       if (feof(pFile)) {
           LOG_D("%s: EOF reached: res is %zu, payloadLen is %d",__func__,res, sth_request->payloadLen);
       }
    }

    fclose(pFile);

	return TEEC_SUCCESS;
}


uint32_t FSD::FSD_WriteFile(void)
{
	FILE * pFile=NULL;
	int fd=0;
	STH_FSD_message_t* sth_request=NULL;
	size_t res=0;
	int stat=0;
	std::string storage = getTbStoragePath();
	char TAdirpath[storage.length()+1+TEE_UUID_STRING_SIZE+1];
	char Filepath[storage.length()+1+TEE_UUID_STRING_SIZE+1+2*FILENAMESIZE+1];
	char Filepath_new[storage.length()+TEE_UUID_STRING_SIZE+2*FILENAMESIZE+strlen(NEW_EXT)+1];

	memset(TAdirpath, 0, storage.length()+1+TEE_UUID_STRING_SIZE+1);
	memset(Filepath, 0, storage.length()+1+TEE_UUID_STRING_SIZE+1+2*FILENAMESIZE+1);
	memset(Filepath_new, 0, storage.length()+TEE_UUID_STRING_SIZE+2*FILENAMESIZE+strlen(NEW_EXT)+1);
	sth_request= &m_dci->sth_request;
	FSD_CreateTaDirPath(
					storage,
					sth_request,
					TAdirpath,
					sizeof(TAdirpath));

    stat = mkdir(TAdirpath, 0700);
	if((stat==-1) && (errno!=EEXIST))
	{
		LOG_E("%s: error when creating TA dir: %s (%s)",__func__,TAdirpath,strerror(errno));
		return TEE_ERROR_STORAGE_NO_SPACE;
	}

	/* Directory exists. */
	FSD_CreateFilePath(
					sth_request,
					Filepath,
					sizeof(Filepath),
					TAdirpath,
					sizeof(TAdirpath));
	strncpy(Filepath_new,Filepath,sizeof(Filepath) - 1);
	strncat(Filepath_new,NEW_EXT,strlen(NEW_EXT));
	LOG_D("%s: filename.new   %s", __func__, Filepath_new);
	if(sth_request->flags == TEE_DATA_FLAG_EXCLUSIVE)
	{
		LOG_D("%s: opening file in exclusive mode",__func__);
		fd = open(Filepath, O_WRONLY | O_CREAT | O_EXCL, S_IWUSR);
		if (fd == -1)
		{
			LOG_E("%s: error creating file: %s",__func__,strerror(errno));
			return TEE_ERROR_CORRUPT_OBJECT;
		}
		else
		{
			close(fd);
		}
	}
	pFile = fopen(Filepath_new, "w");
	LOG_D("%s: opening file for writing",__func__);
	if(pFile==NULL)
	{
		if(remove(Filepath)==-1)
		{
			LOG_E("%s: remove failed: %s",__func__, strerror(errno));
		}
		return TEE_ERROR_STORAGE_NO_SPACE;
	}
	res = fwrite(sth_request->payload,sizeof(char),sth_request->payloadLen,pFile);

	if (ferror(pFile))
	{
		LOG_E("%s: Error writing file res is %zu and errno is %s",__func__,res,strerror(errno));
		fclose(pFile);
		if(remove(Filepath)==-1)
		{
			LOG_E("%s: remove failed: %s",__func__, strerror(errno));
		}
		if(remove(Filepath_new)==-1)
		{
			LOG_E("%s: remove failed: %s",__func__, strerror(errno));
		}
		return TEE_ERROR_STORAGE_NO_SPACE;
	}
	else
	{
		res = fclose(pFile);
		if ((int32_t) res < 0)
		{
			LOG_E("%s: Error closing file res is %zu and errno is %s",__func__,res,strerror(errno));
			if(remove(Filepath)==-1)
            {
                LOG_E("%s: remove failed: %s",__func__, strerror(errno));
            }
			if(remove(Filepath_new)==-1)
            {
                LOG_E("%s: remove failed: %s",__func__, strerror(errno));
            }
			return TEE_ERROR_STORAGE_NO_SPACE;
		}

		res = rename(Filepath_new,Filepath);
		if ((int32_t) res < 0)
		{
			LOG_E("%s: Error renaming: %s",__func__,strerror(errno));
			if(remove(Filepath)==-1)
            {
                LOG_E("%s: remove failed: %s",__func__, strerror(errno));
            }
			if(remove(Filepath_new)==-1)
            {
                LOG_E("%s: remove failed: %s",__func__, strerror(errno));
            }
			return TEE_ERROR_STORAGE_NO_SPACE;
		}
	}
	return TEEC_SUCCESS;
}

uint32_t FSD::FSD_DeleteFile(void)
{
	FILE * pFile=NULL;
	mcResult_t ret;
	size_t res;
	STH_FSD_message_t* sth_request=NULL;
	std::string storage = getTbStoragePath();
	char TAdirpath[storage.length()+1+TEE_UUID_STRING_SIZE+1];
	char Filepath[storage.length()+1+TEE_UUID_STRING_SIZE+1+2*FILENAMESIZE+1];

	memset(TAdirpath, 0, storage.length()+1+TEE_UUID_STRING_SIZE+1);
	memset(Filepath, 0, storage.length()+1+TEE_UUID_STRING_SIZE+1+2*FILENAMESIZE+1);
	sth_request= &m_dci->sth_request;
	FSD_CreateTaDirPath(
					storage,
					sth_request,
					TAdirpath,
					sizeof(TAdirpath));

	FSD_CreateFilePath(
					sth_request,
					Filepath,
					sizeof(Filepath),
					TAdirpath,
					sizeof(TAdirpath));

	pFile = fopen(Filepath, "r");
	if (pFile==NULL)
	{
		LOG_D("%s: file not found: %s (%s)",__func__, Filepath, strerror(errno));
		ret = TEEC_SUCCESS;
	}
	else
	{
		fclose(pFile);
		if(remove(Filepath)==-1)
		{
			ret = TEE_ERROR_STORAGE_NO_SPACE;
		}
	}

	res = rmdir(TAdirpath);
	if (((int32_t) res < 0) && (errno != ENOTEMPTY) && (errno != EEXIST) && (errno != ENOENT))
	{
		ret = TEE_ERROR_STORAGE_NO_SPACE;
		LOG_E("%s: rmdir failed: %s (%s)",__func__, TAdirpath, strerror(errno));
	}
	else
	{
		ret = TEEC_SUCCESS;
	}

	return ret;
}

/**
 * FSD_DeleteDir()
 *
 * Deletes all files from a given directory and then the directory itself
 *
 * @retval TEE_SUCCESS everything went OK
 * @retval TEE_ERROR_CORRUPT_OBJECT any other error reason
 */
mcResult_t FSD::FSD_DeleteDir(void)
{
	STH_FSD_message_t *sth_request = &m_dci->sth_request;

    switch (mcRegistryCleanupGPTAStorage((mcUuid_t *) &sth_request->uuid)) {
    case MC_DRV_OK:
        return TEEC_SUCCESS;
    default:
        return TEE_ERROR_CORRUPT_OBJECT;
    }
}

//------------------------------------------------------------------------------
