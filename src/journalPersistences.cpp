/*
 * journalPersistences.cpp
 *
 *  Created on: 24.10.2017
 *      Author: urinator
 */


#include "journalPersistence.hpp"
#include "driver/driver.hpp"
#include <inttypes.h>

namespace paffs
{

uint16_t
JournalPersistence::getSizeFromMax(const journalEntry::Max &entry)
{
	uint16_t size = 0;
	switch(entry.base.topic)
	{
	case JournalEntry::Topic::checkpoint:
		size = sizeof(journalEntry::Checkpoint);
		break;
	case JournalEntry::Topic::success:
		size = sizeof(journalEntry::Success);
		break;
	case JournalEntry::Topic::superblock:
		switch(entry.superblock.type)
		{
		case journalEntry::Superblock::Type::rootnode:
			size = sizeof(journalEntry::superblock::Rootnode);
			break;
		case journalEntry::Superblock::Type::areaMap:
			switch(entry.superblock_.areaMap.operation)
			{
			case journalEntry::superblock::AreaMap::Operation::type:
				size = sizeof(journalEntry::superblock::areaMap::Type);
				break;
			case journalEntry::superblock::AreaMap::Operation::status:
				size = sizeof(journalEntry::superblock::areaMap::Status);
				break;
			case journalEntry::superblock::AreaMap::Operation::erasecount:
				size = sizeof(journalEntry::superblock::areaMap::Erasecount);
				break;
			case journalEntry::superblock::AreaMap::Operation::position:
				size = sizeof(journalEntry::superblock::areaMap::Type);
				break;
			case journalEntry::superblock::AreaMap::Operation::swap:
				size = sizeof(journalEntry::superblock::areaMap::Swap);
				break;
			}
			break;
		}
		break;
	case JournalEntry::Topic::tree:
		switch(entry.btree.op)
		{
		case journalEntry::BTree::Operation::insert:
			size = sizeof(journalEntry::btree::Insert);
			break;
		case journalEntry::BTree::Operation::update:
			size = sizeof(journalEntry::btree::Update);
			break;
		case journalEntry::BTree::Operation::remove:
			size = sizeof(journalEntry::btree::Remove);
			break;
		}
		break;
	case JournalEntry::Topic::summaryCache:
		switch(entry.summaryCache.subtype)
		{
		case journalEntry::SummaryCache::Subtype::commit:
			size = sizeof(journalEntry::summaryCache::Commit);
			break;
		case journalEntry::SummaryCache::Subtype::remove:
			size = sizeof(journalEntry::summaryCache::Remove);
			break;
		case journalEntry::SummaryCache::Subtype::setStatus:
			size = sizeof(journalEntry::summaryCache::SetStatus);
			break;
		}
		break;
	case JournalEntry::Topic::inode:
		switch(entry.inode.operation)
		{
		case journalEntry::Inode::Operation::add:
			size = sizeof(journalEntry::inode::Add);
			break;
		case journalEntry::Inode::Operation::write:
			size = sizeof(journalEntry::inode::Write);
			break;
		case journalEntry::Inode::Operation::remove:
			size = sizeof(journalEntry::inode::Remove);
			break;
		case journalEntry::Inode::Operation::commit:
			size = sizeof(journalEntry::inode::Commit);
			break;
		}
	}
	return size;
}

uint16_t
JournalPersistence::getSizeFromJE(const JournalEntry& entry)
{
	journalEntry::Max max;
	memcpy(&max, &entry, sizeof(journalEntry::Max));
	return getSizeFromMax(max);
}

//======= MRAM ========
void
MramPersistence::rewind()
{
	curr = sizeof(PageAbs);
}
void
MramPersistence::seek(EntryIdentifier& addr)
{
	curr = addr.page;
}
EntryIdentifier
MramPersistence::tell()
{
	return EntryIdentifier(curr, 0);
}
Result
MramPersistence::appendEntry(const JournalEntry& entry)
{
	if(curr + sizeof(journalEntry::Max) > mramSize)
		return Result::nospace;
	uint16_t size = getSizeFromJE(entry);
	driver.writeMRAM(curr, &entry, size);
	PAFFS_DBG_S((PAFFS_TRACE_JOURNAL | PAFFS_TRACE_VERBOSE),
			"Wrote Entry to %" PRIu64 "-%" PRIu64, curr, curr + size);
	curr += size;
	driver.writeMRAM(0, &curr, sizeof(PageAbs));
	return Result::ok;
}

Result
MramPersistence::clear()
{
	curr = sizeof(PageAbs);
	driver.writeMRAM(0, &curr, sizeof(PageAbs));
	return Result::ok;
}

Result
MramPersistence::readNextElem(journalEntry::Max& entry)
{
	PageAbs hwm;
	driver.readMRAM(0, &hwm, sizeof(PageAbs));
	if(curr >= hwm)
		return Result::nf;

	driver.readMRAM(curr, &entry, sizeof(journalEntry::Max));
	uint16_t size = getSizeFromMax(entry);
	PAFFS_DBG_S((PAFFS_TRACE_JOURNAL | PAFFS_TRACE_VERBOSE),
			"Read entry at %" PRIu64 "-%" PRIu64, curr, curr + size);
	if(size == 0)
	{
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Read unknown entry!");
		return Result::fail;
	}
	curr += size;
	return Result::ok;
}
};

