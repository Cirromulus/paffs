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

uint8_t paffs::colorMap[JournalEntry::numberOfTopics] =
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
        printf("Add event ");
        printMeaning(entry);
    }

    if(entry.topic == JournalEntry::Topic::checkpoint &&
            static_cast<const journalEntry::Checkpoint*>(&entry)->target == JournalEntry::Topic::device)
    {   //This is a special case, because now we are in an absolutely clean state
        //so we can clean up the log
        //TODO: Check for persistence usage and commit everything if nearly full
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

    //TODO: Add a prescan function for JournalEntries so that AreaMgmt and ASCache can
    // preapply / skip all committed areas

    do
    {
        if(entry.base.topic == JournalEntry::Topic::areaMgmt)
        {
            /**
             * AreaMap should calculate the actual position of an area
             * before other Topics like summaryCache scans wrong summaryEntries because
             * a newer (and possibly different) area was committed there.
             * This also applies to Rootnode of IndexTree, where only the newest node may
             * point to valid data.
             * The processing of AreaMap before knowing where the checkpoints are
             * is (mostly!) safe in this special case, because after a AreaMap checkpoint
             * the whole log will be flushed (Superblock commit!)
             */
            r = topics[JournalEntry::Topic::areaMgmt]->processEntry(entry);
            if(r != Result::ok)
            {
                PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not apply areaMgmt elem in pre-run!");
                return r;
            }
        }

        if (entry.base.topic == JournalEntry::Topic::checkpoint)
        {
            if(entry.checkpoint.target == JournalEntry::Topic::areaMgmt)
            {
                PAFFS_DBG(PAFFS_TRACE_BUG, "we met checkpoint of areaMgmt,"
                        "this would require a third run");
                return Result::nimpl;
            }
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
            printf("\t\e[1;%um%s\e[0m: %" PRIu32 ".%" PRIu16 "\n",
                   colorMap[i],
                   topicNames[i],
                   firstUncheckpointedEntry[i].flash.addr,
                   firstUncheckpointedEntry[i].flash.offs);
        }
    }

    PAFFS_DBG_S(PAFFS_TRACE_JOURNAL, "Applying log...");

    //FIXME DEBUG
    AreaManagement* areaMgmt = &reinterpret_cast<Device*>(topics[JournalEntry::Topic::device])->areaMgmt;
    printf("Info: \n\t%" PTYPE_AREAPOS " used Areas\n", areaMgmt->getUsedAreas());
    for (AreaPos i = 0; i < areasNo; i++)
    {
        printf("\tArea %3" PTYPE_AREAPOS " on %4" PTYPE_AREAPOS " as %10s %s\n",
               i,
               areaMgmt->getPos(i),
               areaNames[areaMgmt->getType(i)],
               areaStatusNames[areaMgmt->getStatus(i)]);
        if (i > 128)
        {
            printf("\n -- truncated 128-%" PTYPE_AREAPOS " Areas.\n", areasNo);
            break;
        }
    }
    printf("\t----------------------\n");
    //FIXME DEBUG

    r = applyJournalEntries(firstUncheckpointedEntry);
    if(r != Result::ok)
    {
        PAFFS_DBG(PAFFS_TRACE_ERROR, "Could not apply Journal Entries!");
        return r;
    }

    disabled = false;
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

        if(entry.base.topic == JournalEntry::Topic::checkpoint)
        {
            continue;
        }

        if(entry.base.topic == JournalEntry::Topic::areaMgmt)
        {
            //TODO: remove this by designing the prescan / process scheme
            printf("_skipping_ ");
            printMeaning(entry.base, false);
            printf(" at %" PRIu32 ".%" PRIu16 "\n",
                   persistence.tell().flash.addr,
                   persistence.tell().flash.offs);
        }

        if(!isTopicValid(entry.base.topic))
        {
            PAFFS_DBG(PAFFS_TRACE_BUG, "Read invalid Topic identifier, probably misaligned!");
            return Result::bug;
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
            r = topics[target]->processEntry(entry);
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
            printf("_skipping_ ");
            printMeaning(entry.base, false);
            printf(" at %" PRIu32 ".%" PRIu16 "\n",
                   persistence.tell().flash.addr,
                   persistence.tell().flash.offs);
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
    case JournalEntry::Topic::areaMgmt:
        switch (static_cast<const journalEntry::AreaMgmt*>(&entry)->type)
        {
        case journalEntry::AreaMgmt::Type::rootnode:
            printf("Superblock Rootnode to %" PTYPE_AREAPOS ":%" PTYPE_PAGEOFFS,
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
                printf("increase Erasecount");
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
                printf("Swap with %" PTYPE_AREAPOS,
                       static_cast<const journalEntry::areaMgmt::areaMap::Swap*>(&entry)->b);
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
        //todo: add things and stuff.
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
                   static_cast<const journalEntry::device::InsertIntoDir*>(&entry)->inode);
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
    return persistence.rewind();
}
