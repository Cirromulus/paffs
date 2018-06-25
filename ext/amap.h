#ifndef COBC_AMAP__AMAP_H
#define COBC_AMAP__AMAP_H

#include <outpost/hal/spacewire.h>
#include <outpost/rtos.h>
#include <outpost/time.h>

namespace outpost
{
namespace iff
{
enum Operation
{
    OPERATION_PING = 0xff, OPERATION_READ = 0x01, OPERATION_WRITE = 0x02
};

enum OperationResult
{
    OPERATION_ACK = 0x01,   ///< Success
    OPERATION_NACK_CRC_WRONG = 0x02,    ///< e.g. CRC failed
    OPERATION_NACK_REQUEST_UNDEFINED = 0x03  ///< Request type is unknown
};

struct Information
{
    Information() :
            protocolVersion(0), topDesignConfiguration(0),
            protocolRevisionNumber(0)
    {
    }

    void
    serialize(uint8_t * buffer)
    {
        buffer[0] = protocolVersion;
        buffer[1] = topDesignConfiguration;
        buffer[2] = protocolRevisionNumber >> 8;
        buffer[3] = protocolRevisionNumber & 0xff;
    }

    void
    deserialize(const uint8_t * buffer)
    {
        protocolVersion = buffer[0];
        topDesignConfiguration = buffer[1];
        protocolRevisionNumber = (buffer[2] << 8) | buffer[3];
    }

    // TODO meaning of these values
    uint8_t protocolVersion;
    uint8_t topDesignConfiguration;
    uint16_t protocolRevisionNumber;
};

class ReadHandler
{
public:
    virtual
    ~ReadHandler()
    {
    }

    virtual void
    read(const uint8_t * data,
         std::size_t n) = 0;
};

class WriteHandler
{
public:
    virtual
    ~WriteHandler()
    {
    }

    virtual void
    write(uint8_t * data,
          std::size_t n) = 0;
};

/**
 * A Memory Access Protocol.
 *
 * AMAP provides means for a SpaceWire node to write to and read from
 * memory inside another SpaceWire node.
 *
 * @note    This driver assumes the hal::SpaceWire buffers
 *          to be 32 bit aligned.
 *
 * @author  Fabian Greif <fabian.greif@dlr.de>
 */
// TODO synchronization for access with multiple threads
class Amap
{
public:
    static const uint8_t protocolIdentifier = 240;

    /// Logical address of the AMAP target device (I/F FPGA)
    static const uint8_t targetLogicalAddress = 254;

    /// Own logical address under which the AMAP responses are received.
    static const uint8_t initiatorLogicalAddress = 254;

    /**
     * The header does include the logical address and the protocol identifier
     */
    static const size_t requestHeaderSize = 10;
    static const size_t responseHeaderSize = 6;

    // TODO atomic access to these counters
    struct ErrorCounter
    {
        inline
        ErrorCounter() :
                writeOperation(0), readOperation(0), pingOperation(0),
                responsePacketSize(0), responseHeaderCrc(0), responseNackCrc(0),
                responseNackUndefined(0), responsePayloadLength(0),
                responsePayloadCrc(0)
        {
        }

        /**
         * Reset all error counters to zero.
         */
        void
        reset()
        {
            writeOperation = 0;
            readOperation = 0;
            pingOperation = 0;
            responsePacketSize = 0;
            responseHeaderCrc = 0;
            responseNackCrc = 0;
            responseNackUndefined = 0;
            responsePayloadLength = 0;
            responsePayloadCrc = 0;
        }

        /// Write request failed
        std::size_t writeOperation;

        /// Read request failed
        std::size_t readOperation;

        /// Ping request failed
        std::size_t pingOperation;

        /// Response packet size was wrong
        std::size_t responsePacketSize;

        /// Response header CRC was invalid
        std::size_t responseHeaderCrc;

        /// NACK received: Header or payload CRC of the request was wrong
        std::size_t responseNackCrc;

        /// NACK received: Request identifier was undefined
        std::size_t responseNackUndefined;

        /// Response size doesn't match given length in the header
        std::size_t responsePayloadLength;

        /// Response payload CRC was invalid
        std::size_t responsePayloadCrc;
    };

    /**
     * Constructor.
     *
     * @param   spw
     *      Configured SpaceWire port. The AMAP driver must be the
     *      only driver connected to the SpaceWire Driver as AMAP
     *      uses only a very basic Ping-Pong based
     *      protocol (Request -> Response)!
     */
    Amap(hal::SpaceWire& spw);

    virtual
    ~Amap()
    {
    }

    /**
     * Get the SpaceWire port this driver is connected to.
     *
     * @return
     *     SpaceWire port the drivers uses.
     */
    inline hal::SpaceWire&
    getSpaceWirePort()
    {
        return mSpacewire;
    }

    /**
     * Write to a remote address.
     *
     * Blocks until the write operation has finished.
     *
     * @param[in]  address
     *      Write address.
     * @param[in]  data
     *      Data to write is copied from this buffer.
     * @param[in]  length
     *      Number of 32 bit words to write.
     */
    bool
    write(uint32_t address,
          const uint8_t * data,
          std::size_t length,
          outpost::time::Duration timeout);

    /**
     * Read from a remote address.
     *
     * Blocks until the read operation has finished.
     *
     * @param[in]  address
     *      Read Address
     * @param[out] data
     *      Output buffer. Data from the remote device is copied to
     *      this address.
     * @param[in]  length
     *      Number of 32 bit words to read.
     */
    bool
    read(uint32_t address,
         uint8_t * data,
         std::size_t length,
		 outpost::time::Duration timeout);

    /**
     * Write to remote address without additional copying.
     *
     * @param[in]  address
     * @param[in]  length
     *      Number of 32 bit words to write.
     * @param[in]  handler
     *      Implements a callback mechanism. The write handler gets
     *      direct access to the payload of the generated SpaceWire
     *      message and can fill it at will.
     */
    bool
    write(uint32_t address,
          std::size_t length,
		  outpost::time::Duration timeout,
          WriteHandler& handler);

    /**
     * Read without copying the SpaceWire message.
     *
     * Blocks until the read operation has finished.
     *
     * @param[in]  address
     * @param[in]  length
     *      Number of 32 bit words to read.
     * @param[in]  handler
     *      Read handler. Implements a callback mechanism. The
     *      read handler gets direct access to the payload of the
     *      SpaceWire message the response is received.
     */
    bool
    read(uint32_t address,
         std::size_t length,
		 outpost::time::Duration timeout,
         ReadHandler& handler);

    /**
     * Request information about the AMAP implementation.
     *
     * @param[out]  info
     *      AMAP implementation information
     */
    bool
    ping(Information& info,
    		outpost::time::Duration timeout);

    inline const ErrorCounter&
    getErrorCounter() const
    {
        return mErrorCounter;
    }

    /**
     * Reset error counters.
     *
     * Only needed during testing & verification.
     */
    inline void
    resetErrorCounter()
    {
        mErrorCounter.reset();
    }

    static inline void
    serialize16(uint8_t* buffer,
                uint16_t data)
    {
        buffer[0] = static_cast<uint8_t>(data >> 8);
        buffer[1] = static_cast<uint8_t>(data >> 0);
    }

    static inline uint16_t
    deserialize16(const uint8_t* buffer)
    {
        uint16_t value = 0;
        value |= static_cast<uint16_t>(buffer[0]) << 8;
        value |= static_cast<uint16_t>(buffer[1]) << 0;

        return value;
    }

    static inline void
    serialize32(uint8_t* buffer,
                uint32_t data)
    {
        buffer[0] = static_cast<uint8_t>(data >> 24);
        buffer[1] = static_cast<uint8_t>(data >> 16);
        buffer[2] = static_cast<uint8_t>(data >> 8);
        buffer[3] = static_cast<uint8_t>(data >> 0);
    }

    static inline uint32_t
    deserialize32(const uint8_t* buffer)
    {
        uint32_t value = 0;
        value |= static_cast<uint32_t>(buffer[0]) << 24;
        value |= static_cast<uint32_t>(buffer[1]) << 16;
        value |= static_cast<uint32_t>(buffer[2]) << 8;
        value |= static_cast<uint32_t>(buffer[3]) << 0;

        return value;
    }

private:
    static void
    writeHeader(outpost::Slice<uint8_t> buffer,
                Operation operation,
                uint32_t address,
                std::size_t length);

    bool
    checkResponseHeader(hal::SpaceWire::ReceiveBuffer& buffer,
                        std::size_t expectedPayloadLength);
    void
    hexDump (char *desc, const void *addr, int len);

    hal::SpaceWire& mSpacewire;
    ErrorCounter mErrorCounter;
    uint8_t mRetries;
    outpost::rtos::Mutex mOperationLock;
};
}
}

#endif // COBC_AMAP__AMAP_H
