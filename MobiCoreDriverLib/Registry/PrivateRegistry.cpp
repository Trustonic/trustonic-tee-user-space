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
/** Mobicore Driver Registry.
 *
 * Implements the MobiCore driver registry which maintains trustlets.
 *
 * @file
 * @ingroup MCD_MCDIMPL_DAEMON_REG
 */
#include <stdlib.h>
#include <dirent.h>
#include <stdio.h>
#include <sys/stat.h>
#include <assert.h>
#include <string.h>
#include <string>
#include <cstring>
#include <cstddef>
#include <sys/mman.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>

#include "mcLoadFormat.h"
#include "mcSpid.h"
#include "mcVersionHelper.h"

#include "PrivateRegistry.h"
#include "MobiCoreRegistry.h"

#include "uuid_attestation.h"

#include "log.h"

/** Maximum size of a shared object container in bytes. */
#define MAX_SO_CONT_SIZE  (512)

#define MC_REGISTRY_ALL      0
#define MC_REGISTRY_WRITABLE 1

#define MC_REGISTRY_SYSTEM_PATH "/system/app/mcRegistry"
#define MC_REGISTRY_DATA_PATH "/data/app/mcRegistry"
#define AUTH_TOKEN_FILE_NAME "00000000.authtokcont"
#define AUTH_TOKEN_FILE_NAME_BACKUP_SUFFIX ".backup"
#define ENV_MC_AUTH_TOKEN_PATH "MC_AUTH_TOKEN_PATH"
#define ROOT_FILE_NAME "00000000.rootcont"
#define SP_CONT_FILE_EXT ".spcont"
#define TL_CONT_FILE_EXT ".tlcont"
#define DATA_CONT_FILE_EXT ".datacont"
#define TL_BIN_FILE_EXT ".tlbin"
#define GP_TA_BIN_FILE_EXT ".tabin"
#define GP_TA_SPID_FILE_EXT ".spid"

using namespace std;

//------------------------------------------------------------------------------
static string byteArrayToString(const void *bytes, size_t elems)
{
    char hx[elems * 2 + 1];

    for (size_t i = 0; i < elems; i++) {
        sprintf(&hx[i * 2], "%02x", ((uint8_t *)bytes)[i]);
    }
    return string(hx);
}

//------------------------------------------------------------------------------
static string uint32ToString(uint32_t value)
{
    char hx[8 + 1];
    snprintf(hx, sizeof(hx), "%08X", value);
    string str(hx);
    return string(str.rbegin(), str.rend());
}

//------------------------------------------------------------------------------
static bool doesDirExist(const char *path)
{
    struct stat ss;
    if (path != NULL && stat(path, &ss) == 0 && S_ISDIR(ss.st_mode)) {
        return true;
    }
    return false;
}

//------------------------------------------------------------------------------
string getTbStoragePath()
{
    return MC_REGISTRY_DATA_PATH "/TbStorage";
}

//------------------------------------------------------------------------------
static string getAuthTokenFilePath()
{
    const char *path;
    string authTokenPath;

    // First, attempt to use regular auth token path environment variable.
    path = getenv(ENV_MC_AUTH_TOKEN_PATH);
    if (doesDirExist(path)) {
        LOG_I("getAuthTokenFilePath(): Using MC_AUTH_TOKEN_PATH %s", path);
        authTokenPath = path;
    } else {
        authTokenPath = MC_REGISTRY_DATA_PATH;
        LOG_I("getAuthTokenFilePath(): Using path %s", authTokenPath.c_str());
    }

    return authTokenPath + "/" + AUTH_TOKEN_FILE_NAME;
}

//------------------------------------------------------------------------------
static string getAuthTokenFilePathBackup()
{
    return getAuthTokenFilePath() + AUTH_TOKEN_FILE_NAME_BACKUP_SUFFIX;
}

//------------------------------------------------------------------------------
static string getRootContFilePath()
{
    return MC_REGISTRY_DATA_PATH "/" ROOT_FILE_NAME;
}

//------------------------------------------------------------------------------
static string getSpDataPath(mcSpid_t spid)
{
    return MC_REGISTRY_DATA_PATH "/" + uint32ToString(spid);
}

//------------------------------------------------------------------------------
static string getSpContFilePath(mcSpid_t spid)
{
    return MC_REGISTRY_DATA_PATH "/" + uint32ToString(spid) + SP_CONT_FILE_EXT;
}

//------------------------------------------------------------------------------
static string getTlContFilePath(const mcUuid_t *uuid, const mcSpid_t spid)
{
    return MC_REGISTRY_DATA_PATH "/" + byteArrayToString(uuid, sizeof(*uuid))
           + "." + uint32ToString(spid) + TL_CONT_FILE_EXT;
}

//------------------------------------------------------------------------------
static string getTlDataPath(const mcUuid_t *uuid)
{
    return MC_REGISTRY_DATA_PATH "/" + byteArrayToString(uuid, sizeof(*uuid));
}

//------------------------------------------------------------------------------
static string getTlDataFilePath(const mcUuid_t *uuid, mcPid_t pid)
{
    return getTlDataPath(uuid) + "/" + uint32ToString(pid.data) + DATA_CONT_FILE_EXT;
}

//------------------------------------------------------------------------------
static string getTlBinFilePath(const mcUuid_t *uuid, int registry)
{
    string path_ro_registry = MC_REGISTRY_SYSTEM_PATH"/" + byteArrayToString(uuid, sizeof(*uuid)) + TL_BIN_FILE_EXT;
    string path_rw_registry = MC_REGISTRY_DATA_PATH"/" + byteArrayToString(uuid, sizeof(*uuid)) + TL_BIN_FILE_EXT;

    if (registry == MC_REGISTRY_ALL) {
        struct stat tmp;
        if (stat(path_ro_registry.c_str(), &tmp) == 0) {
            return path_ro_registry;
        }
    }
    return path_rw_registry;
}

//------------------------------------------------------------------------------
static string getTABinFilePath(const mcUuid_t *uuid, int registry)
{
    string path_ro_registry = MC_REGISTRY_SYSTEM_PATH"/" + byteArrayToString(uuid, sizeof(*uuid)) + GP_TA_BIN_FILE_EXT;
    string path_rw_registry = MC_REGISTRY_DATA_PATH"/" + byteArrayToString(uuid, sizeof(*uuid)) + GP_TA_BIN_FILE_EXT;

    if (registry == MC_REGISTRY_ALL) {
        struct stat tmp;
        if (stat(path_ro_registry.c_str(), &tmp) == 0) {
            return path_ro_registry;
        }
    }
    return path_rw_registry;
}

//------------------------------------------------------------------------------
static string getTASpidFilePath(const mcUuid_t *uuid, int registry)
{
    string path_ro_registry = MC_REGISTRY_SYSTEM_PATH"/" + byteArrayToString(uuid, sizeof(*uuid)) + GP_TA_SPID_FILE_EXT;
    string path_rw_registry = MC_REGISTRY_DATA_PATH"/" + byteArrayToString(uuid, sizeof(*uuid)) + GP_TA_SPID_FILE_EXT;

    if (registry == MC_REGISTRY_ALL) {
        struct stat tmp;
        if (stat(path_ro_registry.c_str(), &tmp) == 0) {
            return path_ro_registry;
        }
    }
    return path_rw_registry;
}

//------------------------------------------------------------------------------
mcResult_t mcRegistryStoreAuthToken(void *so, uint32_t size)
{
    int res = 0;
    if (so == NULL || size > 3 * MAX_SO_CONT_SIZE) {
        LOG_E("mcRegistry store So.Soc failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    const string &authTokenFilePath = getAuthTokenFilePath();
    LOG_I("store AuthToken: %s", authTokenFilePath.c_str());

    FILE *fs = fopen(authTokenFilePath.c_str(), "wb");
    if (fs==NULL) {
        LOG_E("mcRegistry store So.Soc failed: %d", MC_DRV_ERR_INVALID_DEVICE_FILE);
        return MC_DRV_ERR_INVALID_DEVICE_FILE;
    }
    res = fseek(fs, 0, SEEK_SET);
    if (res!=0) {
        LOG_E("mcRegistry store So.Soc failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        fclose(fs);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    fwrite((char *)so, 1, size, fs);
    if (ferror(fs)) {
        LOG_E("mcRegistry store So.Soc failed %d", MC_DRV_ERR_OUT_OF_RESOURCES);
        fclose(fs);
        return MC_DRV_ERR_OUT_OF_RESOURCES;
    }
    fflush(fs);
    fclose(fs);

    return MC_DRV_OK;
}


//------------------------------------------------------------------------------
mcResult_t mcRegistryReadAuthToken(mcSoAuthTokenCont_t *so)
{
    int res = 0;
    if (NULL == so) {
        LOG_E("mcRegistry read So.Soc failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    const string &authTokenFilePath = getAuthTokenFilePath();
    LOG_I("read AuthToken: %s", authTokenFilePath.c_str());

    FILE *fs = fopen(authTokenFilePath.c_str(), "rb");
    if (fs==NULL) {
        LOG_W("mcRegistry read So.Soc failed: %d", MC_DRV_ERR_INVALID_DEVICE_FILE);
        return MC_DRV_ERR_INVALID_DEVICE_FILE;
    }
    res = fseek(fs, 0, SEEK_END);
    if (res!=0) {
        LOG_E("mcRegistry read So.Soc failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        fclose(fs);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    int32_t filesize = ftell(fs);
    // We ensure that mcSoAuthTokenCont_t matches with filesize, as ferror (during fread operation) can't
    // handle the case where mcSoAuthTokenCont_t < filesize
    if (sizeof(mcSoAuthTokenCont_t) != filesize) {
        fclose(fs);
        LOG_W("mcRegistry read So.Soc failed: %d", MC_DRV_ERR_OUT_OF_RESOURCES);
        return MC_DRV_ERR_OUT_OF_RESOURCES;
    }
    res = fseek(fs, 0, SEEK_SET);
    if (res!=0) {
        LOG_E("mcRegistry read So.Soc failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        fclose(fs);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    res = fread((char *)so, 1, sizeof(mcSoAuthTokenCont_t), fs);
    if (ferror(fs)) {
        fclose(fs);
        LOG_E("mcRegistry read So.Soc failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    if ((unsigned)res<sizeof(mcSoAuthTokenCont_t)) {
        //File is shorter than expected
        if (feof(fs)) {
            LOG_E("%s(): EOF reached: res is %u, size of mcSoAuthTokenCont_t is %zu", __func__, (unsigned)res,
            sizeof(mcSoAuthTokenCont_t));
        }
        fclose(fs);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    fclose(fs);

    return MC_DRV_OK;
}

//------------------------------------------------------------------------------
mcResult_t mcRegistryReadAuthTokenBackup(mcSoAuthTokenCont_t *so)
{
    int res = 0;
    if (NULL == so) {
        LOG_E("mcRegistry read So.Soc failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    const string &authTokenFilePath = getAuthTokenFilePathBackup();
    LOG_I("read AuthToken: %s", authTokenFilePath.c_str());

    FILE *fs = fopen(authTokenFilePath.c_str(), "rb");
    if (fs==NULL) {
        LOG_W("mcRegistry read So.Soc failed: %d", MC_DRV_ERR_INVALID_DEVICE_FILE);
        return MC_DRV_ERR_INVALID_DEVICE_FILE;
    }
    res = fseek(fs, 0, SEEK_END);
    if (res!=0) {
        LOG_E("mcRegistry read So.Soc failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        fclose(fs);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    int32_t filesize = ftell(fs);
    // We ensure that mcSoAuthTokenCont_t matches with filesize, as ferror (during fread operation) can't
    // handle the case where mcSoAuthTokenCont_t < filesize
    if (sizeof(mcSoAuthTokenCont_t) != filesize) {
        fclose(fs);
        LOG_W("mcRegistry read So.Soc failed: %d", MC_DRV_ERR_OUT_OF_RESOURCES);
        return MC_DRV_ERR_OUT_OF_RESOURCES;
    }
    res = fseek(fs, 0, SEEK_SET);
    if (res!=0) {
        LOG_E("mcRegistry read So.Soc failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        fclose(fs);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    res = fread((char *)so, 1, sizeof(mcSoAuthTokenCont_t), fs);
    if (ferror(fs)) {
        fclose(fs);
        LOG_E("mcRegistry read So.Soc failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    if ((unsigned)res<sizeof(mcSoAuthTokenCont_t)) {
        //File is shorter than expected
        if (feof(fs)) {
            LOG_E("%s(): EOF reached: res is %u, size of mcSoAuthTokenCont_t is %zu", __func__,
            (unsigned)res, sizeof(mcSoAuthTokenCont_t));
        }
        fclose(fs);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    fclose(fs);

    return MC_DRV_OK;
}

//------------------------------------------------------------------------------
mcResult_t mcRegistryDeleteAuthToken(void)
{
    if (rename(getAuthTokenFilePath().c_str(), getAuthTokenFilePathBackup().c_str())) {
        LOG_ERRNO("Rename Auth token file!");
        return MC_DRV_ERR_UNKNOWN;
    } else
        return MC_DRV_OK;
}


//------------------------------------------------------------------------------
mcResult_t mcRegistryStoreRoot(void *so, uint32_t size)
{
    int res = 0;
    if (so == NULL || size > 3 * MAX_SO_CONT_SIZE) {
        LOG_E("mcRegistry store So.Root failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }

    const string &rootContFilePath = getRootContFilePath();
    LOG_I("store Root: %s", rootContFilePath.c_str());

    FILE *fs = fopen(rootContFilePath.c_str(), "wb");
    if (fs==NULL) {
        LOG_E("mcRegistry store So.Root failed: %d", MC_DRV_ERR_INVALID_DEVICE_FILE);
        return MC_DRV_ERR_INVALID_DEVICE_FILE;
    }
    res = fseek(fs, 0, SEEK_SET);
    if (res!=0) {
        LOG_E("mcRegistry store So.Root failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        fclose(fs);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    fwrite((char *)so, 1, size, fs);
    if (ferror(fs)) {
        LOG_E("mcRegistry store So.Root failed: %d", MC_DRV_ERR_OUT_OF_RESOURCES);
        fclose(fs);
        return MC_DRV_ERR_OUT_OF_RESOURCES;
    }
    fflush(fs);
    fclose(fs);

    return MC_DRV_OK;
}


//------------------------------------------------------------------------------
mcResult_t mcRegistryReadRoot(void *so, uint32_t *size)
{
    const string &rootContFilePath = getRootContFilePath();
    size_t readBytes;

    if (so == NULL) {
        LOG_E("mcRegistry read So.Root failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    LOG_I(" Opening %s", rootContFilePath.c_str());

    FILE *fs = fopen(rootContFilePath.c_str(), "rb");
    if (fs==NULL) {
        LOG_W("mcRegistry read So.Root failed: %d", MC_DRV_ERR_INVALID_DEVICE_FILE);
        return MC_DRV_ERR_INVALID_DEVICE_FILE;
    }
    readBytes = fread((char *)so, 1, *size, fs);
    if (ferror(fs)) {
        fclose(fs);
        LOG_E("mcRegistry read So.Root failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    fclose(fs);

    if (readBytes > 0) {
        *size = readBytes;
        return MC_DRV_OK;
    } else {
        LOG_E("mcRegistry read So.Root failed: %d", MC_DRV_ERR_INVALID_DEVICE_FILE);
        return MC_DRV_ERR_INVALID_DEVICE_FILE;
    }
}


//------------------------------------------------------------------------------
mcResult_t mcRegistryStoreSp(mcSpid_t spid, void *so, uint32_t size)
{
    int res = 0;
    if ((spid == 0) || (so == NULL) || size > 3 * MAX_SO_CONT_SIZE) {
        LOG_E("mcRegistry store So.Sp(SpId) failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }

    const string &spContFilePath = getSpContFilePath(spid);
    LOG_I("store SP: %s", spContFilePath.c_str());

    FILE *fs = fopen(spContFilePath.c_str(), "wb");
    if (fs==NULL) {
        LOG_E("mcRegistry store So.Sp(SpId) failed: %d", MC_DRV_ERR_INVALID_DEVICE_FILE);
        return MC_DRV_ERR_INVALID_DEVICE_FILE;
    }
    res = fseek(fs, 0, SEEK_SET);
    if (res!=0) {
        LOG_E("mcRegistry store So.Sp(SpId) failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        fclose(fs);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    fwrite((char *)so, 1, size, fs);
    if (ferror(fs)) {
        LOG_E("mcRegistry store So.Sp(SpId) failed: %d", MC_DRV_ERR_OUT_OF_RESOURCES);
        fclose(fs);
        return MC_DRV_ERR_OUT_OF_RESOURCES;
    }
    fflush(fs);
    fclose(fs);

    return MC_DRV_OK;
}


//------------------------------------------------------------------------------
mcResult_t mcRegistryReadSp(mcSpid_t spid, void *so, uint32_t *size)
{
    const string &spContFilePath = getSpContFilePath(spid);
    size_t readBytes;
    if ((spid == 0) || (so == NULL)) {
        LOG_E("mcRegistry read So.Sp(SpId=0x%x) failed", spid);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    LOG_I(" Reading %s", spContFilePath.c_str());

    FILE *fs = fopen(spContFilePath.c_str(), "rb");
    if (fs==NULL) {
        LOG_E("mcRegistry read So.Sp(SpId) failed: %d", MC_DRV_ERR_INVALID_DEVICE_FILE);
        return MC_DRV_ERR_INVALID_DEVICE_FILE;
    }
    readBytes = fread((char *)so, 1, *size, fs);
    if (ferror(fs)) {
        fclose(fs);
        LOG_E("mcRegistry read So.Sp(SpId) failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    fclose(fs);

    if (readBytes > 0) {
        *size = readBytes;
        return MC_DRV_OK;
    } else {
        LOG_E("mcRegistry read So.Sp(SpId) failed: %d", MC_DRV_ERR_INVALID_DEVICE_FILE);
        return MC_DRV_ERR_INVALID_DEVICE_FILE;
    }
}


//------------------------------------------------------------------------------
mcResult_t mcRegistryStoreTrustletCon(const mcUuid_t *uuid, const mcSpid_t spid, void *so, uint32_t size)
{
    int res = 0;
    if ((uuid == NULL) || (so == NULL) || size > 3 * MAX_SO_CONT_SIZE) {
        LOG_E("mcRegistry store So.TrustletCont(uuid) failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }

    const string &tlContFilePath = getTlContFilePath(uuid, spid);
    LOG_I("store TLc: %s", tlContFilePath.c_str());

    FILE *fs = fopen(tlContFilePath.c_str(), "wb");
    if (fs==NULL) {
        LOG_E("mcRegistry store So.TrustletCont(uuid) failed: %d", MC_DRV_ERR_INVALID_DEVICE_FILE);
        return MC_DRV_ERR_INVALID_DEVICE_FILE;
    }
    res = fseek(fs, 0, SEEK_SET);
    if (res!=0) {
        LOG_E("mcRegistry store So.TrustletCont(uuid) failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        fclose(fs);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    fwrite((char *)so, 1, size, fs);
    if (ferror(fs)) {
        LOG_E("mcRegistry store So.TrustletCont(uuid) failed: %d", MC_DRV_ERR_OUT_OF_RESOURCES);
        fclose(fs);
        return MC_DRV_ERR_OUT_OF_RESOURCES;
    }
    fflush(fs);
    fclose(fs);

    return MC_DRV_OK;
}

static uint32_t getAsUint32BE(
    void *pValueUnaligned
)
{
    uint8_t *p = (uint8_t *)pValueUnaligned;
    uint32_t val = p[3] | (p[2] << 8) | (p[1] << 16) | (p[0] << 24);
    return val;
}

mcResult_t mcRegistryStoreTABlob(mcSpid_t spid, void *blob, uint32_t size)
{
    int res = 0;
    LOG_I("mcRegistryStoreTABlob started");

    // Check blob size
    if (size < sizeof(mclfHeaderV24_t)) {
        LOG_E("RegistryStoreTABlob failed - TA blob length is less then header size");
        return MC_DRV_ERR_INVALID_PARAMETER;
    }

    mclfHeaderV24_t *header24 = (mclfHeaderV24_t *)blob;
    mclfHeaderV2_t *header20 = (mclfHeaderV2_t *)blob;

    // Check header version
    if (header20->intro.version < MC_MAKE_VERSION(2, 4)) {
        LOG_E("RegistryStoreTABlob failed - TA blob header version is less than 2.4");
        return MC_DRV_ERR_TA_HEADER_ERROR;
    }

    //Check GP version
    if (header24->gp_level != 1) {
        LOG_E("RegistryStoreTABlob failed - TA blob header gp_level is not equal to 1");
        return MC_DRV_ERR_TA_HEADER_ERROR;
    }

    TEEC_UUID uuid;
    switch (header20->serviceType) {
    case SERVICE_TYPE_SYSTEM_TRUSTLET: {
        // Check spid
        if (spid != MC_SPID_SYSTEM) {
            LOG_E("RegistryStoreTABlob failed - SPID is not equal to %d for System TA", spid);
            return MC_DRV_ERR_INVALID_PARAMETER;
        }
        memcpy(&uuid, &header20->uuid, sizeof(mcUuid_t));
        break;
    }
    case SERVICE_TYPE_SP_TRUSTLET: {
        // Check spid
        if (spid >= MC_SPID_SYSTEM) {
            LOG_E("RegistryStoreTABlob failed - SPID is equal to %u ", spid);
            return MC_DRV_ERR_INVALID_PARAMETER;
        }

        uuid_attestation *pUa = (uuid_attestation *) & ((uint8_t *)blob)[header24->attestationOffset];
        // Check attestation size
        if ((header24->attestationOffset > size) && (header24->attestationOffset + getAsUint32BE(&pUa->size) > size)) {
            LOG_E("RegistryStoreTABlob failed - Attestation size is not correct");
            return MC_DRV_ERR_TA_HEADER_ERROR;
        }

        // Check attestation size
        if (getAsUint32BE(&pUa->size) < sizeof(uuid_attestation)) {
            LOG_E("RegistryStoreTABlob failed - Attestation size is equal to %d and is less then %zu", getAsUint32BE(&pUa->size), sizeof(uuid_attestation));
            return MC_DRV_ERR_TA_ATTESTATION_ERROR;
        }

        // Check magic word
        if (memcmp(pUa->magic, MAGIC, AT_MAGIC_SIZE)) {
            LOG_E("RegistryStoreTABlob failed - Attestation magic word is not correct");
            return MC_DRV_ERR_TA_ATTESTATION_ERROR;
        }

        // Check version
        if (getAsUint32BE(&pUa->version) != AT_VERSION) {
            LOG_E("RegistryStoreTABlob failed - Attestation version is equal to %08X. It has to be equal to %08X", getAsUint32BE(&pUa->version), AT_VERSION);
            return MC_DRV_ERR_TA_ATTESTATION_ERROR;
        }

        memcpy(&uuid, &pUa->uuid, sizeof(mcUuid_t));
        break;
    }
    default: {
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    }
    const string taBinFilePath = getTABinFilePath((mcUuid_t *)&uuid, MC_REGISTRY_WRITABLE);

    LOG_I("Store TA blob at: %s", taBinFilePath.c_str());

    FILE *fs = fopen(taBinFilePath.c_str(), "wb");
    if (fs==NULL) {
        LOG_E("RegistryStoreTABlob failed - TA blob file open error: %d", MC_DRV_ERR_INVALID_DEVICE_FILE);
        return MC_DRV_ERR_INVALID_DEVICE_FILE;
    }
    res = fseek(fs, 0, SEEK_SET);
    if (res!=0) {
        LOG_E("RegistryStoreTABlob failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        fclose(fs);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    fwrite(blob, 1, size, fs);
    if (ferror(fs)) {
        LOG_E("RegistryStoreTABlob failed: %d", MC_DRV_ERR_OUT_OF_RESOURCES);
        fclose(fs);
        return MC_DRV_ERR_OUT_OF_RESOURCES;
    }
    fflush(fs);
    fclose(fs);

    if (header20->serviceType == SERVICE_TYPE_SP_TRUSTLET) {
        const string taspidFilePath = getTASpidFilePath((mcUuid_t *)&uuid, MC_REGISTRY_WRITABLE);

        LOG_I("Store spid file at: %s", taspidFilePath.c_str());

        FILE *fs = fopen(taspidFilePath.c_str(), "wb");
        if (fs==NULL) {
            //TODO: shouldn't we delete TA blob file ?
            LOG_E("RegistryStoreTABlob failed - TA blob file open error: %d", MC_DRV_ERR_INVALID_DEVICE_FILE);
            return MC_DRV_ERR_INVALID_DEVICE_FILE;
        }
        res = fseek(fs, 0, SEEK_SET);
        if (res!=0) {
            LOG_E("RegistryStoreTABlob failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
            fclose(fs);
            return MC_DRV_ERR_INVALID_PARAMETER;
        }
        fwrite(&spid, 1, sizeof(mcSpid_t), fs);
        if (ferror(fs)) {
            LOG_E("RegistryStoreTABlob failed: %d", MC_DRV_ERR_OUT_OF_RESOURCES);
            fclose(fs);
            return MC_DRV_ERR_OUT_OF_RESOURCES;
        }
        fflush(fs);
        fclose(fs);
    }
    return MC_DRV_OK;
}

//------------------------------------------------------------------------------
mcResult_t mcRegistryReadTrustletCon(const mcUuid_t *uuid, const mcSpid_t spid, void *so, uint32_t *size)
{
    int res = 0;
    if ((uuid == NULL) || (so == NULL)) {
        LOG_E("mcRegistry read So.TrustletCont(uuid) failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    size_t readBytes;
    const string &tlContFilePath = getTlContFilePath(uuid, spid);
    LOG_I("read TLc: %s", tlContFilePath.c_str());

    FILE *fs = fopen(tlContFilePath.c_str(), "rb");
    if (fs==NULL) {
        LOG_E("mcRegistry read So.TrustletCont(uuid) failed: %d", MC_DRV_ERR_INVALID_DEVICE_FILE);
        return MC_DRV_ERR_INVALID_DEVICE_FILE;
    }
    res = fseek(fs, 0, SEEK_SET);
    if (res!=0) {
        LOG_E("mcRegistry read So.TrustletCont(uuid) failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        fclose(fs);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    readBytes = fread((char *)so, 1, *size, fs);
    if (ferror(fs)) {
        fclose(fs);
        LOG_E("mcRegistry read So.TrustletCont(uuid) failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    fclose(fs);

    if (readBytes > 0) {
        *size = readBytes;
        return MC_DRV_OK;
    } else {
        LOG_E("mcRegistry read So.TrustletCont(uuid) failed: %d", MC_DRV_ERR_INVALID_DEVICE_FILE);
        return MC_DRV_ERR_INVALID_DEVICE_FILE;
    }
}


//------------------------------------------------------------------------------
mcResult_t mcRegistryStoreData(void *so, uint32_t size)
{
    mcSoDataCont_t *dataCont = (mcSoDataCont_t *)so;
    int res = 0;

    if (dataCont == NULL || size != sizeof(mcSoDataCont_t)) {
        LOG_E("mcRegistry store So.Data failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    string pathname, filename;

    switch (dataCont->cont.type) {
    case CONT_TYPE_SPDATA:
        LOG_E("SPDATA not supported");
        return MC_DRV_ERR_INVALID_PARAMETER;
        break;
    case CONT_TYPE_TLDATA:
        pathname = getTlDataPath(&dataCont->cont.uuid);
        filename = getTlDataFilePath(&dataCont->cont.uuid, dataCont->cont.pid);
        break;
    default:
        LOG_E("mcRegistry store So.Data(cid/pid) failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    if (mkdir(pathname.c_str(), 0777) < 0)
    {
        LOG_E("mcRegistry store So.Data(cid/pid) failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }

    LOG_I("store DT: %s", filename.c_str());

    FILE *fs = fopen(filename.c_str(), "wb");
    if (fs==NULL) {
        LOG_E("mcRegistry store So.Data(cid/pid) failed: %d", MC_DRV_ERR_INVALID_DEVICE_FILE);
        return MC_DRV_ERR_INVALID_DEVICE_FILE;
    }
    res = fseek(fs, 0, SEEK_SET);
    if (res!=0) {
        LOG_E("mcRegistry store So.Data(cid/pid) failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        fclose(fs);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    fwrite((char *)dataCont, 1, MC_SO_SIZE(dataCont->soHeader.plainLen, dataCont->soHeader.encryptedLen), fs);
    if (ferror(fs)) {
        LOG_E("mcRegistry store So.Data(cid/pid) failed: %d", MC_DRV_ERR_OUT_OF_RESOURCES);
        fclose(fs);
        return MC_DRV_ERR_OUT_OF_RESOURCES;
    }
    fflush(fs);
    fclose(fs);

    return MC_DRV_OK;
}


//------------------------------------------------------------------------------
mcResult_t mcRegistryReadData(uint32_t context, const mcCid_t *cid, mcPid_t pid,
                              mcSoDataCont_t *so, uint32_t maxLen)
{
    int res = 0;

    if ((NULL == cid) || (NULL == so)) {
        LOG_E("mcRegistry read So.Data failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    string filename;
    switch (context) {
    case 0:
        LOG_E("SPDATA not supported");
        return MC_DRV_ERR_INVALID_PARAMETER;
        break;
    case 1:
        filename = getTlDataFilePath(&so->cont.uuid, so->cont.pid);
        break;
    default:
        LOG_E("mcRegistry read So.Data(cid/pid) failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    LOG_I("read DT: %s", filename.c_str());

    FILE *fs = fopen(filename.c_str(), "rb");
    if (fs==NULL) {
        LOG_E("mcRegistry read So.Data(cid/pid) failed: %d", MC_DRV_ERR_INVALID_DEVICE_FILE);
        return MC_DRV_ERR_INVALID_DEVICE_FILE;
    }
    res = fseek(fs, 0, SEEK_END);
    if (res!=0) {
        LOG_E("mcRegistry read So.Data(cid/pid) failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        fclose(fs);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    uint32_t filesize = ftell(fs);
    if (maxLen < filesize) {
        fclose(fs);
        LOG_E("mcRegistry read So.Data(cid/pid) failed: %d", MC_DRV_ERR_OUT_OF_RESOURCES);
        return MC_DRV_ERR_OUT_OF_RESOURCES;
    }
    res = fseek(fs, 0, SEEK_SET);
    if (res!=0) {
        LOG_E("mcRegistry read So.Data(cid/pid) failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        fclose(fs);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    char *p = (char *) so;
    res = fread(p, 1, sizeof(mcSoHeader_t), fs);
    if (ferror(fs)) {
        fclose(fs);
        LOG_E("mcRegistry read So.Data(cid/pid) failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    if ((unsigned)res<sizeof(mcSoHeader_t)) {
        //File is shorter than expected
        if (feof(fs)) {
            LOG_E("%s(): EOF reached: res is %u, size of mcSoHeader_t is %zu", __func__, (unsigned)res, sizeof(mcSoHeader_t));
        }
        fclose(fs);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    p += sizeof(mcSoHeader_t);
    res = fread(p, 1, MC_SO_SIZE(so->soHeader.plainLen,
                so->soHeader.encryptedLen)
                - sizeof(mcSoHeader_t), fs);
    if (ferror(fs)) {
        fclose(fs);
        LOG_E("mcRegistry read So.Data(cid/pid) failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    if ((unsigned)res<(MC_SO_SIZE(so->soHeader.plainLen, so->soHeader.encryptedLen) - sizeof(mcSoHeader_t))) {
        //File is shorter than expected
        if (feof(fs)) {
            LOG_E("%s(): EOF reached: res is %u, size of secure object is %zu", __func__, (unsigned)res,
            MC_SO_SIZE(so->soHeader.plainLen, so->soHeader.encryptedLen) - sizeof(mcSoHeader_t));
        }
        fclose(fs);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    fclose(fs);

    return MC_DRV_OK;
}


//------------------------------------------------------------------------------
static size_t getFileContent(
    const char *pPath,
    uint8_t **ppContent)
{
    FILE   *pStream;
    long    filesize;
    uint8_t *content = NULL;
    int res = 0;

    /* Open the file */
    pStream = fopen(pPath, "rb");
    if (pStream == NULL) {
        LOG_E("Error: Cannot open file: %s.", pPath);
        return 0;
    }

    if (fseek(pStream, 0L, SEEK_END) != 0) {
        LOG_E("Error: Cannot read file: %s.", pPath);
        goto error;
    }

    filesize = ftell(pStream);
    if (filesize < 0) {
        LOG_E("Error: Cannot get the file size: %s.", pPath);
        goto error;
    }

    if (filesize == 0) {
        LOG_E("Error: Empty file: %s.", pPath);
        goto error;
    }

    /* Set the file pointer at the beginning of the file */
    if (fseek(pStream, 0L, SEEK_SET) != 0) {
        LOG_E("Error: Cannot read file: %s.", pPath);
        goto error;
    }

    /* Allocate a buffer for the content */
    content = (uint8_t *)malloc(filesize);
    if (content == NULL) {
        LOG_E("Error: Cannot read file: Out of memory.");
        goto error;
    }

    /* Read data from the file into the buffer */
    res = fread(content, (size_t)filesize, 1, pStream);
    if (ferror(pStream)) {
        LOG_E("Error: Cannot read file: %s.", pPath);
        goto error;
    }
    if ((unsigned)res<1) {
        //File is shorter than expected
        if (feof(pStream)) {
            LOG_E("Error: EOF reached: %s.", pPath);
        }
        goto error;
    }

    /* Close the file */
    fclose(pStream);
    *ppContent = content;

    /* Return number of bytes read */
    return (size_t)filesize;

error:
    if (content  != NULL) {
        free(content);
    }
    fclose(pStream);
    return 0;
}

//------------------------------------------------------------------------------
static bool mcCheckUuid(const mcUuid_t *uuid, const char *filename)
{
    uint8_t    *pTAData = NULL;
    uint32_t    nTASize;
    bool        res;

    nTASize = getFileContent(filename, &pTAData);
    if (nTASize == 0) {
        LOG_E("err: Trusted Application not found.");
        return false;
    }

    // Check blob size
    if (nTASize < sizeof(mclfHeaderV24_t)) {
        free(pTAData);
        LOG_E("RegistryStoreTABlob failed - TA blob length is less then header size");
        return false;
    }

    mclfHeaderV2_t *header20 = (mclfHeaderV2_t *)pTAData;

    // Check header version
    if (header20->intro.version < MC_MAKE_VERSION(2, 4)) {
        free(pTAData);
        LOG_E("RegistryStoreTABlob failed - TA blob header version is less than 2.4");
        return false;
    }

    // Check blob size
    if (memcmp(uuid, &header20->uuid, sizeof(mcUuid_t)) == 0) {
        res = true;
    } else {
        res = false;
    }

    free(pTAData);

    return res;
}

//this function deletes all the files owned by a GP TA and stored in the tbase secure storage dir.
//then it deletes GP TA folder.
static int CleanupGPTAStorage(const char *uuid)
{
	DIR            *dp;
	struct dirent  *de;
	int             e;
	string TAPath = getTbStoragePath() + "/" + uuid;
	if (NULL != (dp = opendir(TAPath.c_str()))) {
		while (NULL != (de = readdir(dp))) {
			if (de->d_name[0] != '.') {
				string dname = TAPath + "/" + string (de->d_name);
				LOG_I("delete DT: %s", dname.c_str());
				if (0 != (e = remove(dname.c_str()))) {
					LOG_E("remove UUID-files %s failed! error: %d", dname.c_str(), e);
				}
			}
		}
		if (dp) {
			closedir(dp);
		}
		LOG_I("delete dir: %s", TAPath.c_str());
		if (0 != (e = rmdir(TAPath.c_str()))) {
			LOG_E("remove UUID-dir failed! errno: %d", e);
			return e;
		}
	}
	return MC_DRV_OK;
}


mcResult_t mcRegistryCleanupGPTAStorage(mcUuid_t *uuid)
{
    return CleanupGPTAStorage(byteArrayToString(uuid, sizeof(*uuid)).c_str());
}

static void deleteSPTA(const mcUuid_t *uuid, const mcSpid_t spid)
{
    DIR            *dp;
    struct dirent  *de;
    int             e;

    // Delete TABIN and SPID files - we loop searching required spid file
    if (NULL != (dp = opendir(MC_REGISTRY_DATA_PATH))) {
        while (NULL != (de = readdir(dp))) {
            string spidFile;
            string tabinFile;
            string tabinUuid;
            size_t pch_dot, pch_slash;
            spidFile = MC_REGISTRY_DATA_PATH "/" + string(de->d_name);
            pch_dot = spidFile.find_last_of('.');
            if (pch_dot == string::npos) continue;
            pch_slash = spidFile.find_last_of('/');
            if ((pch_slash != string::npos) && (pch_slash > pch_dot))  continue;
            if (spidFile.substr(pch_dot).compare(GP_TA_SPID_FILE_EXT) != 0) continue;

            mcSpid_t curSpid = 0;

            int fd = open(spidFile.c_str(), O_RDONLY);
            if (fd != -1) {
                if (read(fd, &curSpid, sizeof(mcSpid_t))!=sizeof(mcSpid_t)) {
                    curSpid = 0;
                }
                close(fd);
            }
            if (spid == curSpid) {
                tabinFile =  spidFile.substr(0, pch_dot) + GP_TA_BIN_FILE_EXT;
                if (mcCheckUuid(uuid, tabinFile.c_str())) {
                	tabinUuid = spidFile.substr(0, pch_dot);
                	tabinUuid = tabinUuid.substr(tabinUuid.find_last_of('/')+1);
                	LOG_I("Remove TA storage %s", tabinUuid.c_str());
                	if (0 != (e = CleanupGPTAStorage(tabinUuid.c_str()))){
                		LOG_E("Remove TA storage failed! errno: %d", e);
                		/* Discard error */
                	}
                	LOG_I("Remove TA file %s", tabinFile.c_str());
                    if (0 != (e = remove(tabinFile.c_str()))) {
                        LOG_E("Remove TA file failed! errno: %d", e);
                        /* Discard error */
                    }
                    LOG_I("Remove spid file %s", spidFile.c_str());
                    if (0 != (e = remove(spidFile.c_str()))) {
                        LOG_E("Remove spid file failed! errno: %d", e);
                        /* Discard error */
                    }
                    break;
                }
            }
        }
        if (dp) {
            closedir(dp);
        }
    }
}

//------------------------------------------------------------------------------
mcResult_t mcRegistryCleanupTrustlet(const mcUuid_t *uuid, const mcSpid_t spid)
{
    DIR            *dp;
    struct dirent  *de;
    int             e;

    if (NULL == uuid) {
        LOG_E("mcRegistry cleanupTrustlet(uuid) failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }

    // Delete all TA related data
    string pathname = getTlDataPath(uuid);
    if (NULL != (dp = opendir(pathname.c_str()))) {
        while (NULL != (de = readdir(dp))) {
            if (de->d_name[0] != '.') {
                string dname = pathname + "/" + string (de->d_name);
                LOG_I("delete DT: %s", dname.c_str());
                if (0 != (e = remove(dname.c_str()))) {
                    LOG_E("remove UUID-data %s failed! error: %d", dname.c_str(), e);
                }
            }
        }
        if (dp) {
            closedir(dp);
        }
        LOG_I("delete dir: %s", pathname.c_str());
        if (0 != (e = rmdir(pathname.c_str()))) {
            LOG_E("remove UUID-dir failed! errno: %d", e);
            return MC_DRV_ERR_UNKNOWN;
        }
    }

    string tlBinFilePath = getTlBinFilePath(uuid, MC_REGISTRY_WRITABLE);
    struct stat tmp;
    string tlContFilePath = getTlContFilePath(uuid, spid);;

    if (stat(tlBinFilePath.c_str(), &tmp) == 0) {
        /* Legacy TA */
        LOG_I("Remove TA file %s", tlBinFilePath.c_str());
        if (0 != (e = remove(tlBinFilePath.c_str()))) {
            LOG_E("Remove TA file failed! errno: %d", e);
        }
    } else {
        /* GP TA */
        deleteSPTA(uuid, spid);
    }

    LOG_I("Remove TA container %s", tlContFilePath.c_str());
    if (0 != (e = remove(tlContFilePath.c_str()))) {
        LOG_E("Remove TA container failed! errno: %d", e);
        return MC_DRV_ERR_UNKNOWN;
    }

    return MC_DRV_OK;
}


//------------------------------------------------------------------------------
mcResult_t mcRegistryCleanupSp(mcSpid_t spid)
{
    DIR *dp;
    struct dirent  *de;
    mcResult_t ret;
    mcSoSpCont_t data;
    uint32_t i, len;
    int e;

    if (0 == spid) {
        LOG_E("mcRegistry cleanupSP(SpId) failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    len = sizeof(mcSoSpCont_t);
    ret = mcRegistryReadSp(spid, &data, &len);
    if (MC_DRV_OK != ret || len != sizeof(mcSoSpCont_t)) {
        LOG_E("read SP->UUID aborted! Return code: %d", ret);
        return ret;
    }
    for (i = 0; (i < MC_CONT_CHILDREN_COUNT) && (ret == MC_DRV_OK); i++) {
        if (0 != strncmp((const char *) & (data.cont.children[i]), (const char *)&MC_UUID_FREE, sizeof(mcUuid_t))) {
            ret = mcRegistryCleanupTrustlet(&(data.cont.children[i]), spid);
        }
    }
    if (MC_DRV_OK != ret) {
        LOG_E("delete SP->UUID failed! Return code: %d", ret);
        return ret;
    }

    string pathname = getSpDataPath(spid);

    if (NULL != (dp = opendir(pathname.c_str()))) {
        while (NULL != (de = readdir(dp))) {
            if (de->d_name[0] != '.') {
                string dname = pathname + "/" + string (de->d_name);
                LOG_I("delete DT: %s", dname.c_str());
                if (0 != (e = remove(dname.c_str()))) {
                    LOG_E("remove SPID-data %s failed! error: %d", dname.c_str(), e);
                }
            }
        }
        if (dp) {
            closedir(dp);
        }
        LOG_I("delete dir: %s", pathname.c_str());
        if (0 != (e = rmdir(pathname.c_str()))) {
            LOG_E("remove SPID-dir failed! error: %d", e);
            return MC_DRV_ERR_UNKNOWN;
        }
    }
    string spContFilePath = getSpContFilePath(spid);
    LOG_I("delete Sp: %s", spContFilePath.c_str());
    if (0 != (e = remove(spContFilePath.c_str()))) {
        LOG_E("remove SP failed! error: %d", e);
        return MC_DRV_ERR_UNKNOWN;
    }
    return MC_DRV_OK;
}


//------------------------------------------------------------------------------
mcResult_t mcRegistryCleanupRoot(void)
{
    mcResult_t ret;
    mcSoRootCont_t data;
    uint32_t i, len;
    int e;
    len = sizeof(mcSoRootCont_t);
    ret = mcRegistryReadRoot(&data, &len);
    if (MC_DRV_OK != ret || len != sizeof(mcSoRootCont_t)) {
        LOG_E("read Root aborted! Return code: %d", ret);
        return ret;
    }
    for (i = 0; (i < MC_CONT_CHILDREN_COUNT) && (ret == MC_DRV_OK); i++) {
        mcSpid_t spid = data.cont.children[i];
        if (spid != MC_SPID_FREE) {
            ret = mcRegistryCleanupSp(spid);
            if (MC_DRV_OK != ret) {
                LOG_E("Cleanup SP failed! Return code: %d", ret);
                return ret;
            }
        }
    }

    string rootContFilePath = getRootContFilePath();
    LOG_I("Delete root: %s", rootContFilePath.c_str());
    if (0 != (e = remove(rootContFilePath.c_str()))) {
        LOG_E("Delete root failed! error: %d", e);
        return MC_DRV_ERR_UNKNOWN;
    }
    return MC_DRV_OK;
}

//------------------------------------------------------------------------------
regObject_t *mcRegistryMemGetServiceBlob(mcSpid_t spid, void *trustlet, uint32_t tlSize)
{
    regObject_t *regobj = NULL;

    // Ensure that a UUID is provided.
    if (NULL == trustlet) {
        LOG_E("No trustlet buffer given");
        return NULL;
    }

    mclfIntro_t *pIntro = (mclfIntro_t *)trustlet;
    // Check TL magic value.
    if (pIntro->magic != MC_SERVICE_HEADER_MAGIC_BE) {
        LOG_E("mcRegistryMemGetServiceBlob() failed: wrong header magic value: %d", pIntro->magic);
        return NULL;
    }

    // Get service type.
    mclfHeaderV2_t *pHeader = (mclfHeaderV2_t *)trustlet;
#ifndef NDEBUG
    {
        const char *service_types[] = {
            "illegal", "Driver", "Trustlet", "System Trustlet", "Middleware"
        };
        int serviceType_safe = pHeader->serviceType > SERVICE_TYPE_MIDDLEWARE ? SERVICE_TYPE_ILLEGAL : pHeader->serviceType;
        LOG_I(" Service is a %s (service type %d)", service_types[serviceType_safe], pHeader->serviceType);
    }
#endif

    LOG_I(" Trustlet text %u data %u ", pHeader->text.len, pHeader->data.len);

    // If loadable driver or system trustlet.
    if (pHeader->serviceType == SERVICE_TYPE_DRIVER  || pHeader->serviceType == SERVICE_TYPE_MIDDLEWARE  ||
	pHeader->serviceType == SERVICE_TYPE_SYSTEM_TRUSTLET) {
        // Take trustlet blob 'as is'.
        if (NULL == (regobj = (regObject_t *) (malloc(sizeof(regObject_t) + tlSize)))) {
            LOG_E("mcRegistryMemGetServiceBlob() failed: Out of memory");
            return NULL;
        }
        regobj->len = tlSize;
        regobj->tlStartOffset = 0;
        memcpy((char *)regobj->value, trustlet, tlSize);
        // If user trustlet.
    } else if (pHeader->serviceType == SERVICE_TYPE_SP_TRUSTLET) {
        // Take trustlet blob and append root, sp, and tl container.
        size_t regObjValueSize = tlSize + sizeof(mcBlobLenInfo_t) + 3 * MAX_SO_CONT_SIZE;

        // Prepare registry object.
        if (NULL == (regobj = (regObject_t *) malloc(sizeof(regObject_t) + regObjValueSize))) {
            LOG_E("mcRegistryMemGetServiceBlob() failed: Out of memory");
            return NULL;
        }
        regobj->len = regObjValueSize;
        regobj->tlStartOffset = sizeof(mcBlobLenInfo_t);
        uint8_t *p = regobj->value;

        // Reserve space for the blob length structure
        mcBlobLenInfo_ptr lenInfo = (mcBlobLenInfo_ptr)p;
        lenInfo->magic = MC_TLBLOBLEN_MAGIC;
        p += sizeof(mcBlobLenInfo_t);
        // Fill in trustlet blob after the len info
        memcpy(p, trustlet, tlSize);
        p += tlSize;

        // Final registry object value looks like this:
        //
        //    +---------------+---------------------------+-----------+---------+---------+
        //    | Blob Len Info | TL-Header TL-Code TL-Data | Root Cont | SP Cont | TL Cont |
        //    +---------------+---------------------------+-----------+-------------------+
        //                    /------ Trustlet BLOB ------/
        //
        //    /------------------ regobj->header.len -------------------------------------/

        // start at the end of the trustlet blob
        mcResult_t ret;
        do {
            uint32_t soTltContSize = MAX_SO_CONT_SIZE;
            uint32_t len;

            // Fill in root container.
            len = sizeof(mcSoRootCont_t);
            if (MC_DRV_OK != (ret = mcRegistryReadRoot(p, &len))) {
                break;
            }
            lenInfo->rootContBlobSize = len;
            p += len;

            // Fill in SP container.
            len = sizeof(mcSoSpCont_t);
            if (MC_DRV_OK != (ret = mcRegistryReadSp(spid, p, &len))) {
                break;
            }
            lenInfo->spContBlobSize = len;
            p += len;

            // Fill in TLT Container
            // We know exactly how much space is left in the buffer
            soTltContSize = regObjValueSize - tlSize + sizeof(mcBlobLenInfo_t)
                            - lenInfo->spContBlobSize - lenInfo->rootContBlobSize;
            if (MC_DRV_OK != (ret = mcRegistryReadTrustletCon(&pHeader->uuid, spid, p, &soTltContSize))) {
                break;
            }
            lenInfo->tlContBlobSize = soTltContSize;
            LOG_I(" Trustlet container %u bytes loaded", soTltContSize);
            // Depending on the trustlet container size we decide which structure to use
            // Unfortunate design but it should have to do for now
            if (soTltContSize == sizeof(mcSoTltCont_2_0_t)) {
                LOG_I(" Using 2.0 trustlet container");
            } else if (soTltContSize == sizeof(mcSoTltCont_2_1_t)) {
                LOG_I(" Using 2.1 trustlet container");
            } else {
                LOG_E("Trustlet container has unknown size");
                break;
            }
        } while (false);

        if (MC_DRV_OK != ret) {
            LOG_E("mcRegistryMemGetServiceBlob() failed: Error code: %d", ret);
            free(regobj);
            return NULL;
        }
        // Now we know the sizes for all containers so set the correct size
        regobj->len = sizeof(mcBlobLenInfo_t) + tlSize +
                      lenInfo->rootContBlobSize +
                      lenInfo->spContBlobSize +
                      lenInfo->tlContBlobSize;
        // Any other service type.
    } else {
        LOG_E("mcRegistryMemGetServiceBlob() failed: Unsupported service type %u", pHeader->serviceType);
    }
    return regobj;
}


//------------------------------------------------------------------------------
regObject_t *mcRegistryFileGetServiceBlob(const char *trustlet, mcSpid_t spid)
{
    struct stat sb;
    regObject_t *regobj = NULL;
    void *buffer;

    // Ensure that a file name is provided.
    if (trustlet == NULL) {
        LOG_E("No file given");
        return NULL;
    }

    int fd = open(trustlet, O_RDONLY);
    if (fd == -1) {
        LOG_W("Cannot open %s", trustlet);
        return NULL;
    }

    if (fstat(fd, &sb) == -1) {
        LOG_E("mcRegistryFileGetServiceBlob() failed: Cound't get file size");
        goto error;
    }

    buffer = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (buffer == MAP_FAILED) {
        LOG_E("mcRegistryFileGetServiceBlob(): Failed to map file to memory");
        goto error;
    }

    regobj = mcRegistryMemGetServiceBlob(spid, buffer, sb.st_size);

    // We don't actually care if either of them fails but should still print warnings
    if (munmap(buffer, sb.st_size)) {
        LOG_E("mcRegistryFileGetServiceBlob(): Failed to unmap memory");
    }

error:
    if (close(fd)) {
        LOG_E("mcRegistryFileGetServiceBlob(): Failed to close file %s", trustlet);
    }

    return regobj;
}


//------------------------------------------------------------------------------
regObject_t *mcRegistryGetServiceBlob(const mcUuid_t *uuid, bool isGpUuid)
{
    // Ensure that a UUID is provided.
    if (NULL == uuid) {
        LOG_E("No UUID given");
        return NULL;
    }

    // Open service blob file.
    string tlBinFilePath;
    if (isGpUuid) {
        tlBinFilePath = getTABinFilePath(uuid, MC_REGISTRY_ALL);
    } else {
        tlBinFilePath = getTlBinFilePath(uuid, MC_REGISTRY_ALL);
    }
    LOG_I("Loading %s", tlBinFilePath.c_str());

    mcSpid_t spid = 0;
    if (isGpUuid) {
        string taspidFilePath = getTASpidFilePath(uuid, MC_REGISTRY_ALL);
        int fd = open(taspidFilePath.c_str(), O_RDONLY);
        if (fd == -1) {
            // This can be ok for System TAs
            //LOG_ERRNO("open");
            //LOG_E("Cannot open %s", taspidFilePath.c_str());
            //return NULL;
        } else {
            if (read(fd, &spid, sizeof(mcSpid_t))!=sizeof(mcSpid_t)) {
                close(fd);
                return NULL;
            }
            close(fd);
        }
    }

    return mcRegistryFileGetServiceBlob(tlBinFilePath.c_str(), spid);
}

//------------------------------------------------------------------------------
regObject_t *mcRegistryGetDriverBlob(const char *filename)
{
    regObject_t *regobj = mcRegistryFileGetServiceBlob(filename, 0);

    if (regobj == NULL) {
        LOG_E("mcRegistryGetDriverBlob() failed");
        return NULL;
    }

    // Get service type.
    mclfHeaderV2_t *pHeader = (mclfHeaderV2_t *)regobj->value;

    // If file is not a driver we are not interested
    if ((pHeader->serviceType != SERVICE_TYPE_DRIVER) &&
        (pHeader->serviceType != SERVICE_TYPE_MIDDLEWARE)) {
        LOG_E("mcRegistryGetDriverBlob() failed: Unsupported service type %u", pHeader->serviceType);
        pHeader = NULL;
        free(regobj);
        regobj = NULL;
    }

    return regobj;
}

