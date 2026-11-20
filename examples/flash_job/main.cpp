// Example 2: flash job parameter construction -> (optional) start/poll -> cleanup.
// DEFAULT MODE is dry-run: validate config and print the planned process only.
// No discover/connect/UDS/flash network calls unless --execute is passed.
// Execution can erase/write/reset an ECU — use only on intentional targets.
// 0x27 security access is NOT implemented by the library; jobs that require it
// fail with DOIPLITE_ERR_NOT_IMPLEMENTED (handled explicitly below).

#include "station_config.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

void printUsage(const char *argv0)
{
    std::fprintf(
        stderr,
        "Usage: %s --config <station.conf> [--execute]\n"
        "\n"
        "Default (no --execute): validate flash parameters from the config and\n"
        "print the planned flash process. Performs NO network I/O and does NOT\n"
        "mutate any ECU.\n"
        "\n"
        "With --execute: discover, select device by identity selectors, connect,\n"
        "activate, DoipLiteFlashStart/Poll until DONE or FAILED, then close.\n"
        "WARNING: --execute can erase, write, and/or reset an ECU. Confirm the\n"
        "config targets the intended device before using this flag.\n"
        "\n"
        "Security (0x27): if flash_require_security=1, the library returns\n"
        "DOIPLITE_ERR_NOT_IMPLEMENTED during the SECURITY phase; this example\n"
        "reports that clearly and does not attempt seed-key algorithms.\n"
        "\n"
        "See examples/config/station_template.conf and examples/README_zh.md.\n",
        argv0 ? argv0 : "doiplite-example-flash-job");
}

const char *requireValue(int argc, char **argv, int *i, const char *name)
{
    if (*i + 1 >= argc)
    {
        std::fprintf(stderr, "error: %s requires a value\n", name);
        return nullptr;
    }
    ++(*i);
    return argv[*i];
}

void printVehicle(const DoipLiteVehicle &v)
{
    char vin[18];
    std::memset(vin, 0, sizeof(vin));
    for (int i = 0; i < 17; ++i)
    {
        const unsigned char c = v.vin[i];
        vin[i] = (c >= 32 && c < 127) ? static_cast<char>(c) : '.';
    }
    std::printf("  ip=%s tcp_port=%u logical_address=0x%04X vin=%s\n",
                v.ip,
                static_cast<unsigned>(v.tcpPort),
                static_cast<unsigned>(v.logicalAddress),
                vin);
}

void printPlannedProcess(const doiplite_examples::StationConfig &cfg)
{
    std::puts("=== flash dry-run (no network, no ECU changes) ===");
    std::printf("identity selectors:");
    if (cfg.haveSelectLogicalAddress)
    {
        std::printf(" logical_address=0x%04X",
                    static_cast<unsigned>(cfg.selectLogicalAddress));
    }
    if (cfg.haveSelectVin)
    {
        std::printf(" vin=%s", cfg.selectVin.c_str());
    }
    std::fputc('\n', stdout);
    std::printf("tester_address=0x%04X activation_type=0x%04X timeout_ms=%u\n",
                static_cast<unsigned>(cfg.testerAddress),
                static_cast<unsigned>(cfg.haveActivationType ? cfg.activationType : 0),
                static_cast<unsigned>(cfg.timeoutMs));

    std::puts("planned phases:");
    if (cfg.haveFlashSessionType)
    {
        std::printf("  1. SESSION (0x10) session_type=0x%02X\n",
                    static_cast<unsigned>(cfg.flashSessionType));
    }
    else
    {
        std::puts("  1. SESSION skipped (flash_session_type not set)");
    }

    if (cfg.haveSecurity)
    {
        std::printf(
            "  2. SECURITY (0x27) level=0x%02X — library returns NOT_IMPLEMENTED\n",
            static_cast<unsigned>(cfg.securityLevel));
    }
    else
    {
        std::puts("  2. SECURITY skipped (flash_require_security=0)");
    }

    if (cfg.haveFlashEraseSubFunction && cfg.haveFlashEraseRoutineId)
    {
        std::printf("  3. ERASE (0x31) sub=0x%02X id=0x%04X option_len=%zu\n",
                    static_cast<unsigned>(cfg.flashEraseRoutineSubFunction),
                    static_cast<unsigned>(cfg.flashEraseRoutineId),
                    cfg.flashEraseRoutineOption.size());
    }
    else
    {
        std::puts("  3. ERASE skipped");
    }

    std::printf("  4. REQUEST_DOWNLOAD (0x34) alfid=0x%02X fmt=0x%02X addr=0x%llX",
                static_cast<unsigned>(cfg.downloadAlfid),
                static_cast<unsigned>(cfg.haveDownloadDataFormatId ? cfg.downloadDataFormatId
                                                                   : 0),
                static_cast<unsigned long long>(cfg.downloadMemoryAddress));
    if (cfg.haveDownloadMemorySize)
    {
        std::printf(" size=0x%llX",
                    static_cast<unsigned long long>(cfg.downloadMemorySize));
    }
    else
    {
        std::fputs(" size=<payload length>", stdout);
    }
    std::fputc('\n', stdout);

    std::printf("  5. TRANSFER (0x36) package_path=%s", cfg.packagePath.c_str());
    if (cfg.haveFlashMaxBlockDataCap)
    {
        std::printf(" max_block_data_cap=%u",
                    static_cast<unsigned>(cfg.flashMaxBlockDataCap));
    }
    std::fputc('\n', stdout);

    std::printf("  6. TRANSFER_EXIT (0x37) param_len=%zu\n",
                cfg.transferExitParam.size());

    if (cfg.haveFlashResetType)
    {
        std::printf("  7. RESET (0x11) type=0x%02X\n",
                    static_cast<unsigned>(cfg.flashResetType));
    }
    else
    {
        std::puts("  7. RESET skipped");
    }

    if (cfg.haveFlashKeepAliveInterval && cfg.haveFlashKeepAliveSubFunction)
    {
        std::printf(
            "  keep-alive: TesterPresent (0x3E) interval_ms=%u sub_function=0x%02X "
            "(between flash steps / 0x36 blocks only; not during response waits)\n",
            static_cast<unsigned>(cfg.flashKeepAliveIntervalMs),
            static_cast<unsigned>(cfg.flashKeepAliveSubFunction));
    }
    else
    {
        std::puts("  keep-alive: disabled (set both flash_keep_alive_interval_ms>0 "
                  "and flash_keep_alive_sub_function to enable; no library defaults)");
    }

    std::puts("dry-run complete. Pass --execute to perform discover/connect/flash.");
    std::puts(
        "WARNING: --execute can erase/write/reset an ECU. Confirm target identity first.");
}

int runExecute(const doiplite_examples::StationConfig &cfg)
{
    std::fputs(
        "WARNING: executing flash job — this may erase, write, and/or reset an ECU.\n",
        stderr);

    const char *dest = cfg.broadcast.empty() ? nullptr : cfg.broadcast.c_str();
    const uint16_t udpPort = cfg.haveUdpPort ? cfg.udpPort : 0;
    const uint32_t discoverMs =
        cfg.haveDiscoverTimeout ? cfg.discoverTimeoutMs : cfg.timeoutMs;

    constexpr uint32_t kCap = 64;
    DoipLiteVehicle vehicles[kCap];
    uint32_t count = 0;
    const DoipLiteStatus disc =
        DoipLiteDiscoverEx(discoverMs, dest, udpPort, vehicles, kCap, &count);
    if (disc != DOIPLITE_OK)
    {
        std::fprintf(stderr,
                     "error: discover failed: %s (count=%u)\n",
                     DoipLiteStatusString(disc),
                     static_cast<unsigned>(count));
        return 1;
    }
    std::printf("discover: %u vehicle(s)\n", static_cast<unsigned>(count));

    uint32_t index = 0;
    uint32_t matches = 0;
    std::string err;
    const auto sel = doiplite_examples::SelectUniqueVehicle(
        vehicles, count, cfg, &index, &matches, &err);
    if (sel != doiplite_examples::DeviceSelectStatus::Ok)
    {
        std::fprintf(stderr, "error: device select: %s (matches=%u)\n", err.c_str(),
                     static_cast<unsigned>(matches));
        return 1;
    }
    const DoipLiteVehicle &chosen = vehicles[index];
    std::fputs("selected:\n", stdout);
    printVehicle(chosen);

    DoipLiteSession *session = DoipLiteOpen(chosen.ip, chosen.tcpPort);
    if (!session)
    {
        std::fputs("error: DoipLiteOpen failed\n", stderr);
        return 1;
    }

    const uint16_t activationType =
        cfg.haveActivationType ? cfg.activationType : static_cast<uint16_t>(0x00);
    DoipLiteStatus st = DoipLiteRoutingActivationEx(
        session, cfg.testerAddress, activationType, nullptr, 0, cfg.timeoutMs);
    if (st != DOIPLITE_OK)
    {
        std::fprintf(stderr,
                     "error: routing activation failed: %s (doip_code=0x%02X)\n",
                     DoipLiteStatusString(st),
                     static_cast<unsigned>(DoipLiteLastDoipCode(session)));
        DoipLiteClose(session);
        return 1;
    }

    if (cfg.haveP2)
    {
        DoipLiteSetP2Timing(session, cfg.p2Ms, cfg.p2StarMs);
    }

    DoipLiteFlashJob job{};
    doiplite_examples::FillFlashJob(cfg, &job);

    st = DoipLiteFlashStart(session, &job);
    if (st != DOIPLITE_OK)
    {
        std::fprintf(stderr, "error: FlashStart failed: %s\n", DoipLiteStatusString(st));
        DoipLiteClose(session);
        return 1;
    }

    DoipLiteFlashProgress prog{};
    for (int i = 0; i < 100000; ++i)
    {
        st = DoipLiteFlashPoll(session, &prog);
        std::printf("flash poll phase=%s percent=%u status=%s\n",
                    doiplite_examples::FlashPhaseName(prog.phase),
                    static_cast<unsigned>(prog.percent),
                    DoipLiteStatusString(st));
        if (prog.phase == DOIPLITE_FLASH_DONE || prog.phase == DOIPLITE_FLASH_FAILED)
        {
            break;
        }
    }

    if (prog.phase == DOIPLITE_FLASH_FAILED || st != DOIPLITE_OK)
    {
        const DoipLiteStatus failSt = (st != DOIPLITE_OK) ? st : prog.err;
        if (failSt == DOIPLITE_ERR_NOT_IMPLEMENTED &&
            prog.phase == DOIPLITE_FLASH_SECURITY)
        {
            std::fputs(
                "error: flash stopped at SECURITY — UDS 0x27 is not implemented "
                "by DoipLite (DOIPLITE_ERR_NOT_IMPLEMENTED). Clear "
                "flash_require_security or provide an external security solution "
                "outside this library.\n",
                stderr);
        }
        else if (failSt == DOIPLITE_ERR_NOT_IMPLEMENTED)
        {
            std::fprintf(stderr,
                         "error: flash failed with NOT_IMPLEMENTED phase=%s\n",
                         doiplite_examples::FlashPhaseName(prog.phase));
        }
        else
        {
            std::fprintf(stderr,
                         "error: flash failed: %s phase=%s percent=%u\n",
                         DoipLiteStatusString(failSt),
                         doiplite_examples::FlashPhaseName(prog.phase),
                         static_cast<unsigned>(prog.percent));
        }
        DoipLiteClose(session);
        return 1;
    }

    std::printf("flash ok percent=%u\n", static_cast<unsigned>(prog.percent));
    DoipLiteClose(session);
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    const char *configPath = nullptr;
    bool execute = false;

    for (int i = 1; i < argc; ++i)
    {
        const char *arg = argv[i];
        if (std::strcmp(arg, "--help") == 0 || std::strcmp(arg, "-h") == 0)
        {
            printUsage(argv[0]);
            return 0;
        }
        if (std::strcmp(arg, "--config") == 0)
        {
            configPath = requireValue(argc, argv, &i, "--config");
            if (!configPath)
            {
                return 2;
            }
            continue;
        }
        if (std::strcmp(arg, "--execute") == 0)
        {
            execute = true;
            continue;
        }
        std::fprintf(stderr, "error: unknown argument: %s\n", arg);
        printUsage(argv[0]);
        return 2;
    }

    if (!configPath)
    {
        std::fputs("error: --config <path> is required\n", stderr);
        printUsage(argv[0]);
        return 2;
    }

    doiplite_examples::StationConfig cfg;
    std::string err;
    const auto loadSt = doiplite_examples::LoadStationConfig(configPath, &cfg, &err);
    if (loadSt != doiplite_examples::ConfigLoadStatus::Ok)
    {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 2;
    }
    if (!doiplite_examples::ValidateFlashConfig(cfg, &err))
    {
        std::fprintf(stderr, "error: invalid config: %s\n", err.c_str());
        return 2;
    }

    if (!execute)
    {
        printPlannedProcess(cfg);
        return 0;
    }
    return runExecute(cfg);
}
