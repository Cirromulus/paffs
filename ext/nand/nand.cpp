#include "nand.h"

#include <cstdio>

#include <spacewire_light.h>
#include <outpost/utils/serialize.h>

#define DEBUG

#ifdef DEBUG
#	include <stdint.h>
#   include <inttypes.h>
#	define LOG(x)	x
#	define LOG_BEGIN
#	define LOG_END
#else
#	define LOG(x)
#	define LOG_BEGIN if (0) {
#	define LOG_END	}
#endif

using namespace outpost;

// ----------------------------------------------------------------------------
Nand::Nand(outpost::iff::Amap &amapInput,
           uint32_t baseAddressInput) :
        amap(amapInput), baseAddress(baseAddressInput), lastStatus(0)
{
}

// ----------------------------------------------------------------------------
bool
Nand::isReady()
{
    uint8_t buffer[4];

    uint32_t address = baseAddress + flashAndNandCtrlStatusRegister;
    if (!amap.read(address, buffer, 1, outpost::time::Milliseconds(2)))
    {
        LOG(printf("NAND: Failure to read 'Flash and nand ctrl status'\n")
        ;);
    }

    Deserialize payload(buffer);

    lastStatus = payload.read<uint32_t>();
    if ((lastStatus & 0x07) == 0)
    {
//		LOG(printf("Status ok (%08x)\n", static_cast<unsigned int>(lastStatus)););
        return true;
    }
    else
    {
//		LOG_BEGIN
//		printf("NAND: Flash and nand ctrl status (%08x):\n",
//				static_cast<unsigned int>(lastStatus));
//		for (int i = 0; i < 4; ++i) {
//			printf("0x%02x ", buffer[i]);
//		}
//		printf("\n");
//		LOG_END

        return false;
    }
}

bool
Nand::isSuccess()
{
    return (lastStatus & (1 << 6));
}

void
Nand::enableLatchUpProtection()
{
    uint8_t buffer[4];
    Serialize payload(buffer);

    payload.store < uint32_t > ((1 << 3) | (1 << 2) | (1 << 1) | (1 << 0));
    uint32_t address = baseAddress + latchUpProtectionCommandRegister;
    if (!amap.write(address, buffer, 1, outpost::time::Milliseconds(2)))
    {
        LOG(printf("NAND: Failure in LUP command\n")
        ;
    )
}
else
{
    LOG(printf("NAND: LUP command enable\n")
    ;
)
}
}

void
Nand::disableLatchUpProtection()
{
uint8_t buffer[4];
Serialize payload(buffer);

payload.store < uint32_t > ((1 << 7) | (1 << 6) | (1 << 5) | (1 << 4));
uint32_t address = baseAddress + latchUpProtectionCommandRegister;
if (!amap.write(address, buffer, 1, outpost::time::Milliseconds(2)))
{
LOG(printf("NAND: Failure in LUP command\n")
;
)
}
else
{
LOG(printf("NAND: LUP command disable\n")
;
)
}
}

// ----------------------------------------------------------------------------
bool
Nand::readId(uint8_t bank,
             uint8_t device,
             uint8_t *buffer)
{
Serialize payload(buffer);

  // request reading id
payload.store < uint32_t
> ((device << 28) | (bank << 26) | (1 << 2) | (1 << 0));
uint32_t address = baseAddress + writeFlashCommandRegister;
if (!amap.write(address, buffer, 1, outpost::time::Milliseconds(2)))
{
LOG(printf("NAND: Failure to write flash command (request id)\n")
;
)
return false;
}
else
{
//		LOG(printf("NAND: Request Id\n");)
}

  // wait until flash is ready
while (!isReady())
{
	rtems_task_wake_after(10);
	LOG(printf("Status: %04"PRIX32"\n", getStatus())
	;
	)
}

  // reading
address = baseAddress;
if (!amap.read(address, buffer, 2, outpost::time::Milliseconds(2)))
{
return false;
}

return true;
}

// ----------------------------------------------------------------------------

void
Nand::readPage(uint8_t bank,
               uint8_t device,
               uint32_t page,
               uint8_t *buffer)
{
Serialize payload(buffer);

  // request reading of a page
payload.store < uint32_t
> ((device << 28) | (bank << 26) | (page << 7) | (1 << 3) | (1 << 0));
uint32_t address = baseAddress + writeFlashCommandRegister;

if (!amap.write(address, buffer, 1, outpost::time::Milliseconds(2)))
	{
	LOG(printf("NAND: Failure to write flash command (read page)\n")
	;
	)
	}
else
	{
	LOG(printf("NAND: read Page %"PRIu32"\n", page)
	;
	)
}

while (!Nand::isReady())
{
  // wait until flash is ready
}

  // read page and fill buffer with page data
address = baseAddress;

if (!amap.read(address, buffer, 4224 / 4, outpost::time::Milliseconds(2)))
{
printf("read failed!\n");
}
}

void
Nand::writePage(uint8_t bank,
                uint8_t device,
                uint32_t page,
                const uint8_t *buffer)
{
  // write buffer data into RAM buffer
uint32_t address = baseAddress;
if (!amap.write(address, buffer, 4224 / 4, outpost::time::Milliseconds(2)))
{
printf("write failed!\n");
}

  // send flash command to write RAM buffer content to page
uint8_t commandBuffer[4];
Serialize payload(commandBuffer);

payload.store < uint32_t
> ((device << 28) | (bank << 26) | (page << 7) | (1 << 4) | (1 << 0));
address = baseAddress + writeFlashCommandRegister;

if (!amap.write(address, commandBuffer, 1, outpost::time::Milliseconds(2)))
{
LOG(printf("NAND: Failure to write flash command (write page)\n")
;
)
}
else
{
		LOG(printf("NAND: write Page %"PRIu32"\n", page);)
}

while (!Nand::isReady())
{
  // wait until flash is ready
}
}

void
Nand::eraseBlock(uint8_t bank,
                 uint8_t device,
                 uint32_t block)
{
uint8_t buffer[4];
Serialize payload(buffer);

payload.store < uint32_t
> ((device << 28) | (bank << 26) | (block << 13) | (1 << 5) | (1 << 0));
uint32_t address = baseAddress + writeFlashCommandRegister;

if (!amap.write(address, buffer, 1, outpost::time::Milliseconds(2)))
{
LOG(printf("NAND: Failure to write flash command (block erase)\n")
;
)
}
else
{
		LOG(printf("NAND: Erase block %"PRIu32" (page %"PRIu32"..%"PRIu32")\n",
				block, block*64,block*64+63);)
}

while (!Nand::isReady())
{
  // wait until flash is ready
}
}

// ----------------------------------------------------------------------------

void
Nand::writeRamBuffer(uint8_t *buffer,
                     size_t length)
{
if (length % 4 != 0)
{
printf("invalid length, must be divisible by four\n");
return;
}

uint32_t address = baseAddress;
if (!amap.write(address, buffer, length / 4, outpost::time::Milliseconds(2)))
{
LOG(printf("write RAM buffer failed\n")
;
)
}
//	else printf("write OK\n");
}

void
Nand::readRamBuffer(uint8_t *buffer,
                    size_t length)
{
if (length % 4 != 0)
{
printf("invalid length, must be divisible by four\n");
return;
}

uint32_t address = baseAddress;
if (!amap.read(address, buffer, length / 4, outpost::time::Milliseconds(2)))
{
LOG(printf("read RAM buffer failed\n")
;
)
}
//	else printf("read OK\n");
}

