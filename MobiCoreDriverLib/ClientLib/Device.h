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
 * Client library device management.
 *
 * Device and Trustlet Session management Functions.
 */
#ifndef DEVICE_H_
#define DEVICE_H_

#include <stdint.h>
#include <vector>

#include "public/MobiCoreDriverApi.h"
#include "Session.h"
#include "CWsm.h"


class Device
{

private:
    sessionList_t   sessionList; /**< MobiCore Trustlet session associated with the device */
    wsmList_t       wsmL2List; /**< WSM L2 Table  */


public:
    uint32_t     deviceId; /**< Device identifier */
    Connection   *connection; /**< The device connection */
    CMcKMod_ptr  pMcKMod;
    uint32_t     openCount;

    Device(
        uint32_t    deviceId,
        Connection  *connection
    );

    virtual ~Device(
        void
    );

    /**
     * Open the device.
     * @param deviceName Name of the kernel modules device file.
     * @return true if the device has been opened successfully
     */
    bool open(
        const char *deviceName
    );

    /**
     * Closes the device.
     */
    void close(
        void
    );

    /**
     * Check if the device has open sessions.
     * @return true if the device has one or more open sessions.
     */
    bool hasSessions(
        void
    );

    /**
     * Add a session to the device.
     * @param sessionId session ID
     * @param connection session connection
     * @return Session object created
     */
    Session *createNewSession(
        uint32_t    sessionId,
        Connection  *connection
    );

    /**
     * Remove the specified session from the device.
     * The session object will be destroyed and all resources associated with it will be freed.
     *
     * @param sessionId Session of the session to remove.
     * @return true if a session has been found and removed.
     */
    bool removeSession(
        uint32_t sessionId
    );

    /**
     * Get as session object for a given session ID.
     * @param sessionId Identified of a previously opened session.
     * @return Session object if available or NULL if no session has been found.
     */
    Session *resolveSessionId(
        uint32_t sessionId
    );

    /**
     * Allocate a block of contiguous WSM.
     * @param len The virtual address to be registered.
     * @param wsm The CWsm object of the allocated memory.
     * @return MC_DRV_OK if successful.
     */
    mcResult_t allocateContiguousWsm(
        uint32_t len,
        CWsm **wsm
    );

    /**
     * Unregister a vaddr from a device.
     * @param vaddr The virtual address to be registered.
     * @param paddr The physical address to be registered.
     */
    mcResult_t freeContiguousWsm(
        CWsm_ptr  pWsm
    );

    /**
     * Get a WSM object for a given virtual address.
     * @param vaddr The virtual address which has been allocate with mcMallocWsm() in advance.
     * @return the WSM object or NULL if no address has been found.
     */
    CWsm_ptr findContiguousWsm(
        addr_t  virtAddr
    );

    /**
     * Map a buffer from tlc VA to TL(Create L2 table for the buffer
     * @param buf The virtual address of hte buffer
     * @param len The length of the buffer
     * @param blkBuf The buffer object created
     * @return MC_DRV_OK if successful.
     */
    mcResult_t mapBulkBuf(
        addr_t buf,
        uint32_t len,
        BulkBufferDescriptor **blkBuf
    );

};

#endif /* DEVICE_H_ */

