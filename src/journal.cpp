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

#include "journal.hpp"
#include "journalDebug.hpp"
#include "area.hpp"
#include "commonTypes.hpp"
#include "driver/driver.hpp"
#include "inttypes.h"

using namespace paffs;
using namespace std;

Result
Journal::addEvent(const JournalEntry& entry)
{
    if (disabled)
    {
        // Skipping, because we are currently replaying a buffer or formatting fs
        return Result::ok;
    }
    Result r = persistence.appendEntry(entry);
    if (r == Result::nospace)
    {
        PAFFS_DBG(PAFFS_TRACE_JOURNAL, "Log full. should be flushed.");
        return r;
    }
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not append Entry to persistence");
        return r;
    }
    if ((traceMask & PAFFS_TRACE_JOURNAL) && (traceMask & PAFFS_TRACE_VERBOSE))
    {
        printMeaning(entry);
    }
    return Result::ok;
}

Result
Journal::clear()
{
    return persistence.clear();
}

Result
Journal::processBuffer()
{
    journalEntry::Max entry;
    EntryIdentifier firstUncheckpointedEntry[JournalEntry::numberOfTopics];

    Result r = persistence.rewind();
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not rewind Journal!");
        return r;
    }
    for (EntryIdentifier& id : firstUncheckpointedEntry)
    {
        id = persistence.tell();
    }
    r = persistence.readNextElem(entry);
    if (r == Result::notFound)
    {
        PAFFS_DBG_S(PAFFS_TRACE_VERBOSE, "No Replay of Journal needed");
        return Result::ok;
    }
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not read First element of Journal!");
        return r;
    }

    disabled = true;  //FIXME: just while journal does not have write and read pointer
    PAFFS_DBG_S(PAFFS_TRACE_JOURNAL, "Replay of Journal needed");
    PAFFS_DBG_S(PAFFS_TRACE_VERBOSE | PAFFS_TRACE_JOURNAL, "Scanning for success entries...");

    do
    {
        if ((traceMask & PAFFS_TRACE_JOURNAL) && (traceMask & PAFFS_TRACE_VERBOSE))
        {
            printMeaning(entry.base);
        }
        if (entry.base.topic == JournalEntry::Topic::checkpoint)
        {
            firstUncheckpointedEntry[entry.checkpoint.target] = persistence.tell();
        }
    } while ((r = persistence.readNextElem(entry)) == Result::ok);
    if (r != Result::notFound)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not read next element of Journal!");
        return r;
    }

    if ((traceMask & PAFFS_TRACE_JOURNAL) && (traceMask & PAFFS_TRACE_VERBOSE))
    {
        printf("FirstUncheckpointedEntry:\n");
        for (unsigned i = 0; i < JournalEntry::numberOfTopics; i++)
        {
            printf("\t%s: %" PRIu32 ".%" PRIu16 "\n",
                   topicNames[i],
                   firstUncheckpointedEntry[i].flash.addr,
                   firstUncheckpointedEntry[i].flash.offs);
        }
    }

    PAFFS_DBG_S(PAFFS_TRACE_JOURNAL, "Applying log...");

    r = applyJournalEntries(firstUncheckpointedEntry);
    if(r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not apply Journal Entries!");
        return r;
    }

    disabled = false;
    return Result::ok;
}

Result
Journal::applyJournalEntries(EntryIdentifier firstUncheckpointedEntry[JournalEntry::numberOfTopics])
{
    Result r = persistence.rewind();
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not rewind Journal!");
        return r;
    }

    while (true)
    {
        journalEntry::Max entry;
        r = persistence.readNextElem(entry);
        if (r == Result::notFound)
        {
            break;
        }
        if (r != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_BUG, "Could not read journal to target end");
            return r;
        }

        for (JournalTopic* worker : topics)
        {
            if (entry.base.topic == worker->getTopic() ||
                    (entry.base.topic == JournalEntry::Topic::pagestate
                     && entry.pagestate.target == worker->getTopic()))
            {
                if (persistence.tell() >= firstUncheckpointedEntry[worker->getTopic()])
                {
                    if ((traceMask & PAFFS_TRACE_JOURNAL) && (traceMask & PAFFS_TRACE_VERBOSE))
                    {
                        printf("Processing entry ");
                        printMeaning(entry.base, false);
                        printf(" by %s ",
                               topicNames[worker->getTopic()]);
                        printf("at %" PRIu32 ".%" PRIu16 "\n",
                               persistence.tell().flash.addr,
                               persistence.tell().flash.offs);
                    }
                    r = worker->processEntry(entry);
                    if(r != Result::ok)
                    {
                        PAFFS_DBG(PAFFS_TRACE_ERROR, "%s: Could not apply %s entry at %" PRIu32 ".%" PRIu16,
                                  err_msg(r),
                                  topicNames[worker->getTopic()],
                                  persistence.tell().flash.addr,
                                  persistence.tell().flash.offs);
                        printMeaning(entry.base);
                        return r;
                    }
                }
            }
        }
    }

    PAFFS_DBG_S(PAFFS_TRACE_JOURNAL, "Signalling end of Log");
    for (JournalTopic* worker : topics)
    {
        worker->signalEndOfLog();
    }

    return Result::ok;
}

void
Journal::printMeaning(const JournalEntry& entry, bool withNewline)
{
    bool found = false;
    switch (entry.topic)
    {
    case JournalEntry::Topic::invalid:
        printf("\tInvalid/Empty");
        found = true;
        break;
    case JournalEntry::Topic::checkpoint:
        printf("\tcheckpoint of %s",
               topicNames[static_cast<const journalEntry::Checkpoint*>(&entry)->target]);
        found = true;
        break;
    case JournalEntry::Topic::pagestate:
    {
        auto ps = static_cast<const journalEntry::Pagestate*>(&entry);
        printf("Pagestate for %s ", topicNames[ps->target]);
        switch(ps->type)
        {
            case journalEntry::Pagestate::Type::pageUsed:
                printf("setPageused %" PTYPE_AREAPOS ":%" PTYPE_PAGEOFFS,
                       extractLogicalArea(static_cast<const journalEntry::pagestate::PageUsed*>(&entry)->addr),
                       extractPageOffs(static_cast<const journalEntry::pagestate::PageUsed*>(&entry)->addr));
                found = true;
                break;
            case journalEntry::Pagestate::Type::pagePending:
                printf("setPagePending %" PTYPE_AREAPOS ":%" PTYPE_PAGEOFFS,
                       extractLogicalArea(static_cast<const journalEntry::pagestate::PagePending*>(&entry)->addr),
                       extractPageOffs(static_cast<const journalEntry::pagestate::PagePending*>(&entry)->addr));
                found = true;
                break;
            case journalEntry::Pagestate::Type::success:
                printf("success");
                found = true;
                break;
            case journalEntry::Pagestate::Type::invalidateOldPages:
                printf("invalidateAllOldPages");
                found = true;
                break;
        }
        break;
    }
    case JournalEntry::Topic::areaMgmt:
        switch (static_cast<const journalEntry::AreaMgmt*>(&entry)->type)
        {
        case journalEntry::AreaMgmt::Type::rootnode:
            printf("Rootnode to %" PTYPE_AREAPOS ":%" PTYPE_PAGEOFFS,
                   extractLogicalArea(static_cast<const journalEntry::areaMgmt::Rootnode*>(&entry)->addr),
                   extractPageOffs(static_cast<const journalEntry::areaMgmt::Rootnode*>(&entry)->addr));
            found = true;
            break;
        case journalEntry::AreaMgmt::Type::areaMap:
            printf("AreaMap %" PTYPE_AREAPOS " ",
                   static_cast<const journalEntry::areaMgmt::AreaMap*>(&entry)->offs);
            switch (static_cast<const journalEntry::areaMgmt::AreaMap*>(&entry)->operation)
            {
            case journalEntry::areaMgmt::AreaMap::Operation::type:
                printf("set Type to %s",
                       areaNames[static_cast<const journalEntry::areaMgmt::areaMap::Type*>(&entry)
                                         ->type]);
                found = true;
                break;
            case journalEntry::areaMgmt::AreaMap::Operation::status:
                printf("set Status to %s",
                       areaStatusNames[static_cast<const journalEntry::areaMgmt::areaMap::Status*>(
                                               &entry)
                                               ->status]);
                found = true;
                break;
            case journalEntry::areaMgmt::AreaMap::Operation::increaseErasecount:
                printf("set Erasecount");
                found = true;
                break;
            case journalEntry::areaMgmt::AreaMap::Operation::position:
            {
                const journalEntry::areaMgmt::areaMap::Position* p =
                        static_cast<const journalEntry::areaMgmt::areaMap::Position*>(&entry);
                printf("set Position to %02X:%03X",
                       extractLogicalArea(p->position),
                       extractPageOffs(p->position));
                found = true;
                break;
            }
            case journalEntry::areaMgmt::AreaMap::Operation::swap:
                printf("Swap");
                found = true;
                break;
            }
            break;
        case journalEntry::AreaMgmt::Type::activeArea:
            printf("Set ActiveArea of %s to %" PTYPE_AREAPOS,
                   areaNames[static_cast<const journalEntry::areaMgmt::ActiveArea*>(&entry)->type],
                   static_cast<const journalEntry::areaMgmt::ActiveArea*>(&entry)->area);
            found = true;
            break;
        case journalEntry::AreaMgmt::Type::usedAreas:
            printf("Set used Areas to %" PTYPE_AREAPOS,
                   static_cast<const journalEntry::areaMgmt::UsedAreas*>(&entry)->usedAreas);
            found = true;
            break;
        }
        break;
    case JournalEntry::Topic::tree:
        printf("Treenode ");
        switch (static_cast<const journalEntry::BTree*>(&entry)->op)
        {
        case journalEntry::BTree::Operation::insert:
            printf("insert %" PRIu32,
                   static_cast<const journalEntry::btree::Insert*>(&entry)->inode.no);
            found = true;
            break;
        case journalEntry::BTree::Operation::update:
            printf("update %" PRIu32,
                   static_cast<const journalEntry::btree::Update*>(&entry)->inode.no);
            found = true;
            break;
        case journalEntry::BTree::Operation::remove:
            printf("remove %" PRIu32, static_cast<const journalEntry::btree::Remove*>(&entry)->no);
            found = true;
            break;
        case journalEntry::BTree::Operation::setRootnode:
            printf("SetRootnode to %" PTYPE_AREAPOS ":%" PTYPE_PAGEOFFS,
                   extractLogicalArea(static_cast<const journalEntry::btree::SetRootnode*>(&entry)->address),
                   extractPageOffs(static_cast<const journalEntry::btree::SetRootnode*>(&entry)->address));
            found = true;
            break;
        }
        break;
    case JournalEntry::Topic::summaryCache:
        printf("SummaryCache Area %" PTYPE_AREAPOS " ",
               static_cast<const journalEntry::SummaryCache*>(&entry)->area);
        switch (static_cast<const journalEntry::SummaryCache*>(&entry)->subtype)
        {
        case journalEntry::SummaryCache::Subtype::commit:
            printf("Commit");
            found = true;
            break;
        case journalEntry::SummaryCache::Subtype::remove:
            printf("Remove");
            found = true;
            break;
        case journalEntry::SummaryCache::Subtype::setStatus:
            printf("set Page %" PTYPE_PAGEOFFS " to %s",
                   static_cast<const journalEntry::summaryCache::SetStatus*>(&entry)->page,
                   summaryEntryNames[static_cast<unsigned>(
                           static_cast<const journalEntry::summaryCache::SetStatus*>(&entry)
                                   ->status)]);
            found = true;
            break;
        }
        break;
    case JournalEntry::Topic::pac:
        printf("pac ");
        switch (static_cast<const journalEntry::PAC*>(&entry)->operation)
        {
        case journalEntry::PAC::Operation::setInode:
            printf("set Inode %" PTYPE_INODENO, static_cast<const journalEntry::pac::SetInode*>(&entry)->inodeNo);
            found = true;
            break;
        case journalEntry::PAC::Operation::setAddress:
            {
            auto sa = static_cast<const journalEntry::pac::SetAddress*>(&entry);
            printf("set Address at %" PTYPE_PAGEOFFS " to %" PTYPE_AREAPOS ":%" PTYPE_PAGEOFFS,
                   sa->page, extractLogicalArea(sa->addr), extractPageOffs(sa->addr));
            found = true;
            break;
            }
        case journalEntry::PAC::Operation::updateAddresslist:
            printf("update address list");
            found = true;
            break;
        }
        break;
    case JournalEntry::Topic::dataIO:
        //todo: add things and stuff.
        break;
    case JournalEntry::Topic::device:
        //todo: add things and stuff.
        break;
    }
    if (!found)
        printf("Unrecognized");
    printf(" event");
    if (withNewline)
        printf(".\n");
}

void
Journal::disable()
{
    disabled = true;
}

Result
Journal::enable()
{
    disabled = false;
    return persistence.rewind();
}
