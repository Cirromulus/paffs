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

#ifndef IT_OFFICE_MODEL_SYSTEM_H_
#define IT_OFFICE_MODEL_SYSTEM_H_

#include <rtems.h>

// For device driver prototypes
#include <bsp.h>

#define CONFIGURE_INIT_TASK_ATTRIBUTES      RTEMS_FLOATING_POINT
#define CONFIGURE_INIT_TASK_STACK_SIZE      10*1024

// Configuration information
#define CONFIGURE_APPLICATION_NEEDS_CONSOLE_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_CLOCK_DRIVER
//#define	CONFIGURE_APPLICATION_NEEDS_TIMER_DRIVER //only one driver is allowed

// ----------------------------------------------------------------------------
// Tasks
#define CONFIGURE_MAXIMUM_TASKS             1
#define CONFIGURE_RTEMS_INIT_TASKS_TABLE
#define CONFIGURE_EXTRA_TASK_STACKS         (3 * RTEMS_MINIMUM_STACK_SIZE)

// Configure start task
#define	CONFIGURE_INIT_TASK_ENTRY_POINT		task_system_init

#ifdef __cplusplus
extern "C" {
#endif
// Forward declaration needed for task table
rtems_task task_system_init (rtems_task_argument );
#ifdef __cplusplus
}
#endif

extern const char* bsp_boot_cmdline;
#define CONFIGURE_INIT_TASK_ARGUMENTS     ((rtems_task_argument) &bsp_boot_cmdline)

#define	CONFIGURE_MICROSECONDS_PER_TICK		1000
#define	CONFIGURE_TICKS_PER_TIMESLICE		20

// ----------------------------------------------------------------------------
// Mutex/Semaphores
// C++ requires at least one Semaphore for the constructor calls and the
// initialization of static member variables.
#define	CONFIGURE_MAXIMUM_SEMAPHORES		300
//#define	CONFIGURE_MAXIMUM_POSIX_MUTEXES		100

// ----------------------------------------------------------------------------
// Timer support
#define	CONFIGURE_MAXIMUM_TIMERS			20
//#define	CONFIGURE_MAXIMUM_POSIX_TIMERS		4

#define CONFIGURE_MAXIMUM_PERIODS			10

//-----------------------------------------------------------------------------
// Message queues
// ApbUarts use message queues
#define CONFIGURE_MAXIMUM_MESSAGE_QUEUES    6

// ----------------------------------------------------------------------------
// Configure user defined error handler for fatal failures
void //changed uint32_t to unsigned int
//fatalErrorHandler(Internal_errors_Source source, bool isInternal, unsigned int errorCode);
fatalErrorHandler(Internal_errors_Source source, bool isInternal, uint32_t errorCode);

#define CONFIGURE_INITIAL_EXTENSIONS  { NULL, NULL, NULL, NULL, NULL, NULL, NULL, fatalErrorHandler }

#define CONFIGURE_INIT
// ----------------------------------------------------------------------------
// disable task stack checker extension
#define CONFIGURE_STACK_CHECKER_ENABLED

// ----------------------------------------------------------------------------
#include <rtems/confdefs.h>

// Add Timer and UART Driver
#define CONFIGURE_DRIVER_AMBAPP_GAISLER_GPTIMER
#define CONFIGURE_DRIVER_AMBAPP_GAISLER_APBUART

#include <drvmgr/drvmgr_confdefs.h>

#endif /* IT_OFFICE_MODEL_SYSTEM_H_ */
