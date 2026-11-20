#include "doiplite/doiplite.h"

#include <cstdio>
#include <cstring>
#include <string>

static void printHelp()
{
    std::fputs(
        "doiplite-cli — DoipLite command-line shell (skeleton)\n"
        "\n"
        "Usage:\n"
        "  doiplite-cli --help\n"
        "  doiplite-cli discover  [--config <path>]\n"
        "  doiplite-cli activate  [--config <path>]\n"
        "  doiplite-cli read-version [--config <path>]\n"
        "\n"
        "M1 will implement discover and activate. These commands currently\n"
        "return not_implemented and do not open a network connection.\n"
        "Pass addresses and DIDs via a local config file; do not hard-code\n"
        "field values. See examples/config/example.conf.\n",
        stdout);
}

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
    for (int i = 2; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--config") == 0)
        {
            if (i + 1 >= argc)
            {
                std::fprintf(stderr, "doiplite-cli: --config requires a path\n");
                return 2;
            }
            config = argv[++i];
        }
        else
        {
            std::fprintf(stderr, "doiplite-cli: unknown argument: %s\n", argv[i]);
            return 2;
        }
    }
    (void)config;

    if (std::strcmp(cmd, "discover") == 0 || std::strcmp(cmd, "activate") == 0 ||
        std::strcmp(cmd, "read-version") == 0)
    {
        std::fprintf(stderr,
                     "doiplite-cli: '%s' is not implemented yet (%s)\n",
                     cmd,
                     DoipLiteStatusString(DOIPLITE_ERR_NOT_IMPLEMENTED));
        return 1;
    }

    std::fprintf(stderr, "doiplite-cli: unknown command: %s\n", cmd);
    return 2;
}
