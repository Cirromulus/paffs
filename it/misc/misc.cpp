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


void smallTest();

void exportLog();

int main(int argc, char **argv) {
	(void) argc;
	(void) argv;

	exportLog();
}

void smallTest()
{
	std::vector<paffs::Driver*> drv;
	drv.push_back(paffs::getDriver(0));

	Paffs fs(drv);
	Device* dev = fs.getDevice(0);



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
	dev->journal.addEvent(journalEntry::superblock::Rootnode(1234));
	dev->journal.checkpoint();
	dev->journal.addEvent(journalEntry::superblock::Rootnode(5678));

	//whoops, power went out without write (and Status::success misses)
	dev->journal.processBuffer();

	printf("Rootnode Addr: %lu\n", dev->superblock.getRootnodeAddr());
}


void exportLog()
{
	std::vector<paffs::Driver*> drv;
	drv.push_back(paffs::getDriver(0));

	Paffs fs(drv);
	Device* dev = fs.getDevice(0);

	BadBlockList bbl[maxNumberOfDevices];
	fs.format(bbl);
	fs.setTraceMask(fs.getTraceMask() |
	                PAFFS_TRACE_ERROR |
	                PAFFS_TRACE_BUG   |
	                PAFFS_TRACE_INFO  );

	fs.mount();

	Obj* fil = fs.open("/a.txt", paffs::FW | paffs::FC);
	if(fil == nullptr)
	{
		cout << "File could not be opened" << endl;
		return;
	}
	char text[] = "Das Pferd frisst keinen Gurkensalat";
	unsigned int bw = 0;
	Result r = fs.write(*fil, text, sizeof(text), &bw);
	if(r != Result::ok)
	{
		cout << "File could not be written" << endl;
		return;
	}

	//---- Whoops, power went out! ----//

	cout << "END" << endl;

	//TODO: Export
}
