/*
 * fatal_error_handler.cpp
 *
 *  Created on: Mar 7, 2017
 *      Author: Pascal Pieper
 */

extern "C"{
#include <rtems.h>
// For device driver prototypes
#include <bsp.h>
#include <outpost/rtos/failure_handler.h>

}

extern "C" void
fatalErrorHandler(Internal_errors_Source source,
                  bool isInternal,
                  uint32_t errorCode);

static void
printRtosResource(uint16_t resource)
{
    switch (resource)
    {
        case outpost::rtos::Resource::other: printk("Other"); break;
        case outpost::rtos::Resource::thread: printk("Thread"); break;
        case outpost::rtos::Resource::timer: printk("Timer"); break;
        case outpost::rtos::Resource::semaphore: printk("Semaphore"); break;
        case outpost::rtos::Resource::mutex: printk("Mutex"); break;
        case outpost::rtos::Resource::interrupt: printk("Interrupt"); break;
        case outpost::rtos::Resource::messageQueue: printk("Message Queue"); break;
        case outpost::rtos::Resource::clock: printk("Clock"); break;
        case outpost::rtos::Resource::periodicTask: printk("Periodic Task"); break;
        case outpost::rtos::Resource::driverManager: printk("Driver Manager"); break;
        default:
            printk("Unknown");
            break;
    }
}

static void
printRtosError(uint32_t error)
{
    uint16_t cause = (error & 0x0FFF0000) >> 16;
    switch (cause)
    {
        case 1:
            printk("Resource allocation failed for ");
            printRtosResource(error & 0xFFFF);
            printk("\n");
            break;

        case 2:
            printk("Return from thread\n");
            break;

        case 3:
            printk("Generic runtime error for ");
            printRtosResource(error & 0xFFFF);
            printk("\n");
            break;

        default:
            printk("Unknown cause\n");
            break;
    }
}


static void
printHwError(uint32_t error)
{
    printk("HW Error: ");

    uint16_t cause = (error & 0x0FFF0000) >> 16;
    switch (cause)
    {
        case 1:
            printk("Telemetry Encoder start failed\n");
            break;

        default:
            printk("Unknown cause\n");
            break;
    }

}

extern "C" void
fatalErrorHandler(Internal_errors_Source source,
                  bool isInternal,
                  uint32_t errorCode)
{
    printk("\n\n" \
           "----------------------------------------\n");
    printk("Fatal Error: ");
    switch (source)
    {
        case INTERNAL_ERROR_RTEMS_API:
            printk("RTEMS API (0x%08X)\n\n", errorCode);
            if ((errorCode & 0xF0000000) == 0xF0000000)
            {
                printRtosError(errorCode);
            }
            else if ((errorCode & 0xF0000000) == 0x10000000)
            {
                printHwError(errorCode);
            }
            else
            {
                printk("Unknown Error!\n");
            }

            break;

        case INTERNAL_ERROR_CORE:
        case INTERNAL_ERROR_POSIX_API:
        case INTERNAL_ERROR_ITRON_API:
            printk("Other\n\n");
            printk("Source: %i, %i\n", source, isInternal);
            printk("Code  : 0x%08X\n", errorCode);
            break;
    }
    printk("----------------------------------------\n\n");

    while (1)
    {
    }
}
