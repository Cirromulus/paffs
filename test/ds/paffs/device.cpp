/*
 * device.cpp
 *
 *  Created on: 24 Feb 2017
 *      Author: rooot
 */
#include <iostream>

#include "commonTest.hpp"

class FileTest : public InitFs{

};

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

	r = fs.seek(fil, paffs::dataBytesPerPage + 20, paffs::Seekmode::set);
	ASSERT_EQ(r, paffs::Result::ok);

	r = fs.write(fil, txt, strlen(txt), &bw);
	EXPECT_EQ(bw, strlen(txt));
	ASSERT_EQ(r, paffs::Result::ok);

	r = fs.seek(fil, -strlen(txt), paffs::Seekmode::cur);
	ASSERT_EQ(r, paffs::Result::ok);

	r = fs.read(fil, buf, strlen(txt), &bw);
	EXPECT_EQ(bw, strlen(txt));
	ASSERT_EQ(r, paffs::Result::ok);
	ASSERT_TRUE(ArraysMatch(txt, buf, strlen(txt)));

	r = fs.seek(fil, 20, paffs::Seekmode::set);
	ASSERT_EQ(r, paffs::Result::ok);

	r = fs.read(fil, buf, 1, &bw);
	EXPECT_EQ(bw, static_cast<unsigned int>(1));
	ASSERT_EQ(r, paffs::Result::ok);
	ASSERT_EQ(buf[0], 0);

	r = fs.seek(fil, -strlen(txt), paffs::Seekmode::end);
	ASSERT_EQ(r, paffs::Result::ok);

	r = fs.read(fil, buf, strlen(txt), &bw);
	EXPECT_EQ(bw, strlen(txt));
	ASSERT_EQ(r, paffs::Result::ok);
	ASSERT_TRUE(ArraysMatch(txt, buf, strlen(txt)));

	r = fs.close(fil);
	ASSERT_EQ(r, paffs::Result::ok);
}

TEST_F(FileTest, createReadWriteDeleteFile){
	//operate on indirection layer
	unsigned const int filesize = (paffs::maxAddrs - 1)*paffs::dataBytesPerPage + 50;
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
	r = fs.write(fil, tl, filesize, &bytes);
	EXPECT_EQ(bytes, filesize);
	ASSERT_EQ(r, paffs::Result::ok);


	r = fs.getObjInfo("/file", &info);
	ASSERT_EQ(r, paffs::Result::ok);
	ASSERT_EQ(info.isDir, false);
	ASSERT_EQ(info.size, filesize);

	//read
	r = fs.seek(fil, 0, paffs::Seekmode::set);
	ASSERT_EQ(r, paffs::Result::ok);
	r = fs.read(fil, buf, filesize, &bytes);
	ASSERT_EQ(r, paffs::Result::ok);
	ASSERT_EQ(bytes, filesize);
	EXPECT_TRUE(ArraysMatch(buf, tl));

	//misaligned write
	memcpy(&tl[paffs::dataBytesPerPage - strlen(quer) / 2], quer, strlen(quer));
	r = fs.seek(fil, paffs::dataBytesPerPage - strlen(quer) / 2, paffs::Seekmode::set);
	ASSERT_EQ(r, paffs::Result::ok);
	r = fs.write(fil, quer, strlen(quer), &bytes);
	EXPECT_EQ(bytes, strlen(quer));
	ASSERT_EQ(r, paffs::Result::ok);

	//read
	r = fs.seek(fil, 0, paffs::Seekmode::set);
	ASSERT_EQ(r, paffs::Result::ok);
	r = fs.read(fil, buf, filesize, &bytes);
	ASSERT_EQ(r, paffs::Result::ok);
	ASSERT_EQ(bytes, filesize);
	EXPECT_TRUE(ArraysMatch(buf, tl));

	r = fs.close(fil);
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
	entr = fs.readDir(dir);
	ASSERT_NE(entr, nullptr);
	ASSERT_EQ(entr->node->type, paffs::InodeType::dir);
	EXPECT_TRUE(StringsMatch(entr->name, "a/"));
	entr = fs.readDir(dir);
	ASSERT_NE(entr, nullptr);
	ASSERT_EQ(entr->node->type, paffs::InodeType::dir);
	EXPECT_TRUE(StringsMatch(entr->name, "b/"));
	r = fs.closeDir(dir);
	ASSERT_EQ(r, paffs::Result::ok);

	//a
	dir = fs.openDir("/a");
	ASSERT_NE(dir, nullptr);
	entr = fs.readDir(dir);
	ASSERT_NE(entr, nullptr);
	ASSERT_EQ(entr->node->type, paffs::InodeType::dir);
	EXPECT_TRUE(StringsMatch(entr->name, "b/"));
	r = fs.closeDir(dir);
	ASSERT_EQ(r, paffs::Result::ok);

	// a/b
	dir = fs.openDir("/a/b");
	ASSERT_NE(dir, nullptr);
	entr = fs.readDir(dir);
	ASSERT_NE(entr, nullptr);
	ASSERT_EQ(entr->node->type, paffs::InodeType::file);
	EXPECT_TRUE(StringsMatch(entr->name, "file1"));
	r = fs.closeDir(dir);
	ASSERT_EQ(r, paffs::Result::ok);

	//b
	dir = fs.openDir("/b");
	ASSERT_NE(dir, nullptr);
	entr = fs.readDir(dir);
	ASSERT_NE(entr, nullptr);
	ASSERT_EQ(entr->node->type, paffs::InodeType::dir);
	EXPECT_TRUE(StringsMatch(entr->name, "c/"));
	entr = fs.readDir(dir);
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

	r = fs.write(fil, txt, strlen(txt), &bw);
	EXPECT_EQ(bw, strlen(txt));
	ASSERT_EQ(r, paffs::Result::ok);

	r = fs.close(fil);
	ASSERT_EQ(r, paffs::Result::ok);

	r = fs.chmod("/file", paffs::R);
	ASSERT_EQ(r, paffs::Result::ok);

	fil = fs.open("/file", paffs::FW);
	EXPECT_EQ(fs.getLastErr(), paffs::Result::noperm);
	ASSERT_EQ(fil, nullptr);
	fs.resetLastErr();

	fil = fs.open("/file", paffs::FR);
	ASSERT_NE(fil, nullptr);

	r = fs.write(fil, txt, strlen(txt), &bw);
	EXPECT_EQ(bw, static_cast<unsigned int>(0));
	ASSERT_EQ(r, paffs::Result::noperm);

	r = fs.close(fil);
	ASSERT_EQ(r, paffs::Result::ok);
}
