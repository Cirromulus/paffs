/*
 * journal.cpp
 *
 *  Created on: Sep 19, 2017
 *      Author: user
 */

#include "journal.hpp"
#include "commonTypes.hpp"
#include "driver/driver.hpp"
#include "inttypes.h"
#include "area.hpp"

using namespace paffs;
using namespace std;

#define WRITEELEM(type, obj, ptr) \
		driver.writeMRAM(ptr, &obj, sizeof(type)); \
		ptr += sizeof(type);

void
Journal::addEvent(const JournalEntry& entry){
	if(pos + sizeof(journalEntry::Max) > mramSize){
		PAFFS_DBG(PAFFS_TRACE_JOURNAL, "Log full. should be flushed.");
		return;
	}
	writeEntry(pos, entry);
	driver.writeMRAM(0, &pos, sizeof(PageAbs));
}

void
Journal::checkpoint()
{
	addEvent(journalEntry::Checkpoint());
}

void
Journal::clear()
{
	pos = sizeof(PageAbs);
	driver.writeMRAM(0, &pos, sizeof(PageAbs));
}

void
Journal::processBuffer(){
	PageAbs hwm;
	driver.readMRAM(0, &hwm, sizeof(PageAbs));


	if(hwm == sizeof(PageAbs))
	{
		PAFFS_DBG_S(PAFFS_TRACE_VERBOSE, "No Replay of Journal needed");
		return;
	}

	PAFFS_DBG_S(PAFFS_TRACE_JOURNAL, "Replay of Journal needed");

	PageAbs firstUnsuccededEntry[JournalEntry::numberOfTopics];
	memset(&firstUnsuccededEntry, 0, sizeof(firstUnsuccededEntry));
	PageAbs curr = sizeof(PageAbs);

	//Scan for success messages
	while(curr <= hwm)
	{
		journalEntry::Max entry;
		readNextEntry(curr, entry);
		if(entry.base.topic == JournalEntry::Topic::success)
		{
			firstUnsuccededEntry[static_cast<unsigned>(entry.success.target)] = curr;
		}
	}

	if(traceMask & (PAFFS_TRACE_JOURNAL | PAFFS_TRACE_VERBOSE))
	{
		printf("FirstUnsucceededEntry:\n");
		for(unsigned i = 0; i < JournalEntry::numberOfTopics; i++)
		{
			printf("\t%s: %" PRIu64 "\n", JournalEntry::topicNames[i], firstUnsuccededEntry[i]);
		}
	}
	PAFFS_DBG_S((PAFFS_TRACE_JOURNAL | PAFFS_TRACE_VERBOSE), "Scanning for checkpoints...");

	PageAbs lastUnprocessedEntry = sizeof(PageAbs);
	curr = lastUnprocessedEntry;
	while(curr <= hwm)
	{
		journalEntry::Max entry;
		readNextEntry(curr, entry);
		if(entry.base.topic == JournalEntry::Topic::checkpoint)
		{
			applyCheckpointedJournalEntries(lastUnprocessedEntry, curr, firstUnsuccededEntry);
			lastUnprocessedEntry = curr;
		}
	}
	if(lastUnprocessedEntry != hwm)
	{
		PAFFS_DBG_S(PAFFS_TRACE_JOURNAL, "Uncheckpointed Entries exist");
		applyUncheckpointedJournalEntries(lastUnprocessedEntry, hwm);
	}
}

void
Journal::applyCheckpointedJournalEntries(PageAbs from, PageAbs to,
		PageAbs firstUnsuccededEntry[JournalEntry::numberOfTopics])
{
	PAFFS_DBG_S(PAFFS_TRACE_JOURNAL, "Applying Checkpointed Entries "
			"from %" PRIu64 " to %" PRIu64, from, to);
	PageAbs curr = from;
	while(curr < to)
	{
		journalEntry::Max entry;
		readNextEntry(curr, entry);
		for(JournalTopic* worker : topics)
		{
			if(entry.base.topic == worker->getTopic())
			{
				if(curr >= firstUnsuccededEntry[to_underlying(worker->getTopic())])
				{
					worker->processEntry(entry.base);
				}
			}
		}
	}
}

void
Journal::applyUncheckpointedJournalEntries(PageAbs from, PageAbs to)
{
	PAFFS_DBG_S(PAFFS_TRACE_JOURNAL, "Applying UNcheckpointed Entries "
			"from %" PRIu64 " to %" PRIu64, from, to);
	PageAbs curr = from;
	while(curr <= to)
	{
		journalEntry::Max entry;
		readNextEntry(curr, entry);
		for(JournalTopic* worker : topics)
		{
			if(entry.base.topic == worker->getTopic())
			{
				worker->processUncheckpointedEntry(entry.base);
			}
		}
	}
}

void
Journal::writeEntry(PageAbs &pointer, const JournalEntry& entry){
	switch(entry.topic)
	{
	case JournalEntry::Topic::checkpoint:
		WRITEELEM(journalEntry::Checkpoint, entry, pointer);
		break;
	case JournalEntry::Topic::success:
		WRITEELEM(journalEntry::Success, entry, pointer);
		break;
	case JournalEntry::Topic::superblock:
		switch(static_cast<const journalEntry::Superblock*>(&entry)->type)
		{
		case journalEntry::Superblock::Type::rootnode:
			WRITEELEM(journalEntry::superblock::Rootnode, entry, pointer);
			break;
		case journalEntry::Superblock::Type::areaMap:
			switch(static_cast<const journalEntry::superblock::AreaMap*>(&entry)->operation)
			{
			case journalEntry::superblock::AreaMap::Operation::type:
				WRITEELEM(journalEntry::superblock::areaMap::Type, entry, pointer);
				break;
			case journalEntry::superblock::AreaMap::Operation::status:
				WRITEELEM(journalEntry::superblock::areaMap::Status, entry, pointer);
				break;
			case journalEntry::superblock::AreaMap::Operation::erasecount:
				WRITEELEM(journalEntry::superblock::areaMap::Erasecount, entry, pointer);
				break;
			case journalEntry::superblock::AreaMap::Operation::position:
				WRITEELEM(journalEntry::superblock::areaMap::Type, entry, pointer);
				break;
			case journalEntry::superblock::AreaMap::Operation::swap:
				WRITEELEM(journalEntry::superblock::areaMap::Swap, entry, pointer);
				break;
			}
			break;
		}
		break;
	case JournalEntry::Topic::tree:
		switch(static_cast<const journalEntry::BTree*>(&entry)->op)
		{
		case journalEntry::BTree::Operation::insert:
			WRITEELEM(journalEntry::btree::Insert, entry, pointer);
			break;
		case journalEntry::BTree::Operation::update:
			WRITEELEM(journalEntry::btree::Update, entry, pointer);
			break;
		case journalEntry::BTree::Operation::remove:
			WRITEELEM(journalEntry::btree::Remove, entry, pointer);
			break;
		}
		break;
	case JournalEntry::Topic::summaryCache:
		switch(static_cast<const journalEntry::SummaryCache*>(&entry)->subtype)
		{
		case journalEntry::SummaryCache::Subtype::commit:
			WRITEELEM(journalEntry::summaryCache::Commit, entry, pointer);
			break;
		case journalEntry::SummaryCache::Subtype::remove:
			WRITEELEM(journalEntry::summaryCache::Remove, entry, pointer);
			break;
		case journalEntry::SummaryCache::Subtype::setStatus:
			WRITEELEM(journalEntry::summaryCache::SetStatus, entry, pointer);
			break;
		}
		break;
	case JournalEntry::Topic::inode:
		switch(static_cast<const journalEntry::Inode*>(&entry)->operation)
		{
		case journalEntry::Inode::Operation::add:
			WRITEELEM(journalEntry::inode::Add, entry, pointer);
			break;
		case journalEntry::Inode::Operation::write:
			WRITEELEM(journalEntry::inode::Write, entry, pointer);
			break;
		case journalEntry::Inode::Operation::remove:
			WRITEELEM(journalEntry::inode::Remove, entry, pointer);
			break;
		case journalEntry::Inode::Operation::commit:
			WRITEELEM(journalEntry::inode::Commit, entry, pointer);
			break;
		}
	}
}

void
Journal::readNextEntry(PageAbs& pointer, journalEntry::Max& entry){
	driver.readMRAM(pointer, &entry, sizeof(journalEntry::Max));
	if(traceMask & (PAFFS_TRACE_JOURNAL | PAFFS_TRACE_VERBOSE))
	{
		printf("Read entry at %" PRIu64 ":\n\t", pointer);
		printMeaning(entry.base);
	}
	pointer += getSizeFromMax(entry);
}

PageAbs
Journal::getSizeFromMax(const journalEntry::Max &entry)
{
	PageAbs size = 0;
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

void
Journal::printMeaning(const JournalEntry& entry)
{
	bool found = false;
	switch(entry.topic)
	{
	case JournalEntry::Topic::checkpoint:
		printf("\tCheckpoint");
		found = true;
		break;
	case JournalEntry::Topic::success:
		printf("\tCommit success at %s", JournalEntry::topicNames[
					to_underlying(static_cast<const journalEntry::Success*>(&entry)->target)]);
		found = true;
		break;
	case JournalEntry::Topic::superblock:
		switch(static_cast<const journalEntry::Superblock*>(&entry)->type)
		{
		case journalEntry::Superblock::Type::rootnode:
			printf("rootnode change to %" PRIu32 ":%" PRIu32,
					extractLogicalArea(static_cast<const journalEntry::superblock::Rootnode*>(&entry)->rootnode),
					extractPageOffs(static_cast<const journalEntry::superblock::Rootnode*>(&entry)->rootnode));
			found = true;
			break;
		case journalEntry::Superblock::Type::areaMap:
			printf("AreaMap %" PRIu32 " ", static_cast<const journalEntry::superblock::AreaMap*>(&entry)->offs);
			switch(static_cast<const journalEntry::superblock::AreaMap*>(&entry)->operation)
			{
			case journalEntry::superblock::AreaMap::Operation::type:
				printf("set Type to %s",
						areaNames[static_cast<const journalEntry::superblock::areaMap::Type*>(&entry)->type]);
				found = true;
				break;
			case journalEntry::superblock::AreaMap::Operation::status:
				printf("set Status to %s",
						areaStatusNames[static_cast<const journalEntry::superblock::areaMap::Status*>(&entry)->status]);
				found = true;
				break;
			case journalEntry::superblock::AreaMap::Operation::erasecount:
				printf("set Erasecount");
				found = true;
				break;
			case journalEntry::superblock::AreaMap::Operation::position:
				printf("set Position to %X:%X",
						extractLogicalArea(static_cast<const journalEntry::superblock::areaMap::Position*>(&entry)->position),
						extractPageOffs(static_cast<const journalEntry::superblock::areaMap::Position*>(&entry)->position));
				found = true;
				break;
			case journalEntry::superblock::AreaMap::Operation::swap:
				printf("Swap");
				found = true;
				break;
			}
			break;
		}
		break;
	case JournalEntry::Topic::tree:
		printf("Treenode ");
		switch(static_cast<const journalEntry::BTree*>(&entry)->op)
		{
		case journalEntry::BTree::Operation::insert:
			printf("insert %" PRIu32, static_cast<const journalEntry::btree::Insert*>(&entry)->inode.no);
			found = true;
			break;
		case journalEntry::BTree::Operation::update:
			printf("update %" PRIu32, static_cast<const journalEntry::btree::Update*>(&entry)->inode.no);
			found = true;
			break;
		case journalEntry::BTree::Operation::remove:
			printf("remove %" PRIu32, static_cast<const journalEntry::btree::Remove*>(&entry)->no);
			found = true;
			break;
		}
		break;
	case JournalEntry::Topic::summaryCache:
		printf("SummaryCache Area %" PRIu32 " ", static_cast<const journalEntry::SummaryCache*>(&entry)->area);
		switch(static_cast<const journalEntry::SummaryCache*>(&entry)->subtype)
		{
		case journalEntry::SummaryCache::Subtype::commit:
			printf("Commit");
			found = true;
			break;
		case journalEntry::SummaryCache::Subtype::remove:
			printf("Remove");
			found = true;
			break;
		case journalEntry::SummaryCache::Subtype::setStatus:
			printf("set Page %" PRIu32 " to %s",
					static_cast<const journalEntry::summaryCache::SetStatus*>(&entry)->page,
					summaryEntryNames[static_cast<unsigned>(
							static_cast<const journalEntry::summaryCache::SetStatus*>(&entry)->status)]
					);
			found = true;
			break;
		}
		break;
	case JournalEntry::Topic::inode:
		switch(static_cast<const journalEntry::Inode*>(&entry)->operation)
		{
		case journalEntry::Inode::Operation::add:
			printf("Inode add");
			found = true;
			break;
		case journalEntry::Inode::Operation::write:
			printf("Inode write");
			found = true;
			break;
		case journalEntry::Inode::Operation::remove:
			printf("Inode remove");
			found = true;
			break;
		case journalEntry::Inode::Operation::commit:
			printf("Inode commit");
			found = true;
			break;
		}
		break;
	}
	if(!found)
		printf("Unrecognized");
	printf(" event.\n");
}
