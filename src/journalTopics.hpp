/*
 * journalTopics.hpp
 *
 *  Created on: 22.09.2017
 *      Author: urinator
 */

#pragma once

#include "journalEntry.hpp"
#include "treeTypes.hpp"

namespace paffs
{

class JournalTopic{
public:
	virtual
	~JournalTopic(){};
	virtual
	JournalEntry::Topic getTopic() = 0;
	virtual void
	processEntry(JournalEntry& entry) = 0;
	virtual void
	finalize() = 0;
};

namespace journalTopic
{

class SuperBlock : public JournalTopic
{
	Addr rootnode;
	//Others currently not implemented
public:
	virtual JournalEntry::Topic
	getTopic() override;
	virtual void
	processEntry(JournalEntry& entry) override;
	virtual void
	finalize() override;
};

class TreeCache : public JournalTopic
{
	TreeCacheNode tcn;
	//Others currently not implemented
public:
	virtual JournalEntry::Topic
	getTopic() override;
	virtual void
	processEntry(JournalEntry& entry) override;
	virtual void
	finalize() override;
};

}
}
