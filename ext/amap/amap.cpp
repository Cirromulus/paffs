#include "amap.h"

#include <cstdio>

// ----------------------------------------------------------------------------
void
printErrorCounter(const cobc::iff::Amap::ErrorCounter& errorCounter)
{
    printf("AMAP Error Counters:\n");
    printf("- Write Operation         : %3i\n", errorCounter.writeOperation);
    printf("- Read  Operation         : %3i\n", errorCounter.readOperation);
    printf("- Ping  Operation         : %3i\n", errorCounter.pingOperation);
    printf("- Response Packet Size    : %3i\n",
            errorCounter.responsePacketSize);
    printf("- Response Header CRC     : %3i\n", errorCounter.responseHeaderCrc);
    printf("- Response NACK CRC       : %3i\n", errorCounter.responseNackCrc);
    printf("- Response NACK Undefined : %3i\n",
            errorCounter.responseNackUndefined);
    printf("- Response Payload Length : %3i\n",
            errorCounter.responsePayloadLength);
    printf("- Response Payload CRC    : %3i\n",
            errorCounter.responsePayloadCrc);
}

// ----------------------------------------------------------------------------
void
pingAmap(cobc::iff::Amap * amap)
{
    cobc::iff::Information information;
    if (amap->ping(information, cobc::time::Milliseconds(0)))
    {
        printf("Ping: 0x%02x 0x%02x 0x%04x\n", information.protocolVersion,
                information.topDesignConfiguration,
                information.protocolRevisionNumber);
    }
    else
    {
        printf("AMAP: Ping failed\n");
        printErrorCounter(amap->getErrorCounter());
    }
}
