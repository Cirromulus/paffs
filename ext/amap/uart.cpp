/**
 * Copyright (c) 2012, German Aerospace Center (DLR)
 * All rights reserved.
 *
 * @author	Fabian Greif <fabian.greif@dlr.de>
 */
// ----------------------------------------------------------------------------
#include "uart.h"

#include <cstdlib>

#include <outpost/rtos/failure_handler.h>

using namespace outpost::iff;


UartBase::UartBase(Amap *amap,
                   uint32_t baseAddress,
                   std::size_t hardwareTxBufferSize,
                   std::size_t hardwareRxBufferSize,
                   outpost::Slice<uint8_t> txBuffer,
                   outpost::Slice<uint8_t> rxBuffer) :
        mAmap(amap), mBaseAddress(baseAddress), mInitialized(false),
        mControlRegister(0), mTx(hardwareTxBufferSize, txBuffer),
        mRx(hardwareRxBufferSize, rxBuffer)
{
    //static_assert(sizeof(txBuffer) % 4 == 0, "Buffer size must a multiple of 4");
    //static_assert(sizeof(rxBuffer) % 4 == 0, "Buffer size must a multiple of 4");

    for (uint_fast8_t i = 0; i < 3; ++i)
    {
        mUnalignedDataBuffer[i] = 0;
    }
}

UartBase::~UartBase()
{
}

bool
UartBase::initialize()
{
    // Perform a hardware reset
    reset();

    uint8_t data[4 * 5];
    if (mAmap->read(mBaseAddress, data, 5, outpost::time::Milliseconds(500)))
    {
        mControlRegister = Amap::deserialize32(&data[0]);

        mRx.mAmap.mHead = Amap::deserialize16(&data[12]);
        mRx.mAmap.mTail = Amap::deserialize16(&data[14]);

        mTx.mAmap.mHead = Amap::deserialize16(&data[16]);
        mTx.mAmap.mTail = Amap::deserialize16(&data[18]);

        // TODO what to do when head != tail != 0?

        // TODO if tx.head != 0 read unaligned data and save it

        mInitialized = true;
    }

    return mInitialized;
}

bool
UartBase::isAvailable()
{
    // TODO implementation
    return mInitialized;
}

//bool
//outpost::amap::UartBase::read(uint8_t& c)
//{
//	if (rx.buffer.isEmpty()) {
//		return false;
//	}
//
//	c = rx.buffer.getFront();
//	rx.buffer.removeFront();
//
//	return true;
//}

std::size_t
UartBase::read(outpost::Slice<uint8_t> data,
               time::Duration /*timeout*/)
{
    std::size_t i = 0;
    for (; i < data.getNumberOfElements(); ++i)
    {
        if (mRx.mBuffer.isEmpty())
        {
            // If no data is buffered, check the external FPGA buffer
            synchronizeReceiveBuffer();
        }

        if (mRx.mBuffer.isEmpty())
        {
            return i;
        }
        else
        {
            data[i] = mRx.mBuffer.getFront();
            mRx.mBuffer.removeFront();
        }
    }

    return i;
}

std::size_t
UartBase::write(outpost::Slice<const uint8_t> data,
                time::Duration /*timeout*/)
{
    std::size_t i = 0;
    size_t bytesToSend = data.getNumberOfElements();
    size_t sent = 0;

    // TODO add timeout
    while (bytesToSend-- != 0)
    {
        while (!mTx.mBuffer.append(data[i]))
        {
            synchronizeTransmitBuffer();
        };
        sent++;
        i++;
    }

    return sent;
}

void
UartBase::flush()
{
    // receive transmit should go first as it updates the rx and tx pointers
    synchronizeReceiveBuffer();
    synchronizeTransmitBuffer();
}

// ----------------------------------------------------------------------------
void
UartBase::reset()
{
    uint8_t buffer[4];
    Amap::serialize32(buffer, static_cast<uint32_t>(Flags::reset));
    mAmap->write(mBaseAddress + static_cast<uint32_t>(Register::control),
            buffer, 1, outpost::time::Milliseconds(500));
}

// ----------------------------------------------------------------------------
void
UartBase::enableReception(bool enable)
{
    uint32_t control = mControlRegister;

    if (enable)
    {
        mControlRegister |= static_cast<uint32_t>(Flags::receptionEnable);
    }
    else
    {
        mControlRegister &= ~static_cast<uint32_t>(Flags::receptionEnable);
    }

    // Check if the control register will change. AMAP transfer is only
    // started on changes.
    if (control != mControlRegister)
    {
        // Update register in hardware
        uint8_t buffer[4];
        Amap::serialize32(buffer, mControlRegister);
        mAmap->write(mBaseAddress + static_cast<uint32_t>(Register::control),
                buffer, 1, outpost::time::Milliseconds(500));
    }
}

void
UartBase::enableTransmission(bool enable)
{
    uint32_t control = mControlRegister;

    if (enable)
    {
        mControlRegister |= static_cast<uint32_t>(Flags::transmissionEnable);
    }
    else
    {
        mControlRegister &= ~static_cast<uint32_t>(Flags::transmissionEnable);
    }

    // Check if the control register will change. AMAP transfer is only
    // started on changes.
    if (control != mControlRegister)
    {
        // Update register in hardware
        uint8_t buffer[4];
        Amap::serialize32(buffer, mControlRegister);
        mAmap->write(mBaseAddress + static_cast<uint32_t>(Register::control),
                buffer, 1, outpost::time::Milliseconds(500));
    }
}

// ----------------------------------------------------------------------------
void
UartBase::setBaudrateDivider(uint32_t divider)
{
    mControlRegister = (mControlRegister & 0xFF000000) | (divider & 0x00FFFFFF);

    uint8_t buffer[4];
    Amap::serialize32(buffer, mControlRegister);
    mAmap->write(mBaseAddress + static_cast<uint32_t>(Register::control),
            buffer, 1, outpost::time::Milliseconds(500));
}

// ----------------------------------------------------------------------------
void
UartBase::enableParity(bool odd)
{
    uint32_t control = mControlRegister;

    // Enabling parity bit
    mControlRegister = (mControlRegister | 0x10000000);

    // Setting parity levels
    if (odd)
    {
        mControlRegister = (mControlRegister | 0x08000000);
    }
    else
    {
        mControlRegister = (mControlRegister & 0xF7FFFFFF);
    }

    if (control != mControlRegister)
    {
        uint8_t buffer[4];
        Amap::serialize32(buffer, mControlRegister);
        mAmap->write(mBaseAddress + static_cast<uint32_t>(Register::control),
                buffer, 1, outpost::time::Milliseconds(500));
    }
}

// ----------------------------------------------------------------------------
void
UartBase::disableParity()
{
    uint32_t control = mControlRegister;

    // Disabling parity bit
    mControlRegister = (mControlRegister & 0xE7FFFFFF);

    if (control != mControlRegister)
    {
        uint8_t buffer[4];
        Amap::serialize32(buffer, mControlRegister);
        mAmap->write(mBaseAddress + static_cast<uint32_t>(Register::control),
                buffer, 1, outpost::time::Milliseconds(500));
    }
}
// ----------------------------------------------------------------------------
uint32_t
UartBase::getStatus()
{
    uint8_t buffer[4];
    if (!mAmap->read(mBaseAddress + static_cast<uint32_t>(Register::status),
            buffer, 1, outpost::time::Milliseconds(500)))
    {
        // TODO better error message
        outpost::rtos::FailureHandler::fatal(
                outpost::rtos::FailureCode::genericRuntimeError());
    }

    return Amap::deserialize32(buffer);
}

// ----------------------------------------------------------------------------
void
UartBase::updateExternalBufferPointers()
{
    uint8_t buffer[8];

    mAmap->read(mBaseAddress + static_cast<uint32_t>(Register::rxControl),
            buffer, 2, outpost::time::Milliseconds(500));

    mRx.mAmap.mHead = Amap::deserialize16(buffer + 0);
    mRx.mAmap.mTail = Amap::deserialize16(buffer + 2);

    // Also update the transmit pointers as it requires minimal overhead
    // and may save a complete AMAP transaction when flushing the transmit
    // buffers.
    mTx.mAmap.mHead = Amap::deserialize16(buffer + 4);
    mTx.mAmap.mTail = Amap::deserialize16(buffer + 6);

    // TODO check for invalid pointers
}

// ----------------------------------------------------------------------------
namespace
{
class ReceiveReadHandler : public ReadHandler
{
public:
    ReceiveReadHandler(outpost::Deque<uint8_t>& buffer,
                       uint16_t offset,
                       uint16_t size) :
            mBuffer(buffer), mOffset(offset), mSize(size)
    {
    }

    virtual void
    read(const uint8_t* data,
         std::size_t /*n*/)
    {
        for (uint_fast16_t i = mOffset; i < (mOffset + mSize); ++i)
        {
            mBuffer.append(data[i]);
        }
    }

    outpost::Deque<uint8_t>& mBuffer;
    uint16_t mOffset;
    uint16_t mSize;
};
}

void
UartBase::synchronizeReceiveBuffer()
{
    // Step 1: Read head and tail pointer
    updateExternalBufferPointers();

    uint16_t size = getUsedSlots(mRx.mAmap);
    if (size == 0)
    {
        // Nothing to receive
        return;
    }

    // Limit number of bytes to read to the available space in the internal
    // receive buffer.
    if (size > mRx.mBuffer.getAvailableSpace())
    {
        size = mRx.mBuffer.getAvailableSpace();
    }

    div_t d = div(mRx.mAmap.mTail, 4);

    // First data-word to read. Nonetheless the address is a byte address,
    // therefore convert the word address to a byte address (*4)
    uint16_t addressOffset = d.quot * 4;
    uint16_t wordOffset = d.rem;			// First valid byte in the data-word

    d = div(size + wordOffset, 4);
    uint16_t numberOfWordsToRead = d.quot;
    if (d.rem > 0)
    {
        numberOfWordsToRead += 1;
    }

    // Step 2: read data
    ReceiveReadHandler handler(mRx.mBuffer, wordOffset, size);
    mAmap->read(
            mBaseAddress + static_cast<uint32_t>(Register::rxFifo)
                    + addressOffset, numberOfWordsToRead,
            outpost::time::Milliseconds(500), handler);

    // Step 3: write tail pointer
    mRx.mAmap.mTail += size;
    if (mRx.mAmap.mTail >= mRx.mAmap.mTotalSize)
    {
        mRx.mAmap.mTail -= mRx.mAmap.mTotalSize;
    }

    uint8_t buffer[4];
    Amap::serialize16(buffer + 0, mRx.mAmap.mHead);
    Amap::serialize16(buffer + 2, mRx.mAmap.mTail);
    mAmap->write(mBaseAddress + static_cast<uint32_t>(Register::rxControl),
            buffer, 1, outpost::time::Milliseconds(500));
}

namespace
{
class TransmitWriteHandler : public WriteHandler
{
public:
    TransmitWriteHandler(outpost::Deque<uint8_t>& buffer,
                         uint16_t offsetStart,
                         uint16_t offsetEnd,
                         uint16_t size,
                         uint8_t* unaligned) :
            mBuffer(buffer), mOffsetStart(offsetStart), mOffsetEnd(offsetEnd),
            mSize(size), mUnaligned(unaligned)
    {
    }

    virtual void
    write(uint8_t * data,
          std::size_t n)
    {
        uint_fast16_t i = 0;
        // rewrite data from last access, otherwise it would be overwritten
        for (i = 0; i < mOffsetStart; ++i)
        {
            *data++ = mUnaligned[i];
        }

        for (; i < mOffsetStart + mSize; ++i)
        {
            *data++ = mBuffer.getFront();

            if (i >= mOffsetEnd)
            {
                mUnaligned[i - mOffsetEnd] = mBuffer.getFront();
            }

            mBuffer.removeFront();
        }

        for (; i < n; ++i)
        {
            *data++ = 0;
        }
    }

    outpost::Deque<uint8_t>& mBuffer;
    uint16_t mOffsetStart;
    uint16_t mOffsetEnd;
    uint16_t mSize;
    uint8_t* mUnaligned;
};
}

void
UartBase::synchronizeTransmitBuffer()
{
    if (mTx.mBuffer.getSize() == 0)
    {
        // Nothing to transmit
        return;
    }

    uint8_t buffer[4];

    // Step 1:	read pointers if there is more data to write than
    // 			currently known to be free
    uint16_t size = mTx.mBuffer.getSize();
    uint16_t freeSize = getFreeTransmitSlots(mTx.mAmap);
    if (size > freeSize)
    {
        updateExternalBufferPointers();

        // Update mSize
        freeSize = getFreeTransmitSlots(mTx.mAmap);
    }

    if (size > freeSize)
    {
        size = freeSize;
    }

    div_t d = div(mTx.mAmap.mHead, 4);
    // First data-word to read. Nonetheless the address is a byte address,
    // therefore convert the word address to a byte address (*4)
    uint16_t addressOffset = d.quot * 4;
    uint16_t startWordOffset = d.rem;	// First byte to write in the data-word

    d = div(size + startWordOffset, 4);
    uint16_t numberOfWordsToWrite = d.quot;
    uint16_t endWordOffset = d.rem;
    if (endWordOffset > 0)
    {
        numberOfWordsToWrite += 1;
    }

    uint16_t endOffset = startWordOffset + size - endWordOffset;

    // Step 2: write aligned data
    TransmitWriteHandler handler(mTx.mBuffer, startWordOffset, endOffset, size,
            mUnalignedDataBuffer);
    mAmap->write(
            mBaseAddress + static_cast<uint32_t>(Register::txFifo)
                    + addressOffset, numberOfWordsToWrite,
            outpost::time::Milliseconds(500), handler);

    mTx.mAmap.mHead += size;
    if (mTx.mAmap.mHead >= mTx.mAmap.mTotalSize)
    {
        mTx.mAmap.mHead -= mTx.mAmap.mTotalSize;
    }

    // Step 3: write head pointer
    Amap::serialize16(buffer + 0, mTx.mAmap.mHead);
    Amap::serialize16(buffer + 2, mTx.mAmap.mTail);
    mAmap->write(mBaseAddress + static_cast<uint32_t>(Register::txControl),
            buffer, 1, outpost::time::Milliseconds(500));
}

// ----------------------------------------------------------------------------
uint16_t
UartBase::getUsedSlots(Channel::AmapQueue& amap)
{
    uint16_t slots;
    if (amap.mHead >= amap.mTail)
    {
        slots = amap.mHead - amap.mTail;
    }
    else
    {
        slots = amap.mTotalSize - (amap.mTail - amap.mHead);
    }

    return slots;
}

uint16_t
UartBase::getFreeTransmitSlots(Channel::AmapQueue& amap)
{
    uint16_t slots;
    if (amap.mHead >= amap.mTail)
    {
        slots = amap.mTotalSize - (amap.mHead - amap.mTail) - 1;
    }
    else
    {
        slots = (amap.mTail - amap.mHead) - 1;
    }

    // First valid byte in the data-word
    uint16_t tailOffset = amap.mTail % 4;

    // To avoid having to write the word in which the tail pointer is
    // located we adjust the available mSize accordingly. Otherwise we would
    // have to read the word first to avoid overwriting the end of the mBuffer.
    if (tailOffset >= 2)
    {
        slots = slots - tailOffset - 1;
    }

    return slots;
}
