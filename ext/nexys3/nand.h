
#ifndef NAND_H
#define NAND_H

#include <amap.h>

/**
 *
 * K9F8G08U0M
 *
 * 1 page = 4096 + 128 byte = 4224 byte
 * 1 block = 64 pages
 * 1 device = 4096 blocks
 */

class Nand
{
public:
	Nand(outpost::iff::Amap& amapInput, uint32_t baseAddressInput);

	bool
	isReady();

	bool
	isSuccess();

	inline uint32_t
	getStatus() const
	{
		return lastStatus;
	}

	void
	enableLatchUpProtection();

	void
	disableLatchUpProtection();

	/*
	 * Read id from device
	 */
	bool
	readId(uint8_t bank, uint8_t device, uint8_t *buffer);

	/**
	 * Read a page and write its content into a buffer.
	 *
	 * @param bank
	 * 		nand bank with device
	 * @param device
	 * 		device number on NAND bank (0..3)
	 * @param page
	 * 		Page number (0..262143)
	 * @param *buffer
	 * 		contains page data when reading has finished
	 */
	void
	readPage(uint8_t bank, uint8_t device, uint32_t page, uint8_t *buffer);

	/**
	 * Write data from a buffer into a page
	 *
	 * @param bank
	 * 		nand bank with device
	 * @param device
	 * 		device number on NAND bank (0..3)
	 * @param page
	 * 		Page number (0..262143)
	 * @param *buffer
	 * 		buffer that contains data to write into the page
	 */
	void
	writePage(uint8_t bank, uint8_t device, uint32_t page, const uint8_t *buffer);

	/**
	 * Erase a complete block.
	 *
	 * This will set all pages in the block to `0xff`.
	 *
	 * 64 pages = 1 block.
	 *
	 * @param bank
	 * 		nand bank with device
	 * @param device
	 * 		device number on NAND bank (0..3)
	 * @param block
	 * 		Block to erase (0..4095).
	 */
	void
	eraseBlock(uint8_t bank, uint8_t device, uint32_t block);

	/**
	 * write dummy data into RAM buffer without executing write/read commands
	 *
	 * @param buffer
	 * 		write data from this buffer to RAM buffer
	 * @param length
	 * 		number of bytes to write (must be divisible by four)
	 */
	void
	writeRamBuffer(uint8_t *buffer, size_t length);

	/**
	 * read data out of RAM buffer
	 *
	 * @param buffer
	 * 		store data from RAM buffer here
	 * @param length
	 * 		number of bytes to read (must be divisible by four)
	 */
	void
	readRamBuffer(uint8_t *buffer, size_t length);

private:
	outpost::iff::Amap& amap;
	const uint32_t baseAddress;
	uint32_t lastStatus;

	static const uint32_t controlAndStatusRegister = 0x00002000;
	static const uint32_t writeFlashCommandRegister = 0x00002004;
	static const uint32_t flashAndNandCtrlStatusRegister = 0x00002008;
	static const uint32_t latchUpProtectionStatusRegister = 0x0000200C;
	static const uint32_t latchUpProtectionCommandRegister = 0x00002010;
};


#endif	// NAND_H
