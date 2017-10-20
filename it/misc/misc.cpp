/*
 * unittests.cpp
 *
 *  Created on: Feb 22, 2017
 *      Author: user
 */
#include <paffs.hpp>
#include <simu/flashCell.h>
#include <simu/mram.hpp>
#include <iostream>
#include <fstream>

using namespace paffs;
using namespace std;


void smallTest();

void exportLog();

void import();

int main(int argc, char **argv) {
	(void) argc;
	(void) argv;

	if(argc > 1)
		exportLog();
	else
		import();
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

void import()
{
	std::vector<paffs::Driver*> drv;
	FlashCell* fc = new FlashCell();
	Mram* mram = new Mram(mramSize);
	drv.push_back(paffs::getDriverSpecial(0, fc, mram));

	//Deserialize
	ifstream in("flashCell_export", ios::in | ios::binary);
	if(!in.is_open())
	{
		cout << "Serialized flash could not be opened" << endl;
		return;
	}
	fc->getDebugInterface()->deserialize(in);
	in.close();

	Paffs fs(drv);
	Result r = fs.mount();
	if(r != Result::ok)
	{
		cout << "Could not mount filesystem!" << endl;
		return;
	}

	ObjInfo info;
	r = fs.getObjInfo("/a.txt", info);
	if(r != paffs::Result::ok)
	{
		cout << "could not get Info of file!" << endl;
	}
	Obj* fil = fs.open("/a.txt", paffs::FR);
	if(fil == nullptr)
	{
		cout << "File could not be opened" << endl;
		return;
	}
	char text[info.size+1];
	unsigned int br;
	r = fs.read(*fil, text, info.size, &br);
	if(r != paffs::Result::ok)
	{
		cout << "File could not be read!" << endl;
		return;
	}
	text[info.size] = 0;

	cout << "File contents:" << endl << text << endl;

	fs.close(*fil);
	fs.unmount();

}

void exportLog()
{
	std::vector<paffs::Driver*> drv;
	FlashCell* fc = new FlashCell();
	Mram* mram = new Mram(mramSize);
	drv.push_back(paffs::getDriverSpecial(0, fc, mram));

	Paffs fs(drv);

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

	//For debug
	fs.close(*fil);
	fs.unmount();

	//---- Whoops, power went out! ----//


	ofstream out("flashCell_export", ios::out | ios::binary);
	fc->getDebugInterface()->serialize(out);
	out.close();

	//TODO: Export
	delete fc;
	delete mram;
}
