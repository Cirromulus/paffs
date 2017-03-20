/*
 * Copyright (c) 2013, German Aerospace Center (DLR)
 *
 * This file is part of liboutpost 0.4.
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

#include "../nexys3/spacewire_light.h"
#include <outpost/rtos/rtems/interval.h>

#include <stdio.h>


// ----------------------------------------------------------------------------
outpost::leon3::SpaceWireLight::SpaceWireLight(uint32_t index) :
    mIndex(index),
    mHandle(0),
    mFirstTransmit(true),
    mTxSync(1),
	mTxBuffer(mTxBufferData),
	mTxBufferData(mTxBufferData_)
{
}

outpost::leon3::SpaceWireLight::~SpaceWireLight()
{
    // Close port if it is still open
    if (mHandle != 0)
    {
        close();
    }
}

// ----------------------------------------------------------------------------
bool
outpost::leon3::SpaceWireLight::open()
{
    const struct spwl_options options =
    {
        4,        // Number of receive buffers to allocate.
        4,        // Number of transmit buffers to allocate.
        maxPacketLength,    // Size of each receive buffer in bytes.
		maxPacketLength    // Size of each allocated transmit buffer in bytes.
    };

    rtems_status_code status = spwl_open(&mHandle, mIndex, &options);
    if (status != RTEMS_SUCCESSFUL)
    {
        goto error;
    }

    // TODO
    // This function sets the TX bit rate to
    //   (txclkfreq / (scaler + 1)) bits per second;
    //   where txclkfreq is determined by the hardware configuration of the core.
    spwl_set_linkspeed(mHandle, 1);

    return true;
error:
    return false;
}

void
outpost::leon3::SpaceWireLight::close()
{
    spwl_close(mHandle);
    mHandle = 0;
}

// ----------------------------------------------------------------------------
bool
outpost::leon3::SpaceWireLight::up(outpost::time::Duration timeout)
{
    spwl_set_linkmode(mHandle, SPWL_LINKMODE_START);
	unsigned int mask = SPWL_COND_LINKUP;
	rtems_status_code ret;
	ret = spwl_wait(mHandle, &mask, rtos::rtems::getInterval(timeout));
	return ret == RTEMS_SUCCESSFUL;
}

void
outpost::leon3::SpaceWireLight::down(outpost::time::Duration timeout)
{
    spwl_set_linkmode(mHandle, SPWL_LINKMODE_DISABLE);

	unsigned int mask = SPWL_COND_LINKDOWN;
	spwl_wait(mHandle, &mask, rtos::rtems::getInterval(timeout));
}

bool
outpost::leon3::SpaceWireLight::isUp()
{
    spwl_linkstatus status;
    unsigned int errors;

    spwl_get_linkstatus(mHandle, &status, &errors);

    return (status == SPWL_LINK_RUN);
}

// ----------------------------------------------------------------------------
outpost::hal::SpaceWire::Result::Type
outpost::leon3::SpaceWireLight::requestBuffer(
		TransmitBuffer *& buffer, outpost::time::Duration timeout)
{
//    if (!txSync.acquire()) {
//        return FAILURE;
//    }
	// TODO buffer management
	(void) timeout; //TODO: implement timeout

    if (!mFirstTransmit)
    {
        struct spwl_txbuf* reclaimedBuffer;
        rtems_status_code status = spwl_reclaim_txbuf(mHandle, &reclaimedBuffer, SPWL_WAIT);
        if (status != RTEMS_SUCCESSFUL)
        {
            return Result::Type::failure;
        }
    }
    else
    {
        mFirstTransmit = false;
    }

    mTxBuffer.setLength(0);
    mTxBuffer.setEndMarker(eop);
    buffer = &mTxBuffer;

    return Result::Type::success;
}

outpost::hal::SpaceWire::Result::Type
outpost::leon3::SpaceWireLight::send(
		TransmitBuffer* buffer)
{
    mSpwlBuffer.data = &buffer->getData()[0];
    mSpwlBuffer.nbytes = buffer->getLength();
    mSpwlBuffer.eop = toEopFlags(buffer->getEndMarker());

    rtems_status_code status = spwl_send_txbuf(mHandle, &mSpwlBuffer, SPWL_WAIT);

    if (status == RTEMS_SUCCESSFUL)
    {
        return Result::Type::success;
    }
    else
    {
        return Result::Type::failure;
    }
}

outpost::hal::SpaceWire::Result::Type
outpost::leon3::SpaceWireLight::receive(ReceiveBuffer& buffer, outpost::time::Duration timeout)
{
	(void) timeout; //TODO: implement timeout

    void* data;
    uint16_t bytesReceived;
    unsigned int eopFlags;

    rtems_status_code status =
            spwl_recv_rxbuf(mHandle, &data, &bytesReceived, &eopFlags, SPWL_WAIT);

    if (status == RTEMS_SUCCESSFUL)
    {
        buffer = ReceiveBuffer(
        		outpost::BoundedArray<const uint8_t>(
				static_cast<const uint8_t *>(data), bytesReceived),
        		toEopMarker(eopFlags));
        return Result::Type::success;
    }
    else
    {
        return Result::Type::failure;
    }
}

void
outpost::leon3::SpaceWireLight::releaseBuffer(const ReceiveBuffer& buffer)
{
    // Const cast needed here because the spwl interface requires a
    // writeable void pointer. The const is constraint is introduced by
    // this class so we can safely remove it.
    spwl_release_rxbuf(mHandle,
    		const_cast<uint8_t*>
    		(&buffer.getData()[0]));
}

void
outpost::leon3::SpaceWireLight::flushReceiveBuffer()
{

}

/**
 * Get the maximum length of a SpaceWire packet.
 */
size_t
outpost::leon3::SpaceWireLight::getMaximumPacketLength() const{
	return maxPacketLength;
}

