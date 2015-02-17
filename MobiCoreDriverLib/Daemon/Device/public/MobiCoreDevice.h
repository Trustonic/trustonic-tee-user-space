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
 * MobiCore device.
 * The MobiCore device class handles the MCP processing within the driver.
 * Concrete devices implementing the communication behavior for the platforms have to be derived
 * from this.
 */
#ifndef MOBICOREDEVICE_H_
#define MOBICOREDEVICE_H_

#include <stdint.h>
#include <vector>

#include "McTypes.h"
#include "MobiCoreDriverApi.h"

#include "Mci/mcimcp.h"
#include "mcLoadFormat.h"
#include "MobiCoreDriverCmd.h"

#include "Connection.h"
#include "CWsm.h"

#include "DeviceScheduler.h"
#include "DeviceIrqHandler.h"
#include "TAExitHandler.h"
#include "NotificationQueue.h"
#include "TrustletSession.h"
#include "mcVersionInfo.h"


class MobiCoreDevice;

typedef struct {
    uint64_t baseAddr;       /**< Physical address of the data to load. */
    uint32_t offs;           /**< Offset to the data. */
    uint32_t len;            /**< Length of the data to load. */
    mclfHeader_ptr tlHeader; /**< Pointer to trustlet header. */
    bool is_gpta;            /**< is the service GP or legacy? */
} loadDataOpenSession_t, *loadDataOpenSession_ptr;

typedef struct {
    uint64_t addr;      /**< Physical address of the data to load. */
    uint64_t offs;      /**< Offset to the data. */
    uint64_t len;       /**< Length of the data to load. */
} loadTokenData_t, *loadTokenData_ptr;

/**
 * Factory method to return the platform specific MobiCore device.
 * Implemented in the platform specific *Device.cpp
 */
extern MobiCoreDevice *getDeviceInstance(void);

class MobiCoreDevice : public DeviceScheduler, public DeviceIrqHandler, public TAExitHandler
{

protected:

    NotificationQueue   *nq;    /**< Pointer to the notification queue within the MCI buffer */
    mcFlags_t           *mcFlags; /**< Pointer to the MC flags within the MCI buffer */
    mcpMessage_t        *mcpMessage; /**< Pointer to the MCP message structure within the MCI buffer */
    CSemaphore          mcpSessionNotification; /**< Semaphore to synchronize incoming notifications for the MCP session */

    trustletSessionList_t trustletSessions; /**< Available Trustlet Sessions */
    mcVersionInfo_t     *mcVersionInfo; /**< MobiCore version info. */
    bool                mcFault; /**< Signal RTM fault */
    bool                mciReused; /**< Signal restart of Daemon. */
    CMutex              mutex_connection; // Mutex to share session->notificationConnection for GP cases


    /* In a special case a Trustlet can create a race condition in the daemon.
     * If at Trustlet start it detects an error of some sort and calls the
     * exit function before waiting for any notifications from NWD then the daemon
     * will receive the openSession notification from RTM and the error notification
     * from the Trustlet at the same time but because the internal objects in
     * the daemon are not yet completely setup then the error notification will
     * never be sent to the TLC!
     *
     * This queue holds notifications received between the time the daemon
     * puts the MCP command for open session until the internal session objects
     * are setup correctly.
     */
    std::queue<notification_t> notifications; /**<  Notifications queue for open session notification */

    MobiCoreDevice();

    mcResult_t closeSessionInternal(
        TrustletSession* session);

    mcResult_t sendSessionCloseCmd(
        uint32_t sessionId);

    TrustletSession* findSession(
        Connection *deviceConnection,
        uint32_t sessionId);

    TrustletSession *getTrustletSession(
        uint32_t sessionId);

    mcResult_t mshNotifyAndWait(void);

    void signalMcpNotification(void);

    bool waitMcpNotification(void);

private:
    virtual bool yield(void) = 0;

    virtual bool nsiq(void) = 0;

    virtual bool waitSsiq(void) = 0;

public:
    CMutex mutex_mcp;    // This mutex should be taken before any access to below functions
    CMutex mutex_tslist; // Mutex to share Trustlet  session list  ==> WARNING, do not CI, temporary fix.

    virtual ~MobiCoreDevice();

    //Connection *getSessionConnection(uint32_t sessionId, notification_t *notification);

    bool open(Connection *connection);

    void close(Connection *connection);

    mcResult_t openSession(Connection *deviceConnection,
                           loadDataOpenSession_ptr         pLoadDataOpenSession,
                           uint32_t                        tciHandle,
                           uint32_t                        tciLen,
                           uint32_t                        tciOffset,
                           mcDrvRspOpenSessionPayload_ptr  pRspOpenSessionPayload);

    mcResult_t checkLoad(loadDataOpenSession_ptr           pLoadDataOpenSession,
                         mcDrvRspOpenSessionPayload_ptr   pRspOpenSessionPayload);


    TrustletSession *registerTrustletConnection(Connection *connection,
            MC_DRV_CMD_NQ_CONNECT_struct  *cmdNqConnect);


    void freeSession(TrustletSession *session);

    mcResult_t closeSession(Connection *deviceConnection, uint32_t sessionId);

    virtual mcResult_t notify(Connection *deviceConnection, uint32_t  sessionId);

    virtual void notify(uint32_t  sessionId) = 0;

    mcResult_t mapBulk(Connection *deviceConnection, uint32_t sessionId, uint32_t handle, uint64_t pAddrL2,
                        uint32_t offsetPayload, uint32_t lenBulkMem, uint32_t *secureVirtualAdr);

    mcResult_t unmapBulk(Connection *deviceConnection, uint32_t sessionId, uint32_t handle,
                         uint32_t secureVirtualAdr, uint32_t lenBulkMem);

    void start();

    mcResult_t getMobiCoreVersion(mcDrvRspGetMobiCoreVersionPayload_ptr pRspGetMobiCoreVersionPayload);

    bool getMcFault() {
        return mcFault;
    }

    void queueUnknownNotification(notification_t notification);

    virtual void dumpMobicoreStatus(void) = 0;

    virtual uint32_t getMobicoreStatus(void) = 0;

    virtual bool schedulerAvailable(void) = 0;

    virtual void schedule(void) = 0;

    virtual void handleIrq(void) = 0;

    //virtual bool freeWsm(CWsm_ptr pWsm) = 0;

    /**
     * Initialize MobiCore.
     *
     * @param devFile the device node to speak to.
     * @param loadMobiCore
     * @param mobicoreImage
     * @param enableScheduler
     *
     * @returns true if MobiCore is already initialized.
     * */
    virtual bool initDevice(
        const char  *devFile,
        bool        enableScheduler
    ) = 0;

    virtual void initDeviceStep2(void) = 0;

    virtual bool getMciInstance(uint32_t len, CWsm_ptr *mci, bool *reused) = 0;

    virtual CWsm_ptr registerWsmL2(addr_t buffer, uint32_t len, uint32_t  pid) = 0;

    virtual bool unregisterWsmL2(CWsm_ptr pWsm) = 0;

    virtual bool lockWsmL2(uint32_t handle) = 0;

    virtual bool unlockWsmL2(uint32_t handle) = 0;

    virtual uint64_t findWsmL2(uint32_t handle, int fd) = 0;

    virtual bool findContiguousWsm(uint32_t handle, int fd, uint64_t *phys, uint32_t *len) = 0;

    virtual bool setupLog(void) = 0;

    /**
     * Allocates persistent WSM memory for TL (won't be released when TLC exits).
     */
    virtual CWsm_ptr allocateContiguousPersistentWsm(uint32_t len) = 0;

    mcResult_t loadToken(Connection        *deviceConnection,
                         loadTokenData_ptr pLoadTokenData);

};

#endif /* MOBICOREDEVICE_H_ */

