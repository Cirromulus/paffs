/*
 * unittests.cpp
 *
 *  Created on: Feb 22, 2017
 *      Author: user
 */
#include <paffs.hpp>
#include <journal.hpp>
#include <iostream>

using namespace paffs;
using namespace std;

int main(int argc, char **argv) {
	(void) argc;
	(void) argv;

	journalEntry::superblock::areaMap::Type test(0, AreaType::retired);
	journalEntry::superblock::Rootnode rootnode(1234);
	journalEntry::superblock::Commit commit;
	journalEntry::treeCache::treeModify::Add add(0, 0, 0xBEEF, true);
	cout << test << endl;
	cout << rootnode << endl;
	cout << commit << endl;
	cout << add << endl;
}


