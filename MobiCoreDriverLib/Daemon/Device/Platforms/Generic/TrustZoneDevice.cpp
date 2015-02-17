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
#include <cstdlib>
#include <stdio.h>
#include <inttypes.h>
#include <list>

#include "McTypes.h"
#include "mc_linux.h"
#include "McTypes.h"
#include "Mci/mci.h"
#include "mcVersionHelper.h"

#include "CSemaphore.h"
#include "CMcKMod.h"

#include "MobiCoreDevice.h"
#include "TrustZoneDevice.h"
#include "NotificationQueue.h"

#include "log.h"


#define NQ_NUM_ELEMS      (16)
#define NQ_BUFFER_SIZE    (2 * (sizeof(notificationQueueHeader_t)+  NQ_NUM_ELEMS * sizeof(notification_t)))
#define MCP_BUFFER_SIZE   (sizeof(mcpBuffer_t))
#define MCI_BUFFER_SIZE   (NQ_BUFFER_SIZE + MCP_BUFFER_SIZE)

//------------------------------------------------------------------------------
MC_CHECK_VERSION(MCI, 1, 0);

//------------------------------------------------------------------------------
#define LOG_I_RELEASE(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

//------------------------------------------------------------------------------
__attribute__ ((weak)) MobiCoreDevice *getDeviceInstance(
    void
)
{
    return new TrustZoneDevice();
}

//------------------------------------------------------------------------------
TrustZoneDevice::TrustZoneDevice(
    void
)
{
    schedulerEnabled = false;
    pMcKMod = NULL;
    pWsmMcp = NULL;
    mobicoreInDDR = NULL;
}

//------------------------------------------------------------------------------
TrustZoneDevice::~TrustZoneDevice(
    void
)
{
    delete pMcKMod;
    delete pWsmMcp;
    delete nq;
}


//------------------------------------------------------------------------------
/**
 * Set up MCI and wait till MC is initialized
 * @return true if <t-base is already initialized
 */
bool TrustZoneDevice::initDevice(
    const char  *devFile,
    bool        enableScheduler)
{
    notificationQueue_t *nqStartOut;
    notificationQueue_t *nqStartIn;
    addr_t mciBuffer;

    pMcKMod = new CMcKMod();
    mcResult_t ret = pMcKMod->open(devFile);
    if (ret != MC_DRV_OK)
    {
        LOG_W(" Opening kernel module device failed");
        return false;
    }
    if (!pMcKMod->checkVersion())
    {
        LOG_E("kernel module version mismatch");
        return false;
    }

    this->schedulerEnabled = enableScheduler;

    // Init MC with NQ and MCP buffer addresses

    // Set up MCI buffer
    if (!getMciInstance(MCI_BUFFER_SIZE, &pWsmMcp, &mciReused))
    {
        LOG_E("getMciInstance failed");
        return false;
    }
    mciBuffer = pWsmMcp->virtAddr;

    if (!checkMciVersion())
    {
        LOG_E("checkMciVersion failed");
        return false;
    }

    // Only do a fastcall if MCI has not been reused (MC already initialized)
    if (!mciReused)
    {
        // Wipe memory before first usage
        memset(mciBuffer, 0, MCI_BUFFER_SIZE);

        // Init MC with NQ and MCP buffer addresses
        int ret = pMcKMod->fcInit(NQ_BUFFER_SIZE, NQ_BUFFER_SIZE, MCP_BUFFER_SIZE);
        if (ret != 0)
        {
            LOG_E("pMcKMod->fcInit() failed");
            return false;
        }

        // Here we are safe to setup the <t-base logs
        setupLog();

        // First empty N-SIQ which results in set up of the MCI structure
        if (!nsiq())
        {
            LOG_E("sending N-SIQ failed");
            return false;
        }

        // Wait until <t-base state switches to MC_STATUS_INITIALIZED
        // It is assumed that <t-base always switches state at a certain point in time.
        for(;;)
        {
            uint32_t timeslot;
            uint32_t status = getMobicoreStatus();

            if (MC_STATUS_INITIALIZED == status)
            {
                break;
            }

            if (MC_STATUS_NOT_INITIALIZED == status)
            {
                // Switch to <t-base to give it more CPU time.
                for (timeslot = 0; timeslot < 10; timeslot++)
                {
                    if (!yield())
                    {
                        LOG_E("yielding to SWd failed");
                        return false;
                    }
                }
                continue;
            }

            if (MC_STATUS_HALT == status)
            {
                dumpMobicoreStatus();
                LOG_E("<t-base halted during init !!!, state is 0x%x", status);
                return false;
            }

            // MC_STATUS_BAD_INIT or anything else
            LOG_E("MCI buffer init failed, state is 0x%x", status);
            return false;

        } // for(;;)
    }

    nqStartOut = (notificationQueue_t *) mciBuffer;
    nqStartIn = (notificationQueue_t *) ((uint8_t *) nqStartOut
                                         + sizeof(notificationQueueHeader_t) + NQ_NUM_ELEMS
                                         * sizeof(notification_t));

    // Set up the NWd NQ
    nq = new NotificationQueue(nqStartIn, nqStartOut, NQ_NUM_ELEMS);

    mcpBuffer_t *mcpBuf = (mcpBuffer_t *) ((uint8_t *) mciBuffer + NQ_BUFFER_SIZE);

    // Set up the MC flags
    mcFlags = &(mcpBuf->mcFlags);

    // Set up the MCP message
    mcpMessage = &(mcpBuf->mcpMessage);

    // convert virtual address of mapping to physical address for the init.
    LOG_I("MCI established, at %p, phys=0x%jx, reused=%s",
          pWsmMcp->virtAddr,
          pWsmMcp->physAddr,
          mciReused ? "true" : "false");
    return true;
}


//------------------------------------------------------------------------------
void TrustZoneDevice::initDeviceStep2(
    void
)
{
    // not needed
}


//------------------------------------------------------------------------------
bool TrustZoneDevice::yield(
    void
)
{
    int32_t ret = pMcKMod->fcYield();
    if (ret != 0) {
        LOG_E("pMcKMod->fcYield() failed: %d", ret);
    }
    return ret == 0;
}


//------------------------------------------------------------------------------
bool TrustZoneDevice::nsiq(
    void
)
{
    // There is no need to set the NON-IDLE flag here. Sending an N-SIQ will
    // make the <t-base run until it could set itself to a state where it
    // set the flag itself. IRQs and FIQs are disbaled for this period, so
    // there is no way the NWd can interrupt here.

    // not needed: mcFlags->schedule = MC_FLAG_SCHEDULE_NON_IDLE;

    int32_t ret = pMcKMod->fcNSIQ();
    if (ret != 0) {
        LOG_E("pMcKMod->fcNSIQ() failed : %d", ret);
        return false;
    }
    // now we have to wake the scheduler, so <t-base gets CPU time.
    DeviceScheduler::wakeup();
    return true;
}


//------------------------------------------------------------------------------
void TrustZoneDevice::notify(
    uint32_t sessionId
)
{
    // Check if it is MCP session - handle openSession() command
    if (sessionId != SID_MCP) {
//        // Check if session ID exists to avoid flooding of nq by clients
//        TrustletSession *ts = getTrustletSession(sessionId);
//        if (ts == NULL) {
//            LOG_E("no session with id %03x", sessionId);
//            return;
//        }
        LOG_I(" Sending notification for session %03x to <t-base", sessionId);
    } else {
        LOG_I(" Sending MCP notification to <t-base");
    }

    // Notify <t-base about new data
    notification_t notification = { sessionId : sessionId, payload : 0 };

    nq->putNotification(&notification);
    //IMPROVEMENT-2012-03-07-maneaval What happens when/if nsiq fails?
    //In the old days an exception would be thrown but it was uncertain
    //where it was handled, some server(sock or Netlink). In that case
    //the server would just die but never actually signaled to the client
    //any error condition
    (void)nsiq();
}

//------------------------------------------------------------------------------
uint32_t TrustZoneDevice::getMobicoreStatus(void)
{
    uint32_t status = MC_STATUS_NOT_INITIALIZED;
    //IMPROVEMENT-2012-03-07-maneaval Can fcInfo ever fail? Before it threw an
    //exception but the handler depended on the context.
    pMcKMod->fcInfo(0, &status, NULL);

    return status;
}

//------------------------------------------------------------------------------
bool TrustZoneDevice::checkMciVersion(void)
{
    uint32_t version = 0;
    int ret;
    char *errmsg;

    ret = pMcKMod->fcInfo(MC_EXT_INFO_ID_MCI_VERSION, NULL, &version);
    if (ret != 0) {
        LOG_E("pMcKMod->fcInfo() failed with %d", ret);
        return false;
    }

    // Run-time check.
    if (!checkVersionOkMCI(version, &errmsg)) {
        LOG_E("%s", errmsg);
        return false;
    }
    LOG_I("%s", errmsg);
    return true;
}

//------------------------------------------------------------------------------
void TrustZoneDevice::dumpMobicoreStatus(
    void
) {
    uint32_t status = MC_STATUS_NOT_INITIALIZED;
    uint32_t info = 0;
    mcUuid_t uuid;

    memset(&uuid, 0, sizeof(uuid));

    // read additional info about exception-point and print
    LOG_E("<t-base halted. Status dump:");
    pMcKMod->fcInfo(1, &status, &info);
    LOG_I_RELEASE("  flags               = 0x%08x", info);
    pMcKMod->fcInfo(2, &status, &info);
    LOG_I_RELEASE("  haltCode            = 0x%08x", info);
    pMcKMod->fcInfo(3, &status, &info);
    LOG_I_RELEASE("  haltIp              = 0x%08x", info);
    pMcKMod->fcInfo(4, &status, &info);
    LOG_I_RELEASE("  faultRec.cnt        = 0x%08x", info);
    pMcKMod->fcInfo(5, &status, &info);
    LOG_I_RELEASE("  faultRec.cause      = 0x%08x", info);
    pMcKMod->fcInfo(6, &status, &info);
    LOG_I_RELEASE("  faultRec.meta       = 0x%08x", info);
    pMcKMod->fcInfo(7, &status, &info);
    LOG_I_RELEASE("  faultRec.thread     = 0x%08x", info);
    pMcKMod->fcInfo(8, &status, &info);
    LOG_I_RELEASE("  faultRec.ip         = 0x%08x", info);
    pMcKMod->fcInfo(9, &status, &info);
    LOG_I_RELEASE("  faultRec.sp         = 0x%08x", info);
    pMcKMod->fcInfo(10, &status, &info);
    LOG_I_RELEASE("  faultRec.arch.dfsr  = 0x%08x", info);
    pMcKMod->fcInfo(11, &status, &info);
    LOG_I_RELEASE("  faultRec.arch.adfsr = 0x%08x", info);
    pMcKMod->fcInfo(12, &status, &info);
    LOG_I_RELEASE("  faultRec.arch.dfar  = 0x%08x", info);
    pMcKMod->fcInfo(13, &status, &info);
    LOG_I_RELEASE("  faultRec.arch.ifsr  = 0x%08x", info);
    pMcKMod->fcInfo(14, &status, &info);
    LOG_I_RELEASE("  faultRec.arch.aifsr = 0x%08x", info);
    pMcKMod->fcInfo(15, &status, &info);
    LOG_I_RELEASE("  faultRec.arch.ifar  = 0x%08x", info);
    pMcKMod->fcInfo(16, &status, &info);
    LOG_I_RELEASE("  mcData.flags        = 0x%08x", info);
    pMcKMod->fcInfo(19, &status, &info);
    LOG_I_RELEASE("  mcExcep.partner     = 0x%08x", info);
    pMcKMod->fcInfo(20, &status, &info);
    LOG_I_RELEASE("  mcExcep.peer        = 0x%08x", info);
    pMcKMod->fcInfo(21, &status, &info);
    LOG_I_RELEASE("  mcExcep.cause       = 0x%08x", info);

    // Note: we define UUID is a uint8_t[]. On litte endian systems (ARM, Intel)
    // the bytes {0xdd 0xcc 0xbb 0xaa} become {{0xaabbccdd}} as integer. Since
    // the code creating the integers does the same, we are effectively
    // unscrambling the UUID again here. We must never print the integer values
    // here, as this give a wrong UUID

    pMcKMod->fcInfo(23, &status, ((uint32_t*)&uuid.value[0]));
    pMcKMod->fcInfo(24, &status, ((uint32_t*)&uuid.value[4]));
    pMcKMod->fcInfo(25, &status, ((uint32_t*)&uuid.value[8]));
    pMcKMod->fcInfo(26, &status, ((uint32_t*)&uuid.value[12]));
    LOG_I_RELEASE("  mcExcep.uuid        = 0x%02x%02x%02x%02x%02x%02x%02x%02x"
                  "%02x%02x%02x%02x%02x%02x%02x%02x",
                  uuid.value[0],uuid.value[1],uuid.value[2],uuid.value[3],
                  uuid.value[4],uuid.value[5],uuid.value[6],uuid.value[7],
                  uuid.value[8],uuid.value[9],uuid.value[10],uuid.value[11],
                  uuid.value[12],uuid.value[13],uuid.value[14],uuid.value[15]);

    pMcKMod->fcInfo(22, &status, &info);
    LOG_I_RELEASE("  mcExcep.meta        = 0x%08x", info);
    LOG_I_RELEASE("Daemon exiting.");
    ::exit(2);
}

//------------------------------------------------------------------------------
bool TrustZoneDevice::waitSsiq(void)
{
    uint32_t cnt;
    if (!pMcKMod->waitSSIQ(&cnt)) {
        LOG_E("pMcKMod->SSIQ() failed");
        return false;
    }
    LOG_I(" Received SSIQ interrupt from <t-base, counter=%u", cnt);
    return true;
}


//------------------------------------------------------------------------------
bool TrustZoneDevice::getMciInstance(uint32_t len, CWsm_ptr *mci, bool *reused)
{
    addr_t virtAddr = NULL;
    bool isReused = true;
    if (len == 0) {
        LOG_E("allocateWsm() length is 0");
        return false;
    }

    mcResult_t ret = pMcKMod->mapMCI(len, &virtAddr, &isReused);
    if (ret != MC_DRV_OK) {
        LOG_E("pMcKMod->mmap() failed: %x", ret);
        return false;
    }

    *mci = new CWsm(virtAddr, len, 0, 0);
    *reused = isReused;
    return true;
}


//------------------------------------------------------------------------------
//bool TrustZoneDevice::freeWsm(CWsm_ptr pWsm)
//{
//  int ret = pMcKMod->free(pWsm->handle, pWsm->virtAddr, pWsm->len);
//  if (ret != 0) {
//      LOG_E("pMcKMod->free() failed: %d", ret);
//      return false;
//  }
//  delete pWsm;
//  return true;
//}


//------------------------------------------------------------------------------
CWsm_ptr TrustZoneDevice::registerWsmL2(addr_t buffer, uint32_t len, uint32_t pid)
{
    uint64_t physAddr = 0;
    uint32_t handle = 0;

    int ret = pMcKMod->registerWsmL2(
                  buffer,
                  len,
                  pid,
                  &handle,
                  &physAddr);
    if (ret != 0) {
        LOG_E("ipMcKMod->registerWsmL2() failed: %d", ret);
        return NULL;
    }

    return new CWsm(buffer, len, handle, physAddr);
}


//------------------------------------------------------------------------------
CWsm_ptr TrustZoneDevice::allocateContiguousPersistentWsm(uint32_t len)
{
    CWsm_ptr pWsm = NULL;
    // Allocate shared memory
    addr_t virtAddr = NULL;
    uint32_t handle = 0;

    if (len == 0 )
        return NULL;

    if (pMcKMod->mapWsm(len, &handle, &virtAddr))
        return NULL;

    // Register (vaddr,paddr) with device
    pWsm = new CWsm(virtAddr, len, handle, 0);

    // Return pointer to the allocated memory
    return pWsm;
}


//------------------------------------------------------------------------------
bool TrustZoneDevice::unregisterWsmL2(CWsm_ptr pWsm)
{
    int ret = pMcKMod->unregisterWsmL2(pWsm->handle);
    /* Free pWsm in all the cases */
    delete pWsm;

    if (ret != 0) {
        LOG_E("pMcKMod->unregisterWsmL2 failed: %d", ret);
        return false;
    }
    return true;
}

//------------------------------------------------------------------------------
bool TrustZoneDevice::lockWsmL2(uint32_t handle)
{
    int ret = pMcKMod->lockWsmL2(handle);
    if (ret != 0) {
        LOG_E("pMcKMod->unregisterWsmL2 failed: %d", ret);
        return false;
    }
    return true;
}

//------------------------------------------------------------------------------
bool TrustZoneDevice::unlockWsmL2(uint32_t handle)
{
    LOG_I(" Unlocking buffer with handle %u", handle);
    int ret = pMcKMod->unlockWsmL2(handle);
    if (ret != 0) {
        // Failure here is not important
        LOG_I(" pMcKMod->unregisterWsmL2 failed: %d", ret);
        return false;
    }
    return true;
}

//------------------------------------------------------------------------------
uint64_t TrustZoneDevice::findWsmL2(uint32_t handle, int fd)
{
	uint64_t ret = pMcKMod->findWsmL2(handle, fd);
    if (!ret) {
        LOG_E("pMcKMod->findWsmL2 failed");
        return 0;
    }
    LOG_I("Resolved buffer with handle %u to 0x%jx", handle, ret);
    return ret;
}

//------------------------------------------------------------------------------
bool TrustZoneDevice::findContiguousWsm(uint32_t handle, int fd, uint64_t *phys, uint32_t *len)
{
    if (pMcKMod->findContiguousWsm(handle, fd, phys, len)) {
        LOG_V(" pMcKMod->findContiguousWsm failed");
        return false;
    }
    LOG_I("Resolved buffer with handle %u to 0x%jx", handle, *phys);
    return true;
}
//------------------------------------------------------------------------------
bool TrustZoneDevice::setupLog(void)
{
    if (pMcKMod->setupLog()) {
        LOG_W("pMcKMod->setupLog failed");
        return false;
    }
    return true;
}

//------------------------------------------------------------------------------
bool TrustZoneDevice::schedulerAvailable(void)
{
    return schedulerEnabled;
}

//------------------------------------------------------------------------------
//TODO Schedulerthread to be switched off if MC is idle. Will be woken up when
//     driver is called again.
void TrustZoneDevice::schedule(void)
{
    uint32_t timeslice = SCHEDULING_FREQ;
    uint32_t nextTimeoutInMs;
    struct timeval curTime, wakeupTime;

    /* Clear wakeup time, meaning no timeout is on-going */
    wakeupTime.tv_sec  = (time_t)0;

    // loop forever
    for (;;)
    {
        // Scheduling decision
        if (MC_FLAG_SCHEDULE_IDLE == mcFlags->schedule)
        {
            switch( wakeupTime.tv_sec )
            {
            case 0:
                /* No timeout currently awaiting in SWd */
            // <t-base is IDLE. Prevent unnecessary consumption of CPU cycles
            // and wait for S-SIQ
            DeviceScheduler::sleep();
            if (DeviceScheduler::shouldTerminate())
                    goto terminate_scheduler;
                continue;
            default:
                /* A timeout is currently awaiting in SWd */
                if (    ( gettimeofday(&curTime,NULL) == 0 )      \
                     && ( curTime.tv_sec  >= wakeupTime.tv_sec)   \
                     && ( curTime.tv_usec >= wakeupTime.tv_usec)  )
                {
                    /* Timeout reached */
                    LOG_I("Secure timeout reached !!! Forcing SIQ...");
                    /* If we are here, it means that a timeout is scheduled to occur very
                       soon in secure. Make sure it is checked by t-base by forcing a
                       scheduler call (SIQ) */
                    timeslice = 0;
                    mcFlags->timeout = (uint32_t)-1;
                break;
                }
                else
                {
                    /* Timeout not reached */
            continue;
        }
                /* Will never reach this point... */
                break;
            }
        }

        // <t-base is no longer IDLE, Check timeslice
        if (timeslice--)
        {
            // Slice not used up, simply hand over control to the MC
            if (!yield())
            {
                LOG_E("yielding to SWd failed");
                break;
            }
        }
        else
        {
            // Slice expired, so force MC internal scheduling decision
            timeslice = SCHEDULING_FREQ;
            if (!nsiq())
            {
                LOG_E("sending N-SIQ failed");
                break;
            }
        }

        // Now check if t-base signaled an awaiting timeout while releasing CPU */
        nextTimeoutInMs = mcFlags->timeout;
        if (nextTimeoutInMs != (uint32_t)(-1))
        {
            /* Setup timeout */
            gettimeofday(&wakeupTime,NULL);
            while(nextTimeoutInMs >= MS_PER_S)
            {
                /* Increase timeout seconds */
                wakeupTime.tv_sec++;
                /* Decrement timeout micro-seconds */
                nextTimeoutInMs -= MS_PER_S;
            }
            wakeupTime.tv_usec += nextTimeoutInMs * US_PER_MS;
            if (wakeupTime.tv_usec > US_PER_MS * MS_PER_S)
            {
                wakeupTime.tv_sec++;
                wakeupTime.tv_usec -= US_PER_MS * MS_PER_S;
            }
        }
        else
        {
            /* Clear timeout */
            wakeupTime.tv_sec  = (time_t)0;
        }
    } //for (;;)

terminate_scheduler:
    LOG_E("schedule loop terminated");
}


//------------------------------------------------------------------------------
void TrustZoneDevice::handleTaExit(void)
{
    LOG_I("Starting Trusted Application Exit handler...");
    for (;;) {
        // Wait until we get a notification without CA
        TAExitHandler::sleep();
        if (TAExitHandler::shouldTerminate())
            break;

        // Wait until socket server frees MCP
        // Make sure we don't interfere with handleConnection/dropConnection
        mutex_mcp.lock();

        // Check all sessions
        // Socket server might have closed already and removed the session we were waken up for
        for (;;) {
            mutex_tslist.lock();
            TrustletSession* ts = NULL;
            for (trustletSessionList_t::iterator it = trustletSessions.begin(); it != trustletSessions.end(); it++) {
                if ((*it)->sessionState == TrustletSession::TS_TA_DEAD) {
                    ts = *it;
                    break;
                }
            }
            mutex_tslist.unlock();
            if (!ts) {
                break;
            }
#ifndef NDEBUG
            uint32_t sessionId = ts->sessionId;
#endif
            LOG_I("Cleaning up session %03x", sessionId);
            // Tell t-base to close the session (list gets locked in handleIrq() when MCP replies)
            mcResult_t mcRet = closeSessionInternal(ts);
            // If ok, remove objects
            if (mcRet == MC_DRV_OK) {
                freeSession(ts);
                LOG_I("TA session %03x finally closed", sessionId);
            } else {
                LOG_I("TA session %03x could not be closed yet.", sessionId);
            }
        }
        mutex_mcp.unlock();
    }
    TAExitHandler::setExiting();
    signalMcpNotification();

    LOG_E("schedule loop terminated");
}


//------------------------------------------------------------------------------
void TrustZoneDevice::handleIrq(
    void
) {
    LOG_I("Starting Notification Queue IRQ handler...");

    for (;;)
    {

        LOG_I(" No notifications pending, waiting for S-SIQ");
        if (!waitSsiq())
        {
            LOG_E("Waiting for S-SIQ failed");
            break;
        }

        LOG_V("S-SIQ received");

        // get notifications from queue
        for (;;)
        {
            notification_t *notification = nq->getNotification();
            if (NULL == notification)
            {
                break;
            }

            // process the notification
            // check if the notification belongs to the MCP session
            if (notification->sessionId == SID_MCP)
            {
                LOG_I(" Notification for MCP, payload=%d",
                      notification->payload);

                // Signal main thread of the driver to continue after MCP
                // command has been processed by the MC
                signalMcpNotification();

                continue;
            }

            LOG_I(" Notification for session %03x, payload=%d",
                notification->sessionId, notification->payload);

            // Get the Trustlet session for the session ID
            TrustletSession *ts = NULL;

            mutex_tslist.lock();
            ts = getTrustletSession(notification->sessionId);
            if (ts == NULL) {
                /* Couldn't find the session for this notifications
                 * In practice this only means one thing: there is
                 * a race condition between RTM and the Daemon and
                 * RTM won. But we shouldn't drop the notification
                 * right away we should just queue it in the device
                 */
                LOG_W("Notification for unknown session ID");
                queueUnknownNotification(*notification);
            } else {
                mutex_connection.lock();
                // Get the NQ connection for the session ID
                Connection *connection = ts->notificationConnection;
                if (connection == NULL) {
                    ts->queueNotification(notification);
                    if (ts->deviceConnection == NULL) {
                        LOG_I("  Notification for disconnected client, scheduling cleanup of sessions.");
                        TAExitHandler::wakeup();
                    }
                } else {
                    LOG_I(" Forward notification to McClient.");
                    // Forward session ID and additional payload of
                    // notification to the TLC/Application layer
                    connection->writeData((void *)notification,
                                          sizeof(notification_t));
                }
                mutex_connection.unlock();
            }
            mutex_tslist.unlock();
        } // for (;;) over notifiction queue

        // finished processing notifications. It does not matter if there were
        // any notification or not. S-SIQs can also be triggered by an SWd
        // driver which was waiting for a FIQ. In this case the S-SIQ tells
        // NWd that SWd is no longer idle an will need scheduling again
        DeviceScheduler::wakeup();

    } //for (;;)


    LOG_E("S-SIQ exception");
    // Tell main thread that "something happened"
    // MSH thread MUST not block!
    DeviceIrqHandler::setExiting();
    signalMcpNotification();
}

