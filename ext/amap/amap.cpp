#include "amap.h"

#include <cstring>
#include <stdio.h>
#include <outpost/rtos/thread.h>
#include <outpost/utils/crc.h>
#include <outpost/utils/serialize.h>

using namespace outpost::iff;

#define AMAP_DEBUG

#ifdef AMAP_DEBUG
#   define DEBUG(x) x
#	include <inttypes.h>
#else
#   define DEBUG(x)
#endif

// ----------------------------------------------------------------------------
class DefaultWriteHandler : public WriteHandler
{
public:
    DefaultWriteHandler(const uint8_t * source) :
            mSource(source)
    {
    }

    virtual void
    write(uint8_t * data,
          std::size_t n)
    {
        std::memcpy(data, mSource, n);
    }

private:
    const uint8_t* mSource;
};

class DefaultReadHandler : public ReadHandler
{
public:
    DefaultReadHandler(uint8_t * destination) :
            mDestination(destination)
    {
    }

    virtual void
    read(const uint8_t * data,
         std::size_t n)
    {
        std::memcpy(mDestination, data, n);
    }

private:
    uint8_t* mDestination;
};

// ----------------------------------------------------------------------------
// for the packet definition see lib/vhdl/amap/documentation.rst

Amap::Amap(outpost::hal::SpaceWire& spw) :
        mSpacewire(spw), mRetries(0), mOperationLock()
{
}

// ----------------------------------------------------------------------------
bool
Amap::write(uint32_t address,
            const uint8_t* data,
            std::size_t length,
            outpost::time::Duration timeout)
{
    DefaultWriteHandler handler(data);
    return write(address, length, timeout, handler);
}

bool
Amap::write(uint32_t address,
            std::size_t length,
            outpost::time::Duration timeout,
            WriteHandler& handler)
{
	printk("Amap::write before mutex lock\n");
    // Guard operation against concurrent accesses
    outpost::rtos::MutexGuard lock(mOperationLock);

    printk("Amap::write before flushReceiveBuffer \n");
    // Remove all previously received messages
    mSpacewire.flushReceiveBuffer();

    hal::SpaceWire::TransmitBuffer * txBuffer = 0;
    printk("Amap::write transmit buffer before request: %p\n", txBuffer);
    if (mSpacewire.requestBuffer(txBuffer, timeout) != hal::SpaceWire::Result::success)
    {
        mErrorCounter.writeOperation++;
        return false;
    }
    printk("Amap::write transmit buffer after request: %p\n", txBuffer);

    printk("Amap::write before write Header with txBuffer->data: %p\n", txBuffer->getData().begin());
    // Write AMAP header
    writeHeader(txBuffer->getData(), OPERATION_WRITE, address, length);

    printk("Amap::write before write main Body\n");
    // Write payload to buffer
    handler.write(&txBuffer->getData()[requestHeaderSize], length * 4);

    printk("Amap::write before CRC calc\n");

    const std::size_t index = requestHeaderSize + length * 4;
    txBuffer->getData()[index] = Crc8Ccitt::calculate(
    		outpost::BoundedArray<uint8_t>(
    				&txBuffer->getData()[requestHeaderSize],
					length * 4)
			);
    txBuffer->setLength(index + 1);

#ifdef AMAP_DEBUG
    for(uint_fast16_t i = 0; i < txBuffer->getLength(); i++)
    {
        printf("AMAP-WR TxBuff[%d]: %d, %X\n", i, txBuffer->getData()[i], txBuffer->getData()[i]);
    }
#endif

    // Send message
    //spacewire.send(txBuffer);
    printk("Amap::write before send\n");
    mRetries = 0;
    while (mSpacewire.send(txBuffer) != hal::SpaceWire::Result::success)
    {
      printf(".\n");
        // retry 100 times until packet can be send
        mRetries++;
        if (mRetries > 100)
            break;
    }

    printk("Amap::write before receive Buffer\n");
    // Receive response
    hal::SpaceWire::ReceiveBuffer rxBuffer;
    //rxBuffer.getLength() = length;//FIXME: Receiver length has to be specified for GR712
    //spacewire.receive(rxBuffer, hal::SpaceWire::blocking);
    if (mSpacewire.receive(rxBuffer, timeout) != hal::SpaceWire::Result::success)
    {
    	printk("Amap::write Could not receive\n");
        // FIXME timeout
        // Could not receive a matching message
        mErrorCounter.writeOperation++;
        return false;
    }

#ifdef AMAP_DEBUG
    for(uint_fast16_t i = 0; i < rxBuffer.getLength(); i++)
    {
        printf("AMAP-WR RxBuff[%d]: %d, %X\n", i, rxBuffer.getData()[i], rxBuffer.getData()[i]);
    }
#endif

    if (!checkResponseHeader(rxBuffer, 0))
    {
        mErrorCounter.writeOperation++;
        mSpacewire.releaseBuffer(rxBuffer);
        return false;
    }

    mSpacewire.releaseBuffer(rxBuffer);
    return true;
}

bool
Amap::read(uint32_t address,
           uint8_t * data,
           std::size_t length,
           outpost::time::Duration timeout)
{
    DefaultReadHandler handler(data);
    return read(address, length, timeout, handler);
}

bool
Amap::read(uint32_t address,
           std::size_t length,
           outpost::time::Duration timeout,
           ReadHandler& handler)
{
    // Guard operation against concurrent accesses
    outpost::rtos::MutexGuard lock(mOperationLock);

    // Remove all previously received messages
    mSpacewire.flushReceiveBuffer();

    hal::SpaceWire::TransmitBuffer * txBuffer = 0;
    if (mSpacewire.requestBuffer(txBuffer, timeout) != hal::SpaceWire::Result::success)
    {
        mErrorCounter.readOperation++;
        return false;
    }

    writeHeader(txBuffer->getData(), OPERATION_READ, address, length);
    txBuffer->setLength(requestHeaderSize);

#ifdef AMAP_DEBUG
    for(uint_fast16_t i = 0; i < txBuffer->getLength(); i++)
    {
        printf("AMAP-Read: TxBuff[%d]: %d, %X\n", i, txBuffer->getData()[i], txBuffer->getData()[i]);
    }
#endif

    mRetries = 0;
    // Send message
    while (mSpacewire.send(txBuffer) != hal::SpaceWire::Result::success)
    {
        printf(".\n");
        // Retry 100 times until packet can be send
        mRetries++;
        if (mRetries > 100)
            break;
    }

    // Receive response
    hal::SpaceWire::ReceiveBuffer rxBuffer;
    //rxBuffer.getLength() = length;//FIXME: Receiver length has to be specified for GR712 (currently defining maximum length)
    if (mSpacewire.receive(rxBuffer, timeout) != hal::SpaceWire::Result::success)
    {
        // FIXME timeout
        // Could not receive a matching message
        mErrorCounter.readOperation++;
        return false;
    }

#ifdef AMAP_DEBUG
    for(uint_fast16_t i = 0; i < rxBuffer.getLength(); i++)
    {
        printf("AMAP-Read RxBuff[%d]: %d, %X\n", i, rxBuffer.getData()[i], rxBuffer.getData()[i]);
    }
#endif

    if (!checkResponseHeader(rxBuffer, length * 4))
    {
        mErrorCounter.readOperation++;
        mSpacewire.releaseBuffer(rxBuffer);
        return false;
    }

    handler.read(&rxBuffer.getData()[responseHeaderSize], length * 4);

    mSpacewire.releaseBuffer(rxBuffer);

    return true;
}

bool
Amap::ping(Information& info,
           outpost::time::Duration timeout)
{
    // Remove all previously received messages
    //spacewire.flushReceiveBuffer();

    hal::SpaceWire::TransmitBuffer * txBuffer = 0;
    if (mSpacewire.requestBuffer(txBuffer, timeout) != hal::SpaceWire::Result::success)
    {
        mErrorCounter.pingOperation++;
        return false;
    }

    writeHeader(txBuffer->getData(), OPERATION_PING, 0, 0);
    txBuffer->setLength(requestHeaderSize);

#ifdef AMAP_DEBUG
    for(uint_fast16_t i = 0; i < txBuffer->getLength(); i++)
    {
        printf("Buff[%d]: %d, %X\n", i, txBuffer->getData()[i], txBuffer->getData()[i]);
    }
#endif

    // Send message
    mRetries = 0;
    while (mSpacewire.send(txBuffer) != hal::SpaceWire::Result::success)
    {
        // Retry 100 times until packet can be send
        mRetries++;
        if (mRetries > 100)
            break;
    }

    // Receive response
    hal::SpaceWire::ReceiveBuffer rxBuffer;
    //rxBuffer.getLength() = 1; //TODO: setLength
    if (mSpacewire.receive(rxBuffer, timeout) != hal::SpaceWire::Result::success)
    {
        // FIXME timeout
        // Could not receive a matching message
        mErrorCounter.pingOperation++;
        return false;
    }

#ifdef AMAP_DEBUG
    for(uint_fast16_t i = 0; i < rxBuffer.getLength(); i++)
    {
        printf("Buff[%d]: %d, %X\n", i, rxBuffer.getData()[i], rxBuffer.getData()[i]);
    }
#endif

    if (!checkResponseHeader(rxBuffer, 4))
    {
        mErrorCounter.pingOperation++;
        mSpacewire.releaseBuffer(rxBuffer);
        return false;
    }

    info.deserialize(&rxBuffer.getData()[4]);

    mSpacewire.releaseBuffer(rxBuffer);
    return true;
}

//// ----------------------------------------------------------------------------
void
Amap::writeHeader(outpost::BoundedArray<uint8_t> buffer,
                  Operation operation,
                  uint32_t address,
                  std::size_t length)
{
	printk("amap::writeHeader with buffer: %p, address %" PRIu32 " and length: %u\n",
			buffer.begin(), address, length);
    Serialize packet(buffer);

    packet.store(targetLogicalAddress);
    packet.store(protocolIdentifier);
    packet.store(address);
    packet.store < uint16_t > (length);
    packet.store < uint8_t > (operation);
    packet.store < uint8_t > (Crc8Ccitt::calculate(buffer)); //, 10 - 1));

}

bool
Amap::checkResponseHeader(hal::SpaceWire::ReceiveBuffer& buffer,
                          std::size_t expectedPayloadLength)
{
    if (buffer.getEndMarker() != hal::SpaceWire::eop || buffer.getLength() < responseHeaderSize)
    {
        // Wrong packet size
        mErrorCounter.responsePacketSize++;
        return false;
    }

    if (buffer.getData()[0] != initiatorLogicalAddress
            || buffer.getData()[1] != protocolIdentifier)
    {
        // Packet is not an AMAP packet or destinated for a different device
        return false;
    }

    // Check header CRC
    if (Crc8Ccitt::calculate(buffer.getData()) != 0)
    {
        // Header CRC error
        mErrorCounter.responseHeaderCrc++;
        return false;
    }

    if (buffer.getData()[2] != OPERATION_ACK)
    {
        if (buffer.getData()[2] == OPERATION_NACK_CRC_WRONG)
        {
            // Operation failed => No payload available
            mErrorCounter.responseNackCrc++;
        }
        else
        {
            mErrorCounter.responseNackUndefined++;
        }
        return false;
    }

    // AMAP response header is four bytes + optional payload
    std::size_t expectedLength = 4;
    if (expectedPayloadLength > 0)
    {
        // If the length of payload is not zero an additional
        // payload CRC byte is generated
        expectedLength += expectedPayloadLength + 1;
    }

    if (buffer.getLength() - 2 != expectedLength)
    {
        mErrorCounter.responsePayloadLength++;
        return false;
    }

    // Check payload CRC
    if (expectedPayloadLength > 0
            && Crc8Ccitt::calculate(
            		BoundedArray<const uint8_t>(
					&buffer.getData()[responseHeaderSize],
					expectedPayloadLength + 1))
			!= 0)
    {
        mErrorCounter.responsePayloadCrc++;
        return false;   // Payload CRC errorCounter
    }

    return true;
}
