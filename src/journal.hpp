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
	JournalTopic* topics[1];

	static constexpr unsigned int logSize = 100;
	JournalEntry* log[logSize];
	unsigned int pos = 0;
public:
	Journal(JournalTopic& summaryCache){
		topics[0] = &summaryCache;
	}
	void
	addEvent(const JournalEntry& entry);
	void
	processBuffer();
private:
	JournalEntry*
	deserializeFactory(const JournalEntry& entry);
};
};
