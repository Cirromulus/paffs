/*
 * journalTopics.hpp
 *
 *  Created on: 22.09.2017
 *      Author: urinator
 */

#pragma once

#include "journalEntry.hpp"
#include "treeTypes.hpp"
#include <map>

namespace paffs
{

class JournalTopic{
protected:
	journalEntry::Transaction::Status taStatus =
			journalEntry::Transaction::Status::success;
public:
	virtual
	~JournalTopic(){};
	virtual
	JournalEntry::Topic getTopic() = 0;
	virtual void
	setTransactionStatus(journalEntry::Transaction::Status status);
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
	std::map<TreeNodeId, TreeCacheNode*> nodes;
	//Others currently not implemented
public:
	virtual JournalEntry::Topic
	getTopic() override;
	virtual void
	processEntry(JournalEntry& entry) override;
	virtual void
	finalize() override;
};

class PageAddressCache : public JournalTopic
{

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
