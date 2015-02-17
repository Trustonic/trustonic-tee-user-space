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
 * FSD server.
 *
 * Handles incoming storage requests from TA through STH
 */
#include "public/FSD.h"
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <cstdlib>
#include <stdio.h>
#include <assert.h>
//#define LOG_VERBOSE
#include "log.h"

#include "MobiCoreRegistry.h"

/* The following definitions are not exported in the header files of the
   client API. */
#define TEE_DATA_FLAG_EXCLUSIVE              0x00000400
#define TEE_ERROR_STORAGE_NO_SPACE       ((TEEC_Result)0xFFFF3041)
#define TEE_ERROR_CORRUPT_OBJECT         ((TEEC_Result)0xF0100001)

extern string getTbStoragePath();

//------------------------------------------------------------------------------
FSD::FSD(
		void
)
{
    memset(&sessionHandle, 0, sizeof(mcSessionHandle_t));
    dci = NULL;
}

FSD::~FSD(
    void
)
{
	FSD_Close();
}

//------------------------------------------------------------------------------
void FSD::run(
    void
)
{
    struct stat st = {0};
    mcResult_t ret;
    string storage = getTbStoragePath();
    const char* tbstpath = storage.c_str();
    bool loop_again = false;

    /* Create Tbase storage directory */
    if (stat(tbstpath, &st) == -1) {
        LOG_I("%s: Creating <t-base storage Folder %s\n", TAG_LOG, tbstpath);
        if (mkdir(tbstpath, 0700) == -1) {
            LOG_E("%s: failed creating storage folder\n", TAG_LOG);
        }
    }

    do {
        LOG_I("%s: starting File Storage Daemon", TAG_LOG);
        ret = FSD_Open();
        if (ret != MC_DRV_OK)
            break;
        LOG_I("%s: Start listening for request from STH", TAG_LOG);
        FSD_listenDci();

        /* if we are here, something failed in the communication with the
         * Trusted Storage driver. We'll re-start and try again. Shutting down
         * isn't helpful here.
         * Optionally we could count and after 5 attempt abort, but that is also
         * not so ideal.
         */
        loop_again = true;

        /* clean up first, ignore errors */
        (void) FSD_Close();

        LOG_W("Restarting communications with <t-base STH\n");

    } while(loop_again);
    LOG_E("Exiting File Storage Daemon 0x%08X", ret);
}

mcResult_t FSD::FSD_Open(void) {
    mcResult_t   mcRet;
    const mcUuid_t uuid = DRV_STH_UUID;

    memset(&sessionHandle,0, sizeof(mcSessionHandle_t));

    dci = (dciMessage_t*)calloc(DCI_BUFF_SIZE,sizeof(uint8_t));
    if (dci == NULL) {
        LOG_E("FSD_Open(): allocation failed");
        return MC_DRV_ERR_NO_FREE_MEMORY;
    }

    /* Open <t-base device */
    mcRet = mcOpenDevice(MC_DEVICE_ID_DEFAULT);
    if (MC_DRV_OK != mcRet)
    {
        LOG_E("FSD_Open(): mcOpenDevice returned: %d\n", mcRet);
        goto error;
    }

    /* Open session to the sth driver */
    sessionHandle.deviceId = MC_DEVICE_ID_DEFAULT;
    mcRet = mcOpenSession(&sessionHandle,
                          &uuid,
                          (uint8_t *) dci,
                          DCI_BUFF_SIZE);
    if (MC_DRV_OK != mcRet)
    {
        LOG_E("FSD_Open(): mcOpenSession returned: %d\n", mcRet);
        goto close_device;
    }

    /* Wait for notification from SWd */
    mcRet = mcWaitNotification(&sessionHandle, MC_INFINITE_TIMEOUT);
    if (MC_DRV_OK != mcRet)
    {
        goto close_session;
    }

    /**
     * The following notification is required for initial sync up
     * with the driver
     */
    dci->command.header.commandId = CMD_ST_SYNC;
    mcRet = mcNotify(&sessionHandle);
    if (MC_DRV_OK != mcRet)
    {
        LOG_E("FSD_Open(): mcNotify returned: %d\n", mcRet);
        goto close_session;
    }

    /* Wait for notification from SWd */
    mcRet = mcWaitNotification(&sessionHandle, MC_INFINITE_TIMEOUT);
    if (MC_DRV_OK != mcRet)
    {
        goto close_session;
    }
    LOG_I("FSD_Open(): received first notification \n");
    LOG_I("FSD_Open(): send notification  back \n");
    mcRet = mcNotify(&sessionHandle);
    if (MC_DRV_OK != mcRet)
    {
        LOG_E("FSD_Open(): mcNotify returned: %d\n", mcRet);
        goto close_session;
    }

    LOG_I("FSD_Open(): returning success");
    return mcRet;

close_session:
    mcCloseSession(&sessionHandle);

close_device:
    mcCloseDevice(MC_DEVICE_ID_DEFAULT);

error:
    free(dci);
    dci = NULL;

    return mcRet;
}

mcResult_t FSD::FSD_Close(void){
    mcResult_t   mcRet;

    /* Clear DCI message buffer */
    memset(dci, 0, sizeof(dciMessage_t));

    /* Close session to the debug driver trustlet */
    mcRet = mcCloseSession(&sessionHandle);
    if (MC_DRV_OK != mcRet)
    {
        LOG_E("FSD_Close(): mcCloseSession returned: %d\n", mcRet);
    }

    free(dci);
    dci = NULL;
    memset(&sessionHandle,0,sizeof(mcSessionHandle_t));

    /* Close <t-base device */
    mcRet = mcCloseDevice(MC_DEVICE_ID_DEFAULT);
    if (MC_DRV_OK != mcRet)
    {
        LOG_E("FSD_Close(): mcCloseDevice returned: %d\n", mcRet);
    }

    LOG_I("FSD_Close(): returning: 0x%.8x\n", mcRet);

    return mcRet;
}


void FSD::FSD_listenDci(void){
    mcResult_t  mcRet;
    LOG_I("FSD_listenDci(): DCI listener \n");


    for(;;)
    {
        LOG_I("FSD_listenDci(): Waiting for notification\n");

        /* Wait for notification from SWd */
        if (MC_DRV_OK != mcWaitNotification(&sessionHandle, MC_INFINITE_TIMEOUT))
        {
            LOG_E("FSD_listenDci(): mcWaitNotification failed\n");
            break;
        }

        /* Received exception. */
        LOG_I("FSD_listenDci(): Received Command (0x%.8x) from STH\n",
              dci->sth_request.type);

        mcRet = FSD_ExecuteCommand();

        /* notify the STH*/
        mcRet = mcNotify(&sessionHandle);
        if (MC_DRV_OK != mcRet) {
            LOG_E("FSD_executeCommand(): mcNotify returned: %d\n", mcRet);
            break;
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
static void FSD_CreateTaDirName(
				TEE_UUID*			ta_uuid,
				char*				DirName,
				uint32_t            DirNameSize
){

	assert (DirNameSize == sizeof(TEE_UUID) *2 +1);
	char tmp[sizeof(TEE_UUID) * 2 + 1];
	unsigned char* fn;
	uint32_t i=0;

	memset(tmp, 0, sizeof(tmp));
	fn = (unsigned char*)ta_uuid;
	for (i = 0; i < sizeof(TEE_UUID); i++) {
		// the implementation of snprintf counts also the trailing "\0"
		snprintf(&tmp[i * 2], 2 + sizeof("\0"), "%02x", fn[i]);
	}
	strncpy(DirName,tmp,DirNameSize);
}

void FSD_CreateTaDirPath(
				string               storage,
				STH_FSD_message_t    *sth_request,
				char                 *TAdirpath,
				size_t               TAdirpathSize   //sizeof(TAdirpathSize)
){
	const char* tbstpath = storage.c_str();
	size_t tbstpathSize = storage.length();
	char tadirname[TEE_UUID_STRING_SIZE+1] = {0};
	assert (TAdirpathSize == tbstpathSize+1+TEE_UUID_STRING_SIZE+1);

	FSD_CreateTaDirName(&sth_request->uuid,tadirname,sizeof(tadirname));

	strncpy(TAdirpath, tbstpath, tbstpathSize);
	strncat(TAdirpath, "/", strlen(("/")));
	strncat(TAdirpath, tadirname, strlen(tadirname));

	LOG_I("%s: Storage    %s\n", __func__, tbstpath);
	LOG_I("%s: TA dirname %s\n", __func__, tadirname);

}

void FSD_CreateFilePath(
				STH_FSD_message_t    *sth_request,
				char                 *Filepath,
				size_t               FilepathSize, // sizeof(FilepathSize)
				char                 *TAdirpath,
				size_t               TAdirpathSize // sizeof(TAdirpath)

){
	char filename[2*FILENAMESIZE+1] = {0};
	assert (FilepathSize == TAdirpathSize + 2*FILENAMESIZE+1);
	FSD_HexFileName(sth_request->filename,filename,FILENAMESIZE,sizeof(filename));

	strncpy(Filepath, TAdirpath, TAdirpathSize-1);
	strncat(Filepath, "/", strlen("/"));
	strncat(Filepath, filename, strlen(filename));

	LOG_I("%s: filename   %s\n", __func__, filename);
	LOG_I("%s: fullpath   %s\n", __func__, Filepath);

}

//------------------------------------------------------------------------------
mcResult_t FSD::FSD_ExecuteCommand(void){
	switch(dci->sth_request.type)
			{
				//--------------------------------------
				case STH_MESSAGE_TYPE_LOOK:
					LOG_I("FSD_ExecuteCommand(): Looking for file\n");
					dci->sth_request.status=FSD_LookFile();

					break;
				//--------------------------------------
				case STH_MESSAGE_TYPE_READ:
					LOG_I("FSD_ExecuteCommand(): Reading file\n");
					dci->sth_request.status=FSD_ReadFile();

					break;
				//--------------------------------------
				case STH_MESSAGE_TYPE_WRITE:
					LOG_I("FSD_ExecuteCommand(): Writing file\n");
					dci->sth_request.status=FSD_WriteFile();

					break;
				//--------------------------------------
				case STH_MESSAGE_TYPE_DELETE:
					LOG_I("FSD_ExecuteCommand(): Deleting file\n");
					dci->sth_request.status=FSD_DeleteFile();
					LOG_I("FSD_ExecuteCommand(): file deleted status is 0x%08x\n",dci->sth_request.status);
                    break;
                //--------------------------------------
                case STH_MESSAGE_TYPE_DELETE_ALL:
                    LOG_I("FSD_ExecuteCommand(): Deleting directory\n");
                    dci->sth_request.status = FSD_DeleteDir();
                    LOG_I("FSD_ExecuteCommand(): Directory deleted status is "
                          "0x%08x\n", dci->sth_request.status);
                    break;

				//--------------------------------------
				default:
					LOG_E("FSD_ExecuteCommand(): Received unknown command %x. Ignoring..\n", dci->sth_request.type);
					break;
			}
	return dci->sth_request.status;
}


/****************************  File operations  *******************************/


mcResult_t FSD::FSD_LookFile(void){
	FILE * pFile=NULL;
	STH_FSD_message_t* sth_request=NULL;
	size_t res;
	string storage = getTbStoragePath();
	char TAdirpath[storage.length()+1+TEE_UUID_STRING_SIZE+1];
	char Filepath[storage.length()+1+TEE_UUID_STRING_SIZE+1+2*FILENAMESIZE+1];

	memset(TAdirpath, 0, storage.length()+1+TEE_UUID_STRING_SIZE+1);
	memset(Filepath, 0, storage.length()+1+TEE_UUID_STRING_SIZE+1+2*FILENAMESIZE+1);
	sth_request= &dci->sth_request;
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
		LOG_E("%s: Error looking for file 0x%.8x\n",__func__,TEEC_ERROR_ITEM_NOT_FOUND);
		return TEEC_ERROR_ITEM_NOT_FOUND;
	}

	res = fread(sth_request->payload,sizeof(char),sth_request->payloadLen,pFile);

	if (ferror(pFile))
	{
		LOG_E("%s: Error reading file res is %zu and errno is %s\n",__func__,res,strerror(errno));
        fclose(pFile);
		return TEEC_ERROR_ITEM_NOT_FOUND;
	}

    if (res < sth_request->payloadLen)
    {
        //File is shorter than expected
        if (feof(pFile)) {
            LOG_I("%s: EOF reached: res is %zu, payloadLen is %d\n",__func__,res, sth_request->payloadLen);
        }
    }

	fclose(pFile);

	return TEEC_SUCCESS;
}


mcResult_t FSD::FSD_ReadFile(void){
	FILE * pFile=NULL;
	STH_FSD_message_t* sth_request=NULL;
	size_t res;
	string storage = getTbStoragePath();
	char TAdirpath[storage.length()+1+TEE_UUID_STRING_SIZE+1];
	char Filepath[storage.length()+1+TEE_UUID_STRING_SIZE+1+2*FILENAMESIZE+1];

	memset(TAdirpath, 0, storage.length()+1+TEE_UUID_STRING_SIZE+1);
	memset(Filepath, 0, storage.length()+1+TEE_UUID_STRING_SIZE+1+2*FILENAMESIZE+1);
	sth_request= &dci->sth_request;
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
		LOG_E("%s: Error looking for file 0x%.8x\n", __func__,TEEC_ERROR_ITEM_NOT_FOUND);
		return TEEC_ERROR_ITEM_NOT_FOUND;
	}
	res = fread(sth_request->payload,sizeof(char),sth_request->payloadLen,pFile);

	if (ferror(pFile))
	{
		LOG_E("%s: Error reading file res is %zu and errno is %s\n",__func__,res,strerror(errno));
		fclose(pFile);
		return TEE_ERROR_CORRUPT_OBJECT;
	}

    if (res < sth_request->payloadLen)
    {
       //File is shorter than expected
       if (feof(pFile)) {
           LOG_I("%s: EOF reached: res is %zu, payloadLen is %d\n",__func__,res, sth_request->payloadLen);
       }
    }

    fclose(pFile);

	return TEEC_SUCCESS;
}


mcResult_t FSD::FSD_WriteFile(void){
	FILE * pFile=NULL;
	int fd=0;
	STH_FSD_message_t* sth_request=NULL;
	size_t res=0;
	int stat=0;
	string storage = getTbStoragePath();
	char TAdirpath[storage.length()+1+TEE_UUID_STRING_SIZE+1];
	char Filepath[storage.length()+1+TEE_UUID_STRING_SIZE+1+2*FILENAMESIZE+1];
	char Filepath_new[storage.length()+TEE_UUID_STRING_SIZE+2*FILENAMESIZE+strlen(NEW_EXT)+1];

	memset(TAdirpath, 0, storage.length()+1+TEE_UUID_STRING_SIZE+1);
	memset(Filepath, 0, storage.length()+1+TEE_UUID_STRING_SIZE+1+2*FILENAMESIZE+1);
	memset(Filepath_new, 0, storage.length()+TEE_UUID_STRING_SIZE+2*FILENAMESIZE+strlen(NEW_EXT)+1);
	sth_request= &dci->sth_request;
	FSD_CreateTaDirPath(
					storage,
					sth_request,
					TAdirpath,
					sizeof(TAdirpath));

    stat = mkdir(TAdirpath, 0700);
	if((stat==-1) && (errno!=EEXIST))
	{
		LOG_E("%s: error when creating TA dir: %s (%s)\n",__func__,TAdirpath,strerror(errno));
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
	LOG_I("%s: filename.new   %s\n", __func__, Filepath_new);
	if(sth_request->flags == TEE_DATA_FLAG_EXCLUSIVE)
	{
		LOG_I("%s: opening file in exclusive mode\n",__func__);
		fd = open(Filepath, O_WRONLY | O_CREAT | O_EXCL, S_IWUSR);
		if (fd == -1)
		{
			LOG_E("%s: error creating file: %s \n",__func__,strerror(errno));
			return TEE_ERROR_CORRUPT_OBJECT;
		}
		else
		{
			close(fd);
		}
	}
	pFile = fopen(Filepath_new, "w");
	LOG_I("%s: opening file for writing\n",__func__);
	if(pFile==NULL)
	{
		if(remove(Filepath)==-1)
		{
			LOG_E("%s: remove failed: %s\n",__func__, strerror(errno));
		}
		return TEE_ERROR_STORAGE_NO_SPACE;
	}
	res = fwrite(sth_request->payload,sizeof(char),sth_request->payloadLen,pFile);

	if (ferror(pFile))
	{
		LOG_E("%s: Error writing file res is %zu and errno is %s\n",__func__,res,strerror(errno));
		fclose(pFile);
		if(remove(Filepath)==-1)
		{
			LOG_E("%s: remove failed: %s\n",__func__, strerror(errno));
		}
		if(remove(Filepath_new)==-1)
		{
			LOG_E("%s: remove failed: %s\n",__func__, strerror(errno));
		}
		return TEE_ERROR_STORAGE_NO_SPACE;
	}
	else
	{
		res = fclose(pFile);
		if ((int32_t) res < 0)
		{
			LOG_E("%s: Error closing file res is %zu and errno is %s\n",__func__,res,strerror(errno));
			if(remove(Filepath)==-1)
            {
                LOG_E("%s: remove failed: %s\n",__func__, strerror(errno));
            }
			if(remove(Filepath_new)==-1)
            {
                LOG_E("%s: remove failed: %s\n",__func__, strerror(errno));
            }
			return TEE_ERROR_STORAGE_NO_SPACE;
		}

		res = rename(Filepath_new,Filepath);
		if ((int32_t) res < 0)
		{
			LOG_E("%s: Error renaming: %s\n",__func__,strerror(errno));
			if(remove(Filepath)==-1)
            {
                LOG_E("%s: remove failed: %s\n",__func__, strerror(errno));
            }
			if(remove(Filepath_new)==-1)
            {
                LOG_E("%s: remove failed: %s\n",__func__, strerror(errno));
            }
			return TEE_ERROR_STORAGE_NO_SPACE;
		}
	}
	return TEEC_SUCCESS;
}


mcResult_t FSD::FSD_DeleteFile(void){
	FILE * pFile=NULL;
	mcResult_t ret;
	size_t res;
	STH_FSD_message_t* sth_request=NULL;
	string storage = getTbStoragePath();
	char TAdirpath[storage.length()+1+TEE_UUID_STRING_SIZE+1];
	char Filepath[storage.length()+1+TEE_UUID_STRING_SIZE+1+2*FILENAMESIZE+1];

	memset(TAdirpath, 0, storage.length()+1+TEE_UUID_STRING_SIZE+1);
	memset(Filepath, 0, storage.length()+1+TEE_UUID_STRING_SIZE+1+2*FILENAMESIZE+1);
	sth_request= &dci->sth_request;
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
		LOG_I("%s: file not found: %s (%s)\n",__func__, Filepath, strerror(errno));
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
		LOG_E("%s: rmdir failed: %s (%s)\n",__func__, TAdirpath, strerror(errno));
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
    STH_FSD_message_t *sth_request = &dci->sth_request;

    switch (mcRegistryCleanupTA((mcUuid_t *) &sth_request->uuid)) {
    case MC_DRV_OK:
        return TEEC_SUCCESS;
    default:
        return TEE_ERROR_CORRUPT_OBJECT;
    }
}

//------------------------------------------------------------------------------
