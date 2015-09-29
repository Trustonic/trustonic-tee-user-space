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

#ifndef __TEE_UUID_ATTESTATION_H__
#define __TEE_UUID_ATTESTATION_H__

#ifndef __TEE_CLIENT_TYPES_H__
#include "tee_type.h"
#endif

// Sizes of the fields of attestation structure
#define AT_MAGIC_SIZE           8
#define AT_SIZE_SIZE            sizeof(uint32_t)
#define AT_VERSION_SIZE         sizeof(uint32_t)
#define AT_UUID_SIZE            sizeof(TEE_UUID)

// Sizes of the fields used to generate signature
#define AT_TAG_SIZE             20
#define AT_SHA1_HASH_SIZE       20

// Max size of RSA modulus supported
#define AT_MODULUS_MAX_SIZE     256
// Max size of RSA public exponent supported
#define AT_PUBLIC_EXPO_MAX_SIZE 4

// Attestation version
#define AT_VERSION              1

// Name space ID (the UUID of the RSA OID)
const uint8_t       RSA_OID_UUID[AT_UUID_SIZE] = {0x6b, 0x8e, 0x02, 0x6b, 0x63, 0xc1, 0x5d, 0x58, 0xb0, 0x64, 0x00, 0xd3, 0x51, 0x89, 0xce, 0x65};
// Magic word
const char          MAGIC[AT_MAGIC_SIZE] = "TAUUID\0";

// Tag for signature generation
const char          TAG[AT_TAG_SIZE]="Trusted Application";

// Public key structure
typedef struct uuid_public_key {
    uint32_t    type;           // TEE_TYPE_RSA_PUBLIC_KEY: 0xA0000030
    uint16_t    modulus_bytes;  // Length of the modulus in bytes
    uint16_t    exponent_bytes; // Length of the exponent in bytes
    uint8_t     data[];         // Key material
} uuid_public_key;

// Attestation structure
typedef struct uuid_attestation {
    uint8_t         magic[AT_MAGIC_SIZE];   // Magic word: "TAUUID\0\0"
    uint32_t        size;                   // Attestation size (4 bytes)
    uint32_t        version;                // Version number: 1 (4 bytes)
    uint8_t         uuid[AT_UUID_SIZE];     // UUID
    uuid_public_key key;                    // Public key
} uuid_attestation;

#endif /* __TEE_UUID_ATTESTATION_H__ */
