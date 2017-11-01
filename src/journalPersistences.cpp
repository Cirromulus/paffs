/*
 * journalPersistences.cpp
 *
 *  Created on: 24.10.2017
 *      Author: urinator
 */


#include "journalPersistence.hpp"
#include "device.hpp"
#include <inttypes.h>

namespace paffs
{

uint16_t
JournalPersistence::getSizeFromJE(const JournalEntry& entry)
{
	uint16_t size = 0;
	switch(entry.topic)
	{
	case JournalEntry::Topic::checkpoint:
		size = sizeof(journalEntry::Checkpoint);
		break;
	case JournalEntry::Topic::success:
		size = sizeof(journalEntry::Success);
		break;
	case JournalEntry::Topic::superblock:
		switch(static_cast<const journalEntry::Superblock*>(&entry)->type)
		{
		case journalEntry::Superblock::Type::rootnode:
			size = sizeof(journalEntry::superblock::Rootnode);
			break;
		case journalEntry::Superblock::Type::areaMap:
			switch(static_cast<const journalEntry::superblock::AreaMap*>(&entry)->operation)
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
		case journalEntry::Superblock::Type::activeArea:
			size = sizeof(journalEntry::superblock::ActiveArea);
			break;
		case journalEntry::Superblock::Type::usedAreas:
			size = sizeof(journalEntry::superblock::UsedAreas);
			break;
		}
		break;
	case JournalEntry::Topic::tree:
		switch(static_cast<const journalEntry::BTree*>(&entry)->op)
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
		switch(static_cast<const journalEntry::SummaryCache*>(&entry)->subtype)
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
		switch(static_cast<const journalEntry::Inode*>(&entry)->operation)
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
JournalPersistence::getSizeFromMax(const journalEntry::Max &entry)
{
	return getSizeFromJE(entry.base);
}

//======= MRAM ========
Result
MramPersistence::rewind()
{
	curr = sizeof(PageAbs);
	return Result::ok;
}

Result
MramPersistence::seek(EntryIdentifier& addr)
{
	curr = addr.mram.offs;
	return Result::ok;
}

EntryIdentifier
MramPersistence::tell()
{
	return EntryIdentifier(curr);
}

Result
MramPersistence::appendEntry(const JournalEntry& entry)
{
	if(curr + sizeof(journalEntry::Max) > mramSize)
		return Result::nospace;
	uint16_t size = getSizeFromJE(entry);
	device->driver.writeMRAM(curr, &entry, size);
	PAFFS_DBG_S((PAFFS_TRACE_JOURNAL | PAFFS_TRACE_VERBOSE),
			"Wrote Entry to %" PRIu64 "-%" PRIu64, curr, curr + size);
	curr += size;
	device->driver.writeMRAM(0, &curr, sizeof(PageAbs));
	return Result::ok;
}

Result
MramPersistence::clear()
{
	curr = sizeof(PageAbs);
	device->driver.writeMRAM(0, &curr, sizeof(PageAbs));
	return Result::ok;
}

Result
MramPersistence::readNextElem(journalEntry::Max& entry)
{
	PageAbs hwm;
	device->driver.readMRAM(0, &hwm, sizeof(PageAbs));
	if(curr >= hwm)
		return Result::nf;

	device->driver.readMRAM(curr, &entry, sizeof(journalEntry::Max));
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

//======= FLASH =======

//FIXME: no revert of changes of uncheckpointed entries if it is buffered.

Result
FlashPersistence::rewind()
{
	//TODO: ActiveArea has to be consistent even after a remount
	//TODO: Save AA in Superpage
	if(device->areaMgmt.getActiveArea(AreaType::journal) == 0)
	{
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Invalid journal Area (0)!");
		return Result::bug;
	}
	curr.addr = combineAddress(device->areaMgmt.getActiveArea(AreaType::journal), 0);
	curr.offs = 0;
	return Result::ok;
}
Result
FlashPersistence::seek(EntryIdentifier& addr)
{
	if(buf.dirty)
	{
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Skipping a journal Commit in seek!");
		return Result::bug;
	}
	curr = addr.flash;
	Result r = loadCurrentPage();
	if(r != Result::ok)
	{
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not load a page in journal!");
		return r;
	}
	return Result::ok;
}
EntryIdentifier
FlashPersistence::tell()
{
	return EntryIdentifier(curr);
}
Result
FlashPersistence::appendEntry(const JournalEntry& entry)
{
	//YEAH.
	//Keep in mind that a page cant be written twice for wiederaufnahme des loggings nach replay
	uint16_t size = getSizeFromJE(entry);
	Result r;
	if(curr.offs + size > dataBytesPerPage)
	{
		//Commit is needed
		r = commitBuf();
		if(r != Result::ok)
		{
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not commit buffer");
			//Maybe try other?
			return r;
		}
		r = findNextPos();
		if(r != Result::ok)
		{
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not find next Pos for JournalEntry");
			return r;
		}
		//just init current page b.c. we assume free pages after last page
		loadCurrentPage(false);
	}
	memcpy(&buf.data[curr.offs], static_cast<const void*>(&entry), size);
	curr.offs = size;

	if(entry.topic == JournalEntry::Topic::checkpoint)
	{
		//Flush buffer because we have a checkpoint
		r = commitBuf();
		if(r != Result::ok)
		{
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not commit buffer");
			//Maybe try other?
			return r;
		}
		r = findNextPos(true);
		if(r != Result::ok)
		{
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not find next Pos for JournalEntry");
			return r;
		}
		//just init current page b.c. we assume free pages after last page
		loadCurrentPage(false);
	}
	return Result::ok;
}
Result
FlashPersistence::clear()
{
	//TODO: Change Area with garbage collection, gc should notice usage upon mount and delete
	return Result::nimpl;
}

Result
FlashPersistence::readNextElem(journalEntry::Max& entry)
{
	if(buf.data[curr.offs] == 0)
	{
		//Reached end of buf
		Result r = findNextPos();
		if(r != Result::ok)
			return Result::nf;
		loadCurrentPage();
	}
	if(buf.data[curr.offs] == 0)
	{
		return Result::nf;
	}

	memcpy(static_cast<void*>(&entry), &buf.data[curr.offs], curr.offs + sizeof(journalEntry::Max) > dataPagesPerArea ?
	                                    dataPagesPerArea - curr.offs : sizeof(journalEntry::Max));
	uint16_t size = getSizeFromMax(entry);
	PAFFS_DBG_S((PAFFS_TRACE_JOURNAL | PAFFS_TRACE_VERBOSE),
			"Read entry at %" PRIu32 ":%" PRIu32 " %" PRIu16 "-%" PRIu16,
			extractLogicalArea(curr.addr), extractPageOffs(curr.addr), curr.offs, curr.offs + size);
	if(size == 0)
	{
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Did not recognize JournalEntry");
		return Result::fail;
	}
	curr.offs += size;
	return Result::ok;
}

Result
FlashPersistence::commitBuf()
{
	if(buf.readOnly)
	{
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried commiting a buffer that was already written!");
		return Result::bug;
	}
	if(!buf.dirty)
	{
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried commiting a buffer that was not modified!");
		return Result::bug;
	}

	Result r = device->driver.writePage(buf.page, buf.data, dataBytesPerPage);
	if(r != Result::ok)
	{
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not write Page for Journal commit");
		return r;
	}
	buf.dirty = false;
	buf.readOnly = true;
	return Result::ok;
}

Result
FlashPersistence::findNextPos(bool forACheckpoint)
{
	if(buf.dirty)
	{
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried to switch to new Position without commiting old one");
		return Result::bug;
	}
	if(forACheckpoint)
	{
		//Check if we are in reserved space inside area
		//TODO: Commit everything if in safespace
	}
	if(extractPageOffs(curr.addr) == totalPagesPerArea)
	{
		//Ouch, we dont want to reach this
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not find a free page in journal!");
		return Result::nospace;
	}
	curr = EntryIdentifier(combineAddress(extractLogicalArea(curr.addr), extractPageOffs(curr.addr) + 1), 0).flash;
	return Result::ok;
}

Result
FlashPersistence::loadCurrentPage(bool readPage)
{
	if(buf.dirty)
	{
		PAFFS_DBG(PAFFS_TRACE_BUG, "loading page with dirty buf!");
		return Result::bug;
	}

	if(readPage)
	{
		Result r = device->driver.readPage(getPageNumber(curr.addr, *device), buf.data, dataBytesPerPage);
		if(r != Result::ok)
		{
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not read a page for journal!");
			return r;
		}
	}else
	{
		memset(buf.data, 0, dataBytesPerPage);
	}
	buf.page = getPageNumber(curr.addr, *device);
	if(buf.data[0] == 0xFF)
	{
		buf.readOnly = false;
		memset(buf.data, 0, dataBytesPerPage);
	}else
	{
		buf.readOnly = true;
	}
	buf.dirty = false;
	return Result::ok;
}

};
