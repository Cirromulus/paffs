/*
 * journal.cpp
 *
 *  Created on: Sep 19, 2017
 *      Author: user
 */

#include "journal.hpp"
#include <iostream>

using namespace paffs;
using namespace std;

#define NEWELEM(type, obj) \
	new type(*static_cast<const type*> (&obj))

void
Journal::addEvent(const JournalEntry& entry){
	if(pos + 1 == logSize){
		cout << "Log full. should be flushed." << endl;
		return;
	}
	JournalEntry* tba = deserializeFactory(entry);
	if(tba == nullptr)
		return;
	log[pos] = tba;
	pos ++;
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
			cout << "Unknown JournalEntry Topic" << endl;
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
		cout << "Recognized trasaction event." << endl;
		ret = NEWELEM(journalEntry::Transaction, entry);
		break;
	case JournalEntry::Topic::superblock:
		switch(static_cast<const journalEntry::Superblock*>(&entry)->subtype)
		{
		case journalEntry::Superblock::Subtype::rootnode:
			cout << "Recognized rootnode change event." << endl;
			ret = NEWELEM(journalEntry::superblock::Rootnode, entry);
			break;
		default:
			cout << "Did not Recognize Superblock Event" << endl;
			break;
		}
		break;
	case JournalEntry::Topic::treeCache:
		switch(static_cast<const journalEntry::BTree*>(&entry)->op)
		{
		case journalEntry::BTree::Operation::add:
			cout << "Recognized Treenode Modify ADD event." << endl;
			ret = NEWELEM(journalEntry::btree::Add, entry);
			break;
		case journalEntry::BTree::Operation::keyInsert:
			cout << "Recognized Treenode Modify keyInsert event." << endl;
			ret = NEWELEM(journalEntry::btree::KeyInsert, entry);
			break;
		case journalEntry::BTree::Operation::inodeInsert:
			cout << "Recognized Treenode Modify InodeInsert event." << endl;
			ret = NEWELEM(journalEntry::btree::InodeInsert, entry);
			break;
		case journalEntry::BTree::Operation::remove:
			cout << "Recognized Treenode Modify remove event." << endl;
			ret = NEWELEM(journalEntry::btree::InodeInsert, entry);
			break;
		}
		break;
	case JournalEntry::Topic::summaryCache:
		switch(static_cast<const journalEntry::SummaryCache*>(&entry)->subtype)
		{
		case journalEntry::SummaryCache::Subtype::commit:
			cout << "Recognized SummaryCache Commit area event." << endl;
			ret = NEWELEM(journalEntry::summaryCache::Commit, entry);
			break;
		case journalEntry::SummaryCache::Subtype::setStatus:
			cout << "Recognized SummaryCache set Page to Status event." << endl;
			ret = NEWELEM(journalEntry::summaryCache::SetStatus, entry);
			break;
		}
		break;
	default:
		cout << "Did not recognize Event" << endl;
		break;
	}
	return ret;
}
