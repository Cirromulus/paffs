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

namespace paffs
{
class Journal
{
    JournalTopic* topics[3];

    JournalPersistence& persistence;
    bool disabled;  //TODO: remove this for securing mount process

public:
    Journal(JournalPersistence& _persistence,
            JournalTopic& superblock,
            JournalTopic& summaryCache,
            JournalTopic& tree)
        : persistence(_persistence)
    {
        topics[0] = &superblock;
        topics[1] = &summaryCache;
        topics[2] = &tree;
        // TODO Inode

        disabled = false;
    }

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

private:
    Result
    applyJournalEntries(EntryIdentifier firstUncheckpointedEntry[JournalEntry::numberOfTopics]);
    PageAbs
    getSizeFromMax(const journalEntry::Max& entry);
};
};
