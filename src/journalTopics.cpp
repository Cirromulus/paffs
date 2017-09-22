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
#include <iostream>

using namespace paffs;
using namespace paffs::journalTopic;
using namespace std;


JournalEntry::Topic
SuperBlock::getTopic()
{
	return JournalEntry::Topic::superblock;
}

void
SuperBlock::processEntry(JournalEntry& entry)
{
	if(entry.mTopic != getTopic()){
		cout << "Given JournalEntry is not Topic Superblock!" << endl;
		return;
	}
	cout << "Todo: implement processEntry for Superbock" << endl;
}

void
SuperBlock::finalize()
{
	cout << "Todo: implement finalize for Superblock" << endl;
}

//==================================================================

JournalEntry::Topic
TreeCache::getTopic()
{
	return JournalEntry::Topic::treeCache;
}

void
TreeCache::processEntry(JournalEntry& entry)
{
	if(entry.mTopic != getTopic()){
		cout << "Given JournalEntry is not Topic TreeCache!" << endl;
		return;
	}
	cout << "Todo: implement processEntry for TreeCache" << endl;
}

void
TreeCache::finalize()
{
	cout << "Todo: implement finalize for TreeCache" << endl;
}




