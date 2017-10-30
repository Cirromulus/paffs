/*
 * journalPersistence.hpp
 *
 *  Created on: 24.10.2017
 *      Author: urinator
 */

#pragma once

#include "commonTypes.hpp"
#include "journalEntry.hpp"

namespace paffs
{

union EntryIdentifier
{
	struct Flash
	{
		Addr addr;
		uint16_t offs;

		bool operator<(const Flash& other)
		{
			if(addr == other.addr)
			{
				return offs < other.addr;
			}
			return addr < other.addr;
		}
		bool operator==(const Flash& other)
		{
			return addr == other.addr && offs == other.offs;
		}
		bool operator!=(const Flash& other)
		{
			return !(*this == other);
		}
		bool operator>=(const Flash& other)
		{
			return !(*this < other);
		}
	} flash;
	struct Mram
	{
		PageAbs offs;
	} mram;
	EntryIdentifier(){};
	EntryIdentifier(Addr _addr, uint16_t _offs)
	{
		flash.addr = _addr;
		flash.offs = _offs;
	}
	EntryIdentifier(Flash _flash) : flash(_flash){};
	EntryIdentifier(PageAbs _offs)
	{
		mram.offs = _offs;
		flash.offs = 0;
	}


	bool operator<(const EntryIdentifier& other)
	{
		return flash < other.flash;
	}
	bool operator==(const EntryIdentifier& other)
	{
		return flash == other.flash;
	}
	bool operator!=(const EntryIdentifier& other)
	{
		return !(*this == other);
	}
	bool operator>=(const EntryIdentifier& other)
	{
		return !(*this < other);
	}
};

class JournalPersistence
{
protected:
	Device* device;
	uint16_t
	getSizeFromMax(const journalEntry::Max &entry);
	uint16_t
	getSizeFromJE(const JournalEntry& entry);
public:
	JournalPersistence(Device* _device) : device(_device){};
	virtual
	~JournalPersistence(){};

	// Rewind has to be called before scanning Elements
	virtual Result
	rewind() = 0;

	virtual Result
	seek(EntryIdentifier& addr) = 0;

	virtual EntryIdentifier
	tell() = 0;

	virtual Result
	appendEntry(const JournalEntry& entry) = 0;

	virtual Result
	clear() = 0;

	virtual Result
	readNextElem(journalEntry::Max& entry) = 0;
};


class MramPersistence : public JournalPersistence
{
	PageAbs curr;
public:
	MramPersistence(Device* _device) : JournalPersistence(_device), curr(0){};
	Result
	rewind();
	Result
	seek(EntryIdentifier& addr);
	EntryIdentifier
	tell();
	Result
	appendEntry(const JournalEntry& entry);
	Result
	clear();
	Result
	readNextElem(journalEntry::Max& entry);
};

class FlashPersistence : public JournalPersistence
{
	struct FlashBuf{
		unsigned char data [dataBytesPerPage];
		bool readOnly;
		bool dirty;
		PageAbs page;
		FlashBuf()
		{
			memset(data, 0, dataBytesPerPage);
			readOnly = false;
			dirty = false;
			page = 0;
		}
	} buf;
	EntryIdentifier::Flash curr;
public:
	FlashPersistence(Device* _device) : JournalPersistence(_device){};
	Result
	rewind();
	Result
	seek(EntryIdentifier& addr);
	EntryIdentifier
	tell();
	Result
	appendEntry(const JournalEntry& entry);
	Result
	clear();
	Result
	readNextElem(journalEntry::Max& entry);
private:
	Result
	commitBuf();
	Result
	findNextPos(bool afterACheckpoint = false);
	Result
	loadCurrentPage(bool readPage = true);
};
}
