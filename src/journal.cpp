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

#define NEWELEM(type, obj) \
	new type(*static_cast<const type*> (&obj))

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
}

void
Journal::checkpoint()
{
	for(JournalTopic* topic : topics)
	{
		if(topic != nullptr)
			addEvent(journalEntry::Transaction(topic->getTopic(), journalEntry::Transaction::Status::checkpoint));
	}
}

void
Journal::clear()
{
	/*
	TODO: overwrite log with zeroes
	for(unsigned i = 0; i < pos; i++){
		if(traceMask & PAFFS_TRACE_JOURNAL)
			printMeaning(*log[i]);
	}*/
	pos = 0;
}

void
Journal::processBuffer(){
	for(JournalTopic* topic : topics){
		if(topic == nullptr){
			PAFFS_DBG(PAFFS_TRACE_BUG, "Topic is null");
			break;
		}

		topic->setJournalBuffer(&buffer);

		for(unsigned e = 0; e < pos; e++){
/*			if(log[e] == nullptr){
				continue;
			}

			bool found = false;

			if(log[e]->topic == topic->getTopic()){
				topic->enqueueEntry(*log[e]);
				found = true;
			}

			if(log[e]->topic == JournalEntry::Topic::transaction)
			{
				const journalEntry::Transaction* ta =
						static_cast<const journalEntry::Transaction*>(log[e]);
				if(ta->target == topic->getTopic()){
					topic->enqueueEntry(*log[e]);
				}
				found = true;
			}

			if(found)
			{
				delete log[e];
				log[e] = nullptr;
			}*/
		}
		topic->finalize();
		buffer.clear();
	}
}

void Journal::writeEntry(PageAbs &pointer, const JournalEntry& entry){
	switch(entry.topic)
	{
	case JournalEntry::Topic::empty:
	{
		WRITEELEM(journalEntry::Max, entry, pointer);
		break;
	}
	case JournalEntry::Topic::transaction:
		WRITEELEM(journalEntry::Transaction, entry, pointer);
		break;
	case JournalEntry::Topic::superblock:
		switch(static_cast<const journalEntry::Superblock*>(&entry)->subtype)
		{
		case journalEntry::Superblock::Subtype::rootnode:
			WRITEELEM(journalEntry::superblock::Rootnode, entry, pointer);
			break;
		case journalEntry::Superblock::Subtype::areaMap:
			switch(static_cast<const journalEntry::superblock::AreaMap*>(&entry)->element)
			{
			case journalEntry::superblock::AreaMap::Element::type:
				WRITEELEM(journalEntry::superblock::areaMap::Type, entry, pointer);
				break;
			case journalEntry::superblock::AreaMap::Element::status:
				WRITEELEM(journalEntry::superblock::areaMap::Status, entry, pointer);
				break;
			case journalEntry::superblock::AreaMap::Element::erasecount:
				WRITEELEM(journalEntry::superblock::areaMap::Erasecount, entry, pointer);
				break;
			case journalEntry::superblock::AreaMap::Element::position:
				WRITEELEM(journalEntry::superblock::areaMap::Type, entry, pointer);
				break;
			case journalEntry::superblock::AreaMap::Element::swap:
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
		switch(static_cast<const journalEntry::Inode*>(&entry)->subtype)
		{
		case journalEntry::Inode::Subtype::add:
			WRITEELEM(journalEntry::inode::Add, entry, pointer);
			break;
		case journalEntry::Inode::Subtype::write:
			WRITEELEM(journalEntry::inode::Write, entry, pointer);
			break;
		case journalEntry::Inode::Subtype::remove:
			WRITEELEM(journalEntry::inode::Remove, entry, pointer);
			break;
		}
	}
}


void Journal::readNextEntry(PageAbs &pointer, journalEntry::Max* entry){
	driver.readMRAM(pointer, entry, sizeof(journalEntry::Max));

	switch(entry->base.topic)
	{
	case JournalEntry::Topic::empty:
		pointer += sizeof(journalEntry::Max);
		break;
	case JournalEntry::Topic::transaction:
		pointer += sizeof(journalEntry::Transaction);
		break;
	case JournalEntry::Topic::superblock:
		switch(entry->superblock.subtype)
		{
		case journalEntry::Superblock::Subtype::rootnode:
			pointer += sizeof(journalEntry::superblock::Rootnode);
			break;
		case journalEntry::Superblock::Subtype::areaMap:
			switch(entry->superblock_.areaMap.element)
			{
			case journalEntry::superblock::AreaMap::Element::type:
				pointer += sizeof(journalEntry::superblock::areaMap::Type);
				break;
			case journalEntry::superblock::AreaMap::Element::status:
				pointer += sizeof(journalEntry::superblock::areaMap::Status);
				break;
			case journalEntry::superblock::AreaMap::Element::erasecount:
				pointer += sizeof(journalEntry::superblock::areaMap::Erasecount);
				break;
			case journalEntry::superblock::AreaMap::Element::position:
				pointer += sizeof(journalEntry::superblock::areaMap::Type);
				break;
			case journalEntry::superblock::AreaMap::Element::swap:
				pointer += sizeof(journalEntry::superblock::areaMap::Swap);
				break;
			}
			break;
		}
		break;
	case JournalEntry::Topic::tree:
		switch(entry->btree.op)
		{
		case journalEntry::BTree::Operation::insert:
			pointer += sizeof(journalEntry::btree::Insert);
			break;
		case journalEntry::BTree::Operation::update:
			pointer += sizeof(journalEntry::btree::Update);
			break;
		case journalEntry::BTree::Operation::remove:
			pointer += sizeof(journalEntry::btree::Remove);
			break;
		break;
		}
		break;
	case JournalEntry::Topic::summaryCache:
		switch(entry->summaryCache.subtype)
		{
		case journalEntry::SummaryCache::Subtype::commit:
			pointer += sizeof(journalEntry::summaryCache::Commit);
			break;
		case journalEntry::SummaryCache::Subtype::remove:
			pointer += sizeof(journalEntry::summaryCache::Remove);
			break;
		case journalEntry::SummaryCache::Subtype::setStatus:
			pointer += sizeof(journalEntry::summaryCache::SetStatus);
			break;
		}
		break;
	case JournalEntry::Topic::inode:
		switch(entry->inode.subtype)
		{
		case journalEntry::Inode::Subtype::add:
			pointer += sizeof(journalEntry::inode::Add);
			break;
		case journalEntry::Inode::Subtype::write:
			pointer += sizeof(journalEntry::inode::Write);
			break;
		case journalEntry::Inode::Subtype::remove:
			pointer += sizeof(journalEntry::inode::Remove);
			break;
		}
	}
}

JournalEntry* Journal::deserializeFactory(const JournalEntry& entry){
	JournalEntry* ret = nullptr;
	switch(entry.topic)
	{
	case JournalEntry::Topic::empty:
		PAFFS_DBG(PAFFS_TRACE_BUG, "deserializing empty Entry?");
		ret = NEWELEM(JournalEntry, entry);
		break;
	case JournalEntry::Topic::transaction:
		PAFFS_DBG(PAFFS_TRACE_JOURNAL, "Recognized trasaction event.");
		ret = NEWELEM(journalEntry::Transaction, entry);
		break;
	case JournalEntry::Topic::superblock:
		switch(static_cast<const journalEntry::Superblock*>(&entry)->subtype)
		{
		case journalEntry::Superblock::Subtype::rootnode:
			PAFFS_DBG(PAFFS_TRACE_JOURNAL, "Recognized rootnode change event.");
			ret = NEWELEM(journalEntry::superblock::Rootnode, entry);
			break;
		case journalEntry::Superblock::Subtype::areaMap:
			switch(static_cast<const journalEntry::superblock::AreaMap*>(&entry)->element)
			{
			case journalEntry::superblock::AreaMap::Element::type:
				PAFFS_DBG(PAFFS_TRACE_JOURNAL, "Recognized AreaMap set Type event.");
				ret = NEWELEM(journalEntry::superblock::areaMap::Type, entry);
				break;
			case journalEntry::superblock::AreaMap::Element::status:
				PAFFS_DBG(PAFFS_TRACE_JOURNAL, "Recognized AreaMap set Status event.");
				ret = NEWELEM(journalEntry::superblock::areaMap::Status, entry);
				break;
			case journalEntry::superblock::AreaMap::Element::erasecount:
				PAFFS_DBG(PAFFS_TRACE_JOURNAL, "Recognized AreaMap set Erasecount event.");
				ret = NEWELEM(journalEntry::superblock::areaMap::Erasecount, entry);
				break;
			case journalEntry::superblock::AreaMap::Element::position:
				PAFFS_DBG(PAFFS_TRACE_JOURNAL, "Recognized AreaMap set Position event.");
				ret = NEWELEM(journalEntry::superblock::areaMap::Type, entry);
				break;
			case journalEntry::superblock::AreaMap::Element::swap:
				PAFFS_DBG(PAFFS_TRACE_JOURNAL, "Recognized AreaMap Swap event.");
				ret = NEWELEM(journalEntry::superblock::areaMap::Swap, entry);
				break;
			}
			break;
		}
		break;
	case JournalEntry::Topic::tree:
		switch(static_cast<const journalEntry::BTree*>(&entry)->op)
		{
		case journalEntry::BTree::Operation::insert:
			PAFFS_DBG(PAFFS_TRACE_JOURNAL, "Recognized Treenode insert event.");
			ret = NEWELEM(journalEntry::btree::Insert, entry);
			break;
		case journalEntry::BTree::Operation::update:
			PAFFS_DBG(PAFFS_TRACE_JOURNAL, "Recognized Treenode udpate event.");
			ret = NEWELEM(journalEntry::btree::Update, entry);
			break;
		case journalEntry::BTree::Operation::remove:
			PAFFS_DBG(PAFFS_TRACE_JOURNAL, "Recognized Treenode remove event.");
			ret = NEWELEM(journalEntry::btree::Remove, entry);
			break;
		break;
		}
		break;
	case JournalEntry::Topic::summaryCache:
		switch(static_cast<const journalEntry::SummaryCache*>(&entry)->subtype)
		{
		case journalEntry::SummaryCache::Subtype::commit:
			PAFFS_DBG(PAFFS_TRACE_JOURNAL, "Recognized SummaryCache Commit area event.");
			ret = NEWELEM(journalEntry::summaryCache::Commit, entry);
			break;
		case journalEntry::SummaryCache::Subtype::remove:
			PAFFS_DBG(PAFFS_TRACE_JOURNAL, "Recognized SummaryCache Remove area event.");
			ret = NEWELEM(journalEntry::summaryCache::Remove, entry);
			break;
		case journalEntry::SummaryCache::Subtype::setStatus:
			PAFFS_DBG(PAFFS_TRACE_JOURNAL, "Recognized SummaryCache set Page to Status event.");
			ret = NEWELEM(journalEntry::summaryCache::SetStatus, entry);
			break;
		}
		break;
	case JournalEntry::Topic::inode:
		switch(static_cast<const journalEntry::Inode*>(&entry)->subtype)
		{
		case journalEntry::Inode::Subtype::add:
			PAFFS_DBG(PAFFS_TRACE_JOURNAL, "Recognized Inode add event.");
			ret = NEWELEM(journalEntry::inode::Add, entry);
			break;
		case journalEntry::Inode::Subtype::write:
			PAFFS_DBG(PAFFS_TRACE_JOURNAL, "Recognized Inode write event.");
			ret = NEWELEM(journalEntry::inode::Write, entry);
			break;
		case journalEntry::Inode::Subtype::remove:
			PAFFS_DBG(PAFFS_TRACE_JOURNAL, "Recognized Inode remove event.");
			ret = NEWELEM(journalEntry::inode::Remove, entry);
			break;
		}
	}
	return ret;
}

void
Journal::printMeaning(const JournalEntry& entry)
{
	//printf("Recognized ");
	switch(entry.topic)
	{
	case JournalEntry::Topic::empty:
		printf("empty");
		break;
	case JournalEntry::Topic::transaction:
	{
		const journalEntry::Transaction* ta = static_cast<const journalEntry::Transaction*>(&entry);
		printf("\ttransaction %s %s", JournalEntry::topicNames[static_cast<unsigned>(ta->target)],
				journalEntry::Transaction::statusNames[static_cast<unsigned>(ta->status)]);
		break;
	}
	case JournalEntry::Topic::superblock:
		switch(static_cast<const journalEntry::Superblock*>(&entry)->subtype)
		{
		case journalEntry::Superblock::Subtype::rootnode:
			printf("rootnode change to %X:%X",
					extractLogicalArea(static_cast<const journalEntry::superblock::Rootnode*>(&entry)->rootnode),
					extractPageOffs(static_cast<const journalEntry::superblock::Rootnode*>(&entry)->rootnode));
			break;
		case journalEntry::Superblock::Subtype::areaMap:
			printf("AreaMap %" PRIu32 " ", static_cast<const journalEntry::superblock::AreaMap*>(&entry)->offs);
			switch(static_cast<const journalEntry::superblock::AreaMap*>(&entry)->element)
			{
			case journalEntry::superblock::AreaMap::Element::type:
				printf("set Type to %s",
						areaNames[static_cast<const journalEntry::superblock::areaMap::Type*>(&entry)->type]);
				break;
			case journalEntry::superblock::AreaMap::Element::status:
				printf("set Status to %s",
						areaStatusNames[static_cast<const journalEntry::superblock::areaMap::Status*>(&entry)->status]);
				break;
			case journalEntry::superblock::AreaMap::Element::erasecount:
				printf("set Erasecount");
				break;
			case journalEntry::superblock::AreaMap::Element::position:
				printf("set Position to %X:%X",
						extractLogicalArea(static_cast<const journalEntry::superblock::areaMap::Position*>(&entry)->position),
						extractPageOffs(static_cast<const journalEntry::superblock::areaMap::Position*>(&entry)->position));
				break;
			case journalEntry::superblock::AreaMap::Element::swap:
				printf("Swap");
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
			break;
		case journalEntry::BTree::Operation::update:
			printf("update %" PRIu32, static_cast<const journalEntry::btree::Update*>(&entry)->inode.no);
			break;
		case journalEntry::BTree::Operation::remove:
			printf("remove %" PRIu32, static_cast<const journalEntry::btree::Remove*>(&entry)->no);
			break;
		break;
		}
		break;
	case JournalEntry::Topic::summaryCache:
		printf("SummaryCache Area %" PRIu32 " ", static_cast<const journalEntry::SummaryCache*>(&entry)->area);
		switch(static_cast<const journalEntry::SummaryCache*>(&entry)->subtype)
		{
		case journalEntry::SummaryCache::Subtype::commit:
			printf("Commit");
			break;
		case journalEntry::SummaryCache::Subtype::remove:
			printf("Remove");
			break;
		case journalEntry::SummaryCache::Subtype::setStatus:
			printf("set Page %" PRIu32 " to %s",
					static_cast<const journalEntry::summaryCache::SetStatus*>(&entry)->page,
					summaryEntryNames[static_cast<unsigned>(
							static_cast<const journalEntry::summaryCache::SetStatus*>(&entry)->status)]
					);
			break;
		}
		break;
	case JournalEntry::Topic::inode:
		switch(static_cast<const journalEntry::Inode*>(&entry)->subtype)
		{
		case journalEntry::Inode::Subtype::add:
			printf("Inode add");
			break;
		case journalEntry::Inode::Subtype::write:
			printf("Inode write");
			break;
		case journalEntry::Inode::Subtype::remove:
			printf("Inode remove");
			break;
		}
	}
	printf(" event.\n");
}
