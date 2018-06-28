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
