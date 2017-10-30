/*
 * journal.hpp
 *
 *  Created on: Sep 19, 2017
 *      Author: user
 */

#pragma once
#include "journalEntry.hpp"
#include "journalPersistence.hpp"
#include "journalTopic.hpp"

namespace paffs{

class Journal{
	JournalTopic* topics[3];

	JournalPersistence& persistence;
	bool disabled;
public:
	Journal(JournalPersistence& _persistence, JournalTopic& superblock, JournalTopic& summaryCache,
	        JournalTopic& tree): persistence(_persistence){

		topics[0] = &superblock;
		topics[1] = &summaryCache;
		topics[2] = &tree;
		//TODO Inode

		disabled = true;
	}

	Result
	addEvent(const JournalEntry& entry);
	Result
	checkpoint();
	Result
	clear();
	Result
	processBuffer();
	void
	printMeaning(const JournalEntry& entry, bool withNewLine = true);
	void
	disable();
	Result
	enable();
private:
	Result
	applyCheckpointedJournalEntries(EntryIdentifier& from, EntryIdentifier& to,
			EntryIdentifier firstUnsuccededEntry[JournalEntry::numberOfTopics]);
	Result
	applyUncheckpointedJournalEntries(EntryIdentifier& from);
	PageAbs
	getSizeFromMax(const journalEntry::Max &entry);
};
};
