/*
 * journalTopics.cpp
 *
 *  Created on: 22.09.2017
 *      Author: urinator
 */

/*
 * Note: This file is for testing only. Later on, these implementations should reside
 * in the corresponding paffs modules, e.g. SuperBlock, TreeCache, ...
 */

#include "journalTopics.hpp"
#include "paffs_trace.hpp"

using namespace paffs;
using namespace std;

void
JournalTopic::enqueueEntry(const JournalEntry& entry)
{
	if(entry.topic != getTopic() ||
			(entry.topic == JournalEntry::Topic::transaction &&
			static_cast<const journalEntry::Transaction*>(&entry)->target != getTopic())){
		PAFFS_DBG(PAFFS_TRACE_BUG, "Tried enqueueing wrong JournalEntry (%u)", entry.topic);
		return;
	}
	if(buffer.insert(entry) != Result::ok)
	{
		PAFFS_DBG(PAFFS_TRACE_ERROR, "JournalEntry Buffer full!");
	}
}

void
JournalTopic::finalize()
{
	JournalEntry *entry;
	buffer.rewind();
	while((entry = buffer.pop()) != nullptr)
	{
		processEntry(*entry);
	}
	buffer.rewindToUnsucceeded();
	while((entry = buffer.pop()) != nullptr)
	{
		processUnsucceededEntry(*entry);
	}
}
