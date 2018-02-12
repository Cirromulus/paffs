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

namespace paffs
{
union JournalEntryPosition {
    struct Flash
    {
        Addr addr;
        uint16_t offs;

        inline bool
        operator<(const Flash& other)
        {
            if (addr == other.addr)
            {
                return offs < other.addr;
            }
            return addr < other.addr;
        }
        inline bool
        operator==(const Flash& other)
        {
            return addr == other.addr && offs == other.offs;
        }
        inline bool
        operator!=(const Flash& other)
        {
            return !(*this == other);
        }
        inline bool
        operator>=(const Flash& other)
        {
            return !(*this < other);
        }
    } flash;
    struct Mram
    {
        PageAbs offs;
    } mram;
    inline
    JournalEntryPosition(){};
    inline
    JournalEntryPosition(Addr _addr, uint16_t _offs)
    {
        flash.addr = _addr;
        flash.offs = _offs;
    }
    inline
    JournalEntryPosition(Flash _flash) : flash(_flash){};
    inline
    JournalEntryPosition(PageAbs _offs)
    {
        mram.offs = _offs;
        flash.offs = 0;
    }

    inline bool
    operator<(const JournalEntryPosition& other)
    {
        return flash < other.flash;
    }
    inline bool
    operator==(const JournalEntryPosition& other)
    {
        return flash == other.flash;
    }
    inline bool
    operator!=(const JournalEntryPosition& other)
    {
        return !(*this == other);
    }
    inline bool
    operator>=(const JournalEntryPosition& other)
    {
        return !(*this < other);
    }
};

class JournalPersistence
{
protected:
    Device* device;
    uint16_t
    getSizeFromMax(const journalEntry::Max& entry);
    uint16_t
    getSizeFromJE(const JournalEntry& entry);

public:
    inline
    JournalPersistence(Device* _device) : device(_device){};
    virtual
    ~JournalPersistence(){};

    /**
     * \warn Rewind has to be called before scanning or writing Elements
     */
    virtual Result
    rewind() = 0;

    virtual Result
    seek(JournalEntryPosition& addr) = 0;

    virtual JournalEntryPosition
    tell() = 0;

    /**
     * \return noSpace if log is full, lowMem if log capacity
     * is less than configurated reservedLogsize
     */
    virtual Result
    appendEntry(const JournalEntry& entry) = 0;

    virtual Result
    clear() = 0;

    virtual Result
    readNextElem(journalEntry::Max& entry) = 0;
};

class MramPersistence : public JournalPersistence
{
    PageAbs curr;

public:
    inline
    MramPersistence(Device* _device) : JournalPersistence(_device), curr(0){};
    Result
    rewind() override;
    Result
    seek(JournalEntryPosition& addr) override;
    JournalEntryPosition
    tell() override;
    Result
    appendEntry(const JournalEntry& entry) override;
    Result
    clear() override;
    Result
    readNextElem(journalEntry::Max& entry) override;
};

class FlashPersistence : public JournalPersistence
{
    struct FlashBuf
    {
        unsigned char data[dataBytesPerPage];
        bool readOnly;
        bool dirty;
        PageAbs page;
        FlashBuf()
        {
            memset(data, 0, dataBytesPerPage);
            readOnly = false;
            dirty = false;
            page = 0;
        }
    } buf;
    JournalEntryPosition::Flash curr;

public:
    inline
    FlashPersistence(Device* _device) : JournalPersistence(_device){};
    Result
    rewind() override;
    Result
    seek(JournalEntryPosition& addr) override;
    JournalEntryPosition
    tell() override;
    Result
    appendEntry(const JournalEntry& entry) override;
    Result
    clear() override;
    Result
    readNextElem(journalEntry::Max& entry) override;

private:
    Result
    commitBuf();
    Result
    findNextPos(bool afterACheckpoint = false);
    Result
    loadCurrentPage(bool readPage = true);
};
}
