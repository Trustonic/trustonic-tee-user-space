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

#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <libgen.h>

#include <log.h>

#include "mcVersion.h"
#include "PrivateRegistry.h"
#include "MobiCoreDriverDaemon.h"
#include "buildTag.h"

#define DRIVER_TCI_LEN 4096

static SecureWorld* g_secure_world;

//------------------------------------------------------------------------------
/**
 * Print daemon command line options
 */
static
int printUsage(char *path)
{
    fprintf(stderr, "<t-base Driver Daemon %u.%u. \"%s\" %s %s\n",
            DAEMON_VERSION_MAJOR, DAEMON_VERSION_MINOR,
            MOBICORE_COMPONENT_BUILD_TAG, __DATE__, __TIME__);
    fprintf(stderr, "usage: %s [-hbr]\n", basename(path));
    fprintf(stderr, "Start <t-base Daemon\n\n");
    fprintf(stderr, "-h\t\tshow this help\n");
    fprintf(stderr, "-b\t\tfork to background\n");
    fprintf(stderr, "-r DRIVER\t<t-base driver to load at start-up\n");
    return 2;
}

//------------------------------------------------------------------------------
/**
 * Signal handler for daemon termination
 * Using this handler instead of the standard libc one ensures the daemon
 * can cleanup everything -> read() on a FD will now return EINTR
 */
const MobiCoreDriverDaemon::cmd_map_item_t
MobiCoreDriverDaemon::reg_cmd_map[] = {
    {
        &MobiCoreDriverDaemon::reg_read_auth_token,
        0
    },
    {
        &MobiCoreDriverDaemon::reg_store_auth_token,
        0
    },
    {
        &MobiCoreDriverDaemon::reg_delete_auth_token,
        0
    },
    {
        &MobiCoreDriverDaemon::reg_read_root_cont,
        0
    },
    {
        &MobiCoreDriverDaemon::reg_store_root_cont,
        0
    },
    {
        &MobiCoreDriverDaemon::reg_delete_root_cont,
        0
    },
    {
        &MobiCoreDriverDaemon::reg_read_sp_cont,
        sizeof(mcSpid_t)
    },
    {
        &MobiCoreDriverDaemon::reg_store_sp_cont,
        sizeof(TaBlob)
    },
    {
        &MobiCoreDriverDaemon::reg_delete_sp_cont,
        sizeof(mcSpid_t)
    },
    {
        &MobiCoreDriverDaemon::reg_read_tl_cont,
        sizeof(TlBlob)
    },
    {
        &MobiCoreDriverDaemon::reg_store_tl_cont,
        sizeof(TlBlob)
    },
    {
        &MobiCoreDriverDaemon::reg_delete_tl_cont,
        sizeof(TlBlob)
    },
    {
        &MobiCoreDriverDaemon::reg_store_so_data,
        0
    },
    {
        &MobiCoreDriverDaemon::reg_store_ta_blob,
        sizeof(TaBlob)
    },
    {
	&MobiCoreDriverDaemon::reg_delete_ta_objs,
        sizeof(mcUuid_t)
    },
};

static void terminateDaemon(int signum)
{
    LOG_I("Signal %d received", signum);
    if ((signum != SIGUSR1) && g_secure_world) {
        g_secure_world->stopListening();
    }
}

MobiCoreDriverDaemon::MobiCoreDriverDaemon():
    m_reg_server(this, SOCK_PATH)
{
}

//------------------------------------------------------------------------------
int MobiCoreDriverDaemon::init(const std::vector<std::string>& drivers)
{
    sigset_t sigs_to_block;

    if (!m_reg_server.valid())
        return EXIT_FAILURE;

    LOG_D("Initializing Device");

    /* Set main thread's signal mask to block SIGUSR1.
     * All other threads will inherit mask and have it blocked too */
    sigemptyset(&sigs_to_block);
    sigaddset(&sigs_to_block, SIGUSR1);
    pthread_sigmask(SIG_BLOCK, &sigs_to_block, NULL);

    // Open Secure World access
    if (m_secure_world.open() < 0) {
        return EXIT_FAILURE;
    }

    // Load SW device driver(s), if requested
    for (auto driver = drivers.begin(); driver != drivers.end(); driver++) {
        // We don't care will loadDriver failed or not...
        m_secure_world.loadDriver(driver->c_str());
    }

    // Look for tokens and send it to <t-base if any
    installEndorsementToken();

    LOG_D("\n********* successfully initialized Daemon *********");
    return EXIT_SUCCESS;
}

uint32_t MobiCoreDriverDaemon::reg_store_auth_token(CommandHeader& cmd,
        const uint8_t* rx_data, uint32_t* , std::auto_ptr<uint8_t>& )
{
    mcResult_t mcRet = mcRegistryStoreAuthToken(const_cast<uint8_t* >(rx_data), cmd.data_size);
    /* Load authentication token into <t-base */
    if (MC_DRV_OK == mcRet) {
        if (m_secure_world.loadToken(rx_data, sizeof(mcSoAuthTokenCont_t)) < 0) {
            LOG_E("Failed to pass Auth Token to <t-base.");
        }
    }
    return mcRet;
}

uint32_t MobiCoreDriverDaemon::reg_store_root_cont(CommandHeader& cmd,
        const uint8_t* rx_data, uint32_t* , std::auto_ptr<uint8_t>& )
{
    mcResult_t mcRet = mcRegistryStoreRoot(const_cast<uint8_t* >(rx_data), cmd.data_size);
    /* Load root container into <t-base */
    if (MC_DRV_OK == mcRet) {
        if (m_secure_world.loadToken(rx_data, sizeof(mcSoRootCont_t)) < 0) {
            LOG_E("Failed to pass Root Container to <t-base.");
        }
    }
    return mcRet;
}

uint32_t MobiCoreDriverDaemon::reg_store_sp_cont(CommandHeader& cmd,
        const uint8_t* rx_data, uint32_t* , std::auto_ptr<uint8_t>& )
{
    auto blob = reinterpret_cast<const TaBlob*>(rx_data);
    size_t blobSize = cmd.data_size - sizeof(*blob);
    return mcRegistryStoreSp(blob->spid, const_cast<uint8_t* >(blob->blob), blobSize);
}

uint32_t MobiCoreDriverDaemon::reg_store_tl_cont(CommandHeader& cmd,
        const uint8_t* rx_data, uint32_t* , std::auto_ptr<uint8_t>& )
{
    auto blob = reinterpret_cast<const TlBlob*>(rx_data);
    size_t blobSize = cmd.data_size - sizeof(*blob);
    return mcRegistryStoreTrustletCon(&blob->uuid, blob->spid,
                                   const_cast<uint8_t* >(blob->blob), blobSize);
}

uint32_t MobiCoreDriverDaemon::reg_store_so_data(CommandHeader& cmd,
        const uint8_t* rx_data, uint32_t*, std::auto_ptr<uint8_t>&)
{
    return mcRegistryStoreData(const_cast<uint8_t* >(rx_data), cmd.data_size);
}

uint32_t MobiCoreDriverDaemon::reg_store_ta_blob(CommandHeader& cmd,
        const uint8_t* rx_data, uint32_t* , std::auto_ptr<uint8_t>& )
{
    size_t blobSize;
    auto blob = reinterpret_cast<const TaBlob*>(rx_data);

    if (cmd.data_size < sizeof(*blob)) {
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    blobSize = cmd.data_size - sizeof(*blob);
    if (m_secure_world.LoadCheck(blob->spid, blob->blob,
                                 static_cast<uint32_t>(blobSize)) < 0) {
        return MC_DRV_ERR_TRUSTLET_NOT_FOUND;
    }
    return mcRegistryStoreTABlob(blob->spid, blob->blob, blobSize);
}

uint32_t MobiCoreDriverDaemon::reg_read_auth_token(CommandHeader&,
        const uint8_t*, uint32_t* tx_data_size, std::auto_ptr<uint8_t>& tx_data)
{
    uint32_t ret;
    std::auto_ptr<mcSoAuthTokenCont_t> buf(new mcSoAuthTokenCont_t);
    if (buf.get() == NULL)
        return MC_DRV_ERR_NO_FREE_MEMORY;

    ret = mcRegistryReadAuthToken(buf.get());

    if (ret == MC_DRV_OK) {
        tx_data.reset(reinterpret_cast<uint8_t* >(buf.release()));
        *tx_data_size = sizeof(mcSoAuthTokenCont_t);
    }
    return ret;
}

uint32_t MobiCoreDriverDaemon::reg_read_root_cont(CommandHeader&,
        const uint8_t*, uint32_t* tx_data_size, std::auto_ptr<uint8_t>& tx_data)
{
    uint32_t ret;
    uint32_t size = MAX_DATA_SIZE;
    std::auto_ptr<uint8_t> buf(new uint8_t[MAX_DATA_SIZE]);
    if (buf.get() == NULL)
        return MC_DRV_ERR_NO_FREE_MEMORY;

    ret = mcRegistryReadRoot(buf.get(), &size);
    if (ret == MC_DRV_OK) {
        tx_data = buf;
        *tx_data_size = size;
    }
    return ret;
}

uint32_t MobiCoreDriverDaemon::reg_read_sp_cont(CommandHeader&,
        const uint8_t* rx_data, uint32_t* tx_data_size, std::auto_ptr<uint8_t>& tx_data)
{
    uint32_t ret;
    uint32_t size = MAX_DATA_SIZE;
    std::auto_ptr<uint8_t> buf(new uint8_t[MAX_DATA_SIZE]);
    if (buf.get() == NULL)
        return MC_DRV_ERR_NO_FREE_MEMORY;

    const mcSpid_t &spid = *reinterpret_cast<const mcSpid_t *>(rx_data);
    ret = mcRegistryReadSp(spid, buf.get(), &size);
    if (ret == MC_DRV_OK) {
        tx_data = buf;
        *tx_data_size = size;
    }
    return ret;
}

uint32_t MobiCoreDriverDaemon::reg_read_tl_cont(CommandHeader&,
        const uint8_t* rx_data, uint32_t* tx_data_size, std::auto_ptr<uint8_t>& tx_data)
{
    uint32_t ret;
    uint32_t size = MAX_DATA_SIZE;
    std::auto_ptr<uint8_t> buf(new uint8_t[MAX_DATA_SIZE]);
    if (buf.get() == NULL)
        return MC_DRV_ERR_NO_FREE_MEMORY;

    *tx_data_size = MAX_DATA_SIZE;

    auto blob = reinterpret_cast<const TlBlob*>(rx_data);
    ret = mcRegistryReadTrustletCon(&blob->uuid, blob->spid, buf.get(), &size);
    if (ret == MC_DRV_OK) {
        tx_data = buf;
        *tx_data_size = size;
    }
    return ret;
}

uint32_t MobiCoreDriverDaemon::reg_delete_auth_token(CommandHeader& ,
        const uint8_t* , uint32_t* , std::auto_ptr<uint8_t>& )
{
    return mcRegistryDeleteAuthToken();
}

uint32_t MobiCoreDriverDaemon::reg_delete_root_cont(
        CommandHeader& , const uint8_t* ,
        uint32_t* , std::auto_ptr<uint8_t>& )
{
    mcResult_t mcRet = mcRegistryCleanupRoot();

    // Look for tokens and send it to <t-base if any
    if (MC_DRV_OK == mcRet)
        installEndorsementToken();

    return mcRet;
}

uint32_t MobiCoreDriverDaemon::reg_delete_sp_cont(CommandHeader&,
        const uint8_t* rx_data, uint32_t* , std::auto_ptr<uint8_t>& )
{
    const mcSpid_t &spid = *reinterpret_cast<const mcSpid_t *>(rx_data);
    return mcRegistryCleanupSp(spid);
}

uint32_t MobiCoreDriverDaemon::reg_delete_tl_cont(CommandHeader&,
        const uint8_t* rx_data, uint32_t* , std::auto_ptr<uint8_t>& )
{
    auto blob = reinterpret_cast<const TlBlob*>(rx_data);
    return  mcRegistryCleanupTrustlet(&blob->uuid, blob->spid);
}

uint32_t MobiCoreDriverDaemon::reg_delete_ta_objs(CommandHeader&,
        const uint8_t* rx_data, uint32_t* , std::auto_ptr<uint8_t>& )
{
    const mcUuid_t *uuid = reinterpret_cast<const mcUuid_t *>(rx_data);
    return  mcRegistryCleanupGPTAStorage(uuid);
}

bool MobiCoreDriverDaemon::handleConnection(Connection &conn)
{
    bool ret = false;
    CommandHeader  cmd;
    ResponseHeader result = { 0, 0, 0 };
    struct iovec iov[2];
    int count = 1;
    ssize_t write_sz = sizeof(result);

    std::auto_ptr<uint8_t>   rx_data;
    std::auto_ptr<uint8_t>   tx_data;

    LOG_D("handleConnection()==== %p", &conn);

    switch ( conn.readData(&cmd, sizeof(cmd)) ) {
    case sizeof(cmd):
	break;
    case 0:
        LOG_D(" handleConnection(): Connection closed.");
        goto out;
    case -1:
        LOG_E("Socket error.");
        goto out;
    case -2:
        LOG_E("Timeout.");
        goto out;
    default:
        LOG_E("Insufficient data arrived.");
        goto out;
    }

    result.result = MC_DRV_OK;

    if (MC_DRV_REG_END > cmd.cmd) {
        if (cmd.data_size) {
            if (cmd.data_size >= reg_cmd_map[cmd.cmd].min_rx_size) {
                rx_data.reset(new uint8_t[cmd.data_size]);
                if (!rx_data.get()) {
                    LOG_E("Out of memory for command %d", cmd.cmd);
                    goto out;
                } else {
                    uint32_t total=0;
                    uint8_t *ptr = rx_data.get();
                    while(total < cmd.data_size) {
                        ssize_t sz = conn.readData(ptr, cmd.data_size-total);
                        if (sz <= 0) {
                            LOG_E("Payload reading failed for command %d expected=%d received=%zd", cmd.cmd, cmd.data_size-total, sz);
                            result.result = MC_DRV_ERR_UNKNOWN;
                            break;
                        }
                        ptr += sz;
                        total += sz;
                    }
                }
            } else {
                LOG_E("Invalid payload size for command %d", cmd.cmd);
            }
        }

        result.data_size = 0;
        if (result.result == MC_DRV_OK)
            result.result = (this->*(reg_cmd_map[cmd.cmd].handler))(cmd,
                    rx_data.get(),
                    &result.data_size, tx_data);
    } else {
        LOG_E("Unknown command %d", cmd.cmd);
        result.result = MC_DRV_ERR_INVALID_OPERATION;
    }

    iov[0].iov_len  = sizeof(result);
    iov[0].iov_base = const_cast<ResponseHeader *>(&result);

    if (result.data_size) {
        count++;
        write_sz 	+= result.data_size;
        iov[1].iov_len  = result.data_size;
        iov[1].iov_base = tx_data.get();
    }

    if (conn.writeMsg(iov, count) == write_sz)
        ret = true;
out:
    LOG_D("handleConnection()<-------");
    return ret;
}

int MobiCoreDriverDaemon::run(void)
{
    LOG_D("run()====");

    // Start File Storage Daemon and registry server
    m_fsd2.start();
    m_reg_server.start();

    g_secure_world = &m_secure_world;
    int ret = m_secure_world.listen();

    m_reg_server.terminate();
    m_reg_server.kill(SIGUSR1);
    m_reg_server.stop();
    m_reg_server.join();

    // New secure storage runs in parallel to the old one
    m_fsd2.terminate();
    m_fsd2.kill(SIGUSR1);
    m_fsd2.FSD2_Close();
    m_fsd2.join();
    LOG_I("run()<-------");

    return ret;
}

//------------------------------------------------------------------------------
/**
 * Main entry of the <t-base Driver Daemon.
 */
int main(int argc, char *args[])
{
    int ret;

    // Read the Command line options
    extern char *optarg;
    extern int optopt;
    int c, errFlag = 0;

    std::vector<std::string> drivers;
    std::vector<std::string> registry_paths;

    // By default don't fork
    bool is_daemon = false;

    while ((c = getopt(argc, args, "r:bhp:")) != -1) {
        switch (c) {
        case 'h': /* Help */
            errFlag++;
            break;
        case 'b': /* Fork to background */
            is_daemon = true;
            break;
        case 'p': /* Search paths for registry */
            registry_paths.push_back(optarg);
            LOG_D("registry search path %s added", optarg);
            break;
        case 'r': /* Load <t-base driver at start-up */
            drivers.push_back(optarg);
            LOG_D("driver %s enqueued for loading", optarg);
            break;
        case ':': /* -r operand */
            fprintf(stderr, "Option -%c requires an operand\n", optopt);
            errFlag++;
            break;
        case '?':
            fprintf(stderr, "Unrecognized option: -%c\n", optopt);
            errFlag++;
        }
    }

    if (errFlag)
        return printUsage(args[0]);

    // Default registry paths if none specified (for Android)
    if (registry_paths.empty()) {
        registry_paths.push_back("/data/app/mcRegistry");
        registry_paths.push_back("/system/app/mcRegistry");
    }
    setSearchPaths(registry_paths);
    LOG_D("Registry search paths:");
    for (auto path = registry_paths.begin(); path != registry_paths.end(); path++) {
        LOG_D("  %s", path->c_str());
    }

    // Open the device before becoming a daemon
    MobiCoreDriverDaemon mobiCoreDriverDaemon;
    if (mobiCoreDriverDaemon.init(drivers)) {
        return EXIT_FAILURE;
    }

    // We should fork the daemon to background
    if (is_daemon) {
        /* ignore "terminal has been closed" signal */
        signal(SIGHUP, SIG_IGN);

        if (daemon(0, 0) < 0) {
            fprintf(stderr, "Fork failed, exiting...\n");
            return 1;
        }
        LOG_D("Daemon's fork was done");

        /* ignore tty signals */
        signal(SIGTSTP, SIG_IGN);
        signal(SIGTTOU, SIG_IGN);
        signal(SIGTTIN, SIG_IGN);
    }

    // Process signal action
    struct sigaction action;

    // Set up the structure to specify the new action.
    action.sa_handler = terminateDaemon;
    sigemptyset(&action.sa_mask);

    action.sa_flags = 0;
    sigaction(SIGINT,  &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGUSR1, &action, NULL);

    signal(SIGPIPE, SIG_IGN);

    LOG_I("Daemon starting up...");
    LOG_I("Interface version is %u.%u", DAEMON_VERSION_MAJOR,
            DAEMON_VERSION_MINOR);

    LOG_I("%s", MOBICORE_COMPONENT_BUILD_TAG);
    LOG_I("Build timestamp is %s %s", __DATE__, __TIME__);

    ret = mobiCoreDriverDaemon.run();

    LOG_D("Daemon exit with code %d...", ret);
    return ret;
}

void MobiCoreDriverDaemon::installEndorsementToken(void)
{
	/* Look for tokens in the registry and pass them to <t-base for endorsement
	 * purposes.
	 */
	LOG_D("Looking for suitable tokens");

	mcSoAuthTokenCont_t authtoken;
	mcSoAuthTokenCont_t authtokenbackup;
	mcSoRootCont_t rootcont;
	uint32_t sosize;
	const void *p = NULL;

	// Search order:  1. authtoken 2. authtoken backup 3. root container
	sosize = 0;
	mcResult_t ret = mcRegistryReadAuthToken(&authtoken);
	if (ret != MC_DRV_OK) {
	    LOG_D("Failed to read AuthToken (ret=%u). Trying AuthToken backup", ret);

	    ret = mcRegistryReadAuthTokenBackup(&authtokenbackup);
	    if (ret != MC_DRV_OK) {
	        LOG_D("Failed to read AuthToken backup (ret=%u). Trying Root Cont", ret);

	    sosize = sizeof(rootcont);
	    ret = mcRegistryReadRoot(&rootcont, &sosize);
	    if (ret != MC_DRV_OK) {
	            LOG_D("Failed to read Root Cont, (ret=%u).", ret);
	            LOG_W("Device endorsements not supported!");
	            sosize = 0;
	        } else {
	            LOG_D("Found Root Cont.");
	            p = &rootcont;
	        }

	    } else {
	        LOG_D("Found AuthToken backup.");
	        p = &authtokenbackup;
	        sosize = sizeof(authtokenbackup);
	    }

	} else {
	    LOG_D("Found AuthToken.");
	    p = &authtoken;
	    sosize = sizeof(authtoken);
	}

	if (sosize) {
	    LOG_D("Found token of size: %u", sosize);
	    if (m_secure_world.loadToken(p, sosize) < 0) {
	        LOG_E("Failed to pass token to <t-base. "
	              "Device endorsements disabled");
	    }
	}
}
