#include "loopback_entity.h"

#include <cstdio>
#include <csignal>
#include <atomic>
#include <chrono>
#include <thread>

static std::atomic<bool> g_stop{false};

extern "C" void OnSignal(int)
{
    g_stop = true;
}

int main()
{
    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);

    LoopbackEntity::Options opts;
    opts.supportedDid = 0x0001;
    opts.didData = "SIM-1.0";
    opts.rejectUnknownDid = true;
    LoopbackEntity entity(opts);
    if (!entity.start())
    {
        std::fputs("doiplite-loopback-sim: failed to bind 127.0.0.1\n", stderr);
        return 1;
    }
    std::printf("listening 127.0.0.1 %u\n", static_cast<unsigned>(entity.port()));
    std::puts("example_did=0x0001 version=SIM-1.0");
    std::fflush(stdout);
    while (!g_stop)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    entity.stop();
    return 0;
}
