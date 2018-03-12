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

#include "commonTypes.hpp"
#include "journalEntry.hpp"
#include "journalPersistence.hpp"

namespace paffs
{
class JournalTopic
{
public:
    virtual ~JournalTopic(){};

    virtual JournalEntry::Topic
    getTopic() = 0;
    /**
     * These functions get called by the Journal,
     * dispatching journal Entries to the corresponding topics.
     * Each Topic has to do its own actions to revert its state.
     */
    virtual void
    resetState() = 0;

    virtual inline void
    preScan(const journalEntry::Max&, JournalEntryPosition){};
    /**
     * Used to dispatch entries to foreign topics.
     */
    virtual inline bool
    isInterestedIn(const journalEntry::Max&)
    {
        return false;
    }
    /**
     * \warn A JournalTopic must not produce Journal Log entries in this Phase
     */
    virtual Result
    processEntry(const journalEntry::Max& entry, JournalEntryPosition position) = 0;

    /**
     * JournalTopics may write data now.
     */
    virtual inline void
    signalEndOfLog(){};
};
}
