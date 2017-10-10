/*
 * journal.hpp
 *
 *  Created on: Sep 19, 2017
 *      Author: user
 */

#pragma once
#include "journalEntry.hpp"
#include "journalTopics.hpp"

namespace paffs{

class Journal{
	JournalTopic* topics[3];
	//Driver& driver;

	static constexpr unsigned int logSize = 100;
	JournalEntry* log[logSize];
	unsigned int pos = 0;
public:
	Journal(JournalTopic& superblock, JournalTopic& summaryCache,
	        JournalTopic& tree){
		topics[0] = &superblock;
		topics[1] = &summaryCache;
		topics[2] = &tree;
	}
	/*
	 * \warn for testing only
	 */
	Journal(JournalTopic& summaryCache)
	{
		topics[0] = &summaryCache;
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
