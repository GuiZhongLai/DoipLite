#include "doiplite/doiplite.h"
#include "loopback_entity.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#ifdef _WIN32
#define DOIPLITE_POPEN _popen
#define DOIPLITE_PCLOSE _pclose
#else
#include <sys/wait.h>
#define DOIPLITE_POPEN popen
#define DOIPLITE_PCLOSE pclose
#endif

static void expect(bool cond, const char *msg)
{
    if (!cond)
    {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        std::exit(1);
    }
}

static int cmd_status(int raw)
{
    if (raw < 0)
    {
        return raw;
    }
#ifdef _WIN32
    return raw;
#else
    if (WIFEXITED(raw))
    {
        return WEXITSTATUS(raw);
    }
    return raw;
#endif
}

#ifdef DOIPLITE_CLI_PATH
static bool run_cli(const std::string &args, int *exitCode, std::string *output)
{
    const std::string cmd = std::string("\"") + DOIPLITE_CLI_PATH + "\" " + args + " 2>&1";
    FILE *fp = DOIPLITE_POPEN(cmd.c_str(), "r");
    if (!fp)
    {
        return false;
    }
    std::string text;
    char buf[512];
    while (std::fgets(buf, sizeof(buf), fp) != nullptr)
    {
        text += buf;
    }
    const int raw = DOIPLITE_PCLOSE(fp);
    if (exitCode)
    {
        *exitCode = cmd_status(raw);
    }
    if (output)
    {
        *output = text;
    }
    return true;
}
#endif

int main()
{
    expect(DoipLiteStatusString(DOIPLITE_OK) != nullptr, "status string");
    expect(DoipLiteOpen(nullptr, 0) == nullptr, "open null ip");
    expect(DoipLiteOpen("", 0) == nullptr, "open empty ip");
    expect(DoipLiteOpen("not-an-ipv4", 13400) == nullptr, "open bad ip");
    DoipLiteClose(nullptr);

    uint32_t count = 99;
    expect(DoipLiteDiscover(100, nullptr, 0, nullptr) == DOIPLITE_ERR_INVALID_ARG,
           "discover null count");
    expect(DoipLiteDiscoverEx(100, "127.0.0.1", 13400, nullptr, 4, &count) ==
               DOIPLITE_ERR_INVALID_ARG,
           "discover cap without out");
    expect(DoipLiteDiscoverEx(100, "not-an-ipv4", 0, nullptr, 0, &count) ==
               DOIPLITE_ERR_INVALID_ARG,
           "discover bad dest");

    LoopbackEntity::Options timeoutOpts;
    timeoutOpts.replyVid = false;
    timeoutOpts.replyRouting = false;
    LoopbackEntity noReply(timeoutOpts);
    expect(noReply.start(), "start silent entity");
    count = 99;
    const DoipLiteStatus idle = DoipLiteDiscoverEx(
        200, "127.0.0.1", noReply.port(), nullptr, 0, &count);
    expect(idle == DOIPLITE_ERR_TIMEOUT, "discover timeout with no vehicles");
    expect(count == 0, "discover count is zero on timeout");
    noReply.stop();

    LoopbackEntity entity;
    expect(entity.start(), "start loopback entity");
    expect(entity.port() != 0, "ephemeral port assigned");

    DoipLiteVehicle vehicles[4];
    count = 0;
    const DoipLiteStatus found = DoipLiteDiscoverEx(
        1000, entity.ip(), entity.port(), vehicles, 4, &count);
    expect(found == DOIPLITE_OK, "discover finds loopback entity");
    expect(count == 1, "one vehicle");
    expect(std::strcmp(vehicles[0].ip, "127.0.0.1") == 0, "vehicle ip");
    expect(vehicles[0].tcpPort == entity.port(), "vehicle tcp port");
    expect(vehicles[0].logicalAddress == 0x1234, "vehicle logical address");
    expect(std::memcmp(vehicles[0].vin, "DOIPLITEEXAMPLE01", 17) == 0, "example vin");

    DoipLiteSession *session = DoipLiteOpen(entity.ip(), entity.port());
    expect(session != nullptr, "open loopback");
    expect(DoipLiteLastDoipCode(session) == 0, "no nack yet");
    expect(DoipLiteEntityAddress(session) == 0, "no entity yet");
    expect(DoipLiteRoutingActivation(session, 0x0E00, 0x00, nullptr, 0) == DOIPLITE_OK,
           "routing activation success");
    expect(DoipLiteLastDoipCode(session) == 0x10, "success response code");
    expect(DoipLiteEntityAddress(session) == 0x1234, "entity address from response");

    expect(DoipLiteRoutingActivation(nullptr, 0, 0, nullptr, 0) == DOIPLITE_ERR_INVALID_ARG,
           "activate null session");
    expect(DoipLiteRoutingActivation(session, 0x0E00, 0, nullptr, 4) == DOIPLITE_ERR_INVALID_ARG,
           "activate oem without data");

    expect(DoipLiteSendUds(session, 0x22, nullptr, 0, nullptr, 0, nullptr, 100) ==
               DOIPLITE_ERR_NOT_IMPLEMENTED,
           "sendUds still not implemented");
    expect(UdsReadDataByIdentifier(session, 0x0000, nullptr, 0, nullptr, 100) ==
               DOIPLITE_ERR_NOT_IMPLEMENTED,
           "read did still not implemented");

    DoipLiteFlashJob job{};
    job.packagePath = "example.pkg";
    expect(DoipLiteFlashStart(session, &job) == DOIPLITE_ERR_NOT_IMPLEMENTED,
           "flash start stub");
    DoipLiteFlashProgress progress{};
    expect(DoipLiteFlashPoll(session, &progress) == DOIPLITE_ERR_NOT_IMPLEMENTED,
           "flash poll stub");
    expect(progress.phase == DOIPLITE_FLASH_IDLE, "flash idle");
    DoipLiteClose(session);

    LoopbackEntity::Options nackOpts;
    nackOpts.routingCode = 0x06;
    LoopbackEntity nackEntity(nackOpts);
    expect(nackEntity.start(), "start nack entity");
    DoipLiteSession *nackSession = DoipLiteOpen(nackEntity.ip(), nackEntity.port());
    expect(nackSession != nullptr, "open nack entity");
    expect(DoipLiteRoutingActivationEx(nackSession, 0x0E00, 0x00, nullptr, 0, 1000) ==
               DOIPLITE_ERR_DOIP_NACK,
           "routing nack");
    expect(DoipLiteLastDoipCode(nackSession) == 0x06, "nack code surfaced");
    DoipLiteClose(nackSession);
    nackEntity.stop();

    LoopbackEntity::Options hangOpts;
    hangOpts.replyRouting = false;
    LoopbackEntity hangEntity(hangOpts);
    expect(hangEntity.start(), "start hang entity");
    DoipLiteSession *hangSession = DoipLiteOpen(hangEntity.ip(), hangEntity.port());
    expect(hangSession != nullptr, "open hang entity");
    expect(DoipLiteRoutingActivationEx(hangSession, 0x0E00, 0x00, nullptr, 0, 250) ==
               DOIPLITE_ERR_TIMEOUT,
           "routing timeout");
    DoipLiteClose(hangSession);
    hangEntity.stop();

    // A closed TCP port should produce a network error, not hang.
    DoipLiteSession *refused = DoipLiteOpen("127.0.0.1", 1);
    expect(refused != nullptr, "open closed port still allocates");
    const DoipLiteStatus netSt =
        DoipLiteRoutingActivationEx(refused, 0x0E00, 0x00, nullptr, 0, 500);
    expect(netSt == DOIPLITE_ERR_NETWORK || netSt == DOIPLITE_ERR_TIMEOUT,
           "connect to closed port is network or timeout");
    DoipLiteClose(refused);

#ifdef DOIPLITE_CLI_PATH
    int exitCode = 99;
    std::string output;
    expect(run_cli("--help", &exitCode, &output), "run cli help");
    expect(exitCode == 0, "cli --help exits 0");
    expect(output.find("discover") != std::string::npos, "help mentions discover");

    char discArgs[256];
    std::snprintf(discArgs,
                  sizeof(discArgs),
                  "discover --broadcast 127.0.0.1 --port %u --timeout-ms 1000",
                  static_cast<unsigned>(entity.port()));
    expect(run_cli(discArgs, &exitCode, &output), "run cli discover");
    expect(exitCode == 0, "cli discover exits 0");
    expect(output.find("127.0.0.1") != std::string::npos, "cli discover prints ip");
    expect(output.find("0x1234") != std::string::npos, "cli discover prints logical address");

    char actArgs[256];
    std::snprintf(actArgs,
                  sizeof(actArgs),
                  "activate --ip 127.0.0.1 --port %u --tester-addr 0x0E00 --timeout-ms 1000",
                  static_cast<unsigned>(entity.port()));
    expect(run_cli(actArgs, &exitCode, &output), "run cli activate");
    expect(exitCode == 0, "cli activate exits 0");
    expect(output.find("0x1234") != std::string::npos, "cli activate prints entity address");

    char nackArgs[256];
    std::snprintf(nackArgs,
                  sizeof(nackArgs),
                  "activate --ip 127.0.0.1 --port %u --tester-addr 0x0E00 --timeout-ms 1000",
                  static_cast<unsigned>(nackEntity.port()));
    // nack entity was stopped; start a fresh one
    LoopbackEntity::Options nackOpts2;
    nackOpts2.routingCode = 0x00;
    LoopbackEntity nack2(nackOpts2);
    expect(nack2.start(), "start nack entity for cli");
    std::snprintf(nackArgs,
                  sizeof(nackArgs),
                  "activate --ip 127.0.0.1 --port %u --tester-addr 0x0E00 --timeout-ms 1000",
                  static_cast<unsigned>(nack2.port()));
    expect(run_cli(nackArgs, &exitCode, &output), "run cli activate nack");
    expect(exitCode != 0, "cli activate nack is non-zero");
    expect(output.find("doip_nack") != std::string::npos, "cli mentions doip_nack");
    expect(output.find("0x00") != std::string::npos || output.find("0x0") != std::string::npos,
           "cli surfaces nack code");
    nack2.stop();
#endif

    entity.stop();
    std::puts("ok");
    return 0;
}
