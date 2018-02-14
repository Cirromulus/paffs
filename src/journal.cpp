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

//just for debug
#include "device.hpp"

using namespace paffs;

Result
Journal::addEvent(const JournalEntry& entry)
{
    if (disabled)
    {
        // Skipping, because we are currently replaying a buffer or formatting fs
        return Result::ok;
    }
    if ((traceMask & PAFFS_TRACE_JOURNAL) && (traceMask & PAFFS_TRACE_VERBOSE))
    {
        printf("Add event ");
        printMeaning(entry);
    }

    bool lowLogSpace = false;

    if(entry.topic != JournalEntry::Topic::checkpoint ||
            uncheckpointedChanges.getBit(static_cast<const journalEntry::Checkpoint*>(&entry)->target))
    {
        Result r = persistence.appendEntry(entry);
        if (r == Result::noSpace)
        {   //most bad situation
            PAFFS_DBG(PAFFS_TRACE_BUG, "Log full. should have been flushed.");
            return r;
        }
        if (r == Result::lowMem)
        {
            //This is a warning that we fell under the safetythreshold.
            //commit everything as soon as everything is valid
            lowLogSpace = true;
        }
        else if (r != Result::ok)
        {
            PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not append Entry to persistence");
            return r;
        }
        if(entry.topic == JournalEntry::Topic::checkpoint)
        {
            uncheckpointedChanges.resetBit(static_cast<const journalEntry::Checkpoint*>(&entry)->target);
        }
        else
        {
            JournalEntry::Topic target = entry.topic;
            if(target == JournalEntry::Topic::pagestate)
            {
                target = static_cast<const journalEntry::Pagestate*>(&entry)->target;
            }
            uncheckpointedChanges.setBit(target);
        }
    }
    else
    {
        PAFFS_DBG(PAFFS_TRACE_JOURNAL | PAFFS_TRACE_VERBOSE,
                  "Skipped checkpoint because no changes were made");
    }

    return lowLogSpace ? Result::lowMem : Result::ok;
}

Result
Journal::clear()
{
    disabled = false;
    for(JournalTopic* topic : topics)
    {
        if(topic != nullptr)
        {
            uncheckpointedChanges.setBit(topic->getTopic());
        }
    }
    return persistence.clear();
}

Result
Journal::processBuffer()
{
    journalEntry::Max entry;
    JournalEntryPosition firstUncheckpointedEntry[JournalEntry::numberOfTopics];

    Result r = persistence.rewind();
    if (r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not rewind Journal!");
        return r;
    }
    for (JournalEntryPosition& id : firstUncheckpointedEntry)
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
    {   //Prescan for finding the newest checkpoints (Also for ASCache and the newest commits)
        if (entry.base.topic == JournalEntry::Topic::checkpoint)
        {
            firstUncheckpointedEntry[entry.checkpoint.target] = persistence.tell();
            continue;
        }

        if(entry.base.topic == JournalEntry::Topic::pagestate)
        {
            //not needed for pagestate, so skip
            continue;
        }

        if(!isTopicValid(entry.base.topic))
        {
            PAFFS_DBG(PAFFS_TRACE_BUG, "Read invalid Topic identifier, probably misaligned!");
            return Result::bug;
        }

        topics[entry.base.topic]->preScan(entry, persistence.tell());

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
            printf("\t\e[1;%um%s\e[0m: %" PRIu32 ".%" PRIu16 "\n",
                   colorMap[i],
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

    return Result::ok;
}

bool
Journal::isTopicValid(JournalEntry::Topic topic)
{
    return topic > JournalEntry::Topic::invalid
            && topic <= JournalEntry::numberOfTopics
            && (topic == JournalEntry::pagestate || topics[topic] != nullptr);
}

Result
Journal::applyJournalEntries(JournalEntryPosition firstUncheckpointedEntry[JournalEntry::numberOfTopics])
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

        if(entry.base.topic == JournalEntry::Topic::checkpoint)
        {
            continue;
        }

        JournalEntry::Topic target = entry.base.topic;

        if(entry.base.topic == JournalEntry::Topic::pagestate)
        {
            if(!isTopicValid(entry.pagestate.target))
            {
                PAFFS_DBG(PAFFS_TRACE_BUG, "Read invalid Target identifier, probably misaligned!");
                return Result::bug;
            }
            target = entry.pagestate.target;
        }

        if (persistence.tell() >= firstUncheckpointedEntry[target])
        {
            if ((traceMask & PAFFS_TRACE_JOURNAL) && (traceMask & PAFFS_TRACE_VERBOSE))
            {
                printf("Processing ");
                printMeaning(entry.base, false);
                printf(" at %" PRIu32 ".%" PRIu16 "\n",
                       persistence.tell().flash.addr,
                       persistence.tell().flash.offs);
            }
            r = topics[target]->processEntry(entry, persistence.tell());
            if(r != Result::ok)
            {
                PAFFS_DBG(PAFFS_TRACE_ERROR, "%s: Could not apply %s entry at %" PRIu32 ".%" PRIu16,
                          err_msg(r),
                          topicNames[entry.base.topic],
                          persistence.tell().flash.addr,
                          persistence.tell().flash.offs);
                printMeaning(entry.base);
                return r;
            }
        }
        else
        {
            if ((traceMask & PAFFS_TRACE_JOURNAL) && (traceMask & PAFFS_TRACE_VERBOSE))
            {
                printf("_skipping_ ");
                printMeaning(entry.base, false);
                printf(" at %" PRIu32 ".%" PRIu16 "\n",
                       persistence.tell().flash.addr,
                       persistence.tell().flash.offs);
            }
        }
    }

    //FIXME
    //While persistence does not have read/write pointers,
    //we enable it here (It should be enabled before targets process entries
    //This is OK as long as a processEntry does not produce some decisions we would ignore later on
    disabled = false;

    PAFFS_DBG_S(PAFFS_TRACE_JOURNAL, "Signalling end of Log");
    for (JournalTopic* worker : topics)
    {
        if(worker != nullptr)
        {
            worker->signalEndOfLog();
        }
    }


    return Result::ok;
}

void
Journal::printMeaning(const JournalEntry& entry, bool withNewline)
{
    bool found = false;
    printf("\e[1;%um", colorMap[entry.topic]);
    switch (entry.topic)
    {
    case JournalEntry::Topic::invalid:
        printf("\tInvalid/Empty");
        found = true;
        break;
    case JournalEntry::Topic::checkpoint:
        printf("\tcheckpoint of \e[1;%um%s",
               colorMap[static_cast<const journalEntry::Checkpoint*>(&entry)->target],
               topicNames[static_cast<const journalEntry::Checkpoint*>(&entry)->target]);
        found = true;
        break;
    case JournalEntry::Topic::pagestate:
    {
        auto ps = static_cast<const journalEntry::Pagestate*>(&entry);
        printf("Pagestate for \e[1;%um%s \e[1;%um",
               colorMap[ps->target],
               topicNames[ps->target],
               colorMap[entry.topic]);
        switch(ps->type)
        {
        case journalEntry::Pagestate::Type::replacePage:
         {
             auto repl = static_cast<const journalEntry::pagestate::ReplacePage*>(&entry);
             printf("new Page: %" PTYPE_AREAPOS ":%" PTYPE_PAGEOFFS ", old: %" PTYPE_AREAPOS ":%" PTYPE_PAGEOFFS,
                    extractLogicalArea(repl->neu),
                    extractPageOffs(repl->neu),
                    extractLogicalArea(repl->old),
                    extractPageOffs(repl->old));
             found = true;
             break;
         }
        case journalEntry::Pagestate::Type::replacePagePos:
         {
             auto repl = static_cast<const journalEntry::pagestate::ReplacePagePos*>(&entry);
             printf("new Page: %" PTYPE_AREAPOS ":%" PTYPE_PAGEOFFS ", old: %" PTYPE_AREAPOS ":%" PTYPE_PAGEOFFS
                    " at pos %" PTYPE_PAGEABS,
                    extractLogicalArea(repl->neu),
                    extractPageOffs(repl->neu),
                    extractLogicalArea(repl->old),
                    extractPageOffs(repl->old),
                    repl->pos);
             found = true;
             break;
         }
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
    case JournalEntry::Topic::superblock:
        switch (static_cast<const journalEntry::Superblock*>(&entry)->type)
        {
        case journalEntry::Superblock::Type::rootnode:
            printf("Superblock Rootnode to %" PTYPE_AREAPOS ":%" PTYPE_PAGEOFFS,
                   extractLogicalArea(static_cast<const journalEntry::superblock::Rootnode*>(&entry)->addr),
                   extractPageOffs(static_cast<const journalEntry::superblock::Rootnode*>(&entry)->addr));
            found = true;
            break;
        case journalEntry::Superblock::Type::areaMap:
            printf("AreaMap %" PTYPE_AREAPOS " ",
                   static_cast<const journalEntry::superblock::AreaMap*>(&entry)->offs);
            switch (static_cast<const journalEntry::superblock::AreaMap*>(&entry)->operation)
            {
            case journalEntry::superblock::AreaMap::Operation::type:
                printf("set Type to %s",
                       areaNames[static_cast<const journalEntry::superblock::areaMap::Type*>(&entry)
                                         ->type]);
                found = true;
                break;
            case journalEntry::superblock::AreaMap::Operation::status:
                printf("set Status to %s",
                       areaStatusNames[static_cast<const journalEntry::superblock::areaMap::Status*>(
                                               &entry)
                                               ->status]);
                found = true;
                break;
            case journalEntry::superblock::AreaMap::Operation::increaseErasecount:
                printf("increase Erasecount");
                found = true;
                break;
            case journalEntry::superblock::AreaMap::Operation::position:
            {
                const journalEntry::superblock::areaMap::Position* p =
                        static_cast<const journalEntry::superblock::areaMap::Position*>(&entry);
                printf("set Position to %02X:%03X",
                       extractLogicalArea(p->position),
                       extractPageOffs(p->position));
                found = true;
                break;
            }
            case journalEntry::superblock::AreaMap::Operation::swap:
                printf("Swap with %" PTYPE_AREAPOS,
                       static_cast<const journalEntry::superblock::areaMap::Swap*>(&entry)->b);
                found = true;
                break;
            }
            break;
        case journalEntry::Superblock::Type::activeArea:
            printf("Set ActiveArea of %s to %" PTYPE_AREAPOS,
                   areaNames[static_cast<const journalEntry::superblock::ActiveArea*>(&entry)->type],
                   static_cast<const journalEntry::superblock::ActiveArea*>(&entry)->area);
            found = true;
            break;
        case journalEntry::Superblock::Type::usedAreas:
            printf("Set used Areas to %" PTYPE_AREAPOS,
                   static_cast<const journalEntry::superblock::UsedAreas*>(&entry)->usedAreas);
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
            printf("Tree SetRootnode to %" PTYPE_AREAPOS ":%" PTYPE_PAGEOFFS,
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
        case journalEntry::SummaryCache::Subtype::setStatusBlock:
            printf("set Statusblock");
            found = true;
            break;
        }
        break;
    case JournalEntry::Topic::pac:
        printf("pac ");
        switch (static_cast<const journalEntry::PAC*>(&entry)->operation)
        {
        case journalEntry::PAC::Operation::setAddress:
            {
            auto sa = static_cast<const journalEntry::pac::SetAddress*>(&entry);
            printf("set Address of %" PTYPE_INODENO " at %" PTYPE_PAGEOFFS " to %" PTYPE_AREAPOS ":%" PTYPE_PAGEOFFS,
                   sa->inodeNo, sa->page, extractLogicalArea(sa->addr), extractPageOffs(sa->addr));
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
        switch(static_cast<const journalEntry::DataIO*>(&entry)->operation)
        {
        case journalEntry::DataIO::Operation::newInodeSize:
            printf("Change Size of Inode %" PTYPE_INODENO " to %" PTYPE_FILSIZE,
                   static_cast<const journalEntry::dataIO::NewInodeSize*>(&entry)->inodeNo,
                   static_cast<const journalEntry::dataIO::NewInodeSize*>(&entry)->filesize);
            found = true;
            break;
        }
        break;
    case JournalEntry::Topic::device:
        printf("device ");
        switch(static_cast<const journalEntry::Device*>(&entry)->action)
        {
        case journalEntry::Device::Action::mkObjInode:
            printf("make Obj Inode %" PTYPE_INODENO,
                   static_cast<const journalEntry::device::MkObjInode*>(&entry)->inode);
            found = true;
            break;
        case journalEntry::Device::Action::insertIntoDir:
            printf("insert inode into dir %" PTYPE_INODENO,
                   static_cast<const journalEntry::device::InsertIntoDir*>(&entry)->dirInode);
            found = true;
            break;
        case journalEntry::Device::Action::removeObj:
            printf("remove Obj %" PTYPE_INODENO " from dir %" PTYPE_INODENO ,
                   static_cast<const journalEntry::device::RemoveObj*>(&entry)->obj,
                   static_cast<const journalEntry::device::RemoveObj*>(&entry)->parDir);
            found = true;
            break;
        }
        break;
    }
    if (!found)
        printf("Unrecognized");
    printf("\e[0m event");
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
    for(JournalTopic* topic : topics)
    {
        if(topic != nullptr)
        {
            uncheckpointedChanges.setBit(topic->getTopic());
        }
    }
    return persistence.rewind();
}
