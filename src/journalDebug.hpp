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
        "AREAMGMT",
        "SUMMARY CACHE",
        "TREE",
        "PAC",
        "DATAIO",
        "DEVICE",
};

static constexpr const uint8_t colorMap[JournalEntry::numberOfTopics] =
{
        97, //invalid
        31, //checkpoint
        32, //pagestate
        33, //areaMgmt
        36, //summaryCache
        35, //tree
        37, //pac
        91, //dataIO
        93, //device
};

}

