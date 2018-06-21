/*
 * Copyright (c) 2013, German Aerospace Center (DLR)
 *
 * This file is part of libCOBC 0.4.
 *
 * It is distributed under the terms of the GNU General Public License with a
 * linking exception. See the file "LICENSE" for the full license governing
 * this code.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */
// ----------------------------------------------------------------------------

#ifndef COBC_LEON3_SPACEWIRE_H
#define COBC_LEON3_SPACEWIRE_H

#include <stdint.h>
#include <cstddef>

#include <outpost/rtos/semaphore.h>
#include <outpost/utils.h>

#include <outpost/hal/spacewire.h>

extern "C" {
#include "spacewirelight.h"
}

namespace outpost
{
namespace leon3
{

/**
 * SpaceWire Light Interface
 *
 * Uses the SpaceWire Light driver for RTEMS 4.10 on LEON3 from
 * opencores.org.
 *
 * \see        outpost::hal::Spacewire
 * \author    Fabian Greif
 */
class SpaceWireLight : public hal::SpaceWire
{
	static constexpr size_t maxPacketLength = 4500;
public:
    /**
     * Connects to the index'th SpaceWire Light device found in the AMBA
     * plug&play map. Allocation of receive/transmit buffers and a reset
     * of the device core is done when calling open().
     */
    SpaceWireLight(uint32_t index);

    virtual ~SpaceWireLight();

    virtual bool
    open();

    virtual void
    close();

    virtual bool
    up(outpost::time::Duration timeout);

    virtual void
    down(outpost::time::Duration timeout);

    virtual bool
    isUp();


    virtual Result::Type
    requestBuffer(TransmitBuffer *& buffer, outpost::time::Duration timeout);

    virtual Result::Type
    send(TransmitBuffer* buffer, outpost::time::Duration timeout);


    virtual Result::Type
    receive(ReceiveBuffer& buffer, outpost::time::Duration timeout);

    virtual void
    releaseBuffer(const ReceiveBuffer& buffer);

    virtual size_t
    getMaximumPacketLength() const;

    virtual void
    flushReceiveBuffer();

private:
    /// Convert form the hal::SpaceWire Format to spwl flags
    inline uint16_t
    toEopFlags(EndMarker marker)
    {
        uint16_t flags = 0;
        switch (marker)
        {
            case eop: flags = SPWL_EOP; break;
            case eep: flags = SPWL_EEP; break;
            case partial:
            case unknown:
                break;
        }
        return flags;
    }

    /// Convert form spwl flags to the hal::SpaceWire Format
    inline EndMarker
    toEopMarker(uint16_t flags)
    {
        EndMarker marker;
        if (flags & SPWL_EOP)
        {
            marker = eop;
        }
        else if (flags & SPWL_EEP)
        {
            marker = eep;
        }
        else
        {
            marker = partial;
        }

        return marker;
    }

    /// SpaceWire device amba index
    uint32_t mIndex;

    /// Used by the spwl driver to identify the SpaceWire device
    spwl_handle mHandle;

    bool mFirstTransmit;
    rtos::Semaphore mTxSync;
    TransmitBuffer mTxBuffer;

    // Flash page size is 4096+128 byte (=4224) + 9 byte AMAP overhead
    // => 4233 byte minimum
    uint8_t mTxBufferData_[maxPacketLength];
    outpost::Slice<uint8_t> mTxBufferData;
    struct spwl_txbuf mSpwlBuffer;

};

}
}

#endif // COBC_LEON3_SPACEWIRE_H
