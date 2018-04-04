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

#pragma once
#include "journalEntry.hpp"
#include "journalPersistence.hpp"
#include "journalTopic.hpp"
#include "journalDebug.hpp"

namespace paffs
{

class Journal
{
    JournalTopic* topics[JournalEntry::numberOfTopics];
    BitList<JournalEntry::numberOfTopics> uncheckpointedChanges;

    JournalPersistence& persistence;
    bool disabled;
public:
    Journal(JournalPersistence& _persistence,
            JournalTopic& superblock,
            JournalTopic& areaMgmt,
            JournalTopic& garbageColl,
            JournalTopic& summaryCache,
            JournalTopic& tree,
            JournalTopic& dataIO,
            JournalTopic& pac,
            JournalTopic& device)
        : persistence(_persistence)
    {
        memset(topics, 0, sizeof(JournalTopic*) * JournalEntry::numberOfTopics);
        topics[superblock.getTopic()  ] = &superblock;
        topics[areaMgmt.getTopic()    ] = &areaMgmt;
        topics[garbageColl.getTopic() ] = &garbageColl;
        topics[summaryCache.getTopic()] = &summaryCache;
        topics[tree.getTopic()        ] = &tree;
        topics[dataIO.getTopic()      ] = &dataIO;
        topics[pac.getTopic()         ] = &pac;
        topics[device.getTopic()      ] = &device;

        disabled = false;
        for(JournalTopic* topic : topics)
        {
            if(topic != nullptr)
            {
                uncheckpointedChanges.setBit(topic->getTopic());
            }
        }

        if(traceMask & PAFFS_TRACE_VERBOSE)
        {
            for(JournalTopic *topic : topics)
            {
                if(topic == nullptr)
                {
                    continue;
                }
                PAFFS_DBG_S(PAFFS_TRACE_JOURNAL, "registered %s", topicNames[topic->getTopic()]);
            }
        }
    }

    /**
     * \return Result::ok and Result::lowMem both indicate a success.
     * Result::lowMem signals the urgency to flush all caches.
     * return Result::noSpace or others if not successful.
     */
    Result
    addEvent(const JournalEntry& entry);
    Result
    clear();
    Result
    processBuffer();
    void
    printMeaning(const JournalEntry& entry, bool withNewLine = true);
    void
    disable();
    Result
    enable();
    bool
    isEnabled();
private:
    bool
    isTopicValid(JournalEntry::Topic topic);
    Result
    applyJournalEntries(JournalEntryPosition firstUncheckpointedEntry[JournalEntry::numberOfTopics]);
};
};
