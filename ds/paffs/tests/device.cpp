/*
 * device.cpp
 *
 *  Created on: 24 Feb 2017
 *      Author: rooot
 */
#include <iostream>
#include "googletest/gtest/gtest.h"
#include "googletest/gmock/gmock.h"

#include "../paffs.hpp"


class FileTest : public testing::Test{
public:
	//automatically loads default driver "1" with default flash
	paffs::Paffs fs;
	FileTest(){
		fs.setTraceMask(0);
		paffs::Result r = fs.format("1");
				if(r != paffs::Result::ok)
					std::cerr << "Could not format device!" << std::endl;
		r = fs.mount("1");
				if(r != paffs::Result::ok)
					std::cerr << "Could not mount device!" << std::endl;
	}

	virtual ~FileTest(){
		fs.unmount("1");
	}
};

//stack overflow, Fraser '12
template<typename T, size_t size>
::testing::AssertionResult ArraysMatch(const T (&expected)[size],
                                       const T (&actual)[size]){
    for (size_t i(0); i < size; ++i){
        if (expected[i] != actual[i]){
            return ::testing::AssertionFailure() << "array[" << i
                << "] (" << actual[i] << ") != expected[" << i
                << "] (" << expected[i] << ")";
        }
    }

    return ::testing::AssertionSuccess();
}

::testing::AssertionResult StringsMatch(const char *a, const char*b){
	if(strlen(a) != strlen(b))
		return ::testing::AssertionFailure() << "Size differs, " << strlen(a)
				<< " != " << strlen(b);

	for(size_t i(0); i < strlen(a); i++){
        if (a[i] != b[i]){
            return ::testing::AssertionFailure() << "array[" << i
                << "] (" << a[i] << ") != expected[" << i
                << "] (" << b[i] << ")";
        }
	}
	return ::testing::AssertionSuccess();
}

TEST_F(FileTest, createReadWriteFile){
	unsigned const int filesize = 3*paffs::dataBytesPerPage + 50;
	char t[] = ".                         Text";	//30 chars
	char tl[filesize];
	char buf[filesize];
	int i;
	for(i = 0; i * strlen(t) < filesize; i++){
		memcpy(&tl[i * strlen(t)], t, strlen(t));
	}
	//fill Rest
	memset(&tl[i * strlen(t)], 0xAA, filesize - (i-1) * strlen(t));

	paffs::Obj *fil = fs.open("/file", paffs::FW | paffs::FC);
	ASSERT_NE(fil, nullptr);

	//write
	unsigned int bytes = 0;
	paffs::Result r = fs.write(fil, tl, filesize, &bytes);
	EXPECT_EQ(bytes, filesize);
	ASSERT_EQ(r, paffs::Result::ok);

	paffs::ObjInfo info;
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
	char quer[] = "..--";
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
	EXPECT_EQ(bw, 0);
	ASSERT_EQ(r, paffs::Result::noperm);
}
