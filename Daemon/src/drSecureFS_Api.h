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

/**
 * @file   drSecureFS_Api.h
 * @brief  Contains TCI definitions shared with FSD2 daemon and SPT2 TA
 *
 */

#ifndef __DRSFSAPI_H__
#define __DRSFSAPI_H__

#include "service_delegation_protocol.h"


/*
 *  Driver ID. This is managed by Trustonic
 */
#define DRV_STH2_ID 0x0104

/**
 * Driver UUID. Update accordingly after reserving UUID
 */
#define DRV_SFS_UUID { { 0x07, 0x05, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20 } }

/*
 * TCI definitions, relayed by SPT2 TA from FSD2 daemon
 */
#define EXCHANGE_BUFFER_INSTRUCTIONS_NB     1000

/**
 * This type indicates the state of the daemon
 */
typedef enum {
    STH2_DAEMON_LISTENING = 0,
    STH2_DAEMON_PROCESSING = 1,
} STH2_daemon_state;

/**
 * TCI message data.
 */
typedef struct {
    /* indicates that the secure driver has sent instructions to the normal world daemon.
     * This variable is updated in the following ways:
     *      - Initially set to LISTENING by the daemon before it connects to the secure driver.
     *      - The secure driver switches the state from LISTENING to PROCESSING when it wants the daemon to process instructions.
     *      - The daemon switches the state from PROCESSING to LISTENING when it has finished processing the instructions.
     */
    STH2_daemon_state               nDaemonState;
    /* sector size
     * set initially by the secure world.
     * In TF: `g_nSectorSize`.
     */
    uint32_t                        nSectorSize;

    /* workspace length
     * set initially by the daemon.
     * In TF: calculated from the exchange buffer size.
     */
    uint32_t                        nWorkspaceLength;
    /* administrative data
     * written by the normal world.
     * In TF: `g_pExchangeBuffer->sAdministrativeData`.
     */
    DELEGATION_ADMINISTRATIVE_DATA  sAdministrativeData;
    /* number of instructions to be executed by the daemon.
     * Set by the secure world for each command.
     * In TF: the output `size` of `params[1]`
     */
    uint32_t                        nInstructionsBufferSize;
    /* instruction list
     * set by the secure world.
     * In TF: `g_pExchangeBuffer->sInstructions`.
     */
    uint32_t                        sInstructions[EXCHANGE_BUFFER_INSTRUCTIONS_NB];
    /* sectors content, set by either side depending on the instruction.
     * The workspace size is hard-coded based on the maximum sector size (4096).
     *  In TF: `g_pExchangeBuffer->sWorkspace'
     */
    uint8_t                         sWorkspace[];
} STH2_delegation_exchange_buffer_t;


#endif // __DRSFSAPI_H__
