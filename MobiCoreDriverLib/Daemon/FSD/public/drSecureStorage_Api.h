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
 * @file   drSecureStorage_Api.h
 * @brief  Contains DCI command definitions and data structures
 *
 */

#ifndef __DRTEMPLATEAPI_H__
#define __DRTEMPLATEAPI_H__

#include "dci.h"
#include "tee_type.h"
#include "tee_error.h"

#define RW_DATA_SIZE 4096

/**
 * Command ID's for communication
 * FSD <--> STH
 */

#define STH_MESSAGE_TYPE_LOOK       0
#define STH_MESSAGE_TYPE_READ       1
#define STH_MESSAGE_TYPE_WRITE      2
#define STH_MESSAGE_TYPE_DELETE     3
#define STH_MESSAGE_TYPE_DELETE_ALL 4

#define CMD_ST_SYNC			5
#define NOTIFY_DCIH			6
#define NOTIFY_IPCH			7
/*... add more command ids when needed */

#define STH_PUBLIC_FILE_NAME_SIZE 20

typedef struct {
    uint32_t   status;
	uint8_t    type;
    uint8_t    reserved0;
    uint16_t   flags;
    uint32_t   payloadLen;
    TEE_UUID     uuid;
    unsigned char filename[STH_PUBLIC_FILE_NAME_SIZE];
    unsigned char payload[];
} STH_FSD_message_t;

typedef struct
{
    char header[5];
    unsigned char version;
    uint16_t cryptoLen;
    uint32_t dataLen;
}FSD_plaintext;

/**
 * command message.
 *
 * @param len Lenght of the data to process.
 * @param data Data to be processed
 */
typedef struct {
    dciCommandHeader_t  header;     /**< Command header */
    uint32_t            len;        /**< Length of data to process */
} cmd_t;


/**
 * Response structure
 */
typedef struct {
    dciResponseHeader_t header;     /**< Response header */
    uint32_t            len;
} rsp_t;

/**
 * DCI message data.
 */
typedef struct {
    union {
        cmd_t     command;
        rsp_t     response;
    };

    STH_FSD_message_t   sth_request;
} dciMessage_t;

/**
 * Driver UUID. Update accordingly after reserving UUID
 */
#define DRV_STH_UUID { { 0x07, 0x05, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } }


#endif // __DRTEMPLATEAPI_H__
