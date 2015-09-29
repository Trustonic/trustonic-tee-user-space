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
 * @file   service_delegation_protocol.h
 * @brief  Delegation protocol definitions
 *
 */

#ifndef __SERVICE_DELEGATION_PROTOCOL_H__
#define __SERVICE_DELEGATION_PROTOCOL_H__



/* Instruction codes */
#define DELEGATION_INSTRUCTION_SHUTDOWN             0xF0
#define DELEGATION_INSTRUCTION_NOTIFY               0xE0

/* Partition-specific instruction codes (high-nibble encodes the partition identifier) */
#define DELEGATION_INSTRUCTION_PARTITION_CREATE     0x01
#define DELEGATION_INSTRUCTION_PARTITION_OPEN       0x02
#define DELEGATION_INSTRUCTION_PARTITION_READ       0x03
#define DELEGATION_INSTRUCTION_PARTITION_WRITE      0x04
#define DELEGATION_INSTRUCTION_PARTITION_SET_SIZE   0x05
#define DELEGATION_INSTRUCTION_PARTITION_SYNC       0x06
#define DELEGATION_INSTRUCTION_PARTITION_CLOSE      0x07
#define DELEGATION_INSTRUCTION_PARTITION_DESTROY    0x08

#define DELEGATION_NOTIFY_TYPE_ERROR                0x000000E1
#define DELEGATION_NOTIFY_TYPE_WARNING              0x000000E2
#define DELEGATION_NOTIFY_TYPE_INFO                 0x000000E3
#define DELEGATION_NOTIFY_TYPE_DEBUG                0x000000E4

typedef struct
{
   uint32_t nInstructionID;
} DELEGATION_GENERIC_INSTRUCTION;

typedef struct
{
   uint32_t nInstructionID;
   uint32_t nMessageType;
   uint32_t nMessageSize;
   char     nMessage[1];
} DELEGATION_NOTIFY_INSTRUCTION;

typedef struct
{
   uint32_t nInstructionID;
   uint32_t nSectorID;
   uint32_t nWorkspaceOffset;
} DELEGATION_RW_INSTRUCTION;

typedef struct
{
   uint32_t nInstructionID;
   uint32_t nNewSize;
} DELEGATION_SET_SIZE_INSTRUCTION;

typedef union
{
   DELEGATION_GENERIC_INSTRUCTION    sGeneric;
   DELEGATION_NOTIFY_INSTRUCTION     sNotify;
   DELEGATION_RW_INSTRUCTION         sReadWrite;
   DELEGATION_SET_SIZE_INSTRUCTION   sSetSize;
} DELEGATION_INSTRUCTION;

typedef struct
{
   uint32_t    nSyncExecuted;
   uint32_t    nPartitionErrorStates[16];
   uint32_t    nPartitionOpenSizes[16];
} DELEGATION_ADMINISTRATIVE_DATA;

#endif /* __SERVICE_DELEGATION_PROTOCOL_H__ */
