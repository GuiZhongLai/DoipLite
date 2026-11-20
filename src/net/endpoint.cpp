#include "net_internal.h"

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
