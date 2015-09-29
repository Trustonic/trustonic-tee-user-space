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
/** Mobicore Driver Registry.
 *
 * Implements the MobiCore driver registry which maintains trustlets.
 *
 * @file
 * @ingroup MCD_MCDIMPL_DAEMON_REG
 */
#include <stdlib.h>
#include <unistd.h>
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

#include <tee_client_api.h>
#include "uuid_attestation.h"

#include <log.h>

/** Maximum size of a shared object container in bytes. */
#define MAX_SO_CONT_SIZE  (512)

#define MC_REGISTRY_ALL      0
#define MC_REGISTRY_WRITABLE 1

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

static std::vector<std::string> search_paths;
static std::string tb_storage_path;

//------------------------------------------------------------------------------
static std::string byteArrayToString(const void *bytes, size_t elems)
{
    auto cbytes = static_cast<const char *>(bytes);
    char hx[elems * 2 + 1];

    for (size_t i = 0; i < elems; i++) {
        sprintf(&hx[i * 2], "%02x", cbytes[i]);
    }
    return std::string(hx);
}

//------------------------------------------------------------------------------
static std::string uint32ToString(uint32_t value)
{
    char hx[8 + 1];
    snprintf(hx, sizeof(hx), "%08X", value);
    std::string str(hx);
    return std::string(str.rbegin(), str.rend());
}

//------------------------------------------------------------------------------
static bool dirExists(const char *path)
{
    struct stat ss;

    return (path != NULL) && (stat(path, &ss) == 0) && S_ISDIR(ss.st_mode);
}

//------------------------------------------------------------------------------
const std::string& getTbStoragePath()
{
    return tb_storage_path;
}

//------------------------------------------------------------------------------
static std::string getAuthTokenFilePath()
{
    const char *path;
    std::string authTokenPath;

    // First, attempt to use regular auth token path environment variable.
    path = getenv(ENV_MC_AUTH_TOKEN_PATH);
    if (dirExists(path)) {
        LOG_D("getAuthTokenFilePath(): Using MC_AUTH_TOKEN_PATH %s", path);
        authTokenPath = path;
    } else {
        authTokenPath = search_paths[0];
        LOG_D("getAuthTokenFilePath(): Using path %s", authTokenPath.c_str());
    }

    return authTokenPath + "/" + AUTH_TOKEN_FILE_NAME;
}

//------------------------------------------------------------------------------
static std::string getAuthTokenFilePathBackup()
{
    return getAuthTokenFilePath() + AUTH_TOKEN_FILE_NAME_BACKUP_SUFFIX;
}

//------------------------------------------------------------------------------
static std::string getRootContFilePath()
{
    return search_paths[0] + "/" + ROOT_FILE_NAME;
}

//------------------------------------------------------------------------------
static std::string getSpDataPath(mcSpid_t spid)
{
    return search_paths[0] + "/" + uint32ToString(spid);
}

//------------------------------------------------------------------------------
static std::string getSpContFilePath(mcSpid_t spid)
{
    return search_paths[0] + "/" + uint32ToString(spid) + SP_CONT_FILE_EXT;
}

//------------------------------------------------------------------------------
static std::string getTlContFilePath(const mcUuid_t *uuid, const mcSpid_t spid)
{
    return search_paths[0] + "/" + byteArrayToString(uuid, sizeof(*uuid))
           + "." + uint32ToString(spid) + TL_CONT_FILE_EXT;
}

//------------------------------------------------------------------------------
static std::string getTlDataPath(const mcUuid_t *uuid)
{
    return search_paths[0] + "/" + byteArrayToString(uuid, sizeof(*uuid));
}

//------------------------------------------------------------------------------
static std::string getTlDataFilePath(const mcUuid_t *uuid, mcPid_t pid)
{
    return getTlDataPath(uuid) + "/" + uint32ToString(pid.data) + DATA_CONT_FILE_EXT;
}

//------------------------------------------------------------------------------
static std::string getTlBinFilePath(const mcUuid_t *uuid, int registry)
{
    std::string path_rw_registry = search_paths[0] + "/" + byteArrayToString(uuid, sizeof(*uuid)) + TL_BIN_FILE_EXT;

    if ((registry == MC_REGISTRY_ALL) && (search_paths.size() > 1)) {
        std::string path_ro_registry = search_paths[1] + "/" + byteArrayToString(uuid, sizeof(*uuid)) + TL_BIN_FILE_EXT;
        struct stat tmp;
        if (stat(path_ro_registry.c_str(), &tmp) == 0) {
            return path_ro_registry;
        }
    }
    return path_rw_registry;
}

//------------------------------------------------------------------------------
static std::string getTABinFilePath(const mcUuid_t *uuid, int registry)
{
    std::string path_rw_registry = search_paths[0] + "/" + byteArrayToString(uuid, sizeof(*uuid)) + GP_TA_BIN_FILE_EXT;

    if ((registry == MC_REGISTRY_ALL) && (search_paths.size() > 1)) {
        std::string path_ro_registry = search_paths[1] + "/" + byteArrayToString(uuid, sizeof(*uuid)) + GP_TA_BIN_FILE_EXT;
        struct stat tmp;
        if (stat(path_ro_registry.c_str(), &tmp) == 0) {
            return path_ro_registry;
        }
    }
    return path_rw_registry;
}

//------------------------------------------------------------------------------
static std::string getTASpidFilePath(const mcUuid_t *uuid, int registry)
{
    std::string path_rw_registry = search_paths[0] + "/" + byteArrayToString(uuid, sizeof(*uuid)) + GP_TA_SPID_FILE_EXT;

    if ((registry == MC_REGISTRY_ALL) && (search_paths.size() > 1)) {
        std::string path_ro_registry = search_paths[1] + "/" + byteArrayToString(uuid, sizeof(*uuid)) + GP_TA_SPID_FILE_EXT;
        struct stat tmp;
        if (stat(path_ro_registry.c_str(), &tmp) == 0) {
            return path_ro_registry;
        }
    }
    return path_rw_registry;
}

//------------------------------------------------------------------------------
void setSearchPaths(const std::vector<std::string>& paths)
{
    search_paths = paths;
    tb_storage_path = search_paths[0] + "/TbStorage";
}

static inline bool isAllZeros(const unsigned char *so, uint32_t size)
{
    for (uint32_t i = 0; i < size; i++) {
        if (so[i]) {
            return false;
        }
    }

    return true;
}

//------------------------------------------------------------------------------
mcResult_t mcRegistryStoreAuthToken(void *so, size_t size)
{
    int res = 0;
    if (so == NULL || size > 3 * MAX_SO_CONT_SIZE) {
        LOG_E("mcRegistry store So.Soc failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    const std::string &authTokenFilePath = getAuthTokenFilePath();
    LOG_D("store AuthToken: %s", authTokenFilePath.c_str());

    /*
     * This special handling is needed because some of our OEM partners do not
     * supply the AuthToken to Kinibi Sphere. Instead, they retain the
     * AuthToken in a dedicated (reliable) storage area on the device. And in
     * that case, Kinibi Sphere is populated with a all-zero-padded AuthToken.
     * (Obviously the zero-padded AuthToken won't work, so we use that zero-
     * padding as an indicator to trigger the special behaviour.)
     *
     * When Kinibi Sphere supplies the device with the zero-padded AuthToken,
     * the device must look retrieve the AuthToken from its dedicated storage,
     * and use it in place of the zero-padded AuthToken supplied by Kinibi
     * Sphere. Since the AuthToken Backup will already have been retrieved from
     * the dedicated storage area by the time this method is called, using the
     * AuthToken Backup as our source is an acceptable proxy for retrieving it
     * from the dedicated storage area directly.
     *
     * This behaviour is triggered following Root.PA detecting a Factory Reset.
     */
    void *backup = NULL;
    if (isAllZeros((const unsigned char*)so, size)) {
        const std::string &authTokenFilePathBackup = getAuthTokenFilePathBackup();

        LOG_D("AuthToken is all zeros");
        FILE *backupfs = fopen(authTokenFilePathBackup.c_str(), "rb");
        if (backupfs) {
            backup = malloc(size);
            if (backup) {
                size_t readsize = fread(backup, 1, size, backupfs);
                if (readsize == size) {
                    LOG_D("AuthToken reset backup");
                    so = backup;
                } else {
                    LOG_E("AuthToken read size = %zu (%zu)", readsize, size);
                }
            }
            fclose(backupfs);
        } else {
            LOG_W("can't open AuthToken %s", authTokenFilePathBackup.c_str());
        }
    }

    mcResult_t ret = MC_DRV_OK;
    FILE *fs = fopen(authTokenFilePath.c_str(), "wb");
    if (fs == NULL) {
        ret = MC_DRV_ERR_INVALID_DEVICE_FILE;
    } else {
        res = fseek(fs, 0, SEEK_SET);
        if (res != 0) {
            ret = MC_DRV_ERR_INVALID_PARAMETER;
        } else {
            fwrite(so, 1, size, fs);
            if (ferror(fs)) {
                ret = MC_DRV_ERR_OUT_OF_RESOURCES;
            }
        }
        fclose(fs);
    }
    if (ret != MC_DRV_OK) {
        LOG_ERRNO("mcRegistry store So.Soc failed");
    }
    free(backup);

    return ret;
}

//------------------------------------------------------------------------------
mcResult_t mcRegistryReadAuthToken(mcSoAuthTokenCont_t *so)
{
    int res = 0;
    if (NULL == so) {
        LOG_E("mcRegistry read So.Soc failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    const std::string &authTokenFilePath = getAuthTokenFilePath();
    LOG_D("read AuthToken: %s", authTokenFilePath.c_str());

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
    long filesize = ftell(fs);
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
    size_t read_res = fread(so, 1, sizeof(mcSoAuthTokenCont_t), fs);
    if (ferror(fs)) {
        fclose(fs);
        LOG_E("mcRegistry read So.Soc failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    if (read_res<sizeof(mcSoAuthTokenCont_t)) {
        //File is shorter than expected
        if (feof(fs)) {
            LOG_E("%s(): EOF reached: res is %zu, size of mcSoAuthTokenCont_t is %zu", __func__, read_res, sizeof(mcSoAuthTokenCont_t));
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
    const std::string &authTokenFilePath = getAuthTokenFilePathBackup();
    LOG_D("read AuthToken: %s", authTokenFilePath.c_str());

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
    long filesize = ftell(fs);
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
    size_t read_res = fread(so, 1, sizeof(mcSoAuthTokenCont_t), fs);
    if (ferror(fs)) {
        fclose(fs);
        LOG_E("mcRegistry read So.Soc failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    if (read_res<sizeof(mcSoAuthTokenCont_t)) {
        //File is shorter than expected
        if (feof(fs)) {
            LOG_E("%s(): EOF reached: res is %zu, size of mcSoAuthTokenCont_t is %zu", __func__,
            read_res, sizeof(mcSoAuthTokenCont_t));
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
mcResult_t mcRegistryStoreRoot(void *so, size_t size)
{
    int res = 0;
    if (so == NULL || size > 3 * MAX_SO_CONT_SIZE) {
        LOG_E("mcRegistry store So.Root failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }

    const std::string &rootContFilePath = getRootContFilePath();
    LOG_D("store Root: %s", rootContFilePath.c_str());

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
    fwrite(so, 1, size, fs);
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
    const std::string &rootContFilePath = getRootContFilePath();
    size_t readBytes;

    if (so == NULL) {
        LOG_E("mcRegistry read So.Root failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    LOG_D(" Opening %s", rootContFilePath.c_str());

    FILE *fs = fopen(rootContFilePath.c_str(), "rb");
    if (fs==NULL) {
        LOG_W("mcRegistry read So.Root failed: %d", MC_DRV_ERR_INVALID_DEVICE_FILE);
        return MC_DRV_ERR_INVALID_DEVICE_FILE;
    }
    readBytes = fread(so, 1, *size, fs);
    if (ferror(fs)) {
        fclose(fs);
        LOG_E("mcRegistry read So.Root failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    fclose(fs);

    if (readBytes > 0) {
        *size = static_cast<uint32_t>(readBytes);
        return MC_DRV_OK;
    } else {
        LOG_E("mcRegistry read So.Root failed: %d", MC_DRV_ERR_INVALID_DEVICE_FILE);
        return MC_DRV_ERR_INVALID_DEVICE_FILE;
    }
}


//------------------------------------------------------------------------------
mcResult_t mcRegistryStoreSp(mcSpid_t spid, void *so, size_t size)
{
    int res = 0;
    if ((spid == 0) || (so == NULL) || size > 3 * MAX_SO_CONT_SIZE) {
        LOG_E("mcRegistry store So.Sp(SpId) failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }

    const std::string &spContFilePath = getSpContFilePath(spid);
    LOG_D("store SP: %s", spContFilePath.c_str());

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
    fwrite(so, 1, size, fs);
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
    const std::string &spContFilePath = getSpContFilePath(spid);
    size_t readBytes;
    if ((spid == 0) || (so == NULL)) {
        LOG_E("mcRegistry read So.Sp(SpId=0x%x) failed", spid);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    LOG_D(" Reading %s", spContFilePath.c_str());

    FILE *fs = fopen(spContFilePath.c_str(), "rb");
    if (fs==NULL) {
        LOG_E("mcRegistry read So.Sp(SpId) failed: %d", MC_DRV_ERR_INVALID_DEVICE_FILE);
        return MC_DRV_ERR_INVALID_DEVICE_FILE;
    }
    readBytes = fread(so, 1, *size, fs);
    if (ferror(fs)) {
        fclose(fs);
        LOG_E("mcRegistry read So.Sp(SpId) failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    fclose(fs);

    if (readBytes > 0) {
        *size = static_cast<uint32_t>(readBytes);
        return MC_DRV_OK;
    } else {
        LOG_E("mcRegistry read So.Sp(SpId) failed: %d", MC_DRV_ERR_INVALID_DEVICE_FILE);
        return MC_DRV_ERR_INVALID_DEVICE_FILE;
    }
}


//------------------------------------------------------------------------------
mcResult_t mcRegistryStoreTrustletCon(const mcUuid_t *uuid, const mcSpid_t spid, void *so, size_t size)
{
    int res = 0;
    if ((uuid == NULL) || (so == NULL) || size > 3 * MAX_SO_CONT_SIZE) {
        LOG_E("mcRegistry store So.TrustletCont(uuid) failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }

    const std::string &tlContFilePath = getTlContFilePath(uuid, spid);
    LOG_D("store TLc: %s", tlContFilePath.c_str());

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
    fwrite(so, 1, size, fs);
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
    const void *pValueUnaligned
)
{
    auto p = static_cast<const uint8_t *>(pValueUnaligned);
    uint32_t val = p[3] | (p[2] << 8) | (p[1] << 16) | (p[0] << 24);
    return val;
}

mcResult_t mcRegistryStoreTABlob(mcSpid_t spid, const void *blob, size_t size)
{
    int res = 0;
    LOG_D("mcRegistryStoreTABlob started");

    // Check blob size
    if (size < sizeof(mclfHeaderV24_t)) {
        LOG_E("RegistryStoreTABlob failed - TA blob length is less then header size");
        return MC_DRV_ERR_INVALID_PARAMETER;
    }

    auto header24 = reinterpret_cast<const mclfHeaderV24_t *>(blob);
    auto header20 = reinterpret_cast<const mclfHeaderV2_t *>(blob);

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

    mcUuid_t uuid;
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

        auto pUa = reinterpret_cast<const uuid_attestation*>(&static_cast<const uint8_t*>(blob)[header24->attestationOffset]);
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
    const std::string taBinFilePath = getTABinFilePath(&uuid, MC_REGISTRY_WRITABLE);

    LOG_D("Store TA blob at: %s", taBinFilePath.c_str());

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
        const std::string taspidFilePath = getTASpidFilePath(&uuid, MC_REGISTRY_WRITABLE);

        LOG_D("Store spid file at: %s", taspidFilePath.c_str());

        FILE *fs = fopen(taspidFilePath.c_str(), "wb");
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
    const std::string &tlContFilePath = getTlContFilePath(uuid, spid);
    LOG_D("read TLc: %s", tlContFilePath.c_str());

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
    readBytes = fread(so, 1, *size, fs);
    if (ferror(fs)) {
        fclose(fs);
        LOG_E("mcRegistry read So.TrustletCont(uuid) failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    fclose(fs);

    if (readBytes > 0) {
        *size = static_cast<uint32_t>(readBytes);
        return MC_DRV_OK;
    } else {
        LOG_E("mcRegistry read So.TrustletCont(uuid) failed: %d", MC_DRV_ERR_INVALID_DEVICE_FILE);
        return MC_DRV_ERR_INVALID_DEVICE_FILE;
    }
}


//------------------------------------------------------------------------------
mcResult_t mcRegistryStoreData(void *so, size_t size)
{
    mcSoDataCont_t *dataCont = reinterpret_cast<mcSoDataCont_t*>(so);
    int res = 0;

    if (dataCont == NULL || size != sizeof(mcSoDataCont_t)) {
        LOG_E("mcRegistry store So.Data failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    std::string pathname, filename;

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

    LOG_D("store DT: %s", filename.c_str());

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
    fwrite(dataCont, 1, MC_SO_SIZE(dataCont->soHeader.plainLen, dataCont->soHeader.encryptedLen), fs);
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
mcResult_t mcRegistryReadData(uint32_t context, const mcCid_t *cid, mcPid_t,
                              mcSoDataCont_t *so, uint32_t maxLen)
{
    int res = 0;

    if ((NULL == cid) || (NULL == so)) {
        LOG_E("mcRegistry read So.Data failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    std::string filename;
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
    LOG_D("read DT: %s", filename.c_str());

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
    long filesize = ftell(fs);
    if (static_cast<long>(maxLen) < filesize) {
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
    char *p = reinterpret_cast<char*>(so);
    size_t read_res = fread(p, 1, sizeof(mcSoHeader_t), fs);
    if (ferror(fs)) {
        fclose(fs);
        LOG_E("mcRegistry read So.Data(cid/pid) failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    if (read_res<sizeof(mcSoHeader_t)) {
        //File is shorter than expected
        if (feof(fs)) {
            LOG_E("%s(): EOF reached: res is %zu, size of mcSoHeader_t is %zu", __func__, read_res, sizeof(mcSoHeader_t));
        }
        fclose(fs);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    p += sizeof(mcSoHeader_t);
    read_res = fread(p, 1, MC_SO_SIZE(so->soHeader.plainLen,
                so->soHeader.encryptedLen)
                - sizeof(mcSoHeader_t), fs);
    if (ferror(fs)) {
        fclose(fs);
        LOG_E("mcRegistry read So.Data(cid/pid) failed: %d", MC_DRV_ERR_INVALID_PARAMETER);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    if (read_res<(MC_SO_SIZE(so->soHeader.plainLen, so->soHeader.encryptedLen) - sizeof(mcSoHeader_t))) {
        //File is shorter than expected
        if (feof(fs)) {
            LOG_E("%s(): EOF reached: res is %zu, size of secure object is %zu", __func__, read_res,
            MC_SO_SIZE(so->soHeader.plainLen, so->soHeader.encryptedLen) - sizeof(mcSoHeader_t));
        }
        fclose(fs);
        return MC_DRV_ERR_INVALID_PARAMETER;
    }
    fclose(fs);

    return MC_DRV_OK;
}


//------------------------------------------------------------------------------
static uint32_t getFileContent(
    const char *pPath,
    uint8_t **ppContent)
{
    FILE   *pStream;
    long    filesize;
    uint8_t *content = NULL;
    size_t res = 0;

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
    content = new uint8_t[filesize];
    if (content == NULL) {
        LOG_E("Error: Cannot read file: Out of memory.");
        goto error;
    }

    /* Read data from the file into the buffer */
    res = fread(content, filesize, 1, pStream);
    if (ferror(pStream)) {
        LOG_E("Error: Cannot read file: %s.", pPath);
        goto error;
    }
    if (res < 1) {
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
    return static_cast<uint32_t>(filesize);

error:
    if (content != NULL) {
        delete[] content;
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
        delete[] pTAData;
        LOG_E("getFileContent failed - TA length is less than header size");
        return false;
    }

    mclfHeaderV2_t *header20 = reinterpret_cast<mclfHeaderV2_t *>(pTAData);

    // Check header version
    if (header20->intro.version < MC_MAKE_VERSION(2, 4)) {
        delete[] pTAData;
        LOG_E("mcCheckUuid() - TA blob header version is less than 2.4");
        return false;
    }

    // Check uuid
    if (memcmp(uuid, &header20->uuid, sizeof(mcUuid_t)) == 0) {
        res = true;
    } else {
        res = false;
    }

    delete[] pTAData;

    return res;
}

//this function deletes all the files owned by a GP TA and stored in the tbase secure storage dir.
//then it deletes GP TA folder.
static int CleanupGPTAStorage(const char *uuid)
{
	DIR            *dp;
	struct dirent  *de;
	int             e;
	std::string TAPath = getTbStoragePath() + "/" + uuid;
	if (NULL != (dp = opendir(TAPath.c_str()))) {
		while (NULL != (de = readdir(dp))) {
			if (de->d_name[0] != '.') {
				std::string dname = TAPath + "/" + std::string(de->d_name);
				LOG_D("delete DT: %s", dname.c_str());
				if (0 != (e = remove(dname.c_str()))) {
					LOG_E("remove UUID-files %s failed! error: %d", dname.c_str(), e);
				}
			}
		}
		if (dp) {
			closedir(dp);
		}
		LOG_D("delete dir: %s", TAPath.c_str());
		if (0 != (e = rmdir(TAPath.c_str()))) {
			LOG_E("remove UUID-dir failed! errno: %d", e);
			return e;
		}
	}
	return MC_DRV_OK;
}


mcResult_t mcRegistryCleanupGPTAStorage(const mcUuid_t *uuid)
{
    return CleanupGPTAStorage(byteArrayToString(uuid, sizeof(*uuid)).c_str());
}

static void deleteSPTA(const mcUuid_t *uuid, const mcSpid_t spid)
{
    DIR            *dp;
    struct dirent  *de;
    int             e;

    // Delete TABIN and SPID files - we loop searching required spid file
    if (NULL != (dp = opendir(search_paths[0].c_str()))) {
        while (NULL != (de = readdir(dp))) {
            std::string spidFile;
            std::string tabinFile;
            std::string tabinUuid;
            size_t pch_dot, pch_slash;
            spidFile = search_paths[0] + "/" + std::string(de->d_name);
            pch_dot = spidFile.find_last_of('.');
            if (pch_dot == std::string::npos) continue;
            pch_slash = spidFile.find_last_of('/');
            if ((pch_slash != std::string::npos) && (pch_slash > pch_dot))  continue;
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
                	LOG_D("Remove TA storage %s", tabinUuid.c_str());
                	if (0 != (e = CleanupGPTAStorage(tabinUuid.c_str()))){
                		LOG_E("Remove TA storage failed! errno: %d", e);
                		/* Discard error */
                	}
                	LOG_D("Remove TA file %s", tabinFile.c_str());
                    if (0 != (e = remove(tabinFile.c_str()))) {
                        LOG_E("Remove TA file failed! errno: %d", e);
                        /* Discard error */
                    }
                    LOG_D("Remove spid file %s", spidFile.c_str());
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
    std::string pathname = getTlDataPath(uuid);
    if (NULL != (dp = opendir(pathname.c_str()))) {
        while (NULL != (de = readdir(dp))) {
            if (de->d_name[0] != '.') {
                std::string dname = pathname + "/" + std::string(de->d_name);
                LOG_D("delete DT: %s", dname.c_str());
                if (0 != (e = remove(dname.c_str()))) {
                    LOG_E("remove UUID-data %s failed! error: %d", dname.c_str(), e);
                }
            }
        }
        if (dp) {
            closedir(dp);
        }
        LOG_D("delete dir: %s", pathname.c_str());
        if (0 != (e = rmdir(pathname.c_str()))) {
            LOG_E("remove UUID-dir failed! errno: %d", e);
            return MC_DRV_ERR_UNKNOWN;
        }
    }

    std::string tlBinFilePath = getTlBinFilePath(uuid, MC_REGISTRY_WRITABLE);
    struct stat tmp;
    std::string tlContFilePath = getTlContFilePath(uuid, spid);;

    if (stat(tlBinFilePath.c_str(), &tmp) == 0) {
        /* Legacy TA */
        LOG_D("Remove TA file %s", tlBinFilePath.c_str());
        if (0 != (e = remove(tlBinFilePath.c_str()))) {
            LOG_E("Remove TA file failed! errno: %d", e);
        }
    } else {
        /* GP TA */
        deleteSPTA(uuid, spid);
    }

    LOG_D("Remove TA container %s", tlContFilePath.c_str());
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
        if (0 != strncmp(reinterpret_cast<const char*>(&data.cont.children[i]),
                         reinterpret_cast<const char*>(&MC_UUID_FREE),
                         sizeof(mcUuid_t))) {
            ret = mcRegistryCleanupTrustlet(&(data.cont.children[i]), spid);
        }
    }
    if (MC_DRV_OK != ret) {
        LOG_E("delete SP->UUID failed! Return code: %d", ret);
        return ret;
    }

    std::string pathname = getSpDataPath(spid);

    if (NULL != (dp = opendir(pathname.c_str()))) {
        while (NULL != (de = readdir(dp))) {
            if (de->d_name[0] != '.') {
                std::string dname = pathname + "/" + std::string(de->d_name);
                LOG_D("delete DT: %s", dname.c_str());
                if (0 != (e = remove(dname.c_str()))) {
                    LOG_E("remove SPID-data %s failed! error: %d", dname.c_str(), e);
                }
            }
        }
        if (dp) {
            closedir(dp);
        }
        LOG_D("delete dir: %s", pathname.c_str());
        if (0 != (e = rmdir(pathname.c_str()))) {
            LOG_E("remove SPID-dir failed! error: %d", e);
            return MC_DRV_ERR_UNKNOWN;
        }
    }
    std::string spContFilePath = getSpContFilePath(spid);
    LOG_D("delete Sp: %s", spContFilePath.c_str());
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

    std::string rootContFilePath = getRootContFilePath();
    LOG_D("Delete root: %s", rootContFilePath.c_str());
    if (0 != (e = remove(rootContFilePath.c_str()))) {
        LOG_E("Delete root failed! error: %d", e);
        return MC_DRV_ERR_UNKNOWN;
    }
    return MC_DRV_OK;
}

//------------------------------------------------------------------------------
mcResult_t mcRegistryGetTrustletInfo(const mcUuid_t *uuid, bool isGpUuid, mcSpid_t *spid, std::string& path)
{
    // Ensure that a UUID is provided.
    if (NULL == uuid) {
        LOG_E("No UUID given");
        return MC_DRV_ERR_INVALID_PARAMETER;
    }

    if (isGpUuid) {
        path = getTABinFilePath(uuid, MC_REGISTRY_ALL);
    } else {
        path = getTlBinFilePath(uuid, MC_REGISTRY_ALL);
    }

    *spid = 0;
    if (isGpUuid) {
        std::string taspidFilePath = getTASpidFilePath(uuid, MC_REGISTRY_ALL);
        int fd = open(taspidFilePath.c_str(), O_RDONLY);
        if (fd >= 0) {
            bool failed = read(fd, spid, sizeof(*spid)) != sizeof(*spid);
            close(fd);
            if (failed) {
                LOG_E("Could not read SPID (%d)", errno);
                return MC_DRV_ERR_TRUSTLET_NOT_FOUND;
            }
        }
    }

    LOG_D("Returning %s", path.c_str());
    return MC_DRV_OK;
}
