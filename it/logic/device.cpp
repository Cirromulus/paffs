/*
 * Copyright (c) 2017, German Aerospace Center (DLR)
 *
 * This file is part of the development version of OUTPOST.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Authors:
 * - 2017, Pascal Pieper (DLR RY-AVS)
 * - 2017, Fabian Greif (DLR RY-AVS)
 */
// ----------------------------------------------------------------------------

#include <iostream>

#include "commonTest.hpp"

#include <stdlib.h>     /* srand, rand */
#include <time.h>       /* time */

class FileTest : public InitFs{};

TEST_F(FileTest, seekReadWrite){
	paffs::Obj *fil;
	paffs::Result r;
	char txt[] = "Hallo";
	char buf[6];
	unsigned int bw;

	fil = fs.open("/file", paffs::FW | paffs::FC);
	if(fs.getLastErr() != paffs::Result::ok)
		printf("%s!\n", paffs::err_msg(fs.getLastErr()));
	ASSERT_NE(fil, nullptr);

	r = fs.seek(*fil, paffs::dataBytesPerPage + 20, paffs::Seekmode::set);
	ASSERT_EQ(r, paffs::Result::ok);

	r = fs.write(*fil, txt, strlen(txt), &bw);
	EXPECT_EQ(bw, strlen(txt));
	ASSERT_EQ(r, paffs::Result::ok);

	r = fs.seek(*fil, -strlen(txt), paffs::Seekmode::cur);
	ASSERT_EQ(r, paffs::Result::ok);

	r = fs.read(*fil, buf, strlen(txt), &bw);
	EXPECT_EQ(bw, strlen(txt));
	ASSERT_EQ(r, paffs::Result::ok);
	ASSERT_TRUE(ArraysMatch(txt, buf, strlen(txt)));

	r = fs.seek(*fil, 20, paffs::Seekmode::set);
	ASSERT_EQ(r, paffs::Result::ok);

	r = fs.read(*fil, buf, 1, &bw);
	EXPECT_EQ(bw, static_cast<unsigned int>(1));
	ASSERT_EQ(r, paffs::Result::ok);
	ASSERT_EQ(buf[0], 0);

	r = fs.seek(*fil, -strlen(txt), paffs::Seekmode::end);
	ASSERT_EQ(r, paffs::Result::ok);

	r = fs.read(*fil, buf, strlen(txt), &bw);
	EXPECT_EQ(bw, strlen(txt));
	ASSERT_EQ(r, paffs::Result::ok);
	ASSERT_TRUE(ArraysMatch(txt, buf, strlen(txt)));

	r = fs.close(*fil);
	ASSERT_EQ(r, paffs::Result::ok);
}

TEST_F(FileTest, createReadWriteDeleteFile){
	//operate on indirection layer
	unsigned const int filesize = (paffs::addrsPerPage * 2)*paffs::dataBytesPerPage + 50;
	char t[] = ".                         Text";	//30 chars
	char tl[filesize];
	char buf[filesize];
	char quer[] = "..--";
	paffs::Result r;
	paffs::ObjInfo info;
	int i;
	for(i = 0; (i + 1) * strlen(t) < filesize; i++){
		memcpy(&tl[i * strlen(t)], t, strlen(t));
	}
	//fill Rest
	memset(&tl[i * strlen(t)], 0xAA, filesize - i * strlen(t));

	paffs::Obj *fil = fs.open("/file", paffs::FW | paffs::FC);
	ASSERT_NE(fil, nullptr);

	//write
	unsigned int bytes = 0;
	r = fs.write(*fil, tl, filesize, &bytes);
	EXPECT_EQ(bytes, filesize);
	ASSERT_EQ(r, paffs::Result::ok);

	r = fs.getObjInfo("/file", info);
	ASSERT_EQ(r, paffs::Result::ok);
	ASSERT_EQ(info.isDir, false);
	ASSERT_EQ(info.size, filesize);

	//read
	r = fs.seek(*fil, 0, paffs::Seekmode::set);
	ASSERT_EQ(r, paffs::Result::ok);
	r = fs.read(*fil, buf, filesize, &bytes);
	ASSERT_EQ(r, paffs::Result::ok);
	ASSERT_EQ(bytes, filesize);
	EXPECT_TRUE(ArraysMatch(buf, tl));

	//misaligned write
	memcpy(&tl[paffs::dataBytesPerPage - strlen(quer) / 2], quer, strlen(quer));
	r = fs.seek(*fil, paffs::dataBytesPerPage - strlen(quer) / 2, paffs::Seekmode::set);
	ASSERT_EQ(r, paffs::Result::ok);
	r = fs.write(*fil, quer, strlen(quer), &bytes);
	EXPECT_EQ(bytes, strlen(quer));
	ASSERT_EQ(r, paffs::Result::ok);

	//read
	r = fs.seek(*fil, 0, paffs::Seekmode::set);
	ASSERT_EQ(r, paffs::Result::ok);
	r = fs.read(*fil, buf, filesize, &bytes);
	ASSERT_EQ(r, paffs::Result::ok);
	ASSERT_EQ(bytes, filesize);
	EXPECT_TRUE(ArraysMatch(buf, tl));

	r = fs.close(*fil);
	ASSERT_EQ(r, paffs::Result::ok);

	r = fs.remove("/file");
	ASSERT_EQ(r, paffs::Result::ok);

}

TEST_F(FileTest, directoryReadWrite){
	paffs::Permission p = paffs::R | paffs::W;
	paffs::Result r;
	r = fs.mkDir("/a", p);
	ASSERT_EQ(r, paffs::Result::ok);
	r = fs.mkDir("/a/b", p);
	ASSERT_EQ(r, paffs::Result::ok);
	r = fs.touch("/a/b/file1");
	ASSERT_EQ(r, paffs::Result::ok);
	r = fs.mkDir("/b", p);
	ASSERT_EQ(r, paffs::Result::ok);
	r = fs.mkDir("/b/c", p);
	ASSERT_EQ(r, paffs::Result::ok);
	r = fs.touch("/b/file2");
	ASSERT_EQ(r, paffs::Result::ok);

	paffs::Dir* dir;
	paffs::Dirent *entr;

	// root
	dir = fs.openDir("/");
	ASSERT_NE(dir, nullptr);
	entr = fs.readDir(*dir);
	ASSERT_NE(entr, nullptr);
	ASSERT_EQ(entr->node->type, paffs::InodeType::dir);
	EXPECT_TRUE(StringsMatch(entr->name, "a/"));
	entr = fs.readDir(*dir);
	ASSERT_NE(entr, nullptr);
	ASSERT_EQ(entr->node->type, paffs::InodeType::dir);
	EXPECT_TRUE(StringsMatch(entr->name, "b/"));
	entr = fs.readDir(*dir);
	ASSERT_EQ(entr, nullptr);
	r = fs.closeDir(dir);
	ASSERT_EQ(r, paffs::Result::ok);

	//a
	dir = fs.openDir("/a");
	ASSERT_NE(dir, nullptr);
	entr = fs.readDir(*dir);
	ASSERT_NE(entr, nullptr);
	ASSERT_EQ(entr->node->type, paffs::InodeType::dir);
	EXPECT_TRUE(StringsMatch(entr->name, "b/"));
	r = fs.closeDir(dir);
	ASSERT_EQ(r, paffs::Result::ok);

	// a/b
	dir = fs.openDir("/a/b");
	ASSERT_NE(dir, nullptr);
	entr = fs.readDir(*dir);
	ASSERT_NE(entr, nullptr);
	ASSERT_EQ(entr->node->type, paffs::InodeType::file);
	EXPECT_TRUE(StringsMatch(entr->name, "file1"));
	r = fs.closeDir(dir);
	ASSERT_EQ(r, paffs::Result::ok);

	//b
	dir = fs.openDir("/b");
	ASSERT_NE(dir, nullptr);
	entr = fs.readDir(*dir);
	ASSERT_NE(entr, nullptr);
	ASSERT_EQ(entr->node->type, paffs::InodeType::dir);
	EXPECT_TRUE(StringsMatch(entr->name, "c/"));
	entr = fs.readDir(*dir);
	ASSERT_NE(entr, nullptr);
	ASSERT_EQ(entr->node->type, paffs::InodeType::file);
	EXPECT_TRUE(StringsMatch(entr->name, "file2"));
	r = fs.closeDir(dir);
	ASSERT_EQ(r, paffs::Result::ok);
}


TEST_F(FileTest, permissions){
	paffs::Obj *fil;
	paffs::Result r;
	char txt[] = "Hallo";
	unsigned int bw;

	fil = fs.open("/file", paffs::FR);
	EXPECT_EQ(fil, nullptr);
	EXPECT_EQ(fs.getLastErr(), paffs::Result::nf);
	fs.resetLastErr();

	fil = fs.open("/file", paffs::FR | paffs::FC);
	ASSERT_NE(fil, nullptr);

	r = fs.write(*fil, txt, strlen(txt), &bw);
	EXPECT_EQ(bw, strlen(txt));
	ASSERT_EQ(r, paffs::Result::ok);

	r = fs.close(*fil);
	ASSERT_EQ(r, paffs::Result::ok);

	r = fs.chmod("/file", paffs::R);
	ASSERT_EQ(r, paffs::Result::ok);

	fil = fs.open("/file", paffs::FW);
	EXPECT_EQ(fs.getLastErr(), paffs::Result::noperm);
	ASSERT_EQ(fil, nullptr);
	fs.resetLastErr();

	fil = fs.open("/file", paffs::FR);
	ASSERT_NE(fil, nullptr);

	r = fs.write(*fil, txt, strlen(txt), &bw);
	EXPECT_EQ(bw, static_cast<unsigned int>(0));
	ASSERT_EQ(r, paffs::Result::noperm);

	r = fs.close(*fil);
	ASSERT_EQ(r, paffs::Result::ok);
}

TEST_F(FileTest, maxFilesize){
	paffs::Obj *fil;
	paffs::Result r;
	char txt[] = "Hallo";
	unsigned int bw;
	unsigned int wordsize = sizeof(txt);
	unsigned int blocksize = paffs::dataBytesPerPage*1.5;
	char block[blocksize];
	char blockcopy[blocksize];
	unsigned int i = 0, maxFileSize = 0;
	for(;i + wordsize < blocksize; i += wordsize){
		memcpy(&block[i], txt, wordsize);
	}
	if(blocksize - i > 0)
		memcpy(&block[i], txt,  blocksize - i);

	fil = fs.open("/file", paffs::FW | paffs::FC);
	if(fs.getLastErr() != paffs::Result::ok)
		printf("%s!\n", paffs::err_msg(fs.getLastErr()));
	ASSERT_NE(fil, nullptr);
	//fs.setTraceMask(fs.getTraceMask() | PAFFS_TRACE_AREA | PAFFS_TRACE_GC);//PAFFS_TRACE_PACACHE | PAFFS_TRACE_AREA);
	i = 0;
	while(true){
		r = fs.write(*fil, block, blocksize, &bw);
		i += bw;
		if(r == paffs::Result::nospace)
			break;
		EXPECT_EQ(bw, blocksize);
		ASSERT_EQ(r, paffs::Result::ok);
	}
	maxFileSize = i;
	r = fs.close(*fil);
	ASSERT_EQ(r, paffs::Result::ok);
	r = fs.unmount();
	ASSERT_EQ(r, paffs::Result::ok);
	r = fs.mount();
	ASSERT_EQ(r, paffs::Result::ok);

	paffs::ObjInfo info;
	fs.resetLastErr();
	fs.getObjInfo("/file", info);
	ASSERT_EQ(fs.getLastErr(), paffs::Result::ok);
	ASSERT_EQ(maxFileSize, info.size);
	fil = fs.open("/file", paffs::FW);
	if(fs.getLastErr() != paffs::Result::ok)
		printf("%s!\n", paffs::err_msg(fs.getLastErr()));
	ASSERT_NE(fil, nullptr);

	i = 0;
	while(i < maxFileSize){
		memset(blockcopy, 0, blocksize);
		r = fs.read(*fil, blockcopy, blocksize, &bw);
		ASSERT_EQ(r, paffs::Result::ok);
		if(maxFileSize - i >= blocksize){
			ASSERT_EQ(bw, blocksize);
			ASSERT_TRUE(ArraysMatch(block, blockcopy, blocksize));
		}else{
			ASSERT_EQ(bw, maxFileSize - i);
			ASSERT_TRUE(ArraysMatch(block, blockcopy, maxFileSize - i));
		}
		i += bw;
	}

 	r = fs.close(*fil);
	ASSERT_EQ(r, paffs::Result::ok);

	r = fs.truncate("/file", maxFileSize / 2);
	ASSERT_EQ(r, paffs::Result::ok);
	fs.resetLastErr();
	fs.getObjInfo("/file", info);
	ASSERT_EQ(fs.getLastErr(), paffs::Result::ok);
	ASSERT_EQ(maxFileSize / 2, info.size);

	fil = fs.open("/file", paffs::FR);
	if(fs.getLastErr() != paffs::Result::ok)
		printf("%s!\n", paffs::err_msg(fs.getLastErr()));
	ASSERT_NE(fil, nullptr);
	r = fs.seek(*fil, maxFileSize / 2 - blocksize / 2);
	ASSERT_EQ(r, paffs::Result::ok);

	memset(blockcopy, 0, blocksize);
	std::cout << "Intentionally reading over Filesize" << std::endl;
	r = fs.read(*fil, blockcopy, blocksize, &bw);
	EXPECT_EQ(bw, blocksize / 2);
	ASSERT_EQ(r, paffs::Result::ok);
	unsigned blockStart = (maxFileSize / 2 - blocksize / 2) % blocksize;
	unsigned overRun = blocksize - blockStart;
	if(overRun < bw){
		EXPECT_TRUE(ArraysMatch(blockcopy, &block[blockStart], overRun));
		EXPECT_TRUE(ArraysMatch(&blockcopy[overRun], block, bw - overRun));
	}else{
		EXPECT_TRUE(ArraysMatch(blockcopy, &block[blockStart], bw));
	}

	r = fs.remove("/file");
	ASSERT_EQ(r, paffs::Result::einval);
	r = fs.close(*fil);
	ASSERT_EQ(r, paffs::Result::ok);
	r = fs.remove("/file");
	ASSERT_EQ(r, paffs::Result::ok);

	//Check if file is missing
	paffs::Dir* dir;

	// root
	dir = fs.openDir("/");
	ASSERT_NE(dir, nullptr);
	ASSERT_EQ(dir->no_entries, 0);

	r = fs.closeDir(dir);
	ASSERT_EQ(r, paffs::Result::ok);
}

TEST_F(FileTest, multiplePointersToSameFile){
	unsigned char elemsize = 4;
	char elemIn[elemsize+1], elemOut[elemsize+1];
	unsigned ringBufSize = 0, maxRingBufSize = 500;
	unsigned runningNumberIn = 0, runningNumberOut = 0;
	paffs::Obj *inFil, *outFil;
	unsigned int b;
	paffs::Result r;

	/**
	 * critical seeds:
	 * none yet encountered
	 */
	int seed = time(NULL);
	std::cout << "Random seed :" << seed << std::endl;
	srand(seed);
	//open file with different modes
	outFil = fs.open("/file", paffs::FW | paffs::FC);
	if(fs.getLastErr() != paffs::Result::ok)
		printf("%s!\n", paffs::err_msg(fs.getLastErr()));
	ASSERT_NE(outFil, nullptr);
	inFil  = fs.open("/file", paffs::FR);
	if(fs.getLastErr() != paffs::Result::ok)
		printf("%s!\n", paffs::err_msg(fs.getLastErr()));
	ASSERT_NE(inFil, nullptr);

	while(runningNumberOut < 5000){
		if(rand() % 2){
			//insert
			if(ringBufSize >= maxRingBufSize)
				continue;
			sprintf(elemIn, "%0*u", elemsize, runningNumberIn++);
			r = fs.write(*inFil, elemIn, elemsize, &b);
			EXPECT_EQ(b, elemsize);
			ASSERT_EQ(r, paffs::Result::ok);
			//printf("Wrote elem %4s\n", elemIn);
			if(inFil->fp >= maxRingBufSize * elemsize){
				r = fs.seek(*inFil, 0, paffs::Seekmode::set);
				ASSERT_EQ(r, paffs::Result::ok);
			}
			ringBufSize++;
		}else{
			//retrieve
			if(ringBufSize < 1)
				continue;
			r = fs.read(*outFil, elemOut, elemsize, &b);
			EXPECT_EQ(b, elemsize);
			ASSERT_EQ(r, paffs::Result::ok);
			//printf("Read elem %.*s\n",elemsize, elemOut);
			sprintf(elemIn, "%0*u", elemsize, runningNumberOut++);
			ASSERT_EQ(strncmp(elemIn, elemOut, elemsize), 0);
			if(outFil->fp >= maxRingBufSize * elemsize){
				r = fs.seek(*outFil, 0, paffs::Seekmode::set);
				ASSERT_EQ(r, paffs::Result::ok);
			}
			ringBufSize--;
		}
	}
	ASSERT_EQ(fs.getNumberOfOpenFiles(), 2);
	paffs::Obj* list[2];
	r = fs.getListOfOpenFiles(list);
	ASSERT_EQ(r, paffs::Result::ok);
	ASSERT_EQ(list[1], inFil);
	ASSERT_EQ(list[0], outFil);

	r = fs.close(*inFil);
	ASSERT_EQ(r, paffs::Result::ok);
	r = fs.close(*outFil);
	ASSERT_EQ(r, paffs::Result::ok);
}

TEST_F(FileTest, readOnlyChecks){
	paffs::Permission p = paffs::R | paffs::W;
	char word[] = "Hallo1";
	unsigned int wordlen = 6;
	char buf[wordlen];
	paffs::Result r;
	paffs::Obj* fil;
	paffs::Dir* dir;
	paffs::Dirent* entr;

	unsigned int bw = 0;
	r = fs.mkDir("/a", p);
	ASSERT_EQ(r, paffs::Result::ok);
	r = fs.mkDir("/a/b", p);
	ASSERT_EQ(r, paffs::Result::ok);
	r = fs.touch("/a/b/file1");
	ASSERT_EQ(r, paffs::Result::ok);
	r = fs.mkDir("/b", p);
	ASSERT_EQ(r, paffs::Result::ok);
	r = fs.mkDir("/b/c", p);
	ASSERT_EQ(r, paffs::Result::ok);
	r = fs.touch("/b/file2");
	ASSERT_EQ(r, paffs::Result::ok);

	fil = fs.open("/a/b/file1", paffs::FW);
	ASSERT_EQ(fs.getLastErr(),paffs::Result::ok);
	ASSERT_NE(fil, nullptr);
	r = fs.write(*fil, word, wordlen, &bw);
	ASSERT_EQ(r, paffs::Result::ok);
	ASSERT_EQ(bw, wordlen);
	r = fs.close(*fil);

	r = fs.unmount();
	ASSERT_EQ(r, paffs::Result::ok);
	r = fs.mount(true);
	ASSERT_EQ(r, paffs::Result::ok);


	//normal read
	fil = fs.open("/a/b/file1", paffs::FR);
	ASSERT_EQ(fs.getLastErr(),paffs::Result::ok);
	ASSERT_NE(fil, nullptr);
	r = fs.read(*fil, buf, wordlen, &bw);
	ASSERT_EQ(r, paffs::Result::ok);
	ASSERT_EQ(bw, wordlen);
	ASSERT_TRUE(ArraysMatch(buf, word, 6));
	r = fs.close(*fil);

	//Write try
	fil = fs.open("/a/b/file1", paffs::FW);
	ASSERT_EQ(fs.getLastErr(), paffs::Result::readonly);
	ASSERT_EQ(fil, nullptr);
	fs.resetLastErr();

	//existing File
	r = fs.touch("/a/b/file1");
	ASSERT_EQ(r, paffs::Result::readonly);

	//new file
	r = fs.touch("/a/b/fileNEW");
	ASSERT_EQ(r, paffs::Result::readonly);

	//existing Dirs
	//root
	dir = fs.openDir("/");
	ASSERT_NE(dir, nullptr);
	entr = fs.readDir(*dir);
	ASSERT_NE(entr, nullptr);
	ASSERT_EQ(entr->node->type, paffs::InodeType::dir);
	EXPECT_TRUE(StringsMatch(entr->name, "a/"));
	entr = fs.readDir(*dir);
	ASSERT_NE(entr, nullptr);
	ASSERT_EQ(entr->node->type, paffs::InodeType::dir);
	EXPECT_TRUE(StringsMatch(entr->name, "b/"));
	r = fs.closeDir(dir);
	ASSERT_EQ(r, paffs::Result::ok);
	//a
	dir = fs.openDir("/a");
	ASSERT_NE(dir, nullptr);
	entr = fs.readDir(*dir);
	ASSERT_NE(entr, nullptr);
	ASSERT_EQ(entr->node->type, paffs::InodeType::dir);
	EXPECT_TRUE(StringsMatch(entr->name, "b/"));
	r = fs.closeDir(dir);
	ASSERT_EQ(r, paffs::Result::ok);

	// a/b
	dir = fs.openDir("/a/b");
	ASSERT_NE(dir, nullptr);
	entr = fs.readDir(*dir);
	ASSERT_NE(entr, nullptr);
	ASSERT_EQ(entr->node->type, paffs::InodeType::file);
	EXPECT_TRUE(StringsMatch(entr->name, "file1"));
	r = fs.closeDir(dir);
	ASSERT_EQ(r, paffs::Result::ok);

	//b
	dir = fs.openDir("/b");
	ASSERT_NE(dir, nullptr);
	entr = fs.readDir(*dir);
	ASSERT_NE(entr, nullptr);
	ASSERT_EQ(entr->node->type, paffs::InodeType::dir);
	EXPECT_TRUE(StringsMatch(entr->name, "c/"));
	entr = fs.readDir(*dir);
	ASSERT_NE(entr, nullptr);
	ASSERT_EQ(entr->node->type, paffs::InodeType::file);
	EXPECT_TRUE(StringsMatch(entr->name, "file2"));
	r = fs.closeDir(dir);
	ASSERT_EQ(r, paffs::Result::ok);

	//new Dir
	r = fs.mkDir("/c/");
	ASSERT_EQ(r, paffs::Result::readonly);

	//delete dir
	r = fs.remove("/a/b");
	ASSERT_EQ(r, paffs::Result::readonly);
}
