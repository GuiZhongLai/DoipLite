#include "net_internal.h"

#include <asio.hpp>
#include <cstring>

void DoipLiteClearVehicle(DoipLiteVehicle *vehicle)
{
    if (!vehicle)
    {
        return;
    }
    std::memset(vehicle, 0, sizeof(*vehicle));
    vehicle->tcpPort = DOIPLITE_DEFAULT_TCP_PORT;
}

// Standalone Asio is linked so M1 can implement UDP/TCP against this target.
using DoipLiteAsioError = asio::error_code;
