#include "doiplite/doiplite.h"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

// Example default only: first address in the ISO 13400 external test equipment
// range (0x0E00–0x0FFF). Override with --tester-addr or tester_address in config.
// This is not a field / plant address.
constexpr uint16_t kExampleTesterAddr = 0x0E00;
constexpr uint16_t kDefaultActivationType = 0x00;

struct Options
{
    std::string ip;
    std::string broadcast;
    uint16_t port = DOIPLITE_DEFAULT_TCP_PORT;
    uint16_t testerAddr = kExampleTesterAddr;
    uint16_t activationType = kDefaultActivationType;
    uint32_t timeoutMs = DOIPLITE_DEFAULT_TIMEOUT_MS;
    uint16_t did = 0;
    uint8_t sessionType = 0;
    uint8_t testerPresentSub = 0;

    // UDS 0x31 RoutineControl (unique keys; not bare sub_function).
    uint8_t routineSubFunction = 0;
    uint16_t routineId = 0;
    std::vector<uint8_t> routineOption;
    bool haveRoutineSubFunction = false;
    bool haveRoutineId = false;

    // UDS 0x34 RequestDownload
    uint8_t downloadDataFormatId = 0;
    uint8_t downloadAlfid = 0;
    uint64_t downloadMemoryAddress = 0;
    uint64_t downloadMemorySize = 0;
    bool haveDownloadDataFormatId = false;
    bool haveDownloadAlfid = false;
    bool haveDownloadMemoryAddress = false;
    bool haveDownloadMemorySize = false;

    // UDS 0x36 TransferData
    uint8_t transferBlockCounter = 0;
    std::vector<uint8_t> transferData;
    std::string transferDataFile;
    bool haveTransferBlockCounter = false;

    // UDS 0x37 RequestTransferExit
    std::vector<uint8_t> transferExitParam;
    bool haveTransferExitParam = false;

    // UDS 0x11 ECUReset
    uint8_t ecuResetType = 0;
    bool haveEcuResetType = false;

    // flash orchestration
    uint8_t flashSessionType = 0;
    bool haveFlashSessionType = false;
    uint8_t flashEraseRoutineSubFunction = 0;
    uint16_t flashEraseRoutineId = 0;
    std::vector<uint8_t> flashEraseRoutineOption;
    bool haveFlashEraseRoutine = false;
    uint32_t flashMaxBlockDataCap = 0;
    bool haveFlashMaxBlockDataCap = false;
    std::string packagePath;
    uint8_t flashResetType = 0;
    bool haveFlashResetType = false;
    uint32_t flashKeepAliveIntervalMs = 0;
    bool haveFlashKeepAliveInterval = false;
    uint8_t flashKeepAliveSubFunction = 0;
    bool haveFlashKeepAliveSubFunction = false;

    bool havePort = false;
    bool haveTesterAddr = false;
    bool haveActivationType = false;
    bool haveTimeout = false;
    bool haveDid = false;
    bool haveSessionType = false;
    bool haveTesterPresentSub = false;
};

void printHelp()
{
    std::fputs(
        "doiplite-cli — DoipLite command-line tool (ISO 13400-2 generic DoIP)\n"
        "\n"
        "Usage:\n"
        "  doiplite-cli --help\n"
        "  doiplite-cli discover | activate | read-version | session | tester-present\n"
        "  doiplite-cli routine-control | request-download | transfer-data\n"
        "  doiplite-cli request-transfer-exit | ecu-reset | flash\n"
        "\n"
        "Common options:\n"
        "  --config <path>           Load key=value settings (flags override file)\n"
        "  --ip <ipv4>               Entity IPv4 address\n"
        "  --broadcast <ipv4>        Discover destination (default: 255.255.255.255)\n"
        "  --port <n>                UDP/TCP port (default: 13400)\n"
        "  --tester-addr <addr>      Tester logical address (hex 0x0E00 or decimal)\n"
        "  --activation-type <n>     Routing activation type (default: 0x00 ISO default)\n"
        "  --timeout-ms <n>          I/O timeout in milliseconds (default: 2000)\n"
        "\n"
        "Service-specific options (config keys are unique; see examples/config/example.conf):\n"
        "  --did / version_did\n"
        "  --session / session_type\n"
        "  --sub-function / tester_present_sub_function\n"
        "  --routine-sub-function / routine_sub_function\n"
        "  --routine-id / routine_id\n"
        "  --routine-option-hex / routine_option_hex\n"
        "  --download-data-format-id / download_data_format_id\n"
        "  --download-alfid / download_alfid\n"
        "  --download-memory-address / download_memory_address\n"
        "  --download-memory-size / download_memory_size\n"
        "  --transfer-block-counter / transfer_block_sequence_counter\n"
        "  --transfer-data-hex / transfer_data_hex\n"
        "  --transfer-data-file / transfer_data_file\n"
        "  --transfer-exit-param-hex / transfer_exit_param_hex\n"
        "  --ecu-reset-type / ecu_reset_type\n"
        "  --flash-session-type / flash_session_type\n"
        "  --flash-erase-routine-sub-function / flash_erase_routine_sub_function\n"
        "  --flash-erase-routine-id / flash_erase_routine_id\n"
        "  --flash-erase-routine-option-hex / flash_erase_routine_option_hex\n"
        "  --flash-max-block-data-cap / flash_max_block_data_cap\n"
        "  --package-path / package_path\n"
        "  --flash-reset-type / flash_reset_type\n"
        "  --flash-keep-alive-interval-ms / flash_keep_alive_interval_ms\n"
        "  --flash-keep-alive-sub-function / flash_keep_alive_sub_function\n"
        "\n"
        "All field-specific values come from flags/config; the library never invents\n"
        "plant IPs, routine IDs, memory addresses, reset types, or programming session.\n"
        "flash runs the orchestrated sequence (optional session/erase/reset; required\n"
        "download+transfer+exit from package_path or transfer data). 0x27 security is\n"
        "not implemented. Periodic TesterPresent (0x3E) during long transfer requires\n"
        "both flash_keep_alive_interval_ms (>0) and flash_keep_alive_sub_function;\n"
        "the library invents neither default. 0x31 / 0x10 / 0x11 / 0x3E honor suppress\n"
        "bit7 (silence = success; 0x78 then silence = timeout).\n"
        "If --tester-addr is omitted, activate and diagnostic commands use 0x0E00 as an\n"
        "ISO external-test-equipment example default (not a field address).\n",
        stdout);
}

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

// Parse a full-width unsigned value first, then reject anything above max
// before narrowing. This prevents e.g. 0x100000002 from becoming 0x02 after
// a uint32_t truncate on platforms where unsigned long is wider than 32 bits.
bool parseUnsignedMax(const std::string &text, unsigned long max, unsigned long *out)
{
    if (!out || text.empty())
    {
        return false;
    }
    char *end = nullptr;
    const bool hex = text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
    errno = 0;
    const unsigned long value = std::strtoul(text.c_str(), &end, hex ? 16 : 10);
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

bool parseU32(const std::string &text, uint32_t *out)
{
    unsigned long value = 0;
    if (!out || !parseUnsignedMax(text, 0xFFFFFFFFul, &value))
    {
        return false;
    }
    *out = static_cast<uint32_t>(value);
    return true;
}

bool parseU16(const std::string &text, uint16_t *out)
{
    unsigned long value = 0;
    if (!out || !parseUnsignedMax(text, 0xFFFFul, &value))
    {
        return false;
    }
    *out = static_cast<uint16_t>(value);
    return true;
}

bool parseU8(const std::string &text, uint8_t *out)
{
    unsigned long value = 0;
    if (!out || !parseUnsignedMax(text, 0xFFul, &value))
    {
        return false;
    }
    *out = static_cast<uint8_t>(value);
    return true;
}

bool parseU64(const std::string &text, uint64_t *out)
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
    *out = static_cast<uint64_t>(value);
    return true;
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

bool applyKey(Options *opt, const std::string &key, const std::string &value, std::string *err)
{
    if (key == "ip")
    {
        opt->ip = value;
        return true;
    }
    if (key == "broadcast")
    {
        opt->broadcast = value;
        return true;
    }
    if (key == "port")
    {
        if (!parseU16(value, &opt->port) || opt->port == 0)
        {
            *err = "invalid port";
            return false;
        }
        opt->havePort = true;
        return true;
    }
    if (key == "tester_address" || key == "tester-addr")
    {
        if (!parseU16(value, &opt->testerAddr))
        {
            *err = "invalid tester_address";
            return false;
        }
        opt->haveTesterAddr = true;
        return true;
    }
    if (key == "activation_type" || key == "activation-type")
    {
        if (!parseU16(value, &opt->activationType))
        {
            *err = "invalid activation_type";
            return false;
        }
        opt->haveActivationType = true;
        return true;
    }
    if (key == "timeout_ms" || key == "timeout-ms")
    {
        if (!parseU32(value, &opt->timeoutMs))
        {
            *err = "invalid timeout_ms";
            return false;
        }
        opt->haveTimeout = true;
        return true;
    }
    if (key == "version_did" || key == "did")
    {
        if (value.empty() || !parseU16(value, &opt->did))
        {
            *err = "invalid version_did";
            return false;
        }
        opt->haveDid = true;
        return true;
    }
    if (key == "session_type" || key == "session")
    {
        if (value.empty() || !parseU8(value, &opt->sessionType))
        {
            *err = "invalid session_type";
            return false;
        }
        opt->haveSessionType = true;
        return true;
    }
    if (key == "tester_present_sub_function" || key == "sub_function" ||
        key == "sub-function")
    {
        if (value.empty() || !parseU8(value, &opt->testerPresentSub))
        {
            *err = "invalid tester_present_sub_function";
            return false;
        }
        opt->haveTesterPresentSub = true;
        return true;
    }
    if (key == "routine_sub_function")
    {
        if (value.empty() || !parseU8(value, &opt->routineSubFunction))
        {
            *err = "invalid routine_sub_function";
            return false;
        }
        opt->haveRoutineSubFunction = true;
        return true;
    }
    if (key == "routine_id")
    {
        if (value.empty() || !parseU16(value, &opt->routineId))
        {
            *err = "invalid routine_id";
            return false;
        }
        opt->haveRoutineId = true;
        return true;
    }
    if (key == "routine_option_hex")
    {
        if (!parseHexBytes(value, &opt->routineOption))
        {
            *err = "invalid routine_option_hex";
            return false;
        }
        return true;
    }
    if (key == "download_data_format_id")
    {
        if (value.empty() || !parseU8(value, &opt->downloadDataFormatId))
        {
            *err = "invalid download_data_format_id";
            return false;
        }
        opt->haveDownloadDataFormatId = true;
        return true;
    }
    if (key == "download_alfid")
    {
        if (value.empty() || !parseU8(value, &opt->downloadAlfid))
        {
            *err = "invalid download_alfid";
            return false;
        }
        opt->haveDownloadAlfid = true;
        return true;
    }
    if (key == "download_memory_address")
    {
        if (value.empty() || !parseU64(value, &opt->downloadMemoryAddress))
        {
            *err = "invalid download_memory_address";
            return false;
        }
        opt->haveDownloadMemoryAddress = true;
        return true;
    }
    if (key == "download_memory_size")
    {
        if (value.empty() || !parseU64(value, &opt->downloadMemorySize))
        {
            *err = "invalid download_memory_size";
            return false;
        }
        opt->haveDownloadMemorySize = true;
        return true;
    }
    if (key == "transfer_block_sequence_counter")
    {
        if (value.empty() || !parseU8(value, &opt->transferBlockCounter))
        {
            *err = "invalid transfer_block_sequence_counter";
            return false;
        }
        opt->haveTransferBlockCounter = true;
        return true;
    }
    if (key == "transfer_data_hex")
    {
        if (!parseHexBytes(value, &opt->transferData))
        {
            *err = "invalid transfer_data_hex";
            return false;
        }
        return true;
    }
    if (key == "transfer_data_file")
    {
        opt->transferDataFile = value;
        return true;
    }
    if (key == "transfer_exit_param_hex")
    {
        if (!parseHexBytes(value, &opt->transferExitParam))
        {
            *err = "invalid transfer_exit_param_hex";
            return false;
        }
        opt->haveTransferExitParam = true;
        return true;
    }
    if (key == "ecu_reset_type")
    {
        if (value.empty() || !parseU8(value, &opt->ecuResetType))
        {
            *err = "invalid ecu_reset_type";
            return false;
        }
        opt->haveEcuResetType = true;
        return true;
    }
    if (key == "flash_session_type")
    {
        if (value.empty() || !parseU8(value, &opt->flashSessionType))
        {
            *err = "invalid flash_session_type";
            return false;
        }
        opt->haveFlashSessionType = true;
        return true;
    }
    if (key == "flash_erase_routine_sub_function")
    {
        if (value.empty() || !parseU8(value, &opt->flashEraseRoutineSubFunction))
        {
            *err = "invalid flash_erase_routine_sub_function";
            return false;
        }
        opt->haveFlashEraseRoutine = true;
        return true;
    }
    if (key == "flash_erase_routine_id")
    {
        if (value.empty() || !parseU16(value, &opt->flashEraseRoutineId))
        {
            *err = "invalid flash_erase_routine_id";
            return false;
        }
        opt->haveFlashEraseRoutine = true;
        return true;
    }
    if (key == "flash_erase_routine_option_hex")
    {
        if (!parseHexBytes(value, &opt->flashEraseRoutineOption))
        {
            *err = "invalid flash_erase_routine_option_hex";
            return false;
        }
        return true;
    }
    if (key == "flash_max_block_data_cap")
    {
        if (value.empty() || !parseU32(value, &opt->flashMaxBlockDataCap))
        {
            *err = "invalid flash_max_block_data_cap";
            return false;
        }
        opt->haveFlashMaxBlockDataCap = true;
        return true;
    }
    if (key == "package_path")
    {
        opt->packagePath = value;
        return true;
    }
    if (key == "flash_reset_type")
    {
        if (value.empty() || !parseU8(value, &opt->flashResetType))
        {
            *err = "invalid flash_reset_type";
            return false;
        }
        opt->haveFlashResetType = true;
        return true;
    }
    if (key == "flash_keep_alive_interval_ms")
    {
        if (value.empty() || !parseU32(value, &opt->flashKeepAliveIntervalMs))
        {
            *err = "invalid flash_keep_alive_interval_ms";
            return false;
        }
        opt->haveFlashKeepAliveInterval = true;
        return true;
    }
    if (key == "flash_keep_alive_sub_function")
    {
        if (value.empty() || !parseU8(value, &opt->flashKeepAliveSubFunction))
        {
            *err = "invalid flash_keep_alive_sub_function";
            return false;
        }
        opt->haveFlashKeepAliveSubFunction = true;
        return true;
    }
    // Unknown keys (e.g. entity_address) are ignored.
    return true;
}

bool loadConfig(const char *path, Options *opt, std::string *err)
{
    std::ifstream in(path);
    if (!in)
    {
        *err = std::string("cannot read config file: ") + path;
        return false;
    }
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line))
    {
        ++lineNo;
        const auto hash = line.find('#');
        if (hash != std::string::npos)
        {
            line = line.substr(0, hash);
        }
        line = trim(line);
        if (line.empty())
        {
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos)
        {
            *err = "invalid config line " + std::to_string(lineNo);
            return false;
        }
        const std::string key = trim(line.substr(0, eq));
        const std::string value = trim(line.substr(eq + 1));
        if (key.empty())
        {
            *err = "invalid config line " + std::to_string(lineNo);
            return false;
        }
        if (!applyKey(opt, key, value, err))
        {
            return false;
        }
    }
    return true;
}

void mergeFlags(Options *opt, const Options &flags)
{
    if (!flags.ip.empty())
    {
        opt->ip = flags.ip;
    }
    if (!flags.broadcast.empty())
    {
        opt->broadcast = flags.broadcast;
    }
    if (flags.havePort)
    {
        opt->port = flags.port;
        opt->havePort = true;
    }
    if (flags.haveTesterAddr)
    {
        opt->testerAddr = flags.testerAddr;
        opt->haveTesterAddr = true;
    }
    if (flags.haveActivationType)
    {
        opt->activationType = flags.activationType;
        opt->haveActivationType = true;
    }
    if (flags.haveTimeout)
    {
        opt->timeoutMs = flags.timeoutMs;
        opt->haveTimeout = true;
    }
    if (flags.haveDid)
    {
        opt->did = flags.did;
        opt->haveDid = true;
    }
    if (flags.haveSessionType)
    {
        opt->sessionType = flags.sessionType;
        opt->haveSessionType = true;
    }
    if (flags.haveTesterPresentSub)
    {
        opt->testerPresentSub = flags.testerPresentSub;
        opt->haveTesterPresentSub = true;
    }
    if (flags.haveRoutineSubFunction)
    {
        opt->routineSubFunction = flags.routineSubFunction;
        opt->haveRoutineSubFunction = true;
    }
    if (flags.haveRoutineId)
    {
        opt->routineId = flags.routineId;
        opt->haveRoutineId = true;
    }
    if (!flags.routineOption.empty())
    {
        opt->routineOption = flags.routineOption;
    }
    if (flags.haveDownloadDataFormatId)
    {
        opt->downloadDataFormatId = flags.downloadDataFormatId;
        opt->haveDownloadDataFormatId = true;
    }
    if (flags.haveDownloadAlfid)
    {
        opt->downloadAlfid = flags.downloadAlfid;
        opt->haveDownloadAlfid = true;
    }
    if (flags.haveDownloadMemoryAddress)
    {
        opt->downloadMemoryAddress = flags.downloadMemoryAddress;
        opt->haveDownloadMemoryAddress = true;
    }
    if (flags.haveDownloadMemorySize)
    {
        opt->downloadMemorySize = flags.downloadMemorySize;
        opt->haveDownloadMemorySize = true;
    }
    if (flags.haveTransferBlockCounter)
    {
        opt->transferBlockCounter = flags.transferBlockCounter;
        opt->haveTransferBlockCounter = true;
    }
    if (!flags.transferData.empty())
    {
        opt->transferData = flags.transferData;
    }
    if (!flags.transferDataFile.empty())
    {
        opt->transferDataFile = flags.transferDataFile;
    }
    if (flags.haveTransferExitParam || !flags.transferExitParam.empty())
    {
        opt->transferExitParam = flags.transferExitParam;
        opt->haveTransferExitParam = true;
    }
    if (flags.haveEcuResetType)
    {
        opt->ecuResetType = flags.ecuResetType;
        opt->haveEcuResetType = true;
    }
    if (flags.haveFlashSessionType)
    {
        opt->flashSessionType = flags.flashSessionType;
        opt->haveFlashSessionType = true;
    }
    if (flags.haveFlashEraseRoutine)
    {
        opt->flashEraseRoutineSubFunction = flags.flashEraseRoutineSubFunction;
        opt->flashEraseRoutineId = flags.flashEraseRoutineId;
        opt->haveFlashEraseRoutine = true;
    }
    if (!flags.flashEraseRoutineOption.empty())
    {
        opt->flashEraseRoutineOption = flags.flashEraseRoutineOption;
    }
    if (flags.haveFlashMaxBlockDataCap)
    {
        opt->flashMaxBlockDataCap = flags.flashMaxBlockDataCap;
        opt->haveFlashMaxBlockDataCap = true;
    }
    if (!flags.packagePath.empty())
    {
        opt->packagePath = flags.packagePath;
    }
    if (flags.haveFlashResetType)
    {
        opt->flashResetType = flags.flashResetType;
        opt->haveFlashResetType = true;
    }
    if (flags.haveFlashKeepAliveInterval)
    {
        opt->flashKeepAliveIntervalMs = flags.flashKeepAliveIntervalMs;
        opt->haveFlashKeepAliveInterval = true;
    }
    if (flags.haveFlashKeepAliveSubFunction)
    {
        opt->flashKeepAliveSubFunction = flags.flashKeepAliveSubFunction;
        opt->haveFlashKeepAliveSubFunction = true;
    }
}

void printVehicle(const DoipLiteVehicle &v)
{
    char vin[18];
    std::memset(vin, 0, sizeof(vin));
    bool printable = true;
    for (int i = 0; i < 17; ++i)
    {
        const unsigned char c = v.vin[i];
        if (c == 0)
        {
            break;
        }
        if (c < 32 || c > 126)
        {
            printable = false;
            break;
        }
        vin[i] = static_cast<char>(c);
    }
    std::printf("  ip=%s tcp_port=%u logical_address=0x%04X",
                v.ip,
                static_cast<unsigned>(v.tcpPort),
                static_cast<unsigned>(v.logicalAddress));
    if (printable && vin[0] != '\0')
    {
        std::printf(" vin=%s", vin);
    }
    std::fputc('\n', stdout);
}

int cmdDiscover(const Options &opt)
{
    const char *dest = opt.broadcast.empty() ? nullptr : opt.broadcast.c_str();
    const uint16_t port = opt.havePort ? opt.port : DOIPLITE_DEFAULT_UDP_PORT;
    DoipLiteVehicle vehicles[32];
    uint32_t count = 0;
    const DoipLiteStatus st =
        DoipLiteDiscoverEx(opt.timeoutMs, dest, port, vehicles, 32, &count);
    if (st != DOIPLITE_OK)
    {
        std::fprintf(stderr,
                     "doiplite-cli: discover failed: %s\n",
                     DoipLiteStatusString(st));
        return 1;
    }
    const uint32_t shown = count < 32 ? count : 32u;
    std::printf("found %u vehicle(s)\n", static_cast<unsigned>(count));
    for (uint32_t i = 0; i < shown; ++i)
    {
        printVehicle(vehicles[i]);
    }
    return 0;
}

int cmdActivate(const Options &opt)
{
    if (opt.ip.empty())
    {
        std::fputs("doiplite-cli: activate requires --ip or config ip\n", stderr);
        return 2;
    }
    const uint16_t port = opt.havePort ? opt.port : DOIPLITE_DEFAULT_TCP_PORT;
    DoipLiteSession *session = DoipLiteOpen(opt.ip.c_str(), port);
    if (!session)
    {
        std::fprintf(stderr,
                     "doiplite-cli: activate failed: invalid_arg (cannot open %s:%u)\n",
                     opt.ip.c_str(),
                     static_cast<unsigned>(port));
        return 2;
    }
    const DoipLiteStatus st = DoipLiteRoutingActivationEx(
        session, opt.testerAddr, opt.activationType, nullptr, 0, opt.timeoutMs);
    if (st != DOIPLITE_OK)
    {
        std::fprintf(stderr, "doiplite-cli: activate failed: %s", DoipLiteStatusString(st));
        if (st == DOIPLITE_ERR_DOIP_NACK)
        {
            std::fprintf(stderr, " (response code 0x%02X)", DoipLiteLastDoipCode(session));
        }
        std::fputc('\n', stderr);
        DoipLiteClose(session);
        return 1;
    }
    std::printf("routing activation successful\n");
    std::printf("  entity=%s:%u tester_address=0x%04X entity_address=0x%04X\n",
                opt.ip.c_str(),
                static_cast<unsigned>(port),
                static_cast<unsigned>(opt.testerAddr),
                static_cast<unsigned>(DoipLiteEntityAddress(session)));
    DoipLiteClose(session);
    return 0;
}

void printHexBytes(const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; ++i)
    {
        if (i > 0)
        {
            std::fputc(' ', stdout);
        }
        std::printf("%02X", data[i]);
    }
}

bool isPrintableData(const uint8_t *data, uint32_t len, std::string *text)
{
    uint32_t n = len;
    while (n > 0 && data[n - 1] == 0)
    {
        --n;
    }
    std::string out;
    out.reserve(n);
    for (uint32_t i = 0; i < n; ++i)
    {
        const unsigned char c = data[i];
        if (c < 32 || c > 126)
        {
            return false;
        }
        out.push_back(static_cast<char>(c));
    }
    if (text)
    {
        *text = out;
    }
    return n > 0;
}

int cmdReadVersion(const Options &opt)
{
    if (opt.ip.empty())
    {
        std::fputs("doiplite-cli: read-version requires --ip or config ip\n", stderr);
        return 2;
    }
    if (!opt.haveDid)
    {
        std::fputs("doiplite-cli: read-version requires --did or config version_did\n", stderr);
        return 2;
    }

    const uint16_t port = opt.havePort ? opt.port : DOIPLITE_DEFAULT_TCP_PORT;
    DoipLiteSession *session = DoipLiteOpen(opt.ip.c_str(), port);
    if (!session)
    {
        std::fprintf(stderr,
                     "doiplite-cli: read-version failed: invalid_arg (cannot open %s:%u)\n",
                     opt.ip.c_str(),
                     static_cast<unsigned>(port));
        return 2;
    }

    DoipLiteStatus st = DoipLiteRoutingActivationEx(
        session, opt.testerAddr, opt.activationType, nullptr, 0, opt.timeoutMs);
    if (st != DOIPLITE_OK)
    {
        std::fprintf(stderr,
                     "doiplite-cli: read-version failed: %s",
                     DoipLiteStatusString(st));
        if (st == DOIPLITE_ERR_DOIP_NACK)
        {
            std::fprintf(stderr, " (response code 0x%02X)", DoipLiteLastDoipCode(session));
        }
        std::fputc('\n', stderr);
        DoipLiteClose(session);
        return 1;
    }

    uint8_t buf[4096];
    uint32_t len = 0;
    st = UdsReadDataByIdentifier(session, opt.did, buf, sizeof(buf), &len, opt.timeoutMs);
    if (st != DOIPLITE_OK)
    {
        std::fprintf(stderr,
                     "doiplite-cli: read-version failed: %s",
                     DoipLiteStatusString(st));
        if (st == DOIPLITE_ERR_DOIP_NACK)
        {
            std::fprintf(stderr, " (response code 0x%02X)", DoipLiteLastDoipCode(session));
        }
        else if (st == DOIPLITE_ERR_UDS_NRC)
        {
            if (len >= 3)
            {
                std::fprintf(stderr, " (nrc 0x%02X)", buf[2]);
            }
            else if (len > 0)
            {
                std::fprintf(stderr, " (");
                for (uint32_t i = 0; i < len; ++i)
                {
                    if (i > 0)
                    {
                        std::fputc(' ', stderr);
                    }
                    std::fprintf(stderr, "0x%02X", buf[i]);
                }
                std::fputc(')', stderr);
            }
        }
        std::fputc('\n', stderr);
        DoipLiteClose(session);
        return 1;
    }

    const uint8_t udsHead[3] = {
        0x62,
        static_cast<uint8_t>((opt.did >> 8) & 0xff),
        static_cast<uint8_t>(opt.did & 0xff),
    };
    std::printf("read-version ok did=0x%04X\n", static_cast<unsigned>(opt.did));
    std::fputs("  uds=", stdout);
    printHexBytes(udsHead, 3);
    if (len > 0)
    {
        std::fputc(' ', stdout);
        printHexBytes(buf, len);
    }
    std::fputc('\n', stdout);
    std::fputs("  hex=", stdout);
    if (len == 0)
    {
        std::fputs("(empty)", stdout);
    }
    else
    {
        printHexBytes(buf, len);
    }
    std::fputc('\n', stdout);
    std::string text;
    if (isPrintableData(buf, len, &text))
    {
        std::printf("  version=%s\n", text.c_str());
    }

    DoipLiteClose(session);
    return 0;
}


int cmdSession(const Options &opt)
{
    if (opt.ip.empty())
    {
        std::fputs("doiplite-cli: session requires --ip or config ip\n", stderr);
        return 2;
    }
    if (!opt.haveSessionType)
    {
        std::fputs("doiplite-cli: session requires --session or config session_type\n", stderr);
        return 2;
    }

    const uint16_t port = opt.havePort ? opt.port : DOIPLITE_DEFAULT_TCP_PORT;
    DoipLiteSession *session = DoipLiteOpen(opt.ip.c_str(), port);
    if (!session)
    {
        std::fprintf(stderr,
                     "doiplite-cli: session failed: invalid_arg (cannot open %s:%u)\n",
                     opt.ip.c_str(),
                     static_cast<unsigned>(port));
        return 2;
    }

    DoipLiteStatus st = DoipLiteRoutingActivationEx(
        session, opt.testerAddr, opt.activationType, nullptr, 0, opt.timeoutMs);
    if (st != DOIPLITE_OK)
    {
        std::fprintf(stderr,
                     "doiplite-cli: session failed: %s",
                     DoipLiteStatusString(st));
        if (st == DOIPLITE_ERR_DOIP_NACK)
        {
            std::fprintf(stderr, " (response code 0x%02X)", DoipLiteLastDoipCode(session));
        }
        std::fputc('\n', stderr);
        DoipLiteClose(session);
        return 1;
    }

    uint8_t buf[64];
    uint32_t len = 0;
    st = UdsDiagnosticSessionControl(
        session, opt.sessionType, buf, sizeof(buf), &len, opt.timeoutMs);
    if (st != DOIPLITE_OK)
    {
        std::fprintf(stderr,
                     "doiplite-cli: session failed: %s",
                     DoipLiteStatusString(st));
        if (st == DOIPLITE_ERR_DOIP_NACK)
        {
            std::fprintf(stderr, " (response code 0x%02X)", DoipLiteLastDoipCode(session));
        }
        else if (st == DOIPLITE_ERR_UDS_NRC)
        {
            if (len >= 3)
            {
                std::fprintf(stderr, " (nrc 0x%02X)", buf[2]);
            }
            else if (len > 0)
            {
                std::fprintf(stderr, " (");
                for (uint32_t i = 0; i < len; ++i)
                {
                    if (i > 0)
                    {
                        std::fputc(' ', stderr);
                    }
                    std::fprintf(stderr, "0x%02X", buf[i]);
                }
                std::fputc(')', stderr);
            }
        }
        std::fputc('\n', stderr);
        DoipLiteClose(session);
        return 1;
    }

    std::printf("session ok type=0x%02X\n", static_cast<unsigned>(opt.sessionType));
    std::fputs("  uds=", stdout);
    if (len == 0)
    {
        std::fputs("(empty)", stdout);
    }
    else
    {
        printHexBytes(buf, len);
    }
    std::fputc('\n', stdout);
    if (len >= 2)
    {
        std::printf("  session_type=0x%02X\n", static_cast<unsigned>(buf[1]));
    }
    if (len >= 6)
    {
        const unsigned p2 = (static_cast<unsigned>(buf[2]) << 8) | buf[3];
        const unsigned p2star = (static_cast<unsigned>(buf[4]) << 8) | buf[5];
        std::printf("  p2_ms=%u\n", p2);
        std::printf("  p2_star=0x%04X\n", p2star);
    }

    DoipLiteClose(session);
    return 0;
}

int cmdTesterPresent(const Options &opt)
{
    if (opt.ip.empty())
    {
        std::fputs("doiplite-cli: tester-present requires --ip or config ip\n", stderr);
        return 2;
    }
    if (!opt.haveTesterPresentSub)
    {
        std::fputs(
            "doiplite-cli: tester-present requires --sub-function or config "
            "tester_present_sub_function\n",
            stderr);
        return 2;
    }

    const uint16_t port = opt.havePort ? opt.port : DOIPLITE_DEFAULT_TCP_PORT;
    DoipLiteSession *session = DoipLiteOpen(opt.ip.c_str(), port);
    if (!session)
    {
        std::fprintf(stderr,
                     "doiplite-cli: tester-present failed: invalid_arg (cannot open %s:%u)\n",
                     opt.ip.c_str(),
                     static_cast<unsigned>(port));
        return 2;
    }

    DoipLiteStatus st = DoipLiteRoutingActivationEx(
        session, opt.testerAddr, opt.activationType, nullptr, 0, opt.timeoutMs);
    if (st != DOIPLITE_OK)
    {
        std::fprintf(stderr,
                     "doiplite-cli: tester-present failed: %s",
                     DoipLiteStatusString(st));
        if (st == DOIPLITE_ERR_DOIP_NACK)
        {
            std::fprintf(stderr, " (response code 0x%02X)", DoipLiteLastDoipCode(session));
        }
        std::fputc('\n', stderr);
        DoipLiteClose(session);
        return 1;
    }

    uint8_t buf[64];
    uint32_t len = 0;
    st = UdsTesterPresent(
        session, opt.testerPresentSub, buf, sizeof(buf), &len, opt.timeoutMs);
    if (st != DOIPLITE_OK)
    {
        std::fprintf(stderr,
                     "doiplite-cli: tester-present failed: %s",
                     DoipLiteStatusString(st));
        if (st == DOIPLITE_ERR_DOIP_NACK)
        {
            std::fprintf(stderr, " (response code 0x%02X)", DoipLiteLastDoipCode(session));
        }
        else if (st == DOIPLITE_ERR_UDS_NRC)
        {
            if (len >= 3)
            {
                std::fprintf(stderr, " (nrc 0x%02X)", buf[2]);
            }
            else if (len > 0)
            {
                std::fprintf(stderr, " (");
                for (uint32_t i = 0; i < len; ++i)
                {
                    if (i > 0)
                    {
                        std::fputc(' ', stderr);
                    }
                    std::fprintf(stderr, "0x%02X", buf[i]);
                }
                std::fputc(')', stderr);
            }
        }
        std::fputc('\n', stderr);
        DoipLiteClose(session);
        return 1;
    }

    std::printf("tester-present ok sub_function=0x%02X\n",
                static_cast<unsigned>(opt.testerPresentSub));
    std::fputs("  uds=", stdout);
    if (len == 0)
    {
        std::fputs("(empty)", stdout);
    }
    else
    {
        printHexBytes(buf, len);
    }
    std::fputc('\n', stdout);
    if (len >= 2)
    {
        std::printf("  sub_function=0x%02X\n", static_cast<unsigned>(buf[1]));
    }

    DoipLiteClose(session);
    return 0;
}

void printUdsFailure(const char *cmd,
                     DoipLiteSession *session,
                     DoipLiteStatus st,
                     const uint8_t *buf,
                     uint32_t len)
{
    std::fprintf(stderr, "doiplite-cli: %s failed: %s", cmd, DoipLiteStatusString(st));
    if (st == DOIPLITE_ERR_DOIP_NACK)
    {
        std::fprintf(stderr, " (response code 0x%02X)", DoipLiteLastDoipCode(session));
    }
    else if (st == DOIPLITE_ERR_UDS_NRC)
    {
        if (len >= 3)
        {
            std::fprintf(stderr, " (nrc 0x%02X)", buf[2]);
        }
    }
    std::fputc('\n', stderr);
}

DoipLiteSession *openActivated(const Options &opt, const char *cmd, int *exitCode)
{
    if (opt.ip.empty())
    {
        std::fprintf(stderr, "doiplite-cli: %s requires --ip or config ip\n", cmd);
        *exitCode = 2;
        return nullptr;
    }
    const uint16_t port = opt.havePort ? opt.port : DOIPLITE_DEFAULT_TCP_PORT;
    DoipLiteSession *session = DoipLiteOpen(opt.ip.c_str(), port);
    if (!session)
    {
        std::fprintf(stderr,
                     "doiplite-cli: %s failed: invalid_arg (cannot open %s:%u)\n",
                     cmd,
                     opt.ip.c_str(),
                     static_cast<unsigned>(port));
        *exitCode = 2;
        return nullptr;
    }
    const DoipLiteStatus st = DoipLiteRoutingActivationEx(
        session, opt.testerAddr, opt.activationType, nullptr, 0, opt.timeoutMs);
    if (st != DOIPLITE_OK)
    {
        printUdsFailure(cmd, session, st, nullptr, 0);
        DoipLiteClose(session);
        *exitCode = 1;
        return nullptr;
    }
    return session;
}

int cmdRoutineControl(const Options &opt)
{
    if (!opt.haveRoutineSubFunction || !opt.haveRoutineId)
    {
        std::fputs(
            "doiplite-cli: routine-control requires --routine-sub-function and "
            "--routine-id (or config routine_sub_function / routine_id)\n",
            stderr);
        return 2;
    }
    int code = 0;
    DoipLiteSession *session = openActivated(opt, "routine-control", &code);
    if (!session)
    {
        return code;
    }
    uint8_t buf[256];
    uint32_t len = 0;
    const DoipLiteStatus st = UdsRoutineControl(
        session,
        opt.routineSubFunction,
        opt.routineId,
        opt.routineOption.empty() ? nullptr : opt.routineOption.data(),
        static_cast<uint32_t>(opt.routineOption.size()),
        buf,
        sizeof(buf),
        &len,
        opt.timeoutMs);
    if (st != DOIPLITE_OK)
    {
        printUdsFailure("routine-control", session, st, buf, len);
        DoipLiteClose(session);
        return 1;
    }
    std::printf("routine-control ok sub=0x%02X id=0x%04X\n",
                static_cast<unsigned>(opt.routineSubFunction),
                static_cast<unsigned>(opt.routineId));
    std::fputs("  uds=", stdout);
    printHexBytes(buf, len);
    std::fputc('\n', stdout);
    DoipLiteClose(session);
    return 0;
}

int cmdRequestDownload(const Options &opt)
{
    if (!opt.haveDownloadAlfid || !opt.haveDownloadMemoryAddress ||
        !opt.haveDownloadMemorySize)
    {
        std::fputs(
            "doiplite-cli: request-download requires --download-alfid, "
            "--download-memory-address and --download-memory-size\n",
            stderr);
        return 2;
    }
    int code = 0;
    DoipLiteSession *session = openActivated(opt, "request-download", &code);
    if (!session)
    {
        return code;
    }
    uint8_t buf[64];
    uint32_t len = 0;
    uint32_t maxBlock = 0;
    const uint8_t dfid = opt.haveDownloadDataFormatId ? opt.downloadDataFormatId : 0;
    const DoipLiteStatus st = UdsRequestDownload(
        session,
        dfid,
        opt.downloadAlfid,
        opt.downloadMemoryAddress,
        opt.downloadMemorySize,
        &maxBlock,
        buf,
        sizeof(buf),
        &len,
        opt.timeoutMs);
    if (st != DOIPLITE_OK)
    {
        printUdsFailure("request-download", session, st, buf, len);
        DoipLiteClose(session);
        return 1;
    }
    std::printf("request-download ok max_block_length=%u\n",
                static_cast<unsigned>(maxBlock));
    std::fputs("  uds=", stdout);
    printHexBytes(buf, len);
    std::fputc('\n', stdout);
    DoipLiteClose(session);
    return 0;
}

bool loadFileBytes(const std::string &path, std::vector<uint8_t> *out, std::string *err)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        *err = "cannot read file: " + path;
        return false;
    }
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    if (size < 0)
    {
        *err = "cannot size file: " + path;
        return false;
    }
    in.seekg(0, std::ios::beg);
    out->resize(static_cast<std::size_t>(size));
    if (size > 0)
    {
        in.read(reinterpret_cast<char *>(out->data()), size);
        if (!in)
        {
            *err = "cannot read file: " + path;
            return false;
        }
    }
    return true;
}

int cmdTransferData(const Options &opt)
{
    if (!opt.haveTransferBlockCounter)
    {
        std::fputs(
            "doiplite-cli: transfer-data requires --transfer-block-counter "
            "(or config transfer_block_sequence_counter)\n",
            stderr);
        return 2;
    }
    std::vector<uint8_t> data = opt.transferData;
    if (data.empty() && !opt.transferDataFile.empty())
    {
        std::string err;
        if (!loadFileBytes(opt.transferDataFile, &data, &err))
        {
            std::fprintf(stderr, "doiplite-cli: %s\n", err.c_str());
            return 2;
        }
    }
    if (data.empty())
    {
        std::fputs(
            "doiplite-cli: transfer-data requires --transfer-data-hex or "
            "--transfer-data-file\n",
            stderr);
        return 2;
    }
    int code = 0;
    DoipLiteSession *session = openActivated(opt, "transfer-data", &code);
    if (!session)
    {
        return code;
    }
    uint8_t buf[64];
    uint32_t len = 0;
    const DoipLiteStatus st = UdsTransferData(
        session,
        opt.transferBlockCounter,
        data.data(),
        static_cast<uint32_t>(data.size()),
        buf,
        sizeof(buf),
        &len,
        opt.timeoutMs);
    if (st != DOIPLITE_OK)
    {
        printUdsFailure("transfer-data", session, st, buf, len);
        DoipLiteClose(session);
        return 1;
    }
    std::printf("transfer-data ok block=0x%02X bytes=%u\n",
                static_cast<unsigned>(opt.transferBlockCounter),
                static_cast<unsigned>(data.size()));
    std::fputs("  uds=", stdout);
    printHexBytes(buf, len);
    std::fputc('\n', stdout);
    DoipLiteClose(session);
    return 0;
}

int cmdRequestTransferExit(const Options &opt)
{
    int code = 0;
    DoipLiteSession *session = openActivated(opt, "request-transfer-exit", &code);
    if (!session)
    {
        return code;
    }
    uint8_t buf[64];
    uint32_t len = 0;
    const DoipLiteStatus st = UdsRequestTransferExit(
        session,
        opt.transferExitParam.empty() ? nullptr : opt.transferExitParam.data(),
        static_cast<uint32_t>(opt.transferExitParam.size()),
        buf,
        sizeof(buf),
        &len,
        opt.timeoutMs);
    if (st != DOIPLITE_OK)
    {
        printUdsFailure("request-transfer-exit", session, st, buf, len);
        DoipLiteClose(session);
        return 1;
    }
    std::fputs("request-transfer-exit ok\n  uds=", stdout);
    printHexBytes(buf, len);
    std::fputc('\n', stdout);
    DoipLiteClose(session);
    return 0;
}

int cmdEcuReset(const Options &opt)
{
    if (!opt.haveEcuResetType)
    {
        std::fputs(
            "doiplite-cli: ecu-reset requires --ecu-reset-type (or config ecu_reset_type)\n",
            stderr);
        return 2;
    }
    int code = 0;
    DoipLiteSession *session = openActivated(opt, "ecu-reset", &code);
    if (!session)
    {
        return code;
    }
    uint8_t buf[16];
    uint32_t len = 0;
    const DoipLiteStatus st =
        UdsEcuReset(session, opt.ecuResetType, buf, sizeof(buf), &len, opt.timeoutMs);
    if (st != DOIPLITE_OK)
    {
        printUdsFailure("ecu-reset", session, st, buf, len);
        DoipLiteClose(session);
        return 1;
    }
    std::printf("ecu-reset ok type=0x%02X\n", static_cast<unsigned>(opt.ecuResetType));
    std::fputs("  uds=", stdout);
    if (len == 0)
    {
        std::fputs("(empty)", stdout);
    }
    else
    {
        printHexBytes(buf, len);
    }
    std::fputc('\n', stdout);
    DoipLiteClose(session);
    return 0;
}

int cmdFlash(const Options &opt)
{
    if (!opt.haveDownloadAlfid || !opt.haveDownloadMemoryAddress)
    {
        std::fputs(
            "doiplite-cli: flash requires --download-alfid and "
            "--download-memory-address\n",
            stderr);
        return 2;
    }
    if (opt.packagePath.empty() && opt.transferData.empty() &&
        opt.transferDataFile.empty())
    {
        std::fputs(
            "doiplite-cli: flash requires --package-path or transfer data "
            "(--transfer-data-hex / --transfer-data-file)\n",
            stderr);
        return 2;
    }

    std::vector<uint8_t> payload = opt.transferData;
    if (payload.empty() && !opt.transferDataFile.empty())
    {
        std::string err;
        if (!loadFileBytes(opt.transferDataFile, &payload, &err))
        {
            std::fprintf(stderr, "doiplite-cli: %s\n", err.c_str());
            return 2;
        }
    }

    int code = 0;
    DoipLiteSession *session = openActivated(opt, "flash", &code);
    if (!session)
    {
        return code;
    }

    DoipLiteFlashJob job{};
    job.haveFlashSessionType = opt.haveFlashSessionType ? 1 : 0;
    job.flashSessionType = opt.flashSessionType;
    job.haveSecurity = 0;
    job.haveEraseRoutine = opt.haveFlashEraseRoutine ? 1 : 0;
    job.eraseRoutineSubFunction = opt.flashEraseRoutineSubFunction;
    job.eraseRoutineId = opt.flashEraseRoutineId;
    job.eraseRoutineOption =
        opt.flashEraseRoutineOption.empty() ? nullptr : opt.flashEraseRoutineOption.data();
    job.eraseRoutineOptionLen =
        static_cast<uint32_t>(opt.flashEraseRoutineOption.size());
    job.dataFormatId = opt.haveDownloadDataFormatId ? opt.downloadDataFormatId : 0;
    job.addressAndLengthFormatId = opt.downloadAlfid;
    job.memoryAddress = opt.downloadMemoryAddress;
    job.memorySize = opt.haveDownloadMemorySize ? opt.downloadMemorySize : 0;
    if (!payload.empty())
    {
        job.data = payload.data();
        job.dataLen = static_cast<uint32_t>(payload.size());
    }
    else
    {
        job.packagePath = opt.packagePath.c_str();
    }
    job.maxBlockDataCap = opt.haveFlashMaxBlockDataCap ? opt.flashMaxBlockDataCap : 0;
    job.transferExitParam =
        opt.transferExitParam.empty() ? nullptr : opt.transferExitParam.data();
    job.transferExitParamLen = static_cast<uint32_t>(opt.transferExitParam.size());
    job.haveEcuReset = opt.haveFlashResetType ? 1 : 0;
    job.resetType = opt.flashResetType;
    job.keepAliveIntervalMs =
        opt.haveFlashKeepAliveInterval ? opt.flashKeepAliveIntervalMs : 0;
    job.haveKeepAliveSubFunction = opt.haveFlashKeepAliveSubFunction ? 1 : 0;
    job.keepAliveSubFunction = opt.flashKeepAliveSubFunction;
    job.timeoutMs = opt.timeoutMs;

    DoipLiteStatus st = DoipLiteFlashStart(session, &job);
    if (st != DOIPLITE_OK)
    {
        std::fprintf(stderr, "doiplite-cli: flash failed: %s\n", DoipLiteStatusString(st));
        DoipLiteClose(session);
        return 1;
    }

    DoipLiteFlashProgress prog{};
    for (int i = 0; i < 100000; ++i)
    {
        st = DoipLiteFlashPoll(session, &prog);
        if (prog.phase == DOIPLITE_FLASH_DONE || prog.phase == DOIPLITE_FLASH_FAILED)
        {
            break;
        }
    }
    if (prog.phase != DOIPLITE_FLASH_DONE || st != DOIPLITE_OK)
    {
        std::fprintf(stderr,
                     "doiplite-cli: flash failed: %s phase=%d percent=%u\n",
                     DoipLiteStatusString(st != DOIPLITE_OK ? st : prog.err),
                     static_cast<int>(prog.phase),
                     static_cast<unsigned>(prog.percent));
        DoipLiteClose(session);
        return 1;
    }
    std::printf("flash ok percent=%u\n", static_cast<unsigned>(prog.percent));
    DoipLiteClose(session);
    return 0;
}

const char *requireValue(int argc, char **argv, int *i, const char *name)
{
    if (*i + 1 >= argc)
    {
        std::fprintf(stderr, "doiplite-cli: %s requires a value\n", name);
        return nullptr;
    }
    ++*i;
    return argv[*i];
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 2 || std::strcmp(argv[1], "--help") == 0 ||
        std::strcmp(argv[1], "-h") == 0)
    {
        printHelp();
        return 0;
    }

    const char *cmd = argv[1];
    const char *config = nullptr;
    Options flags;
    std::string err;

    for (int i = 2; i < argc; ++i)
    {
        const char *arg = argv[i];
        if (std::strcmp(arg, "--config") == 0)
        {
            config = requireValue(argc, argv, &i, "--config");
            if (!config)
            {
                return 2;
            }
        }
        else if (std::strcmp(arg, "--ip") == 0)
        {
            const char *v = requireValue(argc, argv, &i, "--ip");
            if (!v)
            {
                return 2;
            }
            flags.ip = v;
        }
        else if (std::strcmp(arg, "--broadcast") == 0)
        {
            const char *v = requireValue(argc, argv, &i, "--broadcast");
            if (!v)
            {
                return 2;
            }
            flags.broadcast = v;
        }
        else if (std::strcmp(arg, "--port") == 0)
        {
            const char *v = requireValue(argc, argv, &i, "--port");
            if (!v)
            {
                return 2;
            }
            if (!parseU16(v, &flags.port) || flags.port == 0)
            {
                std::fputs("doiplite-cli: invalid --port\n", stderr);
                return 2;
            }
            flags.havePort = true;
        }
        else if (std::strcmp(arg, "--tester-addr") == 0)
        {
            const char *v = requireValue(argc, argv, &i, "--tester-addr");
            if (!v)
            {
                return 2;
            }
            if (!parseU16(v, &flags.testerAddr))
            {
                std::fputs("doiplite-cli: invalid --tester-addr\n", stderr);
                return 2;
            }
            flags.haveTesterAddr = true;
        }
        else if (std::strcmp(arg, "--activation-type") == 0)
        {
            const char *v = requireValue(argc, argv, &i, "--activation-type");
            if (!v)
            {
                return 2;
            }
            if (!parseU16(v, &flags.activationType))
            {
                std::fputs("doiplite-cli: invalid --activation-type\n", stderr);
                return 2;
            }
            flags.haveActivationType = true;
        }
        else if (std::strcmp(arg, "--timeout-ms") == 0)
        {
            const char *v = requireValue(argc, argv, &i, "--timeout-ms");
            if (!v)
            {
                return 2;
            }
            if (!parseU32(v, &flags.timeoutMs))
            {
                std::fputs("doiplite-cli: invalid --timeout-ms\n", stderr);
                return 2;
            }
            flags.haveTimeout = true;
        }
        else if (std::strcmp(arg, "--did") == 0)
        {
            const char *v = requireValue(argc, argv, &i, "--did");
            if (!v)
            {
                return 2;
            }
            if (!parseU16(v, &flags.did))
            {
                std::fputs("doiplite-cli: invalid --did\n", stderr);
                return 2;
            }
            flags.haveDid = true;
        }
        else if (std::strcmp(arg, "--session") == 0)
        {
            const char *v = requireValue(argc, argv, &i, "--session");
            if (!v)
            {
                return 2;
            }
            if (!parseU8(v, &flags.sessionType))
            {
                std::fputs("doiplite-cli: invalid --session\n", stderr);
                return 2;
            }
            flags.haveSessionType = true;
        }
        else if (std::strcmp(arg, "--sub-function") == 0)
        {
            const char *v = requireValue(argc, argv, &i, "--sub-function");
            if (!v)
            {
                return 2;
            }
            if (!parseU8(v, &flags.testerPresentSub))
            {
                std::fputs("doiplite-cli: invalid --sub-function\n", stderr);
                return 2;
            }
            flags.haveTesterPresentSub = true;
        }
        else if (std::strcmp(arg, "--routine-sub-function") == 0)
        {
            const char *v = requireValue(argc, argv, &i, "--routine-sub-function");
            if (!v)
            {
                return 2;
            }
            if (!parseU8(v, &flags.routineSubFunction))
            {
                std::fputs("doiplite-cli: invalid --routine-sub-function\n", stderr);
                return 2;
            }
            flags.haveRoutineSubFunction = true;
        }
        else if (std::strcmp(arg, "--routine-id") == 0)
        {
            const char *v = requireValue(argc, argv, &i, "--routine-id");
            if (!v)
            {
                return 2;
            }
            if (!parseU16(v, &flags.routineId))
            {
                std::fputs("doiplite-cli: invalid --routine-id\n", stderr);
                return 2;
            }
            flags.haveRoutineId = true;
        }
        else if (std::strcmp(arg, "--routine-option-hex") == 0)
        {
            const char *v = requireValue(argc, argv, &i, "--routine-option-hex");
            if (!v || !parseHexBytes(v, &flags.routineOption))
            {
                std::fputs("doiplite-cli: invalid --routine-option-hex\n", stderr);
                return 2;
            }
        }
        else if (std::strcmp(arg, "--download-data-format-id") == 0)
        {
            const char *v = requireValue(argc, argv, &i, "--download-data-format-id");
            if (!v || !parseU8(v, &flags.downloadDataFormatId))
            {
                std::fputs("doiplite-cli: invalid --download-data-format-id\n", stderr);
                return 2;
            }
            flags.haveDownloadDataFormatId = true;
        }
        else if (std::strcmp(arg, "--download-alfid") == 0)
        {
            const char *v = requireValue(argc, argv, &i, "--download-alfid");
            if (!v || !parseU8(v, &flags.downloadAlfid))
            {
                std::fputs("doiplite-cli: invalid --download-alfid\n", stderr);
                return 2;
            }
            flags.haveDownloadAlfid = true;
        }
        else if (std::strcmp(arg, "--download-memory-address") == 0)
        {
            const char *v = requireValue(argc, argv, &i, "--download-memory-address");
            if (!v || !parseU64(v, &flags.downloadMemoryAddress))
            {
                std::fputs("doiplite-cli: invalid --download-memory-address\n", stderr);
                return 2;
            }
            flags.haveDownloadMemoryAddress = true;
        }
        else if (std::strcmp(arg, "--download-memory-size") == 0)
        {
            const char *v = requireValue(argc, argv, &i, "--download-memory-size");
            if (!v || !parseU64(v, &flags.downloadMemorySize))
            {
                std::fputs("doiplite-cli: invalid --download-memory-size\n", stderr);
                return 2;
            }
            flags.haveDownloadMemorySize = true;
        }
        else if (std::strcmp(arg, "--transfer-block-counter") == 0)
        {
            const char *v = requireValue(argc, argv, &i, "--transfer-block-counter");
            if (!v || !parseU8(v, &flags.transferBlockCounter))
            {
                std::fputs("doiplite-cli: invalid --transfer-block-counter\n", stderr);
                return 2;
            }
            flags.haveTransferBlockCounter = true;
        }
        else if (std::strcmp(arg, "--transfer-data-hex") == 0)
        {
            const char *v = requireValue(argc, argv, &i, "--transfer-data-hex");
            if (!v || !parseHexBytes(v, &flags.transferData))
            {
                std::fputs("doiplite-cli: invalid --transfer-data-hex\n", stderr);
                return 2;
            }
        }
        else if (std::strcmp(arg, "--transfer-data-file") == 0)
        {
            const char *v = requireValue(argc, argv, &i, "--transfer-data-file");
            if (!v)
            {
                return 2;
            }
            flags.transferDataFile = v;
        }
        else if (std::strcmp(arg, "--transfer-exit-param-hex") == 0)
        {
            const char *v = requireValue(argc, argv, &i, "--transfer-exit-param-hex");
            if (!v || !parseHexBytes(v, &flags.transferExitParam))
            {
                std::fputs("doiplite-cli: invalid --transfer-exit-param-hex\n", stderr);
                return 2;
            }
            flags.haveTransferExitParam = true;
        }
        else if (std::strcmp(arg, "--ecu-reset-type") == 0)
        {
            const char *v = requireValue(argc, argv, &i, "--ecu-reset-type");
            if (!v || !parseU8(v, &flags.ecuResetType))
            {
                std::fputs("doiplite-cli: invalid --ecu-reset-type\n", stderr);
                return 2;
            }
            flags.haveEcuResetType = true;
        }
        else if (std::strcmp(arg, "--flash-session-type") == 0)
        {
            const char *v = requireValue(argc, argv, &i, "--flash-session-type");
            if (!v || !parseU8(v, &flags.flashSessionType))
            {
                std::fputs("doiplite-cli: invalid --flash-session-type\n", stderr);
                return 2;
            }
            flags.haveFlashSessionType = true;
        }
        else if (std::strcmp(arg, "--flash-erase-routine-sub-function") == 0)
        {
            const char *v =
                requireValue(argc, argv, &i, "--flash-erase-routine-sub-function");
            if (!v || !parseU8(v, &flags.flashEraseRoutineSubFunction))
            {
                std::fputs(
                    "doiplite-cli: invalid --flash-erase-routine-sub-function\n", stderr);
                return 2;
            }
            flags.haveFlashEraseRoutine = true;
        }
        else if (std::strcmp(arg, "--flash-erase-routine-id") == 0)
        {
            const char *v = requireValue(argc, argv, &i, "--flash-erase-routine-id");
            if (!v || !parseU16(v, &flags.flashEraseRoutineId))
            {
                std::fputs("doiplite-cli: invalid --flash-erase-routine-id\n", stderr);
                return 2;
            }
            flags.haveFlashEraseRoutine = true;
        }
        else if (std::strcmp(arg, "--flash-erase-routine-option-hex") == 0)
        {
            const char *v =
                requireValue(argc, argv, &i, "--flash-erase-routine-option-hex");
            if (!v || !parseHexBytes(v, &flags.flashEraseRoutineOption))
            {
                std::fputs(
                    "doiplite-cli: invalid --flash-erase-routine-option-hex\n", stderr);
                return 2;
            }
        }
        else if (std::strcmp(arg, "--flash-max-block-data-cap") == 0)
        {
            const char *v = requireValue(argc, argv, &i, "--flash-max-block-data-cap");
            if (!v || !parseU32(v, &flags.flashMaxBlockDataCap))
            {
                std::fputs("doiplite-cli: invalid --flash-max-block-data-cap\n", stderr);
                return 2;
            }
            flags.haveFlashMaxBlockDataCap = true;
        }
        else if (std::strcmp(arg, "--package-path") == 0)
        {
            const char *v = requireValue(argc, argv, &i, "--package-path");
            if (!v)
            {
                return 2;
            }
            flags.packagePath = v;
        }
        else if (std::strcmp(arg, "--flash-reset-type") == 0)
        {
            const char *v = requireValue(argc, argv, &i, "--flash-reset-type");
            if (!v || !parseU8(v, &flags.flashResetType))
            {
                std::fputs("doiplite-cli: invalid --flash-reset-type\n", stderr);
                return 2;
            }
            flags.haveFlashResetType = true;
        }
        else if (std::strcmp(arg, "--flash-keep-alive-interval-ms") == 0)
        {
            const char *v =
                requireValue(argc, argv, &i, "--flash-keep-alive-interval-ms");
            if (!v || !parseU32(v, &flags.flashKeepAliveIntervalMs))
            {
                std::fputs(
                    "doiplite-cli: invalid --flash-keep-alive-interval-ms\n", stderr);
                return 2;
            }
            flags.haveFlashKeepAliveInterval = true;
        }
        else if (std::strcmp(arg, "--flash-keep-alive-sub-function") == 0)
        {
            const char *v =
                requireValue(argc, argv, &i, "--flash-keep-alive-sub-function");
            if (!v || !parseU8(v, &flags.flashKeepAliveSubFunction))
            {
                std::fputs(
                    "doiplite-cli: invalid --flash-keep-alive-sub-function\n", stderr);
                return 2;
            }
            flags.haveFlashKeepAliveSubFunction = true;
        }
        else if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0)
        {
            printHelp();
            return 0;
        }
        else
        {
            std::fprintf(stderr, "doiplite-cli: unknown argument: %s\n", arg);
            return 2;
        }
    }

    Options opt;
    if (config)
    {
        if (!loadConfig(config, &opt, &err))
        {
            std::fprintf(stderr, "doiplite-cli: %s\n", err.c_str());
            return 2;
        }
    }
    mergeFlags(&opt, flags);

    if (std::strcmp(cmd, "discover") == 0)
    {
        return cmdDiscover(opt);
    }
    if (std::strcmp(cmd, "activate") == 0)
    {
        return cmdActivate(opt);
    }
    if (std::strcmp(cmd, "read-version") == 0)
    {
        return cmdReadVersion(opt);
    }
    if (std::strcmp(cmd, "session") == 0)
    {
        return cmdSession(opt);
    }
    if (std::strcmp(cmd, "tester-present") == 0)
    {
        return cmdTesterPresent(opt);
    }
    if (std::strcmp(cmd, "routine-control") == 0)
    {
        return cmdRoutineControl(opt);
    }
    if (std::strcmp(cmd, "request-download") == 0)
    {
        return cmdRequestDownload(opt);
    }
    if (std::strcmp(cmd, "transfer-data") == 0)
    {
        return cmdTransferData(opt);
    }
    if (std::strcmp(cmd, "request-transfer-exit") == 0)
    {
        return cmdRequestTransferExit(opt);
    }
    if (std::strcmp(cmd, "ecu-reset") == 0)
    {
        return cmdEcuReset(opt);
    }
    if (std::strcmp(cmd, "flash") == 0)
    {
        return cmdFlash(opt);
    }

    std::fprintf(stderr, "doiplite-cli: unknown command: %s\n", cmd);
    return 2;
}
