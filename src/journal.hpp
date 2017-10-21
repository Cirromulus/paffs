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
	Driver& driver;

	PageAbs pos;
public:
	Journal(Driver& _driver, JournalTopic& superblock, JournalTopic& summaryCache,
	        JournalTopic& tree): driver(_driver){
		//This Order is important, as tree should only be replayed after areaMap is sane
		topics[0] = &superblock;
		topics[1] = &summaryCache;
		topics[2] = &tree;
		//TODO Inode
		pos = sizeof(PageAbs);
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
	void
	applyCheckpointedJournalEntries(PageAbs from, PageAbs to,
			PageAbs firstUnsuccededEntry[JournalEntry::numberOfTopics]);
	void
	applyUncheckpointedJournalEntries(PageAbs from, PageAbs to);
	void
	writeEntry(PageAbs& pointer, const JournalEntry& entry);
	void
	readNextEntry(PageAbs& pointer, journalEntry::Max& entry);
	PageAbs
	getSizeFromMax(const journalEntry::Max &entry);
};
};
