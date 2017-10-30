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

Result
Journal::addEvent(const JournalEntry& entry){
	if(disabled)
	{
		//Skipping, because we are currently replaying a buffer or formatting fs
		return Result::ok;
	}
	Result r = persistence.appendEntry(entry);
	if(r == Result::nospace){
		PAFFS_DBG(PAFFS_TRACE_JOURNAL, "Log full. should be flushed.");
		return r;
	}
	if(r != Result::ok)
	{
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not append Entry to persistence");
		return r;
	}
	if(traceMask & (PAFFS_TRACE_JOURNAL | PAFFS_TRACE_VERBOSE))
	{
		printMeaning(entry);
	}
	return Result::ok;
}

Result
Journal::checkpoint()
{
	return addEvent(journalEntry::Checkpoint());
}

Result
Journal::clear()
{
	return persistence.clear();
}

Result
Journal::processBuffer(){
	journalEntry::Max entry;
	EntryIdentifier firstUnsuccededEntry[JournalEntry::numberOfTopics];

	Result r = persistence.rewind();
	if(r != Result::ok)
	{
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not read rewind Journal!");
		return r;
	}
	for(EntryIdentifier& id : firstUnsuccededEntry)
	{
		id = persistence.tell();
	}
	r = persistence.readNextElem(entry);
	if(r == Result::nf)
	{
		PAFFS_DBG_S(PAFFS_TRACE_VERBOSE, "No Replay of Journal needed");
		return Result::ok;
	}
	if(r != Result::ok)
	{
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not read First element of Journal!");
		return r;
	}

	disabled = true;
	PAFFS_DBG_S(PAFFS_TRACE_JOURNAL, "Replay of Journal needed");
	PAFFS_DBG_S((PAFFS_TRACE_JOURNAL | PAFFS_TRACE_VERBOSE), "Scanning for success entries...");

	do
	{
		if(traceMask & (PAFFS_TRACE_JOURNAL | PAFFS_TRACE_VERBOSE))
		{
			printMeaning(entry.base);
		}
		if(entry.base.topic == JournalEntry::Topic::success)
		{
			firstUnsuccededEntry[static_cast<unsigned>(entry.success.target)] = persistence.tell();
		}
	}while((r = persistence.readNextElem(entry)) == Result::ok);
	if(r != Result::nf)
	{
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not read next element of Journal!");
		return r;
	}

	if(traceMask & (PAFFS_TRACE_JOURNAL | PAFFS_TRACE_VERBOSE))
	{
		printf("FirstUnsucceededEntry:\n");
		for(unsigned i = 0; i < JournalEntry::numberOfTopics; i++)
		{
			printf("\t%s: %" PRIu64 ".%" PRIu16 "\n", JournalEntry::topicNames[i],
					firstUnsuccededEntry[i].flash.addr, firstUnsuccededEntry[i].flash.offs);
		}
	}
	PAFFS_DBG_S((PAFFS_TRACE_JOURNAL | PAFFS_TRACE_VERBOSE), "Scanning for checkpoints...");

	r = persistence.rewind();
	if(r != Result::ok)
	{
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not rewind Journal!");
		return r;
	}

	EntryIdentifier firstUnprocessedEntry = persistence.tell();

	while((r = persistence.readNextElem(entry)) == Result::ok)
	{
		if(entry.base.topic == JournalEntry::Topic::checkpoint)
		{
			EntryIdentifier curr = persistence.tell();
			r = applyCheckpointedJournalEntries(firstUnprocessedEntry, curr, firstUnsuccededEntry);
			if(r != Result::ok)
			{
				PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not apply checkpointed Entries!");
				return r;
			}
			firstUnprocessedEntry = curr;
		}
	}
	if(r != Result::nf)
	{
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not read Journal!");
		return r;
	}

	if(firstUnprocessedEntry != persistence.tell())
	{
		PAFFS_DBG_S(PAFFS_TRACE_JOURNAL, "Uncheckpointed Entries exist");
		r = applyUncheckpointedJournalEntries(firstUnprocessedEntry);
		if(r != Result::ok)
		{
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not apply UNcheckpointed Entries!");
			return r;
		}
	}
	disabled = false;
	return Result::ok;
}

Result
Journal::applyCheckpointedJournalEntries(EntryIdentifier& from, EntryIdentifier& to,
		EntryIdentifier firstUnsuccededEntry[JournalEntry::numberOfTopics])
{
	PAFFS_DBG_S(PAFFS_TRACE_JOURNAL, "Applying Checkpointed Entries "
			"from %" PRIu64 ".%" PRIu16 " to %" PRIu64 ".%" PRIu16,
			from.flash.addr, from.flash.offs, to.flash.addr, to.flash.offs);
	EntryIdentifier restore = persistence.tell();
	Result r = persistence.seek(from);
	if(r != Result::ok)
	{
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not seek persistence!");
		return r;
	}
	while(true)
	{
		journalEntry::Max entry;
		r = persistence.readNextElem(entry);
		if(r != Result::ok)
		{
			PAFFS_DBG(PAFFS_TRACE_BUG, "Could not read journal to target end");
			return r;
		}
		if(persistence.tell() == to)
			break;

		for(JournalTopic* worker : topics)
		{
			if(entry.base.topic == worker->getTopic())
			{
				if(persistence.tell() >= firstUnsuccededEntry[to_underlying(worker->getTopic())])
				{
					if(traceMask & (PAFFS_TRACE_JOURNAL | PAFFS_TRACE_VERBOSE))
					{
						printf("Processing entry ");
						printMeaning(entry.base, false);
						printf(" by %s\n", JournalEntry::topicNames[to_underlying(worker->getTopic())]);
					}
					worker->processEntry(entry.base);
				}
			}
		}
	}
	r = persistence.seek(restore);
	if(r != Result::ok)
	{
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not restore pointer in persistence!");
		return r;
	}
	return Result::ok;
}

Result
Journal::applyUncheckpointedJournalEntries(EntryIdentifier& from)
{
	PAFFS_DBG_S(PAFFS_TRACE_JOURNAL, "Applying UNcheckpointed Entries "
			"from %" PRIu64 ".%" PRIu16, from.flash.addr, from.flash.offs);
	EntryIdentifier restore = persistence.tell();
	Result r = persistence.seek(from);
	if(r != Result::ok)
	{
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not seek persistence!");
		return r;
	}
	journalEntry::Max entry;
	while((r = persistence.readNextElem(entry)) == Result::ok)
	{
		for(JournalTopic* worker : topics)
		{
			if(entry.base.topic == worker->getTopic())
			{
				worker->processUncheckpointedEntry(entry.base);
			}
		}
	}
	if(r != Result::nf)
	{
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not read Entry!");
		return r;
	}
	r = persistence.seek(restore);
	if(r != Result::ok)
	{
		PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not restore pointer in persistence!");
		return r;
	}
	return Result::ok;
}

void
Journal::printMeaning(const JournalEntry& entry, bool withNewline)
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
	printf(" event");
	if(withNewline)
		printf(".\n");
}

void
Journal::disable()
{
	disabled = true;
}

Result
Journal::enable()
{
	disabled = false;
	return persistence.rewind();
}
