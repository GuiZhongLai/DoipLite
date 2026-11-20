#include "station_config.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>

namespace doiplite_examples {
namespace {

std::string trim(std::string s)
{
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos)
    {
        return {};
    }
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

bool parseUnsignedMax(const std::string &text, unsigned long long max, unsigned long long *out)
{
    if (!out || text.empty())
    {
        return false;
    }
    char *end = nullptr;
    const bool hex = text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
    errno = 0;
    const unsigned long long value = std::strtoull(text.c_str(), &end, hex ? 16 : 10);
    if (errno == ERANGE || !end || end == text.c_str() || *end != '\0')
    {
        return false;
    }
    if (value > max)
    {
        return false;
    }
    *out = value;
    return true;
}

bool parseU64(const std::string &text, uint64_t *out)
{
    unsigned long long value = 0;
    if (!out || !parseUnsignedMax(text, std::numeric_limits<uint64_t>::max(), &value))
    {
        return false;
    }
    *out = static_cast<uint64_t>(value);
    return true;
}

bool parseU32(const std::string &text, uint32_t *out)
{
    unsigned long long value = 0;
    if (!out || !parseUnsignedMax(text, 0xFFFFFFFFull, &value))
    {
        return false;
    }
    *out = static_cast<uint32_t>(value);
    return true;
}

bool parseU16(const std::string &text, uint16_t *out)
{
    unsigned long long value = 0;
    if (!out || !parseUnsignedMax(text, 0xFFFFull, &value))
    {
        return false;
    }
    *out = static_cast<uint16_t>(value);
    return true;
}

bool parseU8(const std::string &text, uint8_t *out)
{
    unsigned long long value = 0;
    if (!out || !parseUnsignedMax(text, 0xFFull, &value))
    {
        return false;
    }
    *out = static_cast<uint8_t>(value);
    return true;
}

bool parseBool01(const std::string &text, bool *out)
{
    if (!out)
    {
        return false;
    }
    if (text == "0" || text == "false" || text == "no")
    {
        *out = false;
        return true;
    }
    if (text == "1" || text == "true" || text == "yes")
    {
        *out = true;
        return true;
    }
    return false;
}

int hexNibble(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    return -1;
}

bool parseHexBytes(const std::string &text, std::vector<uint8_t> *out)
{
    if (!out)
    {
        return false;
    }
    out->clear();
    std::string hex;
    hex.reserve(text.size());
    for (char c : text)
    {
        if (c == ' ' || c == '\t' || c == ':' || c == '-')
        {
            continue;
        }
        hex.push_back(c);
    }
    if (hex.empty() || (hex.size() % 2) != 0)
    {
        return false;
    }
    for (std::size_t i = 0; i < hex.size(); i += 2)
    {
        const int hi = hexNibble(hex[i]);
        const int lo = hexNibble(hex[i + 1]);
        if (hi < 0 || lo < 0)
        {
            out->clear();
            return false;
        }
        out->push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return true;
}

bool vinEquals(const uint8_t vehicleVin[18], const std::string &want)
{
    if (want.empty() || want.size() > 17)
    {
        return false;
    }
    for (std::size_t i = 0; i < want.size(); ++i)
    {
        if (static_cast<unsigned char>(want[i]) != vehicleVin[i])
        {
            return false;
        }
    }
    // If want is shorter than 17, require remaining vehicle bytes to be 0.
    for (std::size_t i = want.size(); i < 17; ++i)
    {
        if (vehicleVin[i] != 0)
        {
            return false;
        }
    }
    return true;
}

bool applyKey(StationConfig *cfg, const std::string &key, const std::string &value, std::string *err)
{
    auto fail = [&](const char *msg) -> bool {
        if (err)
        {
            *err = msg;
        }
        return false;
    };

    if (key == "broadcast" || key == "discover_dest_ip")
    {
        cfg->broadcast = value;
        return true;
    }
    if (key == "port" || key == "udp_port" || key == "discover_udp_port")
    {
        if (value.empty() || !parseU16(value, &cfg->udpPort) || cfg->udpPort == 0)
        {
            return fail("invalid discover udp port");
        }
        cfg->haveUdpPort = true;
        return true;
    }
    if (key == "discover_timeout_ms")
    {
        if (value.empty() || !parseU32(value, &cfg->discoverTimeoutMs))
        {
            return fail("invalid discover_timeout_ms");
        }
        cfg->haveDiscoverTimeout = true;
        return true;
    }
    if (key == "select_logical_address" || key == "entity_logical_address")
    {
        if (value.empty() || !parseU16(value, &cfg->selectLogicalAddress))
        {
            return fail("invalid select_logical_address");
        }
        cfg->haveSelectLogicalAddress = true;
        return true;
    }
    if (key == "select_vin" || key == "entity_vin")
    {
        if (value.empty() || value.size() > 17)
        {
            return fail("invalid select_vin (1..17 characters)");
        }
        cfg->selectVin = value;
        cfg->haveSelectVin = true;
        return true;
    }
    if (key == "tester_address")
    {
        if (value.empty() || !parseU16(value, &cfg->testerAddress))
        {
            return fail("invalid tester_address");
        }
        cfg->haveTesterAddress = true;
        return true;
    }
    if (key == "activation_type")
    {
        // Public API parameter is uint16_t, but the on-wire field is 8-bit
        // (session uses activationType & 0xff). Reject values that would truncate.
        uint8_t type8 = 0;
        if (value.empty() || !parseU8(value, &type8))
        {
            return fail("invalid activation_type (must be 0..0xFF)");
        }
        cfg->activationType = type8;
        cfg->haveActivationType = true;
        return true;
    }
    if (key == "timeout_ms")
    {
        if (value.empty() || !parseU32(value, &cfg->timeoutMs))
        {
            return fail("invalid timeout_ms");
        }
        cfg->haveTimeout = true;
        return true;
    }
    if (key == "p2_ms")
    {
        if (value.empty() || !parseU32(value, &cfg->p2Ms))
        {
            return fail("invalid p2_ms");
        }
        cfg->haveP2 = true;
        return true;
    }
    if (key == "p2_star_ms")
    {
        if (value.empty() || !parseU32(value, &cfg->p2StarMs))
        {
            return fail("invalid p2_star_ms");
        }
        cfg->haveP2 = true;
        return true;
    }
    if (key == "version_did")
    {
        if (value.empty() || !parseU16(value, &cfg->versionDid))
        {
            return fail("invalid version_did");
        }
        cfg->haveVersionDid = true;
        return true;
    }
    if (key == "flash_session_type")
    {
        if (value.empty() || !parseU8(value, &cfg->flashSessionType))
        {
            return fail("invalid flash_session_type");
        }
        cfg->haveFlashSessionType = true;
        return true;
    }
    if (key == "flash_require_security" || key == "flash_have_security")
    {
        if (value.empty() || !parseBool01(value, &cfg->haveSecurity))
        {
            return fail("invalid flash_require_security (0|1)");
        }
        return true;
    }
    if (key == "flash_security_level")
    {
        if (value.empty() || !parseU8(value, &cfg->securityLevel))
        {
            return fail("invalid flash_security_level");
        }
        cfg->haveSecurityLevel = true;
        return true;
    }
    if (key == "flash_erase_routine_sub_function")
    {
        if (value.empty() || !parseU8(value, &cfg->flashEraseRoutineSubFunction))
        {
            return fail("invalid flash_erase_routine_sub_function");
        }
        cfg->haveFlashEraseSubFunction = true;
        return true;
    }
    if (key == "flash_erase_routine_id")
    {
        if (value.empty() || !parseU16(value, &cfg->flashEraseRoutineId))
        {
            return fail("invalid flash_erase_routine_id");
        }
        cfg->haveFlashEraseRoutineId = true;
        return true;
    }
    if (key == "flash_erase_routine_option_hex")
    {
        if (!parseHexBytes(value, &cfg->flashEraseRoutineOption))
        {
            return fail("invalid flash_erase_routine_option_hex");
        }
        cfg->haveFlashEraseRoutineOption = true;
        return true;
    }
    if (key == "download_data_format_id")
    {
        if (value.empty() || !parseU8(value, &cfg->downloadDataFormatId))
        {
            return fail("invalid download_data_format_id");
        }
        cfg->haveDownloadDataFormatId = true;
        return true;
    }
    if (key == "download_alfid")
    {
        if (value.empty() || !parseU8(value, &cfg->downloadAlfid))
        {
            return fail("invalid download_alfid");
        }
        cfg->haveDownloadAlfid = true;
        return true;
    }
    if (key == "download_memory_address")
    {
        if (value.empty() || !parseU64(value, &cfg->downloadMemoryAddress))
        {
            return fail("invalid download_memory_address");
        }
        cfg->haveDownloadMemoryAddress = true;
        return true;
    }
    if (key == "download_memory_size")
    {
        if (value.empty() || !parseU64(value, &cfg->downloadMemorySize))
        {
            return fail("invalid download_memory_size");
        }
        cfg->haveDownloadMemorySize = true;
        return true;
    }
    if (key == "package_path")
    {
        cfg->packagePath = value;
        return true;
    }
    if (key == "flash_max_block_data_cap")
    {
        if (value.empty() || !parseU32(value, &cfg->flashMaxBlockDataCap))
        {
            return fail("invalid flash_max_block_data_cap");
        }
        cfg->haveFlashMaxBlockDataCap = true;
        return true;
    }
    if (key == "transfer_exit_param_hex")
    {
        if (!parseHexBytes(value, &cfg->transferExitParam))
        {
            return fail("invalid transfer_exit_param_hex");
        }
        cfg->haveTransferExitParam = true;
        return true;
    }
    if (key == "flash_reset_type")
    {
        if (value.empty() || !parseU8(value, &cfg->flashResetType))
        {
            return fail("invalid flash_reset_type");
        }
        cfg->haveFlashResetType = true;
        return true;
    }
    if (key == "flash_keep_alive_interval_ms")
    {
        if (value.empty() || !parseU32(value, &cfg->flashKeepAliveIntervalMs))
        {
            return fail("invalid flash_keep_alive_interval_ms");
        }
        cfg->haveFlashKeepAliveInterval = true;
        return true;
    }
    if (key == "flash_keep_alive_sub_function")
    {
        if (value.empty() || !parseU8(value, &cfg->flashKeepAliveSubFunction))
        {
            return fail("invalid flash_keep_alive_sub_function");
        }
        cfg->haveFlashKeepAliveSubFunction = true;
        return true;
    }

    // Explicitly reject unknown keys so typos do not silently skip.
    if (err)
    {
        *err = "unknown config key: " + key;
    }
    return false;
}

} // namespace

ConfigLoadStatus LoadStationConfig(const char *path, StationConfig *out, std::string *err)
{
    if (!path || !out)
    {
        if (err)
        {
            *err = "null path or output";
        }
        return ConfigLoadStatus::ParseError;
    }

    std::ifstream in(path);
    if (!in)
    {
        if (err)
        {
            *err = std::string("cannot open config: ") + path;
        }
        return ConfigLoadStatus::IoError;
    }

    StationConfig cfg;
    std::string line;
    unsigned lineNo = 0;
    while (std::getline(in, line))
    {
        ++lineNo;
        std::string t = trim(line);
        if (t.empty() || t[0] == '#')
        {
            continue;
        }
        const auto eq = t.find('=');
        if (eq == std::string::npos)
        {
            if (err)
            {
                *err = "line " + std::to_string(lineNo) + ": expected key=value";
            }
            return ConfigLoadStatus::ParseError;
        }
        const std::string key = trim(t.substr(0, eq));
        const std::string value = trim(t.substr(eq + 1));
        if (key.empty())
        {
            if (err)
            {
                *err = "line " + std::to_string(lineNo) + ": empty key";
            }
            return ConfigLoadStatus::ParseError;
        }
        std::string keyErr;
        if (!applyKey(&cfg, key, value, &keyErr))
        {
            if (err)
            {
                *err = "line " + std::to_string(lineNo) + ": " + keyErr;
            }
            return ConfigLoadStatus::ParseError;
        }
    }

    *out = std::move(cfg);
    return ConfigLoadStatus::Ok;
}

bool HasIdentitySelector(const StationConfig &cfg)
{
    return cfg.haveSelectLogicalAddress || cfg.haveSelectVin;
}

bool VehicleMatchesSelectors(const DoipLiteVehicle &vehicle, const StationConfig &cfg)
{
    if (!HasIdentitySelector(cfg))
    {
        return false;
    }
    if (cfg.haveSelectLogicalAddress &&
        vehicle.logicalAddress != cfg.selectLogicalAddress)
    {
        return false;
    }
    if (cfg.haveSelectVin && !vinEquals(vehicle.vin, cfg.selectVin))
    {
        return false;
    }
    return true;
}

DeviceSelectStatus SelectUniqueVehicle(const DoipLiteVehicle *vehicles,
                                       uint32_t count,
                                       const StationConfig &cfg,
                                       uint32_t *outIndex,
                                       uint32_t *matchCount,
                                       std::string *err)
{
    if (matchCount)
    {
        *matchCount = 0;
    }
    if (!HasIdentitySelector(cfg))
    {
        if (err)
        {
            *err = "no identity selector configured "
                   "(set select_logical_address and/or select_vin); "
                   "refusing to pick the first discovered device";
        }
        return DeviceSelectStatus::NoSelectorConfigured;
    }
    if (!vehicles && count > 0)
    {
        if (err)
        {
            *err = "null vehicle list";
        }
        return DeviceSelectStatus::ZeroMatches;
    }

    uint32_t matches = 0;
    uint32_t first = 0;
    for (uint32_t i = 0; i < count; ++i)
    {
        if (VehicleMatchesSelectors(vehicles[i], cfg))
        {
            if (matches == 0)
            {
                first = i;
            }
            ++matches;
        }
    }
    if (matchCount)
    {
        *matchCount = matches;
    }
    if (matches == 0)
    {
        if (err)
        {
            *err = "no discovered device matches configured identity selectors";
        }
        return DeviceSelectStatus::ZeroMatches;
    }
    if (matches > 1)
    {
        if (err)
        {
            *err = "multiple discovered devices match identity selectors "
                   "(narrow select_logical_address / select_vin)";
        }
        return DeviceSelectStatus::MultipleMatches;
    }
    if (outIndex)
    {
        *outIndex = first;
    }
    return DeviceSelectStatus::Ok;
}

bool ValidateReadVersionConfig(const StationConfig &cfg, std::string *err)
{
    if (!HasIdentitySelector(cfg))
    {
        if (err)
        {
            *err = "require select_logical_address and/or select_vin";
        }
        return false;
    }
    if (!cfg.haveTesterAddress)
    {
        if (err)
        {
            *err = "missing required tester_address";
        }
        return false;
    }
    if (!cfg.haveVersionDid)
    {
        if (err)
        {
            *err = "missing required version_did";
        }
        return false;
    }
    return true;
}

bool ValidateFlashConfig(const StationConfig &cfg, std::string *err)
{
    if (!HasIdentitySelector(cfg))
    {
        if (err)
        {
            *err = "require select_logical_address and/or select_vin";
        }
        return false;
    }
    if (!cfg.haveTesterAddress)
    {
        if (err)
        {
            *err = "missing required tester_address";
        }
        return false;
    }
    if (!cfg.haveDownloadAlfid)
    {
        if (err)
        {
            *err = "missing required download_alfid";
        }
        return false;
    }
    if (!cfg.haveDownloadMemoryAddress)
    {
        if (err)
        {
            *err = "missing required download_memory_address";
        }
        return false;
    }
    if (cfg.packagePath.empty())
    {
        if (err)
        {
            *err = "missing required package_path";
        }
        return false;
    }
    if (cfg.haveSecurity && !cfg.haveSecurityLevel)
    {
        if (err)
        {
            *err = "flash_require_security=1 requires flash_security_level";
        }
        return false;
    }
    const bool wantErase = cfg.haveFlashEraseSubFunction || cfg.haveFlashEraseRoutineId ||
                           cfg.haveFlashEraseRoutineOption;
    if (wantErase && !(cfg.haveFlashEraseSubFunction && cfg.haveFlashEraseRoutineId))
    {
        if (err)
        {
            *err = "erase routine requires both flash_erase_routine_sub_function "
                   "and flash_erase_routine_id";
        }
        return false;
    }
    // Keep-alive: both interval (>0) and sub-function, or neither. Partial
    // config is rejected before any network use (library itself would skip).
    if (cfg.haveFlashKeepAliveInterval != cfg.haveFlashKeepAliveSubFunction)
    {
        if (err)
        {
            *err = "flash keep-alive requires both flash_keep_alive_interval_ms "
                   "and flash_keep_alive_sub_function (or neither)";
        }
        return false;
    }
    if (cfg.haveFlashKeepAliveInterval && cfg.flashKeepAliveIntervalMs == 0)
    {
        if (err)
        {
            *err = "flash_keep_alive_interval_ms must be > 0 when set "
                   "(library has no default interval)";
        }
        return false;
    }
    return true;
}

void FillFlashJob(const StationConfig &cfg, DoipLiteFlashJob *job)
{
    if (!job)
    {
        return;
    }
    std::memset(job, 0, sizeof(*job));
    job->haveFlashSessionType = cfg.haveFlashSessionType ? 1 : 0;
    job->flashSessionType = cfg.flashSessionType;
    job->haveSecurity = cfg.haveSecurity ? 1 : 0;
    job->securityLevel = cfg.securityLevel;
    job->haveEraseRoutine =
        (cfg.haveFlashEraseSubFunction && cfg.haveFlashEraseRoutineId) ? 1 : 0;
    job->eraseRoutineSubFunction = cfg.flashEraseRoutineSubFunction;
    job->eraseRoutineId = cfg.flashEraseRoutineId;
    job->eraseRoutineOption =
        cfg.flashEraseRoutineOption.empty() ? nullptr : cfg.flashEraseRoutineOption.data();
    job->eraseRoutineOptionLen =
        static_cast<uint32_t>(cfg.flashEraseRoutineOption.size());
    job->dataFormatId = cfg.haveDownloadDataFormatId ? cfg.downloadDataFormatId : 0;
    job->addressAndLengthFormatId = cfg.downloadAlfid;
    job->memoryAddress = cfg.downloadMemoryAddress;
    job->memorySize = cfg.haveDownloadMemorySize ? cfg.downloadMemorySize : 0;
    job->packagePath = cfg.packagePath.c_str();
    job->maxBlockDataCap = cfg.haveFlashMaxBlockDataCap ? cfg.flashMaxBlockDataCap : 0;
    job->transferExitParam =
        cfg.transferExitParam.empty() ? nullptr : cfg.transferExitParam.data();
    job->transferExitParamLen = static_cast<uint32_t>(cfg.transferExitParam.size());
    job->haveEcuReset = cfg.haveFlashResetType ? 1 : 0;
    job->resetType = cfg.flashResetType;
    job->keepAliveIntervalMs =
        cfg.haveFlashKeepAliveInterval ? cfg.flashKeepAliveIntervalMs : 0;
    job->haveKeepAliveSubFunction = cfg.haveFlashKeepAliveSubFunction ? 1 : 0;
    job->keepAliveSubFunction = cfg.flashKeepAliveSubFunction;
    job->timeoutMs = cfg.timeoutMs;
}

const char *FlashPhaseName(DoipLiteFlashPhase phase)
{
    switch (phase)
    {
    case DOIPLITE_FLASH_IDLE:
        return "IDLE";
    case DOIPLITE_FLASH_SESSION:
        return "SESSION";
    case DOIPLITE_FLASH_SECURITY:
        return "SECURITY";
    case DOIPLITE_FLASH_ERASE:
        return "ERASE";
    case DOIPLITE_FLASH_REQUEST_DOWNLOAD:
        return "REQUEST_DOWNLOAD";
    case DOIPLITE_FLASH_TRANSFER:
        return "TRANSFER";
    case DOIPLITE_FLASH_TRANSFER_EXIT:
        return "TRANSFER_EXIT";
    case DOIPLITE_FLASH_RESET:
        return "RESET";
    case DOIPLITE_FLASH_DONE:
        return "DONE";
    case DOIPLITE_FLASH_FAILED:
        return "FAILED";
    default:
        return "UNKNOWN";
    }
}

FlashRunResult ClassifyFlashRunResult(DoipLiteFlashPhase phase,
                                      DoipLiteStatus lastPollStatus,
                                      bool pollLimitExhausted)
{
    if (pollLimitExhausted)
    {
        return FlashRunResult::LimitExhausted;
    }
    if (phase == DOIPLITE_FLASH_DONE && lastPollStatus == DOIPLITE_OK)
    {
        return FlashRunResult::DoneOk;
    }
    if (lastPollStatus != DOIPLITE_OK)
    {
        return FlashRunResult::PollError;
    }
    if (phase == DOIPLITE_FLASH_FAILED)
    {
        return FlashRunResult::Failed;
    }
    // Non-terminal phase without hitting the coded limit should not happen;
    // treat as failure so callers never report success.
    return FlashRunResult::Failed;
}

bool IsSecurityNotImplementedFailure(const StationConfig &cfg,
                                     DoipLiteStatus failStatus)
{
    return cfg.haveSecurity && failStatus == DOIPLITE_ERR_NOT_IMPLEMENTED;
}

} // namespace doiplite_examples
