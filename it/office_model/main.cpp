/*
 * main.cpp
 *
 *  Created on: Mar 7, 2017
 *      Author: Pascal Pieper
 */

extern "C"
{
#include <sys/time.h>
#include "system.h"
}
#include <gpio.h>
#include <sevensegment.h>
#include <paffs/paffs.hpp>

using namespace outpost::nexys3;
using namespace paffs;

rtems_task
task_system_init(rtems_task_argument)
{
	//setvbuf(stdout, NULL, _IONBF, 0);
	printf("Build: " __DATE__ " " __TIME__ "\n");
	rtems_stack_checker_report_usage();

	SevenSegment::clear();
	SevenSegment::write(0, 'P');
	SevenSegment::write(1, 'A');
	SevenSegment::write(2, 'F');
	SevenSegment::write(3, 'S');

	uint8_t leds = 0;
	for (uint_fast8_t i = 0; i < 10; ++i)
	{
		leds >>= 1;
		leds |= 0b10000000;
		Gpio::set(leds);
		rtems_task_wake_after(100);
	}

	printf("Before initing FS\n");
	std::vector<paffs::Driver*> drv;
	drv.push_back(paffs::getDriver(0));
	Paffs *fs = new Paffs(drv);
	fs->setTraceMask(PAFFS_TRACE_ALL);

	rtems_stack_checker_report_usage();

	printf("Trying to mount FS...\n");
	Result r = fs->mount();
	printf("\t %s\n", err_msg(r));

	rtems_stack_checker_report_usage();


	while(1){
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
