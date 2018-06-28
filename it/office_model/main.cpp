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

extern "C" {
#include "system.h"
#include <sys/time.h>
}
#include <gpio.h>
#include <paffs.hpp>
#include <sevensegment.h>

#include "../misc/cmd.hpp"

#include <string.h>
#include <stdio.h>

using namespace outpost::nexys3;
using namespace paffs;

// initial bad blocks on NAND board 0
// const BlockAbs badBlocksDev0[] = {1626, 3919};
// const BlockAbs badBlocksDev1[] = {999};
// const BlockAbs badBlocksDev2[] = {2674, 2938};
// const BlockAbs badBlocksDev3[] = {240, 1491, 2827, 3249};

// initial bad blocks on NAND board 1
// const BlockAbs badBlocksDev0[] = {1653, 3289};
// const BlockAbs badBlocksDev1[] = {638, 798, 1382, 2301, 3001};

// initial bad blocks on NAND board 2
// const BlockAbs badBlocksDev0[] = {2026, 3039, 3584, 3646};
// const BlockAbs badBlocksDev1[] = {1059, 1771, 2372, 3434, 3484};

// initial bad blocks on NAND board 3
//const BlockAbs badBlocksDev0[] = {862, 3128};
//const BlockAbs badBlocksDev1[] = {61, 248, 1158, 1476, 2001, 2198};
//const BlockAbs badBlocksDev2[] = {0};
//const BlockAbs badBlocksDev3[] = {0};

// initial bad blocks on NAND board 4
const BlockAbs badBlocksDev0[] = {835, 2860, 3858, 4046};
const BlockAbs badBlocksDev1[] = {367, 2786};

// initial bad blocks on NAND board 5
// const BlockAbs badBlocksDev0[] = {314, 1818, 1925, 2967};
// const BlockAbs badBlocksDev1[] = {614, 2315, 2400, 2906, 2907, 2908, 2909, 3751};
// const BlockAbs badBlocksDev2[] = {418, 1690, 3787};
// const BlockAbs badBlocksDev3[] = {931, 3067};

uint8_t paffsRaw[sizeof(Paffs)];

rtems_task task_system_init(rtems_task_argument)
{
    printf("\n\n\n\nBuild: " __DATE__ " " __TIME__ "\n");

    SevenSegment::clear();
    SevenSegment::write(0, 'P');
    SevenSegment::write(1, 'A');
    SevenSegment::write(2, 'F');
    SevenSegment::write(3, 'S');

    BadBlockList badBlocks[] = {BadBlockList(badBlocksDev0, 4), BadBlockList(badBlocksDev1, 2)};

    uint8_t leds = 0;
    for (uint_fast8_t i = 0; i < 10; ++i)
    {
        leds >>= 1;
        leds |= 0b10000000;
        Gpio::set(leds);
        rtems_task_wake_after(25);
    }

    std::vector<paffs::Driver*> drv;
    drv.push_back(paffs::getDriver(0));
    Paffs* fs = new(paffsRaw) Paffs(drv);
    fs->setTraceMask(PAFFS_TRACE_SOME);

    CmdHandler cmd(fs, badBlocks);

    cmd.prompt();

    printf("Now idling.\n");
    rtems_stack_checker_report_usage();
    while (1)
    {
        for (uint_fast8_t i = 0; i < 12; ++i)
        {
            leds <<= 1;

            if (i <= 3)
            {
                leds |= 1;
            }
            Gpio::set(leds);
            rtems_task_wake_after(75);
        }
    }
}
