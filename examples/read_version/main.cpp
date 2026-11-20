// Example 1: discover -> select by config identity -> TCP -> routing activation
// -> ReadDataByIdentifier (version DID) -> cleanup.
// Uses only the public C API. Station parameters come from a config file.

#include "station_config.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

void printUsage(const char *argv0)
{
    std::fprintf(stderr,
                 "Usage: %s --config <station.conf>\n"
                 "\n"
                 "UDP discover, select exactly one device by identity selectors in the\n"
                 "config, connect/activate, read version_did via UdsReadDataByIdentifier,\n"
                 "then close the session.\n"
                 "\n"
                 "See examples/config/station_template.conf and examples/README_zh.md.\n",
                 argv0 ? argv0 : "doiplite-example-read-version");
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

void printHex(const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; ++i)
    {
        std::printf("%02X%s", static_cast<unsigned>(data[i]), (i + 1 < len) ? " " : "");
    }
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

} // namespace

int main(int argc, char **argv)
{
    const char *configPath = nullptr;
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
    if (!doiplite_examples::ValidateReadVersionConfig(cfg, &err))
    {
        std::fprintf(stderr, "error: invalid config: %s\n", err.c_str());
        return 2;
    }

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
    for (uint32_t i = 0; i < count; ++i)
    {
        printVehicle(vehicles[i]);
    }

    uint32_t index = 0;
    uint32_t matches = 0;
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
    std::printf("routing activation ok entity=0x%04X\n",
                static_cast<unsigned>(DoipLiteEntityAddress(session)));

    if (cfg.haveP2)
    {
        DoipLiteSetP2Timing(session, cfg.p2Ms, cfg.p2StarMs);
    }

    uint8_t data[256];
    uint32_t len = 0;
    st = UdsReadDataByIdentifier(
        session, cfg.versionDid, data, sizeof(data), &len, cfg.timeoutMs);
    if (st != DOIPLITE_OK)
    {
        std::fprintf(stderr,
                     "error: read version DID 0x%04X failed: %s\n",
                     static_cast<unsigned>(cfg.versionDid),
                     DoipLiteStatusString(st));
        if (st == DOIPLITE_ERR_UDS_NRC && len > 0)
        {
            std::fputs("  nrc_frame=", stderr);
            printHex(data, len);
            std::fputc('\n', stderr);
        }
        DoipLiteClose(session);
        return 1;
    }

    std::printf("version DID 0x%04X data_record_len=%u data=",
                static_cast<unsigned>(cfg.versionDid),
                static_cast<unsigned>(len));
    if (len == 0)
    {
        std::fputs("(empty)\n", stdout);
    }
    else
    {
        printHex(data, len);
        std::fputc('\n', stdout);
    }

    DoipLiteClose(session);
    return 0;
}
