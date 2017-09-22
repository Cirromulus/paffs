/*
 * unittests.cpp
 *
 *  Created on: Feb 22, 2017
 *      Author: user
 */
#include <journal.hpp>
#include <iostream>

using namespace paffs;
using namespace std;

int main(int argc, char **argv) {
	(void) argc;
	(void) argv;

	journalTopic::SuperBlock sbt;
	journalTopic::TreeCache tct;

	Journal journal(sbt, tct);

	journalEntry::superblock::areaMap::Type test(0, AreaType::retired);
	journalEntry::superblock::Rootnode rootnode(1234);
	journalEntry::superblock::Commit commit;

	cout << test << endl;
	cout << rootnode << endl;
	cout << commit << endl;

	journal.addEvent(rootnode);

	journal.addEvent(journalEntry::treeCache::Transaction(
			journalEntry::treeCache::Transaction::Operation::start, 0));
	journal.addEvent(journalEntry::treeCache::treeModify::Add(0, 0xBEEF, true));
	Inode node;
	node.type = InodeType::dir;
	journal.addEvent(journalEntry::treeCache::treeModify::InodeInsert(
			0, 0, node));
	journal.addEvent(journalEntry::treeCache::Transaction(
			journalEntry::treeCache::Transaction::Operation::end, 0));

	//whoops, power went out without write (and Operation::success misses)
	journal.processBuffer();

}


