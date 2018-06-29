/*
 * Copyright (c) 2018, German Aerospace Center (DLR)
 *
 * This file is part of the development version of OUTPOST.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Authors:
 * - 2018, Pascal Pieper (DLR RY-AVS)
 */
// ----------------------------------------------------------------------------

#include "cmd.hpp"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

const char* commandNames[] =
{
    "quit",
    "help",
    "cat",
    "info",
    "ls",
    "cd",
    "append",
    "fill",
    "mkdir",
    "touch",
    "del",
    "mount",
    "unmount",
    "format",
    "invalid",
    "num_of_commands"
};

const char* commandUsage[] =
{
    "",
    "",
    "filename",
    "filename",
    "[path to folder]",
    "path to folder",
    "filename text",
    "filename [numOfRepeatedWrites]",
    "filename",
    "foldername",
    "filename",
    "",
    "",
    "quick | complete",
    "",
    ""
};


//This could be much more elegant.
CmdParser::Command CmdParser::parse(char* string)
{
    char* cmd = string;
    char* arg1 = strchr(string, ' ');
    char* arg2 = NULL;
    if(arg1 != NULL)
    {
        arg1[0] = 0; //Null string terminator for cmd argument
        arg1++;
        arg2 = strchr(arg1, ' ');
        if(arg2 != NULL)
        {
            arg2[0] = 0; //Null string terminator for first optional argument
            arg2++;
        }
    }

    if(strcmp(cmd, commandNames[CommandID::cat]) == 0)
    {
        if(arg1 == NULL)
        {
            return Invalid();
        }
        return Cat(arg1);
    }
    else if(strcmp(cmd, commandNames[CommandID::info]) == 0)
    {
        if(arg1 == NULL)
        {
            return Invalid();
        }
        return Info(arg1);
    }
    else if(strcmp(cmd, commandNames[CommandID::quit]) == 0)
    {
        return Quit();
    }
    else if(strcmp(cmd, commandNames[CommandID::help]) == 0)
    {
        return Help();
    }
    else if(strcmp(cmd, commandNames[CommandID::ls]) == 0)
    {
        return arg1 == NULL ? Ls() : Ls(arg1);
    }
    else if(strcmp(cmd, commandNames[CommandID::cd]) == 0)
    {
        if(arg1 == NULL)
        {
            return Invalid();
        }
        return Cd(arg1);
    }
    else if(strcmp(cmd, commandNames[CommandID::append]) == 0)
    {
        if(arg1 == NULL || arg2 == NULL)
        {
            return Invalid();
        }
        return Append(arg1, arg2);
    }
    else if(strcmp(cmd, commandNames[CommandID::fill]) == 0)
    {
        if(arg1 == NULL)
        {
            return Invalid();
        }
        return Fill(arg1, arg2);
    }
    else if(strcmp(cmd, commandNames[CommandID::mkdir]) == 0)
    {
        if(arg1 == NULL)
        {
            return Invalid();
        }
        return Mkdir(arg1);
    }
    else if(strcmp(cmd, commandNames[CommandID::touch]) == 0)
    {
        if(arg1 == NULL)
        {
            return Invalid();
        }
        return Touch(arg1);
    }
    else if(strcmp(cmd, commandNames[CommandID::del]) == 0)
    {
        if(arg1 == NULL)
        {
            return Invalid();
        }
        return Del(arg1);
    }
    else if(strcmp(cmd, commandNames[CommandID::mount]) == 0)
    {
        return Mount();
    }
    else if(strcmp(cmd, commandNames[CommandID::unmount]) == 0)
    {
        return Unmount();
    }
    else if(strcmp(cmd, commandNames[CommandID::format]) == 0)
    {
        if(arg1 == NULL)
        {
            return Invalid();
        }
        if(!strcmp(arg1, "quick") || !strcmp(arg1, "complete"))
        {
            return Format(arg1);
        }
        else
        {
            return Invalid();
        }
    }
    return Invalid();
};

void
CmdParser::listCommands()
{
    printf("Possible commands:\n");
    for(int i = 0; i < CommandID::num_command; i++)
    {
        printf("\t%s\t%s\n", commandNames[i], commandUsage[i]);
    }
}

void CmdHandler::prompt()
{
    using namespace paffs;
    Result r;
    ObjInfo inf;
    Obj* obj;
    Dir* dir;
    Dirent* entry;
    FileSize br;

    char sampleText[] = "This is an example text for testing file writes.\n"
            "Build: " __DATE__ " " __TIME__ "\n\n";

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
                return;
            }
            printf("Unmounted. You are now safe to turn off your computer.\n");
            return;
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
    return;
}
