/*
 * journal.hpp
 *
 *  Created on: Sep 19, 2017
 *      Author: user
 */

#pragma once
#include "commonTypes.hpp"
#include "journalTopics.hpp"
#include "journalEntry.hpp"

namespace paffs{

class Journal{
	JournalTopic* topics[3];
	//Driver& driver;

	JournalEntry* log[internalLogSize];
	JournalEntryBuffer<journalTopicLogSize> buffer;
	unsigned int pos = 0;
public:
	Journal(JournalTopic& superblock, JournalTopic& summaryCache,
	        JournalTopic& tree){
		//This Order is important, as tree should only be replayed after areaMap is sane
		topics[0] = &superblock;
		topics[1] = &summaryCache;
		topics[2] = &tree;
		//TODO Inode
	}

	void
	addEvent(const JournalEntry& entry);
	void
	checkpoint();
	void
	clear();
	void
	processBuffer();
	void
	printMeaning(const JournalEntry& entry);
private:
	JournalEntry*
	deserializeFactory(const JournalEntry& entry);
};
};
