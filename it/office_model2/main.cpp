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

extern "C" {
#include "system.h"
#include <sys/time.h>
}
#include <paffs.hpp>

#include "../misc/cmd.hpp"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

using namespace paffs;

// initial bad blocks on NAND board 0
// const BlockAbs badBlocksDev0[] = {1626, 3919};
// const BlockAbs badBlocksDev1[] = {999};
// const BlockAbs badBlocksDev2[] = {2674, 2938};
// const BlockAbs badBlocksDev3[] = {240, 1491, 2827, 3249};

// initial bad blocks on NAND board 1
// const BlockAbs badBlocksDev0[] = {1653, 3289};
// const BlockAbs badBlocksDev1[] = {638, 798, 1382, 2301, 3001};

// initial bad blocks on NAND board 2
// const BlockAbs badBlocksDev0[] = {2026, 3039, 3584, 3646};
// const BlockAbs badBlocksDev1[] = {1059, 1771, 2372, 3434, 3484};

// initial bad blocks on NAND board 3
//const BlockAbs badBlocksDev0[] = {862, 3128};
//const BlockAbs badBlocksDev1[] = {61, 248, 1158, 1476, 2001, 2198};
//const BlockAbs badBlocksDev2[] = {0};
//const BlockAbs badBlocksDev3[] = {0};

// initial bad blocks on NAND board 4
const BlockAbs badBlocksDev0[] = {835, 2860, 3858, 4046};
const BlockAbs badBlocksDev1[] = {367, 2786};

// initial bad blocks on NAND board 5
// const BlockAbs badBlocksDev0[] = {314, 1818, 1925, 2967};
// const BlockAbs badBlocksDev1[] = {614, 2315, 2400, 2906, 2907, 2908, 2909, 3751};
// const BlockAbs badBlocksDev2[] = {418, 1690, 3787};
// const BlockAbs badBlocksDev3[] = {931, 3067};

uint8_t paffsRaw[sizeof(Paffs)];

rtems_task task_system_init(rtems_task_argument)
{
    char sampleText[] = "This is an example text for testing file writes.\n"
            "Build: " __DATE__ " " __TIME__ "\n\n";

    printf("\n\n\n\nBuild: " __DATE__ " " __TIME__ "\n");



    Result r;
    ObjInfo inf;
    Obj* obj;
    Dir* dir;
    Dirent* entry;
    FileSize br;

    BadBlockList badBlocks[] = {BadBlockList(badBlocksDev0, 4), BadBlockList(badBlocksDev1, 2)};

    std::vector<paffs::Driver*> drv;
    drv.push_back(paffs::getDriver(0));
    Paffs* fs = new(paffsRaw) Paffs(drv);
    fs->setTraceMask(PAFFS_TRACE_SOME);

    CmdParser parser;
    const uint16_t buffersize = 500;
    char line[buffersize];
    CmdParser::Command lastCommand = CmdParser::Invalid();
    while (true) {
        r = Result::ok;
        printf("> ");
        fflush(stdout);
        fgets(line, buffersize, stdin);
        if ((strlen(line) > 0) && (line[strlen (line) - 1] == '\n'))
        {   //remove trailing newline
            line[strlen (line) - 1] = '\0';
        }
        CmdParser::Command cmd = parser.parse(line);
        //recognize "up" key
        if(line[0] == 0x1B && line[1] == 0x5B)
        {
            //Arrow keys
            if(line[2] == 0x41)
            {   //up key
                cmd = lastCommand;
            }
        }
        if(cmd.commandId != CmdParser::CommandID::invalid)
        {
            lastCommand = cmd;
        }
        switch(cmd.commandId)
        {
        case CmdParser::CommandID::quit:
            fs->setTraceMask(PAFFS_TRACE_SOME | PAFFS_TRACE_VERBOSE | PAFFS_TRACE_JOURNAL);
            r = fs->unmount();
            fs->setTraceMask(PAFFS_TRACE_SOME);
            if(r != Result::ok)
            {
                printf("Unmount error: %s\n", resultMsg[static_cast<uint8_t>(r)]);
                goto exit;
            }
            printf("Unmounted. You are now safe to turn off your computer.\n");
            goto exit;
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
            for(uint16_t i = 0; i < (inf.size + 1023) / 1024; i++)
            {
                char buf[1024];
                uint16_t btr = inf.size - 1024 * i > 1024 ? 1024 : inf.size - 1024 * i;
                r = fs->read(*obj, buf, btr, &br);
                if(r != Result::ok)
                {
                    break;
                }
                printf("%.*s", static_cast<int>(btr), buf);
            }
            printf("\n");
            fs->close(*obj);
            break;
        }
        case CmdParser::CommandID::info:
        {
            r = fs->getObjInfo(cmd.argument1, inf);
            if(r != Result::ok)
            {
                break;
            }
            printf("%s\n", inf.isDir ? "Directory" : "File");
            printf("\tsize: %" PTYPE_FILSIZE " Byte\n", inf.size);
            printf("\tperm: %c%c%c\n", inf.perm & R ? 'r' : '-',
                                         inf.perm & W ? 'w' : '-',
                                         inf.perm & X ? 'x' : '-');
            printf("\tcrea: %" PRIu64 " seconds since epoch\n", inf.created.timeSinceEpoch().seconds());
            printf("\tmod.: %" PRIu64 " seconds since epoch\n", inf.modified.timeSinceEpoch().seconds());
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
        case CmdParser::CommandID::fill:
            {
            unsigned long n;
            if(cmd.argument2 == NULL || (n = strtol(cmd.argument2, NULL, 10)) == 0)
            {
                n = 1;
            }
            obj = fs->open(cmd.argument1, FW | FA | FC);
            if (obj == nullptr)
            {
                r = fs->getLastErr();
                break;
            }
            for(uint16_t i = 0; i < n; i++)
            {
                r = fs->write(*obj, sampleText, strlen(sampleText), &br);
                if(r != Result::ok)
                {
                    break;
                }
            }
            fs->close(*obj);
            break;
            }
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
            {
                TraceMask bak = fs->getTraceMask();
                fs->setTraceMask(bak | PAFFS_TRACE_VERBOSE | PAFFS_TRACE_JOURNAL);
                r = fs->mount();
                fs->setTraceMask(bak);
                break;
            }
        case CmdParser::CommandID::unmount:
            fs->setTraceMask(PAFFS_TRACE_SOME | PAFFS_TRACE_VERBOSE | PAFFS_TRACE_JOURNAL);
            r = fs->unmount();
            fs->setTraceMask(PAFFS_TRACE_SOME);
            break;
        case CmdParser::CommandID::format:
            r = fs->format(badBlocks, !strcmp(cmd.argument1, "complete"));
            break;
        default:
            printf("Unknown or invalid command.\n");
        //fall-through
        case CmdParser::CommandID::help:
            parser.listCommands();
            break;
        }
        printf("%s ", resultMsg[static_cast<uint8_t>(r)]);
    }
exit:
    while(true)
    {
        rtems_task_wake_after(1000);
    }
}
