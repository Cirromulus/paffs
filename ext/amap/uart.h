/*
 * Copyright (c) 2012, German Aerospace Center (DLR)
 * All rights reserved.
 *
 * @author	Fabian Greif <fabian.greif@dlr.de>
 */
// ----------------------------------------------------------------------------
#ifndef outpost_AMAP_UART_H
#define outpost_AMAP_UART_H

#include <cstddef>
#include <stdint.h>

#include <outpost/hal/serial.h>
#include <outpost/utils.h>
#include <outpost/utils/deque.h>
#include <outpost/utils/bounded_array.h>

#include "amap.h"

namespace outpost
{
namespace iff
{

/**
 * UART over AMAP driver
 *
 * \author	Fabian Greif
 * \author  Muhammad Bassam
 */
class UartBase : public hal::Serial
{
public:
    /**
     * Constructor
     *
     * \param amap
     *     Configured AMAP device
     * \param baseAddress
     *     AMAP base address of the UART device.
     * \param hardwareTxBufferSize
     *     Buffer size of the hardware transmission FIFO
     * \param hardwareRxBufferSize
     *     Buffer size of the hardware reception FIFO
     * \param txBuffer
     *     Transmit buffer (must be 32 bit aligned)
     * \param rxBuffer
     *     Receive buffer (must be 32 bit aligned)
     */
    UartBase(Amap *amap,
             uint32_t baseAddress,
             std::size_t hardwareTxBufferSize,
             std::size_t hardwareRxBufferSize,
             outpost::BoundedArray<uint8_t> txBuffer,
             outpost::BoundedArray<uint8_t> rxBuffer);

    virtual
    ~UartBase();

    /**
     * Connect to the AMAP UART.
     *
     * \warning	AMAP and therefore SpaceWire have to be initialized
     * 			and connected before calling this function.
     *
     * Reads and stores the initial state of the AMAP UART for
     * later reference.
     */
    bool
    initialize();

public:
    // hal::Serial interface
    virtual void
    close()
    {
    }

    // FIXME Implementation missing!
    virtual size_t
    getNumberOfBytesAvailable()
    {
        return 0;
    }

    virtual bool
    isAvailable(void);

    virtual std::size_t
    read(outpost::BoundedArray<uint8_t> data,
         time::Duration timeout = time::Duration::maximum());

    virtual std::size_t
    write(outpost::BoundedArray<const uint8_t> data,
          time::Duration timeout = time::Duration::maximum());

    virtual void
    flush();

    /**
     * Flush receive buffers.
     *
     * Data remaining in the receive buffer is discarded. Afterwards all
     * internal buffers are empty.
     */
    virtual void inline
    flushReceiver()
    {

    }

    /**
     * Sends eventually buffered data. Afterwards all internal buffers
     * are empty.
     */
    virtual void inline
    flushTransmitter()
    {

    }

public:
    /**
     * Perform a hardware reset of the UART.
     */
    void
    reset();

    /**
     * Enable/disable reception of data.
     */
    void
    enableReception(bool enable = true);

    /**
     * Enable/disable transmission of data.
     */
    void
    enableTransmission(bool enable = true);

    // TODO configure parity

    /**
     * TODO description & correlation to actual baudrate
     *
     * \param divider
     */
    void
    setBaudrateDivider(uint32_t divider);

    void
    enableParity(bool odd);

    void
    disableParity();

    uint32_t
    getBaudrateDivider() const
    {
        return mControlRegister & 0x00FFFFFF;
    }

    uint32_t
    getBaseAddr() const
    {
        return mBaseAddress;
    }

    uint32_t
    getStatus();

    /**
     * Synchronize receive buffer with the I/F FPGA
     */
    void
    synchronizeReceiveBuffer();

    /**
     * Synchronize transmit buffer with the I/F FPGA
     */
    void
    synchronizeTransmitBuffer();

    /**
     * Get receive buffer size of the connected AMAP UART in bytes.
     *
     * \note	Only valid after initialize() returned `true`.
     */
    inline uint16_t
    getHardwareReceiveBufferSize() const
    {
        return mRx.mAmap.mTotalSize;
    }

    /**
     * Get transmit buffer size of the connected AMAP UART in bytes.
     *
     * \note	Only valid after initialize() returned `true`.
     */
    inline uint16_t
    getHardwareTransmitBufferSize() const
    {
        return mTx.mAmap.mTotalSize;
    }

    // TODO get status register

protected:
    /**
     * Register offset addresses in bytes.
     */
    struct Register
    {
        enum Type
        {
            control = 0x0000,
            status = 0x0004,
            threshold = 0x0008,
            rxControl = 0x000C,
            txControl = 0x0010,
            rxFifo = 0x1000,
            txFifo = 0x1200
        };
    };

    /**
     * Flags of the control register (bit 31..27).
     */
    struct Flags
    {
        enum Type
        {
            reset = (1 << 31),
            transmissionEnable = (1 << 30),
            receptionEnable = (1 << 29),
            parityEnable = (1 << 28),
            parityOddEven = (1 << 27)
        };
    };

    struct Channel
    {
        Channel(std::size_t totalSize,
                outpost::BoundedArray<uint8_t> buffer) :
                mBuffer(&buffer[0], buffer.getNumberOfElements()), mAmap()
        {
            mAmap.mTotalSize = totalSize;
        }

        struct AmapQueue
        {
            uint16_t mHead;
            uint16_t mTail;
            uint16_t mTotalSize;		///< Buffer size in bytes
        };

        Deque<uint8_t> mBuffer;
        AmapQueue mAmap;
    };

    /**
     * Read number of bytes currently waiting in the buffer.
     */
    static uint16_t
    getUsedSlots(Channel::AmapQueue& amap);

    /**
     * Get number of free slots for transmission.
     *
     * The free slots are calculated as followed:
     *
     *     free = size - available - 1 - x
     *
     * `-1` because the ring buffer uses only head and tail and no
     * additional size information, therefore it can only be filled
     * with n-1 bytes (n being the total number of slots).
     *
     * The `-x` depends on the current position of the tail pointer
     * with x in range [0..2].
     * To avoid having to do a read-modify-write operation to the word
     * in which the pointer located, this word is marked as not
     * writable.
     */
    static uint16_t
    getFreeTransmitSlots(Channel::AmapQueue& amap);

private:
    /**
     * Read RX/TX mHead and mTail pointers over AMAP.
     */
    void
    updateExternalBufferPointers();

    Amap* mAmap;
    uint32_t mBaseAddress;
    bool mInitialized;

    /// Copy of the CNTL register
    uint32_t mControlRegister;

    Channel mTx;
    Channel mRx;

    uint8_t mUnalignedDataBuffer[3];
};

/**
 * Template class containing the necessary buffers for the
 * UartBase class.
 *
 * \author	Fabian Greif
 */
template<std::size_t TX, std::size_t RX>
class Uart : public UartBase
{
public:
    inline
    Uart(Amap *amap,
         uint32_t baseAddress,
         std::size_t hardwareTxBufferSize,
         std::size_t hardwareRxBufferSize) :
            UartBase(amap, baseAddress, hardwareTxBufferSize,
                    hardwareRxBufferSize,
                    outpost::BoundedArray < uint8_t
                            > (mTxBuffer, sizeof(mTxBuffer)),
                    outpost::BoundedArray < uint8_t
                            > (mRxBuffer, sizeof(mRxBuffer)))
    {
    }

public:
    uint8_t mTxBuffer[TX] __attribute__((aligned(4)));
    uint8_t mRxBuffer[RX] __attribute__((aligned(4)));
};

}
}

#endif // outpost_AMAP_UART_H
