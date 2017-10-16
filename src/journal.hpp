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
		topics[0] = &superblock;
		topics[1] = &summaryCache;
		topics[2] = &tree;
	}

	void
	addEvent(const JournalEntry& entry);
	void
	checkpoint();
	void
	clear();
	void
	processBuffer();
private:
	JournalEntry*
	deserializeFactory(const JournalEntry& entry);
};
};
