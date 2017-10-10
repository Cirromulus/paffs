/*
 * unittests.cpp
 *
 *  Created on: Feb 22, 2017
 *      Author: user
 */
#include <journal.hpp>
#include <summaryCache.hpp>

#include <iostream>

using namespace paffs;
using namespace std;

int main(int argc, char **argv) {
	(void) argc;
	(void) argv;

	SummaryCache sumCache(nullptr);
	Journal journal(sumCache);

	journalEntry::superblock::areaMap::Type test(0, AreaType::retired);
	journalEntry::superblock::Rootnode rootnode(1234);

	cout << "Size of biggest journalEntry: " << sizeof(journalEntry::Max) << " Byte" << endl;

	Inode fil, dir;
	dir.type = InodeType::dir;
	dir.no = 0;
	fil.type = InodeType::file;
	fil.no = 1;

	journal.addEvent(journalEntry::btree::Insert(dir));
	Inode node;
	node.type = InodeType::dir;
	journal.addEvent(journalEntry::inode::Add(node.no));

	journal.addEvent(journalEntry::btree::Insert(fil));
	rootnode.rootnode = 123;
	journal.addEvent(rootnode);
	journal.checkpoint();
	rootnode.rootnode = 456;
	journal.addEvent(rootnode);

	//whoops, power went out without write (and Status::success misses)
	journal.processBuffer();

}


