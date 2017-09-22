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
		JournalEntry& entry = *log[e];
		bool found = false;
		for(JournalTopic* topic : topics){
			if(entry.mTopic == topic->getTopic()){
				topic->processEntry(entry);
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
}


JournalEntry* Journal::deserializeFactory(const JournalEntry& entry){
	JournalEntry* ret = nullptr;
	switch(entry.mTopic)
	{
	case JournalEntry::Topic::superblock:
		switch(static_cast<const journalEntry::Superblock*>(&entry)->mSubtype)
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
		switch(static_cast<const journalEntry::TreeCache*>(&entry)->mSubtype)
		{
		case journalEntry::TreeCache::Subtype::transaction:
			cout << "Recognized Treenode Transaction event." << endl;
			ret = NEWELEM(journalEntry::treeCache::Transaction, entry);
			break;
		case journalEntry::TreeCache::Subtype::treeModify:
			switch(static_cast<const journalEntry::treeCache::TreeModify*>(&entry)->mOp)
			{
			case journalEntry::treeCache::TreeModify::Operation::add:
				cout << "Recognized Treenode Modify ADD event." << endl;
				ret = NEWELEM(journalEntry::treeCache::treeModify::Add, entry);
				break;
			case journalEntry::treeCache::TreeModify::Operation::keyInsert:
				cout << "Recognized Treenode Modify keyInsert event." << endl;
				ret = NEWELEM(journalEntry::treeCache::treeModify::KeyInsert, entry);
				break;
			case journalEntry::treeCache::TreeModify::Operation::inodeInsert:
				cout << "Recognized Treenode Modify InodeInsert event." << endl;
				ret = NEWELEM(journalEntry::treeCache::treeModify::InodeInsert, entry);
				break;
			case journalEntry::treeCache::TreeModify::Operation::keyDelete:
				cout << "Recognized Treenode Modify KeyDelete event." << endl;
				ret = NEWELEM(journalEntry::treeCache::treeModify::KeyDelete, entry);
				break;
			case journalEntry::treeCache::TreeModify::Operation::commit:
				cout << "Recognized Treenode Modify commit event." << endl;
				ret = NEWELEM(journalEntry::treeCache::treeModify::Commit, entry);
				break;
			case journalEntry::treeCache::TreeModify::Operation::remove:
				cout << "Recognized Treenode Modify remove event." << endl;
				ret = NEWELEM(journalEntry::treeCache::treeModify::InodeInsert, entry);
				break;
			}
			break;
		default:
			cout << "Did not recognize TreeCache Event" << endl;
		}
		break;
	default:
		cout << "Did not recognize Event" << endl;
		break;
	}
	return ret;
}
