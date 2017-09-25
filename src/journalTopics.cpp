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
#include "btree.hpp"
#include <iostream>

using namespace paffs;
using namespace paffs::journalTopic;
using namespace std;

void
JournalTopic::setTransactionStatus(journalEntry::Transaction::Status status){
	switch(status)
	{
	case journalEntry::Transaction::Status::start:
		if(taStatus != journalEntry::Transaction::Status::success)
		{
			cout << "Tried starting a new Transaction without stopping old one!" << endl;
			break;
		}
		taStatus = status;
		break;
	case journalEntry::Transaction::Status::end:
		if(taStatus != journalEntry::Transaction::Status::start)
		{
			cout << "Tried stopping a nonexisting Transaction !" << endl;
			break;
		}
		taStatus = status;
		break;
	case journalEntry::Transaction::Status::success:
		if(taStatus != journalEntry::Transaction::Status::end)
		{
			cout << "Tried finalizing a non-succeeded Transaction !" << endl;
			break;
		}
		taStatus = status;
		break;
	}
}


JournalEntry::Topic
journalTopic::SuperBlock::getTopic()
{
	return JournalEntry::Topic::superblock;
}

void
journalTopic::SuperBlock::processEntry(JournalEntry& entry)
{
	if(entry.topic != getTopic()){
		cout << "Given JournalEntry is not Topic Superblock!" << endl;
		return;
	}
	switch(static_cast<const journalEntry::Superblock*>(&entry)->subtype)
	{
	case journalEntry::Superblock::Subtype::rootnode:
		rootnode = static_cast<const journalEntry::superblock::Rootnode*>(&entry)->rootnode;
		break;
	default:
		cout << "Superblock different than rootnode not implemented" << endl;
	}
}

void
journalTopic::SuperBlock::finalize()
{
	//currently nothing to finalize for Superblock
}

//==================================================================

JournalEntry::Topic
journalTopic::TreeCache::getTopic()
{
	return JournalEntry::Topic::treeCache;
}

void
journalTopic::TreeCache::processEntry(JournalEntry& entry)
{
	if(entry.topic != getTopic()){
		cout << "Given JournalEntry is not Topic TreeCache!" << endl;
		return;
	}
	const journalEntry::BTree* tc =
		static_cast<const journalEntry::BTree*>(&entry);
	switch(tc->op)
	{
	case journalEntry::BTree::Operation::add:
	{
		if(nodes.find(tc->id) != nodes.end())
		{
			cout << "tried adding an existing Node to map! (" << tc->id << ")" << endl;
			break;
		}
		const journalEntry::btree::Add* add =
				static_cast<const journalEntry::btree::Add*>(&entry);
		if(add->parent != add->id)
		{
			if(nodes.find(add->parent) != nodes.end())
			{
				cout << "tried adding a Node with unknown parent to map! ("
						<< add->parent << ")" << endl;
				break;
			}
		}
		TreeCacheNode* node = new TreeCacheNode;
		memset(node, 0, sizeof(TreeCacheNode));
		node->raw.self = add->self;
		node->raw.is_leaf = add->isLeaf;
		node->parent = nodes[add->parent];
		nodes.insert(pair<TreeNodeId, TreeCacheNode*>(tc->id, node));
		break;
	}
	case journalEntry::BTree::Operation::keyInsert:
	{
		map<TreeNodeId,TreeCacheNode*>::iterator it = nodes.find(tc->id);
		if(it == nodes.end())
		{
			cout << "tried inserting Key into a non-existing Node! (" << tc->id << ")" << endl;
			break;
		}
		const journalEntry::btree::KeyInsert* ins =
				static_cast<const journalEntry::btree::KeyInsert*>(&entry);
		TreeCacheNode* node = nodes[tc->id];
		//TODO
		break;
	}
	default:
		cout << "Treenode Modify Operation not recognized!" << endl;
	}
}

void
journalTopic::TreeCache::finalize()
{
	cout << "Todo: implement finalize for TreeCache" << endl;
}




