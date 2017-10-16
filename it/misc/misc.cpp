/*
 * unittests.cpp
 *
 *  Created on: Feb 22, 2017
 *      Author: user
 */
#include <paffs.hpp>

#include <iostream>

using namespace paffs;
using namespace std;

int main(int argc, char **argv) {
	(void) argc;
	(void) argv;
	std::vector<paffs::Driver*> drv;
	drv.push_back(paffs::getDriver(0));

	Paffs fs(drv);
	Device* dev = fs.getDevice(0);

	journalEntry::superblock::areaMap::Type test(0, AreaType::retired);
	journalEntry::superblock::Rootnode rootnode(1234);

	cout << "Size of biggest journalEntry: " << sizeof(journalEntry::Max) << " Byte" << endl;

	Inode fil, dir;
	dir.type = InodeType::dir;
	dir.no = 0;
	fil.type = InodeType::file;
	fil.no = 1;

	dev->journal.addEvent(journalEntry::btree::Insert(dir));
	Inode node;
	node.type = InodeType::dir;
	dev->journal.addEvent(journalEntry::inode::Add(node.no));

	dev->journal.addEvent(journalEntry::btree::Insert(fil));
	rootnode.rootnode = 123;
	dev->journal.addEvent(rootnode);
	dev->journal.checkpoint();
	rootnode.rootnode = 456;
	dev->journal.addEvent(rootnode);

	//whoops, power went out without write (and Status::success misses)
	dev->journal.processBuffer();

}


