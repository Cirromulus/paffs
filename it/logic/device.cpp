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


#include "commonTest.hpp"

#include <iostream>
#include <stdlib.h> /* srand, rand */
#include <time.h>   /* time */
#include <memory>
#include <string>

class FileTest : public InitFs
{
};

using namespace paffs;

TEST_F(FileTest, seekReadWrite)
{
    Obj* fil;
    Result r;
    char txt[] = "Hallo";
    char buf[6];
    unsigned int bw;

    fil = fs.open("/file", FW | FC);
    if (fs.getLastErr() != Result::ok)
        printf("%s!\n", err_msg(fs.getLastErr()));
    ASSERT_NE(fil, nullptr);

    r = fs.seek(*fil, dataBytesPerPage + 20, Seekmode::set);
    ASSERT_EQ(r, Result::ok);

    r = fs.write(*fil, txt, strlen(txt), &bw);
    EXPECT_EQ(bw, strlen(txt));
    ASSERT_EQ(r, Result::ok);

    r = fs.seek(*fil, -strlen(txt), Seekmode::cur);
    ASSERT_EQ(r, Result::ok);

    r = fs.read(*fil, buf, strlen(txt), &bw);
    EXPECT_EQ(bw, strlen(txt));
    ASSERT_EQ(r, Result::ok);
    ASSERT_TRUE(ArraysMatch(txt, buf, strlen(txt)));

    r = fs.seek(*fil, 20, Seekmode::set);
    ASSERT_EQ(r, Result::ok);

    r = fs.read(*fil, buf, 1, &bw);
    EXPECT_EQ(bw, static_cast<unsigned int>(1));
    ASSERT_EQ(r, Result::ok);
    ASSERT_EQ(buf[0], 0);

    r = fs.seek(*fil, -strlen(txt), Seekmode::end);
    ASSERT_EQ(r, Result::ok);

    r = fs.read(*fil, buf, strlen(txt), &bw);
    EXPECT_EQ(bw, strlen(txt));
    ASSERT_EQ(r, Result::ok);
    ASSERT_TRUE(ArraysMatch(txt, buf, strlen(txt)));

    r = fs.close(*fil);
    ASSERT_EQ(r, Result::ok);
}

TEST_F(FileTest, createReadWriteDeleteFile)
{
    const FileSize filesize = 5 * dataBytesPerPage + 50;
    char t[] = ".                         Text";  // 30 chars

    std::unique_ptr<char[]> tlB(new char[filesize]);
    std::unique_ptr<char[]> bufB(new char[filesize]);
    char* tl = tlB.get();
    char* buf = bufB.get();
    char quer[] = "..--";
    Result r;
    ObjInfo info;
    int i;
    for (i = 0; (i + 1) * strlen(t) < filesize; i++)
    {
        memcpy(&tl[i * strlen(t)], t, strlen(t));
    }
    // fill Rest
    memset(&tl[i * strlen(t)], 0xAA, filesize - i * strlen(t));

    Obj* fil = fs.open("/file", FW | FC);
    ASSERT_NE(fil, nullptr);

    FileSize bytes = 0;
    r = fs.write(*fil, tl, filesize, &bytes);
    EXPECT_EQ(bytes, filesize);
    ASSERT_EQ(r, Result::ok);

    r = fs.getObjInfo("/file", info);
    ASSERT_EQ(r, Result::ok);
    ASSERT_EQ(info.isDir, false);
    ASSERT_EQ(info.size, filesize);

    // read
    r = fs.seek(*fil, 0, Seekmode::set);
    ASSERT_EQ(r, Result::ok);
    r = fs.read(*fil, buf, filesize, &bytes);
    ASSERT_EQ(r, Result::ok);
    ASSERT_EQ(bytes, filesize);
    EXPECT_TRUE(ArraysMatch(buf, tl, filesize));

    // misaligned write
    memcpy(&tl[dataBytesPerPage - strlen(quer) / 2], quer, strlen(quer));
    r = fs.seek(*fil, dataBytesPerPage - strlen(quer) / 2, Seekmode::set);
    ASSERT_EQ(r, Result::ok);
    r = fs.write(*fil, quer, strlen(quer), &bytes);
    EXPECT_EQ(bytes, strlen(quer));
    ASSERT_EQ(r, Result::ok);

    // read
    r = fs.seek(*fil, 0, Seekmode::set);
    ASSERT_EQ(r, Result::ok);
    r = fs.read(*fil, buf, filesize, &bytes);
    ASSERT_EQ(r, Result::ok);
    ASSERT_EQ(bytes, filesize);
    EXPECT_TRUE(ArraysMatch(buf, tl, filesize));

    r = fs.close(*fil);
    ASSERT_EQ(r, Result::ok);

    r = fs.remove("/file");
    ASSERT_EQ(r, Result::ok);
}

TEST_F(FileTest, createReadWriteDeleteManyFiles)
{
    static constexpr unsigned int blocksize = 1000;
    static constexpr unsigned int numberOfFiles = 500;
    static constexpr unsigned int maxFilesize = 2000;
    Result r;

    unsigned int filesize[numberOfFiles];
    char block[blocksize];

    memset(filesize, 0, numberOfFiles * sizeof(unsigned int));
    for(unsigned i = 0; i < blocksize; i++)
    {
        block[i] = rand();
    }

    for(unsigned round = 0; round < 3; round++)
    {
        for(unsigned fileNo = 0; fileNo < numberOfFiles; fileNo++)
        {
            //Check if every file that should exist actually does exist
            for(unsigned file = 0; file < numberOfFiles; file++)
            {
                if(filesize[file] > 0)
                {
                    ObjInfo info;
                    std::string filename("file");
                    filename.append(std::to_string(file));
                    r = fs.getObjInfo(filename.c_str(), info);
                    if(r != Result::ok)
                    {
                        printf("File %u: %s\n", file, err_msg(r));
                    }
                    ASSERT_EQ(r, Result::ok);
                    ASSERT_EQ(info.size, filesize[file]);
                }
            }

            std::string filename("file");
            filename.append(std::to_string(fileNo));
            Obj* fd;
            fd = fs.open(filename.c_str(), FC | FR | FW);
            ASSERT_NE(fd, nullptr);

            //Check that current file has right contents
            for(unsigned i = 0; i < filesize[fileNo]; i += blocksize)
            {
                char buf[blocksize];
                unsigned int br;
                r = fs.read(*fd, buf, blocksize, &br);
                ASSERT_EQ(r, Result::ok);
                ASSERT_EQ(br, blocksize);

                if(memcmp(block, buf, blocksize))
                {
                    fprintf(stderr, "\nFile %u: contents at %u wrong!\n", fileNo, i);
                    for(unsigned j = 0; j < blocksize / 5; j += 5)
                    {
                        printf("%02X%02X%02X%02X%02X - %02X%02X%02X%02X%02X",
                               static_cast<uint8_t>(block[j]), static_cast<uint8_t>(block[j+1]), static_cast<uint8_t>(block[j+2]), static_cast<uint8_t>(block[j+3]), static_cast<uint8_t>(block[j+4]),
                               static_cast<uint8_t>(buf[j]), static_cast<uint8_t>(buf[j+1]), static_cast<uint8_t>(buf[j+2]), static_cast<uint8_t>(buf[j+3]), static_cast<uint8_t>(buf[j+4]));
                        if(memcmp(&block[j], &buf[j], 5))
                        {
                            printf(" <");
                        }
                        printf("\n");
                    }
                    ASSERT_EQ(true, false);
                }
            }

            if(filesize[fileNo] >= maxFilesize)
            {
                //delete
                fs.close(*fd);
                r = fs.remove(filename.c_str());
                ASSERT_EQ(r, Result::ok);
                filesize[fileNo] = 0;
            }
            else
            {
                //append
                unsigned int bw;
                r = fs.write(*fd, block, blocksize, &bw);
                if(r == Result::noSpace)
                {
                    r = fs.close(*fd);
                    ASSERT_EQ(r, Result::ok);
                    return;
                }
                ASSERT_EQ(r, Result::ok);
                filesize[fileNo] += bw;
                r = fs.close(*fd);
                ASSERT_EQ(r, Result::ok);
            }
        }
    }
}

TEST_F(FileTest, directoryReadWrite)
{
    Permission p = R | W;
    Result r;
    r = fs.mkDir("/a", p);
    ASSERT_EQ(r, Result::ok);
    r = fs.mkDir("/b", p);
    ASSERT_EQ(r, Result::ok);
    r = fs.mkDir("/a/1", p);
    ASSERT_EQ(r, Result::ok);
    r = fs.touch("/a/1/file1");
    ASSERT_EQ(r, Result::ok);
    r = fs.mkDir("/b/c", p);
    ASSERT_EQ(r, Result::ok);
    r = fs.touch("/b/file2");
    ASSERT_EQ(r, Result::ok);

    Dir* dir;
    Dirent* entr;

    // root
    dir = fs.openDir("/");
    ASSERT_NE(dir, nullptr);
    entr = fs.readDir(*dir);
    ASSERT_NE(entr, nullptr);
    ASSERT_EQ(entr->node->type, InodeType::dir);
    EXPECT_TRUE(StringsMatch(entr->name, "a/"));
    entr = fs.readDir(*dir);
    ASSERT_NE(entr, nullptr);
    ASSERT_EQ(entr->node->type, InodeType::dir);
    EXPECT_TRUE(StringsMatch(entr->name, "b/"));
    entr = fs.readDir(*dir);
    ASSERT_EQ(entr, nullptr);
    r = fs.closeDir(dir);
    ASSERT_EQ(r, Result::ok);

    // a
    dir = fs.openDir("/a");
    ASSERT_NE(dir, nullptr);
    entr = fs.readDir(*dir);
    ASSERT_NE(entr, nullptr);
    ASSERT_EQ(entr->node->type, InodeType::dir);
    EXPECT_TRUE(StringsMatch(entr->name, "1/"));
    r = fs.closeDir(dir);
    ASSERT_EQ(r, Result::ok);

    // a/b
    dir = fs.openDir("/a/1");
    ASSERT_NE(dir, nullptr);
    entr = fs.readDir(*dir);
    ASSERT_NE(entr, nullptr);
    ASSERT_EQ(entr->node->type, InodeType::file);
    EXPECT_TRUE(StringsMatch(entr->name, "file1"));
    r = fs.closeDir(dir);
    ASSERT_EQ(r, Result::ok);

    // b
    dir = fs.openDir("/b");
    ASSERT_NE(dir, nullptr);
    entr = fs.readDir(*dir);
    ASSERT_NE(entr, nullptr);
    ASSERT_EQ(entr->node->type, InodeType::dir);
    EXPECT_TRUE(StringsMatch(entr->name, "c/"));
    entr = fs.readDir(*dir);
    ASSERT_NE(entr, nullptr);
    ASSERT_EQ(entr->node->type, InodeType::file);
    EXPECT_TRUE(StringsMatch(entr->name, "file2"));
    r = fs.closeDir(dir);
    ASSERT_EQ(r, Result::ok);
}

TEST_F(FileTest, permissions)
{
    Obj* fil;
    Result r;
    char txt[] = "Hallo";
    unsigned int bw;

    fil = fs.open("/file", FR);
    EXPECT_EQ(fil, nullptr);
    EXPECT_EQ(fs.getLastErr(), Result::notFound);
    fs.resetLastErr();

    fil = fs.open("/file", FR | FC);
    ASSERT_NE(fil, nullptr);

    r = fs.write(*fil, txt, strlen(txt), &bw);
    EXPECT_EQ(bw, strlen(txt));
    ASSERT_EQ(r, Result::ok);

    r = fs.close(*fil);
    ASSERT_EQ(r, Result::ok);

    r = fs.chmod("/file", R);
    ASSERT_EQ(r, Result::ok);

    fil = fs.open("/file", FW);
    EXPECT_EQ(fs.getLastErr(), Result::noPerm);
    ASSERT_EQ(fil, nullptr);
    fs.resetLastErr();

    fil = fs.open("/file", FR);
    ASSERT_NE(fil, nullptr);

    r = fs.write(*fil, txt, strlen(txt), &bw);
    EXPECT_EQ(bw, static_cast<unsigned int>(0));
    ASSERT_EQ(r, Result::noPerm);

    r = fs.close(*fil);
    ASSERT_EQ(r, Result::ok);
}

TEST_F(FileTest, maxFilesize)
{
    Obj* fil;
    Result r;
    char txt[] = "Hallo";
    unsigned int bw;
    unsigned int wordsize = sizeof(txt);
    unsigned int blocksize = dataBytesPerPage * 1.5;
    char block[blocksize];
    char blockcopy[blocksize];
    unsigned int i = 0, maxFileSize = 0;
    for (; i + wordsize < blocksize; i += wordsize)
    {
        memcpy(&block[i], txt, wordsize);
    }
    if (blocksize - i > 0)
    {
        memcpy(&block[i], txt, blocksize - i);
    }

    fil = fs.open("/file", FW | FR | FC);
    if (fs.getLastErr() != Result::ok)
    {
        printf("%s!\n", err_msg(fs.getLastErr()));
    }
    ASSERT_NE(fil, nullptr);
    i = 0;
    while (true)
    {
        r = fs.write(*fil, block, blocksize, &bw);
        i += bw;
        if (r == Result::noSpace)
            break;
        if (r != Result::ok)
        {
            printf("Could not write file at %u because %s\n", fil->fp, err_msg(r));
        }
        ASSERT_EQ(r, Result::ok);
        EXPECT_EQ(bw, blocksize);

        fs.seek(*fil, -bw, Seekmode::cur);
        r = fs.read(*fil, blockcopy, blocksize, &bw);
        ASSERT_EQ(r, Result::ok);
        ASSERT_TRUE(ArraysMatch(block, blockcopy, blocksize));
    }
    maxFileSize = i;
    r = fs.close(*fil);
    ASSERT_EQ(r, Result::ok);
    r = fs.unmount();
    ASSERT_EQ(r, Result::ok);
    r = fs.mount();
    ASSERT_EQ(r, Result::ok);

    ObjInfo info;
    fs.resetLastErr();
    fs.getObjInfo("/file", info);
    ASSERT_EQ(fs.getLastErr(), Result::ok);
    ASSERT_EQ(maxFileSize, info.size);
    fil = fs.open("/file", FW);
    if (fs.getLastErr() != Result::ok)
        printf("%s!\n", err_msg(fs.getLastErr()));
    ASSERT_NE(fil, nullptr);

    i = 0;
    while (i < maxFileSize)
    {
        memset(blockcopy, 0, blocksize);
        r = fs.read(*fil, blockcopy, blocksize, &bw);
        ASSERT_EQ(r, Result::ok);
        if (maxFileSize - i >= blocksize)
        {
            ASSERT_EQ(bw, blocksize);
            ASSERT_TRUE(ArraysMatch(block, blockcopy, blocksize));
        }
        else
        {
            ASSERT_EQ(bw, maxFileSize - i);
            ASSERT_TRUE(ArraysMatch(block, blockcopy, maxFileSize - i));
        }
        i += bw;
    }

    r = fs.close(*fil);
    ASSERT_EQ(r, Result::ok);

    r = fs.truncate("/file", maxFileSize / 2);
    ASSERT_EQ(r, Result::ok);
    fs.resetLastErr();
    fs.getObjInfo("/file", info);
    ASSERT_EQ(fs.getLastErr(), Result::ok);
    ASSERT_EQ(maxFileSize / 2, info.size);

    fil = fs.open("/file", FR);
    if (fs.getLastErr() != Result::ok)
        printf("%s!\n", err_msg(fs.getLastErr()));
    ASSERT_NE(fil, nullptr);
    r = fs.seek(*fil, maxFileSize / 2 - blocksize / 2);
    ASSERT_EQ(r, Result::ok);

    memset(blockcopy, 0, blocksize);
    std::cout << "Intentionally reading over Filesize" << std::endl;
    r = fs.read(*fil, blockcopy, blocksize, &bw);
    EXPECT_EQ(bw, blocksize / 2);
    ASSERT_EQ(r, Result::ok);
    unsigned blockStart = (maxFileSize / 2 - blocksize / 2) % blocksize;
    unsigned overRun = blocksize - blockStart;
    if (overRun < bw)
    {
        EXPECT_TRUE(ArraysMatch(blockcopy, &block[blockStart], overRun));
        EXPECT_TRUE(ArraysMatch(&blockcopy[overRun], block, bw - overRun));
    }
    else
    {
        EXPECT_TRUE(ArraysMatch(blockcopy, &block[blockStart], bw));
    }

    r = fs.remove("/file");
    ASSERT_EQ(r, Result::invalidInput);
    r = fs.close(*fil);
    ASSERT_EQ(r, Result::ok);
    r = fs.remove("/file");
    ASSERT_EQ(r, Result::ok);

    // Check if file is missing
    Dir* dir;

    // root
    dir = fs.openDir("/");
    ASSERT_NE(dir, nullptr);
    ASSERT_EQ(dir->entries, 0);

    r = fs.closeDir(dir);
    ASSERT_EQ(r, Result::ok);
}

TEST_F(FileTest, multiplePointersToSameFile)
{
    unsigned char elemsize = 4;
    char elemIn[elemsize + 1], elemOut[elemsize + 1];
    unsigned ringBufSize = 0, maxRingBufSize = 500;
    unsigned runningNumberIn = 0, runningNumberOut = 0;
    Obj *inFil, *outFil;
    unsigned int b;
    Result r;

    /**
     * critical seeds:
     * none yet encountered
     */
    int seed = time(NULL);
    std::cout << "Random seed :" << seed << std::endl;
    srand(seed);
    // open file with different modes
    outFil = fs.open("/file", FW | FC);
    if (fs.getLastErr() != Result::ok)
        printf("%s!\n", err_msg(fs.getLastErr()));
    ASSERT_NE(outFil, nullptr);
    inFil = fs.open("/file", FR);
    if (fs.getLastErr() != Result::ok)
        printf("%s!\n", err_msg(fs.getLastErr()));
    ASSERT_NE(inFil, nullptr);

    while (runningNumberOut < 5000)
    {
        if (rand() % 2)
        {
            // insert
            if (ringBufSize >= maxRingBufSize)
                continue;
            sprintf(elemIn, "%0*u", elemsize, runningNumberIn++);
            r = fs.write(*inFil, elemIn, elemsize, &b);
            EXPECT_EQ(b, elemsize);
            ASSERT_EQ(r, Result::ok);
            // printf("Wrote elem %4s\n", elemIn);
            if (inFil->fp >= maxRingBufSize * elemsize)
            {
                r = fs.seek(*inFil, 0, Seekmode::set);
                ASSERT_EQ(r, Result::ok);
            }
            ringBufSize++;
        }
        else
        {
            // retrieve
            if (ringBufSize < 1)
                continue;
            r = fs.read(*outFil, elemOut, elemsize, &b);
            EXPECT_EQ(b, elemsize);
            ASSERT_EQ(r, Result::ok);
            // printf("Read elem %.*s\n",elemsize, elemOut);
            sprintf(elemIn, "%0*u", elemsize, runningNumberOut++);
            ASSERT_EQ(strncmp(elemIn, elemOut, elemsize), 0);
            if (outFil->fp >= maxRingBufSize * elemsize)
            {
                r = fs.seek(*outFil, 0, Seekmode::set);
                ASSERT_EQ(r, Result::ok);
            }
            ringBufSize--;
        }
    }
    ASSERT_EQ(fs.getNumberOfOpenFiles(), 2);
    Obj* list[2];
    r = fs.getListOfOpenFiles(list);
    ASSERT_EQ(r, Result::ok);
    ASSERT_EQ(list[1], inFil);
    ASSERT_EQ(list[0], outFil);

    r = fs.close(*inFil);
    ASSERT_EQ(r, Result::ok);
    r = fs.close(*outFil);
    ASSERT_EQ(r, Result::ok);
}

TEST_F(FileTest, readOnlyChecks)
{
    Permission p = R | W;
    char word[] = "Hallo1";
    unsigned int wordlen = 6;
    char buf[wordlen];
    Result r;
    Obj* fil;
    Dir* dir;
    Dirent* entr;

    unsigned int bw = 0;
    r = fs.mkDir("/a", p);
    ASSERT_EQ(r, Result::ok);
    r = fs.mkDir("/a/b", p);
    ASSERT_EQ(r, Result::ok);
    r = fs.touch("/a/b/file1");
    ASSERT_EQ(r, Result::ok);
    r = fs.mkDir("/b", p);
    ASSERT_EQ(r, Result::ok);
    r = fs.mkDir("/b/c", p);
    ASSERT_EQ(r, Result::ok);
    r = fs.touch("/b/file2");
    ASSERT_EQ(r, Result::ok);

    fil = fs.open("/a/b/file1", FW);
    ASSERT_EQ(fs.getLastErr(), Result::ok);
    ASSERT_NE(fil, nullptr);
    r = fs.write(*fil, word, wordlen, &bw);
    ASSERT_EQ(r, Result::ok);
    ASSERT_EQ(bw, wordlen);
    r = fs.close(*fil);

    r = fs.unmount();
    ASSERT_EQ(r, Result::ok);
    r = fs.mount(true);
    ASSERT_EQ(r, Result::ok);

    // normal read
    fil = fs.open("/a/b/file1", FR);
    ASSERT_EQ(fs.getLastErr(), Result::ok);
    ASSERT_NE(fil, nullptr);
    r = fs.read(*fil, buf, wordlen, &bw);
    ASSERT_EQ(r, Result::ok);
    ASSERT_EQ(bw, wordlen);
    ASSERT_TRUE(ArraysMatch(buf, word, 6));
    r = fs.close(*fil);

    // Write try
    fil = fs.open("/a/b/file1", FW);
    ASSERT_EQ(fs.getLastErr(), Result::readOnly);
    ASSERT_EQ(fil, nullptr);
    fs.resetLastErr();

    // existing File
    r = fs.touch("/a/b/file1");
    ASSERT_EQ(r, Result::readOnly);

    // new file
    r = fs.touch("/a/b/fileNEW");
    ASSERT_EQ(r, Result::readOnly);

    // existing Dirs
    // root
    dir = fs.openDir("/");
    ASSERT_NE(dir, nullptr);
    entr = fs.readDir(*dir);
    ASSERT_NE(entr, nullptr);
    ASSERT_EQ(entr->node->type, InodeType::dir);
    EXPECT_TRUE(StringsMatch(entr->name, "a/"));
    entr = fs.readDir(*dir);
    ASSERT_NE(entr, nullptr);
    ASSERT_EQ(entr->node->type, InodeType::dir);
    EXPECT_TRUE(StringsMatch(entr->name, "b/"));
    r = fs.closeDir(dir);
    ASSERT_EQ(r, Result::ok);
    // a
    dir = fs.openDir("/a");
    ASSERT_NE(dir, nullptr);
    entr = fs.readDir(*dir);
    ASSERT_NE(entr, nullptr);
    ASSERT_EQ(entr->node->type, InodeType::dir);
    EXPECT_TRUE(StringsMatch(entr->name, "b/"));
    r = fs.closeDir(dir);
    ASSERT_EQ(r, Result::ok);

    // a/b
    dir = fs.openDir("/a/b");
    ASSERT_NE(dir, nullptr);
    entr = fs.readDir(*dir);
    ASSERT_NE(entr, nullptr);
    ASSERT_EQ(entr->node->type, InodeType::file);
    EXPECT_TRUE(StringsMatch(entr->name, "file1"));
    r = fs.closeDir(dir);
    ASSERT_EQ(r, Result::ok);

    // b
    dir = fs.openDir("/b");
    ASSERT_NE(dir, nullptr);
    entr = fs.readDir(*dir);
    ASSERT_NE(entr, nullptr);
    ASSERT_EQ(entr->node->type, InodeType::dir);
    EXPECT_TRUE(StringsMatch(entr->name, "c/"));
    entr = fs.readDir(*dir);
    ASSERT_NE(entr, nullptr);
    ASSERT_EQ(entr->node->type, InodeType::file);
    EXPECT_TRUE(StringsMatch(entr->name, "file2"));
    r = fs.closeDir(dir);
    ASSERT_EQ(r, Result::ok);

    // new Dir
    r = fs.mkDir("/c/");
    ASSERT_EQ(r, Result::readOnly);

    // delete dir
    r = fs.remove("/a/b");
    ASSERT_EQ(r, Result::readOnly);
}
