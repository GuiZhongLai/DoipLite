#pragma once

#include "doiplite/doiplite.h"

#include <cstdint>
#include <string>
#include <vector>

// Example/application-layer station configuration.
// Not part of the DoipLite core library; parses key=value files only.

namespace doiplite_examples {

struct StationConfig
{
    // Discover destination. Empty broadcast => library limited broadcast.
    std::string broadcast;
    uint16_t udpPort = 0; // 0 => DOIPLITE_DEFAULT_UDP_PORT
    bool haveUdpPort = false;
    uint32_t discoverTimeoutMs = DOIPLITE_DEFAULT_TIMEOUT_MS;
    bool haveDiscoverTimeout = false;

    // Identity selectors from DoipLiteVehicle discovery fields.
    // At least one must be set; matching uses AND across all set selectors.
    uint16_t selectLogicalAddress = 0;
    bool haveSelectLogicalAddress = false;
    std::string selectVin; // compared to vehicle.vin (up to 17 chars)
    bool haveSelectVin = false;

    // Tester / activation / timing
    uint16_t testerAddress = 0;
    bool haveTesterAddress = false;
    uint16_t activationType = 0x00;
    bool haveActivationType = false;
    uint32_t timeoutMs = DOIPLITE_DEFAULT_TIMEOUT_MS;
    bool haveTimeout = false;
    uint32_t p2Ms = 0;
    uint32_t p2StarMs = 0;
    bool haveP2 = false;

    // Read version (0x22)
    uint16_t versionDid = 0;
    bool haveVersionDid = false;

    // Flash job (maps to DoipLiteFlashJob)
    uint8_t flashSessionType = 0;
    bool haveFlashSessionType = false;
    // haveSecurity: if true, FlashPoll fails with NOT_IMPLEMENTED (0x27 not in core).
    bool haveSecurity = false;
    uint8_t securityLevel = 0;
    bool haveSecurityLevel = false;

    uint8_t flashEraseRoutineSubFunction = 0;
    uint16_t flashEraseRoutineId = 0;
    std::vector<uint8_t> flashEraseRoutineOption;
    bool haveFlashEraseSubFunction = false;
    bool haveFlashEraseRoutineId = false;
    bool haveFlashEraseRoutineOption = false;

    uint8_t downloadDataFormatId = 0;
    bool haveDownloadDataFormatId = false;
    uint8_t downloadAlfid = 0;
    bool haveDownloadAlfid = false;
    uint64_t downloadMemoryAddress = 0;
    bool haveDownloadMemoryAddress = false;
    uint64_t downloadMemorySize = 0;
    bool haveDownloadMemorySize = false;

    std::string packagePath;
    uint32_t flashMaxBlockDataCap = 0;
    bool haveFlashMaxBlockDataCap = false;
    std::vector<uint8_t> transferExitParam;
    bool haveTransferExitParam = false;

    uint8_t flashResetType = 0;
    bool haveFlashResetType = false;

    // Optional flash keep-alive (0x3E). Library has no defaults: both
    // interval (>0) and sub-function must be supplied explicitly to enable.
    uint32_t flashKeepAliveIntervalMs = 0;
    bool haveFlashKeepAliveInterval = false;
    uint8_t flashKeepAliveSubFunction = 0;
    bool haveFlashKeepAliveSubFunction = false;
};

enum class ConfigLoadStatus
{
    Ok = 0,
    IoError,
    ParseError
};

enum class DeviceSelectStatus
{
    Ok = 0,
    NoSelectorConfigured,
    ZeroMatches,
    MultipleMatches
};

ConfigLoadStatus LoadStationConfig(const char *path, StationConfig *out, std::string *err);

// Returns true when at least one identity selector is present.
bool HasIdentitySelector(const StationConfig &cfg);

// True if vehicle matches every configured selector (AND).
bool VehicleMatchesSelectors(const DoipLiteVehicle &vehicle, const StationConfig &cfg);

// Filter discovered vehicles. Never picks "first" without selectors.
// On Ok, *outIndex is the unique match index into vehicles[0..count).
DeviceSelectStatus SelectUniqueVehicle(const DoipLiteVehicle *vehicles,
                                       uint32_t count,
                                       const StationConfig &cfg,
                                       uint32_t *outIndex,
                                       uint32_t *matchCount,
                                       std::string *err);

// Validate fields required before any discover/network for read-version example.
bool ValidateReadVersionConfig(const StationConfig &cfg, std::string *err);

// Validate flash job parameters (no network). Dry-run and execute both use this.
bool ValidateFlashConfig(const StationConfig &cfg, std::string *err);

// Fill DoipLiteFlashJob from config. Pointers alias cfg-owned storage
// (packagePath / option / exit param); cfg must outlive the job use.
void FillFlashJob(const StationConfig &cfg, DoipLiteFlashJob *job);

const char *FlashPhaseName(DoipLiteFlashPhase phase);

} // namespace doiplite_examples
