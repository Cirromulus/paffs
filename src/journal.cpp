/*
 * journal.cpp
 *
 *  Created on: Sep 19, 2017
 *      Author: user
 */

#include "journal.hpp"
#include "paffs_trace.hpp"
#include "inttypes.h"
#include "area.hpp"

using namespace paffs;
using namespace std;

#define NEWELEM(type, obj) \
	new type(*static_cast<const type*> (&obj))

void
Journal::addEvent(const JournalEntry& entry){
	if(pos >= internalLogSize){
		PAFFS_DBG(PAFFS_TRACE_JOURNAL, "Log full. should be flushed.");
		clear(); //Just for debug. Actually, flush or (better) reorder
		return;
	}
	JournalEntry* tba = deserializeFactory(entry);
	if(tba == nullptr)
		return;
	log[pos] = tba;
	pos ++;
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
	for(unsigned i = 0; i < pos; i++){
		if(traceMask & PAFFS_TRACE_JOURNAL)
			printMeaning(*log[i]);
		delete log[i];
	}
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
			if(log[e] == nullptr){
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
			}
		}
		topic->finalize();
		buffer.clear();
	}
}


JournalEntry* Journal::deserializeFactory(const JournalEntry& entry){
	JournalEntry* ret = nullptr;
	switch(entry.topic)
	{
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
