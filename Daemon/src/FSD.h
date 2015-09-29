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
#ifndef FSD_H_
#define FSD_H_

#include <sys/types.h>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <pthread.h>

#include "MobiCoreDriverApi.h"
// drSecureStorage_Api.h uses TEE_UUID
#include <tee_client_api.h>
#include "drSecureStorage_Api.h"
#include "CThread.h"

#define DCI_BUFF_SIZE           (1000*1024)

#define TEE_UUID_STRING_SIZE  	32
#define FILENAMESIZE			20
#define NEW_EXT					".new"

#define TAG_LOG	"FSD"

//located in the PrivateRegistry.cpp, don't ask why...
extern const std::string &getTbStoragePath(void);

class FSD: public CThread
{
public:
    /**
     * FSD contructor.
     *
     * @param tbstoragepath Absolute path to the secure storage
     */
    FSD(size_t dci_msg_size = DCI_BUFF_SIZE);

    /**
     * FSD destructor.
     * Close the current session and resources will be freed.
     */
    virtual ~FSD(void);

    void start(void)
    {
        CThread::start(FSD::m_server_name);
    }

    /**
     * Start server and listen for incoming request from STH.
     */
    void run(void);

    /*
    *   FSD_Open
    *
    *   Open a session with the STH
    *
    */
    mcResult_t FSD_Open(void);

    /*
    *   FSD_Close
    *
    *   Close a session opened with the STH
    *
    */
    mcResult_t FSD_Close(void);

    /*
    *   FSD_listenDci
    *
    *   DCI listener function
    *
    */
    void FSD_listenDci(void);

private:
    /** Private methods*/

    /*
    *   FSD_ExecuteCommand
    *
    *   Execute command received from the STH
    *
    */
    void FSD_ExecuteCommand(void);

    /****************************  File operations  ***************************/

    /*
    *   FSD_LookFile
    *
    *   look for a file
    */
    uint32_t FSD_LookFile(void);


    /*
    *   FSD_ReadFile
    *
    *   Read a file
    */
    uint32_t FSD_ReadFile(void);


    /*
    *   FSD_WriteFile
    *
    *   Write a file
    */
    uint32_t FSD_WriteFile(void);


    /*
    *   FSD_DeleteFile
    *
    *   Delete a file
    */
    uint32_t FSD_DeleteFile(void);

private:
    /** Private data */
    pthread_mutex_t     m_close_lock;
    mcSessionHandle_t   m_sessionHandle; /**< current session */
    dciMessage_t        *m_dci; /**< dci buffer */
    size_t              m_dci_msg_size;
    static const char * const m_server_name;

    /**
     * FSD_DeleteDir()
     *
     * Deletes a TA directory and all files belonging to that DIR
     * Note, this is a restricted function only available to a TA with the
     * required capability
     */
    mcResult_t FSD_DeleteDir(void);

};

#endif /* FSD_H_ */

