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
#ifndef FSD2_H_
#define FSD2_H_

#include <sys/stat.h>
#include <pthread.h>
#include "tee_client_api.h"
#include "CThread.h"
#include "sth2ProxyApi.h"

class FSD2: public CThread
{
public:
    /**
     * FSD2 contructor.
     */
    FSD2();

    /**
     * FSD2 destructor.
     * Close the current session and resources will be freed.
     */
    ~FSD2();

    void start()
    {
        CThread::start(FSD2::m_server_name);
    }

    /**
     * Start server and listen for incoming request from STH.
     */
    void run();

    /*
    *   FSD2_Open
    *
    *   Open a session with the STH
    *
    */
    mcResult_t FSD2_Open();

    /*
    *   FSD2_Close
    *
    *   Close a session opened with the STH
    *
    */
    mcResult_t FSD2_Close();

    /*
    *   FSD2_listenDci
    *
    *   DCI listener function
    *
    */
    void FSD2_listenDci();

private:
    /** Private methods*/

    /*
    *   FSD2_ExecuteCommand
    *
    *   Execute command received from the STH
    *
    */
    void FSD2_ExecuteCommand();

private:
    /** Private data */
    pthread_mutex_t             m_close_lock;
    mcSessionHandle_t       	m_sessionHandle;
    static const char * const	m_server_name;

    /*
    * TCI buffer, includes the workspace
    */
    STH2_delegation_exchange_buffer_t *g_pExchangeBuffer;
    uint8_t *g_pExchangeBufferNotAligned;

    /*
    * Pointer to the workspace inside the exchange buffer
    */
    uint8_t* g_pWorkspaceBuffer;
    /*
    * Provided by the SWd
    */
    uint32_t g_nSectorSize;
    /*
    The absolute path name for each of the 16 possible partitions.
    */
    char* g_pPartitionNames[16];

    /* The file context for each of the 16 possible partitions. An entry
    in this array is NULL if the corresponding partition is currently not opened
    */
    FILE* g_pPartitionFiles[16];

    TEEC_Result errno2serror();
    TEEC_Result partitionDestroy(uint32_t nPartitionID);
    TEEC_Result partitionCreate(uint32_t nPartitionID);
    TEEC_Result partitionOpen(uint32_t nPartitionID, uint32_t* pnPartitionSize);
    TEEC_Result partitionClose(uint32_t nPartitionID);
    TEEC_Result partitionRead(uint32_t nPartitionID, uint32_t nSectorIndex, uint32_t nWorkspaceOffset);
    TEEC_Result partitionWrite(uint32_t nPartitionID, uint32_t nSectorIndex, uint32_t nWorkspaceOffset);
    TEEC_Result partitionSetSize(uint32_t nPartitionID, uint32_t nNewSectorCount);
    TEEC_Result partitionSync(uint32_t nPartitionID);
    void notify(const wchar_t* pMessage, uint32_t nMessageType);
    const char* getCommandtypeString(uint32_t nInstructionID);
};

#endif /* FSD2_H_ */

