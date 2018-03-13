/*
 * journalDebug.hpp
 *
 *  Created on: Nov 24, 2017
 *      Author: user
 */

#pragma once


namespace paffs
{
static constexpr const char* topicNames[] =
{
        "INVALID",
        "CHECKPOINT",
        "PAGESTATUS",
        "SUPBLOCK",
        "AREAMGMT",
        "GARBAGE",
        "SUMMARY CACHE",
        "TREE",
        "DATAIO",
        "PAC",
        "DEVICE",
};

static constexpr const uint8_t colorMap[JournalEntry::numberOfTopics] =
{
        97, //invalid
        31, //checkpoint
        32, //pagestate
        33, //superblock
        96, //areaMgmt
        95, //garbage collection
        36, //summaryCache
        35, //tree
        91, //dataIO
        37, //pac
        93, //device
};

}

