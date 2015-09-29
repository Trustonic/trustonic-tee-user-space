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
#ifndef MOBICOREDRIVER_H_
#define MOBICOREDRIVER_H_

#include <string>
#include <vector>

#include "ConnectionHandler.h"
#include "Server.h"
#include "FSD2.h"
#include "SecureWorld.h"

class MobiCoreDriverDaemon: public ConnectionHandler
{
    struct cmd_map_item_t {
            uint32_t (MobiCoreDriverDaemon:: *handler)(
        	    CommandHeader &cmd,
                    const uint8_t *rx_data,
                    uint32_t *tx_data_size, std::auto_ptr<uint8_t> &tx_data);
            uint32_t min_rx_size;
    };

    /**
     * installEndorsementToken
     * Look for tokens in the registry and pass them to <t-base for endorsement purposes
     * Search order:  1. authtoken 2. authtoken backup 3. root container
     */
    void installEndorsementToken(void);

    bool handleConnection(Connection &connection);
    void dropConnection(Connection&) {}

    /* Registry commands, arrived through socket */
    uint32_t reg_store_auth_token(CommandHeader &cmd,
            const uint8_t *rx_data, uint32_t *tx_data_size,
            std::auto_ptr<uint8_t> &tx_data);
    uint32_t reg_store_root_cont(CommandHeader &cmd,
            const uint8_t *rx_data, uint32_t *tx_data_size,
            std::auto_ptr<uint8_t> &tx_data);
    uint32_t reg_store_sp_cont(CommandHeader &cmd,
            const uint8_t *rx_data, uint32_t *tx_data_size,
            std::auto_ptr<uint8_t> &tx_data);
    uint32_t reg_store_tl_cont(CommandHeader &cmd,
            const uint8_t *rx_data, uint32_t *tx_data_size,
            std::auto_ptr<uint8_t> &tx_data);
    uint32_t reg_store_so_data(CommandHeader &cmd,
            const uint8_t *rx_data, uint32_t *tx_data_size,
            std::auto_ptr<uint8_t> &tx_data);
    uint32_t reg_store_ta_blob(CommandHeader &cmd,
            const uint8_t *rx_data, uint32_t *tx_data_size,
            std::auto_ptr<uint8_t> &tx_data);
    uint32_t reg_read_auth_token(CommandHeader &cmd,
            const uint8_t *rx_data, uint32_t *tx_data_size,
            std::auto_ptr<uint8_t> &tx_data);
    uint32_t reg_read_root_cont(CommandHeader &cmd,
            const uint8_t *rx_data, uint32_t *tx_data_size,
            std::auto_ptr<uint8_t> &tx_data);
    uint32_t reg_read_sp_cont(CommandHeader &cmd,
            const uint8_t *rx_data, uint32_t *tx_data_size,
            std::auto_ptr<uint8_t> &tx_data);
    uint32_t reg_read_tl_cont(CommandHeader &cmd,
            const uint8_t *rx_data, uint32_t *tx_data_size,
            std::auto_ptr<uint8_t> &tx_data);
    uint32_t reg_delete_auth_token(CommandHeader &cmd,
            const uint8_t *rx_data, uint32_t *tx_data_size,
            std::auto_ptr<uint8_t> &tx_data);
    uint32_t reg_delete_root_cont(CommandHeader &cmd,
            const uint8_t *rx_data, uint32_t *tx_data_size,
            std::auto_ptr<uint8_t> &tx_data);
    uint32_t reg_delete_sp_cont(CommandHeader &cmd,
            const uint8_t *rx_data, uint32_t *tx_data_size,
            std::auto_ptr<uint8_t> &tx_data);
    uint32_t reg_delete_tl_cont(CommandHeader &cmd,
            const uint8_t *rx_data, uint32_t *tx_data_size,
            std::auto_ptr<uint8_t> &tx_data);
    uint32_t reg_delete_ta_objs(CommandHeader &cmd,
            const uint8_t *rx_data, uint32_t *tx_data_size,
            std::auto_ptr<uint8_t> &tx_data);

    static const cmd_map_item_t reg_cmd_map[];
    static const uint MAX_DATA_SIZE = 512;

    SecureWorld m_secure_world;
    // Create the <t-base File Storage Daemon
    FSD2 m_fsd2;
    Server  m_reg_server;
public:
    MobiCoreDriverDaemon();
    int init(const std::vector<std::string>& drivers);
    int run (void);
};

#endif /* MOBICOREDRIVER_H_ */
