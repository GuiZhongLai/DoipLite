#include "doiplite/doiplite.h"

#include <cstdio>
#include <cstdlib>

static void expect(bool cond, const char *msg)
{
    if (!cond)
    {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        std::exit(1);
    }
}

int main()
{
    expect(DoipLiteStatusString(DOIPLITE_OK) != nullptr, "status string");
    expect(DoipLiteOpen(nullptr, 0) == nullptr, "open null ip");

    uint32_t count = 99;
    expect(DoipLiteDiscover(100, nullptr, 0, nullptr) == DOIPLITE_ERR_INVALID_ARG,
           "discover null count");
    expect(DoipLiteDiscover(100, nullptr, 0, &count) == DOIPLITE_ERR_NOT_IMPLEMENTED,
           "discover stub");
    expect(count == 0, "discover clears count");

    DoipLiteSession *session = DoipLiteOpen("127.0.0.1", DOIPLITE_DEFAULT_TCP_PORT);
    expect(session != nullptr, "open loopback");
    expect(DoipLiteRoutingActivation(session, 0, 0, nullptr, 0) ==
               DOIPLITE_ERR_NOT_IMPLEMENTED,
           "activate stub");
    expect(DoipLiteSendUds(session, 0x22, nullptr, 0, nullptr, 0, nullptr, 100) ==
               DOIPLITE_ERR_NOT_IMPLEMENTED,
           "sendUds stub");
    expect(UdsReadDataByIdentifier(session, 0x0000, nullptr, 0, nullptr, 100) ==
               DOIPLITE_ERR_NOT_IMPLEMENTED,
           "read did stub");

    DoipLiteFlashJob job{};
    job.packagePath = "example.pkg";
    expect(DoipLiteFlashStart(session, &job) == DOIPLITE_ERR_NOT_IMPLEMENTED,
           "flash start stub");
    DoipLiteFlashProgress progress{};
    expect(DoipLiteFlashPoll(session, &progress) == DOIPLITE_ERR_NOT_IMPLEMENTED,
           "flash poll stub");
    expect(progress.phase == DOIPLITE_FLASH_IDLE, "flash idle");

    // Placeholder for a future in-process DoIP mock (loopback entity).
    DoipLiteClose(session);
    std::puts("ok");
    return 0;
}
