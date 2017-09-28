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

	cout << test << endl;
	cout << rootnode << endl;

	journal.addEvent(journalEntry::Transaction(JournalEntry::Topic::treeCache,
			journalEntry::Transaction::Status::start));

	journal.addEvent(journalEntry::btree::Add(0, 0xBEEF, 0xBEEF, true));
	Inode node;
	node.type = InodeType::dir;
	journal.addEvent(journalEntry::inode::Add(node.no));

	journal.addEvent(journalEntry::btree::InodeInsert(0, 0, node.no));
	journal.addEvent(journalEntry::Transaction(JournalEntry::Topic::treeCache,
			journalEntry::Transaction::Status::end));
	journal.addEvent(rootnode);

	//whoops, power went out without write (and Status::success misses)
	journal.processBuffer();

}


