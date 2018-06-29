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
 */
// ----------------------------------------------------------------------------

#include <fstream>
#include <iostream>
#include <paffs.hpp>
#include <simu/flashCell.hpp>
#include <simu/mram.hpp>
#include "cmd.hpp"


using namespace paffs;
using namespace std;

char filename[] = "/a.txt";
char exportFlash[] = "export_flash.bin";
char exportMram[] = "export_mram.bin";
char text[] = "Das Pferd frisst keinen Gurkensalat";

void
exportLog();

void
import();

static const char flashfile[] = "flash.bin";
static const char mramfile[] = "mram.bin";


int
main(int, char**)
{
    FlashCell* fc = new FlashCell();
    Mram* mram = new Mram(mramSize);

    //Read serialized flash and mram
    ifstream inff(flashfile,   ios::in | ios::binary);
    ifstream inmf(mramfile,    ios::in | ios::binary);
    if(inff.is_open() && inmf.is_open())
    {
        fc->getDebugInterface()->deserialize(inff);
        mram->deserialize(inmf);
        inff.close();
        inmf.close();
    }

    //init filesystem
    std::vector<paffs::Driver*> drv;
    drv.push_back(paffs::getDriverSpecial(0, fc, mram));
    BadBlockList bbl[maxNumberOfDevices];
    Paffs* fs = new Paffs(drv);

    printf("Trying to mount FS...");
    fflush(stdout);
    Result r = fs->mount();
    printf("\t %s\n", err_msg(r));

    if (r != Result::ok)
    {
        char buf = 0;
        while (buf != 'y' && buf != 'n' && buf != 'c' && buf != '\n')
        {
            printf("There was no valid image found. Format?\n(y(es)/c(omplete)/N(o)) ");
            fflush(stdout);
            scanf("%1c", &buf);
        }
        if (buf == 'y' || buf == 'c')
        {
            printf("You chose yes.\n");
            r = fs->format(bbl, buf == 'c');
            if (r == Result::ok)
            {
                printf("Success.\n");
            }
            else
            {
                printf("%s!\n", err_msg(r));
                return -1;
            }
        }
        else
        {
            printf("You chose no. \n");
            return 0;
        }

        printf("Trying to mount FS again ...\n");
        r = fs->mount();
        printf("\t %s\n", err_msg(r));
        if (r != Result::ok)
        {
            return -1;
        }
    }

    ObjInfo inf;
    Obj* obj;
    Dir* dir;
    Dirent* entry;
    FileSize br;

    CmdParser parser;
    char *line = NULL;
    size_t len = 0;
    ssize_t read;

    printf("CMD:\n");
    while ((read = getline(&line, &len, stdin)) != -1) {
        if(line[read-1] == '\n')
        {
            line[read-1] = 0;
        }
        CmdParser::Command cmd = parser.parse(line);
        switch(cmd.commandId)
        {
        case CmdParser::CommandID::quit:
            r = fs->unmount();
            if(r != Result::ok)
            {
                printf("Unmount error: %s\n", resultMsg[static_cast<uint8_t>(r)]);
                return -1;
            }
            printf("Unmounted. You are now safe to turn off your computer.\n");
            return 0;
            break;
        case CmdParser::CommandID::cat:
        {
            r = fs->getObjInfo(cmd.argument1, inf);
            if(r != Result::ok)
            {
                break;
            }
            printf("Filesize: %" PTYPE_FILSIZE " Byte\n", inf.size);
            obj = fs->open(cmd.argument1, FR);
            if (obj == nullptr)
            {
                break;
            }
            char buf[inf.size];
            r = fs->read(*obj, buf, inf.size, &br);
            if(r != Result::ok)
            {
                break;
            }
            printf("%.*s\n", static_cast<int>(inf.size), buf);
            fs->close(*obj);
            break;
        }
        case CmdParser::CommandID::ls:
        {
            const char* path = cmd.argument1 == NULL ? "/" : cmd.argument1;
            dir = fs->openDir(path);
            if (dir == nullptr)
            {
                r = fs->getLastErr();
                break;
            }
            printf("Contents of %s:\n", path);
            while((entry = fs->readDir(*dir)) != nullptr)
            {
               printf("\t%s\n", entry->name);
            }
            fs->closeDir(dir);
            break;
        }
        case CmdParser::CommandID::cd:
            printf("Command not implemented yet\n");
            break;
        case CmdParser::CommandID::append:
            obj = fs->open(cmd.argument1, FW | FA | FC);
            if (obj == nullptr)
            {
                break;
            }
            r = fs->write(*obj, cmd.argument2, strlen(cmd.argument2), &br);
            if(r != Result::ok)
            {
                break;
            }
            fs->close(*obj);
            break;
        case CmdParser::CommandID::mkdir:
            r = fs->mkDir(cmd.argument1, R | W | X);
            break;
        case CmdParser::CommandID::touch:
            r = fs->touch(cmd.argument1);
            break;
        case CmdParser::CommandID::del:
            r = fs->remove(cmd.argument1);
            break;
        case CmdParser::CommandID::mount:
            r = fs->mount();
            break;
        case CmdParser::CommandID::unmount:
            r = fs->unmount();
            break;
        case CmdParser::CommandID::format:
            r = fs->format(bbl, !strcmp(cmd.argument1, "complete"));
            break;
        default:
            printf("Unknown or invalid command.\n");
        //fall-through
        case CmdParser::CommandID::help:
            parser.listCommands();
            break;
        }
        printf("%s\n", resultMsg[static_cast<uint8_t>(r)]);
    }
}

void
import()
{
    std::vector<paffs::Driver*> drv;
    FlashCell* fc = new FlashCell();
    Mram* mram = new Mram(mramSize);
    drv.push_back(paffs::getDriverSpecial(0, fc, mram));

    // Deserialize
    ifstream inf(exportFlash, ios::in | ios::binary);
    if (!inf.is_open())
    {
        cout << "Serialized flash could not be opened" << endl;
        return;
    }
    fc->getDebugInterface()->deserialize(inf);
    inf.close();
    ifstream inm(exportMram, ios::in | ios::binary);
    if (!inm.is_open())
    {
        cout << "Serialized flash could not be opened" << endl;
        return;
    }
    mram->deserialize(inm);
    inm.close();

    Paffs fs(drv);
    fs.setTraceMask(fs.getTraceMask() | PAFFS_TRACE_JOURNAL | PAFFS_TRACE_VERBOSE);
    Result r = fs.mount();
    if (r != Result::ok)
    {
        cout << "Could not mount filesystem!" << endl;
        return;
    }

    fs.setTraceMask(fs.getTraceMask() & ~(PAFFS_TRACE_JOURNAL | PAFFS_TRACE_VERBOSE));

    ObjInfo info;
    r = fs.getObjInfo(filename, info);
    if (r != paffs::Result::ok)
    {
        cout << "could not get Info of file!" << endl;
    }
    Obj* fil = fs.open(filename, paffs::FR);
    if (fil == nullptr)
    {
        cout << "File could not be opened" << endl;
        return;
    }
    if(info.size != sizeof(text))
    {
        cout << "filesize differs! (should " << sizeof(text) << ", was " << info.size << ")" << endl;
        return;
    }
    char input[info.size + 1];
    unsigned int br;
    r = fs.read(*fil, input, info.size, &br);
    if (r != paffs::Result::ok)
    {
        cout << "File could not be read!" << endl;
        return;
    }
    input[info.size] = 0;

    if(memcmp(input, text, sizeof(text)) != 0)
    {
        cout << "File contents differ! Should \n" << text << "\nbut was\n" << input << endl;
        return;
    }

    cout << "File contents:" << endl << text << endl;

    cout << endl << "OK" << endl;
    fs.close(*fil);
    fs.unmount();
}

void
exportLog()
{
    std::vector<paffs::Driver*> drv;
    FlashCell* fc = new FlashCell();
    Mram* mram = new Mram(mramSize);
    drv.push_back(paffs::getDriverSpecial(0, fc, mram));

    Paffs fs(drv);

    BadBlockList bbl[maxNumberOfDevices];
    fs.format(bbl);
    fs.setTraceMask(fs.getTraceMask()
                    | PAFFS_TRACE_ERROR
                    | PAFFS_TRACE_BUG
                    | PAFFS_TRACE_INFO
                    | PAFFS_TRACE_JOURNAL);

    fs.mount();

    Obj* fil = fs.open(filename, paffs::FW | paffs::FC);
    if (fil == nullptr)
    {
        cout << "File could not be opened" << endl;
        return;
    }
    unsigned int bw = 0;
    Result r = fs.write(*fil, text, sizeof(text), &bw);
    if (r != Result::ok)
    {
        cout << "File could not be written" << endl;
        return;
    }

    //---- Whoops, power went out! ----//

    ofstream ef(exportFlash, ios::out | ios::binary);
    fc->getDebugInterface()->serialize(ef);
    ef.close();
    ofstream em(exportMram, ios::out | ios::binary);
    mram->serialize(em);
    em.close();

    cout << endl << "OK" << endl;

    fs.close(*fil);
    fs.unmount();

    delete fc;
    delete mram;
}
