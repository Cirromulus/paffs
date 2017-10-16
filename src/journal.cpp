/*
 * journal.cpp
 *
 *  Created on: Sep 19, 2017
 *      Author: user
 */

#include "journal.hpp"
#include "paffs_trace.hpp"

using namespace paffs;
using namespace std;

#define NEWELEM(type, obj) \
	new type(*static_cast<const type*> (&obj))

void
Journal::addEvent(const JournalEntry& entry){
	if(pos + 1 == logSize){
		PAFFS_DBG(PAFFS_TRACE_JOURNAL, "Log full. should be flushed.");
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
		addEvent(journalEntry::Transaction(topic->getTopic(), journalEntry::Transaction::Status::end));
	}
}

void
Journal::clear()
{
	for(unsigned i = 0; i < pos; i++){
		delete log[i];
	}
	pos = 0;
}

void
Journal::processBuffer(){
	for(unsigned e = 0; e < pos; e++){
		if(log[e]->topic == JournalEntry::Topic::transaction)
		{
			const journalEntry::Transaction* ta =
					static_cast<const journalEntry::Transaction*>(log[e]);
			for(JournalTopic* topic : topics)
			{
				if(ta->target == topic->getTopic()){
					topic->enqueueEntry(*log[e]);
				}
			}
			continue;
		}

		bool found = false;
		for(JournalTopic* topic : topics){
			if(log[e]->topic == topic->getTopic()){
				topic->enqueueEntry(*log[e]);
				found = true;
				break;
			}
		}
		if(!found)
		{
			PAFFS_DBG(PAFFS_TRACE_ERROR, "Unknown JournalEntry Topic");
		}else
		{
			delete log[e];
		}
	}

	for(JournalTopic* topic : topics){
		topic->finalize();
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
