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
/**
 * FSD server.
 *
 * Handles incoming storage requests from TA through STH
 */
#ifndef FSD_H_
#define FSD_H_

#include <sys/types.h>
#include <string>
#include <cstdio>
#include "CThread.h"
#include "MobiCoreDriverApi.h"
#include "drSecureStorage_Api.h"
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <android/log.h>


#define max( a, b ) ( ((a) > (b)) ? (a) : (b) )
#define DCI_BUFF_SIZE	1000*1024

#define TEE_UUID_STRING_SIZE  	32
#define FILENAMESIZE			20
#define NEW_EXT					".new"

#define TAG_LOG	"FSD"

class FSD: public CThread
{

public:
    /**
     * FSD contructor.
     *
     * @param tbstoragepath Absolute path to the secure storage
     */
    FSD(
        void
    );

    /**
     * FSD destructor.
     * Close the current session and resources will be freed.
     */
    virtual ~FSD(
        void
    );

    /**
     * Start server and listen for incoming request from STH.
     */
    virtual void run(void);

    /*
    *   FSD_Open
    *
    *   Open a session with the STH
    *
    */
    virtual mcResult_t FSD_Open(void);


    /*
    *   FSD_Close
    *
    *   Close a session opened with the STH
    *
    */
    virtual mcResult_t FSD_Close(void);


    /*
    *   FSD_listenDci
    *
    *   DCI listener function
    *
    */
    virtual void FSD_listenDci(void);



private:
    mcSessionHandle_t   	sessionHandle; /**< current session */
    dciMessage_t*       	dci; /**< dci buffer */


    /** Private methods*/

    /*
    *   FSD_ExecuteCommand
    *
    *   Execute command received from the STH
    *
    */
    mcResult_t FSD_ExecuteCommand(void);

    /****************************  File operations  *******************************/

    /*
    *   FSD_LookFile
    *
    *   look for a file
    */
    mcResult_t FSD_LookFile(void);


    /*
    *   FSD_ReadFile
    *
    *   Read a file
    */
    mcResult_t FSD_ReadFile(void);


    /*
    *   FSD_WriteFile
    *
    *   Write a file
    */
    mcResult_t FSD_WriteFile(void);


    /*
    *   FSD_DeleteFile
    *
    *   Delete a file
    */
    mcResult_t FSD_DeleteFile(void);

    /**
     * FSD_DeleteDir()
     *
     * Deletes a TA directory and all files belonigng to that DIR
     * Note, this is a restricted function only available to a TA with the
     * required capability
     */
    mcResult_t FSD_DeleteDir(void);

};

#endif /* FSD_H_ */

