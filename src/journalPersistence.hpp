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

struct EntryIdentifier
{
	PageAbs page;
	uint16_t offs;
	EntryIdentifier() : page(0), offs(0){};
	EntryIdentifier(PageAbs _page, uint16_t _offs): page(_page), offs(_offs){};
	bool operator<(EntryIdentifier& other)
	{
		if(page == other.page)
		{
			return offs < other.offs;
		}
		return page < other.page;
	}
	bool operator==(const EntryIdentifier other)
	{
		return page == other.page && offs == other.offs;
	}
	bool operator !=(const EntryIdentifier other)
	{
		return !(*this == other);
	}
	bool operator>=(const EntryIdentifier other)
	{
		return !(*this < other);
	}
};

class JournalPersistence
{
protected:
	Driver& driver;
	uint16_t
	getSizeFromMax(const journalEntry::Max &entry);
	uint16_t
	getSizeFromJE(const JournalEntry& entry);
public:
	JournalPersistence(Driver& _driver) : driver(_driver){};
	virtual
	~JournalPersistence(){};

	virtual void
	rewind() = 0;

	virtual void
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
	MramPersistence(Driver& _driver) : JournalPersistence(_driver), curr(sizeof(PageAbs)){};
	void
	rewind();
	void
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
	//TODO
};
}

