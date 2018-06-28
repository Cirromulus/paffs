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

#pragma once

#include <paffs.hpp>

extern const char* commandNames[];
extern const char* commandUsage[];

class CmdParser
{
    public:
    enum CommandID
    {
        quit = 0,
        help,
        cat,
        info,
        ls,
        cd,
        append,
        fill,
        mkdir,
        touch,
        del,
        mount,
        unmount,
        format,
        invalid,
        num_command
    };

    struct Command
    {
    public:
        CommandID commandId;
        char* argument1;
        char* argument2;
        inline
        Command(CommandID _commandId) : commandId(_commandId),
                argument1(nullptr), argument2(nullptr){};
        inline
        Command(CommandID _commandId, char* arg1) : commandId(_commandId),
                argument1(arg1), argument2(nullptr){};
        inline
        Command(CommandID _commandId, char* arg1, char* arg2) : commandId(_commandId),
                argument1(arg1), argument2(arg2){};
    };

    struct Invalid : public Command
    {
        inline
        Invalid() : Command(CommandID::invalid){};
    };

    struct Quit : public Command
    {
        inline
        Quit() : Command(CommandID::quit){};
    };

    struct Help : public Command
    {
        inline
        Help() : Command(CommandID::help){};
    };

    struct Cat : public Command
    {
        inline
        Cat(char* path) : Command(CommandID::cat, path){};
    };


    struct Info : public Command
    {
        inline
        Info(char* path) : Command(CommandID::info, path){};
    };

    struct Ls : public Command
    {
        inline
        Ls() : Command(CommandID::ls){};
        inline
        Ls(char* path) : Command(CommandID::ls, path){};
    };

    struct Cd : public Command
    {
        inline
        Cd(char* path) : Command(CommandID::cd, path){};
    };

    struct Append : public Command
    {
        inline
        Append(char* path, char* string) : Command(CommandID::append, path, string){};
    };

    struct Fill : public Command
    {
        inline
        Fill(char* path, char* number) : Command(CommandID::fill, path, number){};
    };

    struct Mkdir : public Command
    {
        inline
        Mkdir(char* path) : Command(CommandID::mkdir, path){};
    };

    struct Touch : public Command
    {
        inline
        Touch(char* path) : Command(CommandID::touch, path){};
    };

    struct Del : public Command
    {
        inline
        Del(char* path) : Command(CommandID::del, path){};
    };
    struct Mount : public Command
    {
        inline
        Mount() : Command(CommandID::mount){};
    };
    struct Unmount : public Command
    {
        inline
        Unmount() : Command(CommandID::unmount){};
    };
    struct Format : public Command
    {
        inline
        Format(char* mode) : Command(CommandID::format, mode){};
    };

    Command
    parse(char* string);

    void
    listCommands();
};

class CmdHandler
{
    paffs::Paffs* fs;
    paffs::BadBlockList* badBlocks;
public:
    inline
    CmdHandler(paffs::Paffs* filesystem, paffs::BadBlockList* badBlockList)
        : fs(filesystem), badBlocks(badBlockList){};

    void
    prompt();
};
