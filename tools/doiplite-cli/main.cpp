#include "doiplite/doiplite.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

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
    bool havePort = false;
    bool haveTesterAddr = false;
    bool haveActivationType = false;
    bool haveTimeout = false;
};

void printHelp()
{
    std::fputs(
        "doiplite-cli — DoipLite command-line tool (ISO 13400-2 generic DoIP)\n"
        "\n"
        "Usage:\n"
        "  doiplite-cli --help\n"
        "  doiplite-cli discover  [options]\n"
        "  doiplite-cli activate  [options]\n"
        "  doiplite-cli read-version [options]\n"
        "\n"
        "Options:\n"
        "  --config <path>           Load key=value settings (flags override file)\n"
        "  --ip <ipv4>               Entity IPv4 address (activate)\n"
        "  --broadcast <ipv4>        Discover destination (default: 255.255.255.255)\n"
        "  --port <n>                UDP/TCP port (default: 13400)\n"
        "  --tester-addr <addr>      Tester logical address (hex 0x0E00 or decimal)\n"
        "  --activation-type <n>     Routing activation type (default: 0x00 ISO default)\n"
        "  --timeout-ms <n>          I/O timeout in milliseconds (default: 2000)\n"
        "\n"
        "discover prints the IP and logical address of each responding entity and\n"
        "exits 0 if at least one is found. activate opens a TCP diagnostic connection\n"
        "and performs routing activation.\n"
        "\n"
        "If --tester-addr / tester_address is omitted, activate uses 0x0E00 as an\n"
        "example default from the ISO 13400 external test equipment range. Replace it\n"
        "with your own tester address; it is not a field value. Do not hard-code plant\n"
        "IPs, VINs, or secrets. See examples/config/example.conf.\n"
        "\n"
        "read-version is reserved for M2 and is not implemented yet.\n",
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

bool parseU32(const std::string &text, uint32_t *out)
{
    if (!out || text.empty())
    {
        return false;
    }
    char *end = nullptr;
    const bool hex = text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X');
    const unsigned long value = std::strtoul(text.c_str(), &end, hex ? 16 : 10);
    if (!end || *end != '\0')
    {
        return false;
    }
    *out = static_cast<uint32_t>(value);
    return true;
}

bool parseU16(const std::string &text, uint16_t *out)
{
    uint32_t v = 0;
    if (!parseU32(text, &v) || v > 0xffffu)
    {
        return false;
    }
    *out = static_cast<uint16_t>(v);
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
    // Unknown keys (e.g. version_did, entity_address) are ignored.
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
        std::fprintf(stderr,
                     "doiplite-cli: 'read-version' is not implemented yet (%s)\n",
                     DoipLiteStatusString(DOIPLITE_ERR_NOT_IMPLEMENTED));
        return 1;
    }

    std::fprintf(stderr, "doiplite-cli: unknown command: %s\n", cmd);
    return 2;
}
