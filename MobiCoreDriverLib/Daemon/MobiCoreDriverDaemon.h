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
 * <t-base driver class.
 * The <t-base driver class implements the ConnectionHandler interface.
 */
#ifndef MOBICOREDRIVER_H_
#define MOBICOREDRIVER_H_

#include "Server/public/ConnectionHandler.h"
#include "Server/public/Server.h"

#include "MobiCoreDevice.h"
#include <string>
#include <list>


#define MAX_SERVERS 2

class MobicoreDriverResources
{
public:
    Connection *conn;
    CWsm *pTciWsm;
    uint32_t sessionId;

    MobicoreDriverResources(
        Connection *conn,
        CWsm *pTciWsm,
        uint32_t sessionId
    ) {
        this->conn = conn;
        this->pTciWsm = pTciWsm;
        this->sessionId = sessionId;
    };
};

typedef std::list<MobicoreDriverResources *> driverResourcesList_t;

class MobiCoreDriverDaemon : ConnectionHandler
{

public:

    /**
     * Create daemon object
     *
     * @param enableScheduler Enable NQ IRQ scheduler
     * @param loadDriver Load driver at daemon startup
     * @param driverPath Startup driver path
     */
    MobiCoreDriverDaemon(
        bool enableScheduler,

        /**< <t-base driver loading at start-up */
        bool loadDriver,
        std::vector<std::string> drivers
    );

    virtual ~MobiCoreDriverDaemon();
    void dropConnection(Connection *connection);
    virtual bool readCommand(Connection *connection, uint32_t *command_id);
    virtual void handleCommand(Connection *connection, uint32_t command_id);
    virtual void run();
private:
    MobiCoreDevice *mobiCoreDevice;
    /**< Flag to start/stop the scheduler */
    bool enableScheduler;
    /**< Flag to load drivers at startup */
    bool loadDriver;
    std::vector<std::string> drivers;
    /**< List of resources for the loaded drivers */
    driverResourcesList_t driverResources;
    /**< List of servers processing connections */
    Server *servers[MAX_SERVERS];

    bool checkPermission(Connection *connection);

    size_t writeResult(
        Connection  *connection,
        mcResult_t  code
    );

    /**
        * Resolve a device ID to a MobiCore device.
        *
        * @param deviceId Device identifier of the device.
        * @return Reference to the device or NULL if device could not be found.
        */
    MobiCoreDevice *getDevice(
        uint32_t deviceId
    );

    /**
     * Load Device driver
     *
     * @param driverPath Path to the driver file
     * @return True for success/false for failure
     */
    bool loadDeviceDriver(std::string driverPath);

    /**
     * Open Device command
     *
     * @param connection Connection object
     */
    void processOpenDevice(Connection *connection);

    /**
     * Open Session command
     *
     * @param connection Connection object
     */
    void processOpenSession(Connection *connection, bool isGpUuid);

    /**
     * Check Load TA command
     *
     * @param spid claimed
     * @param blob TA blob pointer
     * @param size TA blob pointer size
     * @return true in case of success, false in case of failure
     */

    mcResult_t processLoadCheck(mcSpid_t spid, void *blob, uint32_t size);

    /**
     * Open Trustlet command
     *
     * @param connection Connection object
     */
    void processOpenTrustlet(Connection *connection);

    /**
     * NQ Connect command
     *
     * @param connection Connection object
     */
    void processNqConnect(Connection *connection);

    /**
     * Close Device command
     *
     * @param connection Connection object
     */
    void processCloseDevice(Connection *connection);

    /**
     * Notify command
     *
     * @param connection Connection object
     */
    void processNotify(Connection *connection);

    /**
     * Close Session command
     *
     * @param connection Connection object
     */
    void processCloseSession(Connection *connection);

    /**
     * Map Bulk buf command
     *
     * @param connection Connection object
     */
    void processMapBulkBuf(Connection *connection);

    /**
     * Unmap bulk buf command
     *
     * @param connection Connection object
     */
    void processUnmapBulkBuf(Connection *connection);

    /**
     * Get Version command
     *
     * @param connection Connection object
     */
    void processGetVersion(Connection *connection);

    /**
     * Get MobiCore version command
     *
     * @param connection Connection object
     */
    void processGetMobiCoreVersion(Connection *connection);

    /**
     * Generic Registry read command
     *
     * @param commandId Actual command id
     * @param connection Connection object
     */
    void processRegistryReadData(uint32_t commandId, Connection *connection);

    /**
     * Generic Registry write command
     *
     * @param commandId Actual command id
     * @param connection Connection object
     */
    void processRegistryWriteData(uint32_t commandId, Connection *connection);

    /**
     * Generic Registry Delete command
     *
     * @param commandId Actual command id
     * @param connection Connection object
     */
    void processRegistryDeleteData(uint32_t commandId, Connection *connection);

    /**
     * Load Token
     * This function loads a token (if found) from the registry and uses it as
     * the basis for the device attestation functionality
     *
     * @param token the token to base the attestation on (raw format)
     * @param sosize the size of the token
     */
    bool loadToken(uint8_t *token, uint32_t sosize);

    /**
     * installEndorsementToken
     * Look for tokens in the registry and pass them to <t-base for endorsement purposes
     * Search order:  1. authtoken 2. authtoken backup 3. root container
     */
    void installEndorsementToken(void);
};

#endif /* MOBICOREDRIVER_H_ */

