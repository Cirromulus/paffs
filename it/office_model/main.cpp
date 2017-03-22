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

rtems_task
task_system_init(rtems_task_argument)
{
	SevenSegment::clear();
	SevenSegment::write(0, 'P');
	SevenSegment::write(1, 'A');
	SevenSegment::write(2, 'F');
	SevenSegment::write(3, 'S');

	uint8_t leds = 0;
	for (uint_fast8_t i = 0; i < 12; ++i)
	{
		leds <<= 1;

		if (i <= 3)
		{
			leds |= 1;
		}
		Gpio::set(leds);
		rtems_task_wake_after(50);
	}

	printf("WORSTBRART\n");
	std::vector<paffs::Driver*> drv;
	drv.push_back(paffs::getDriver(0));
	//paffs::Paffs fs(drv);

	while(1){
		leds = 0;
		for (uint_fast8_t i = 0; i < 12; ++i)
		{
			leds <<= 1;

			if (i <= 3)
			{
				leds |= 1;
			}
			Gpio::set(leds);
			rtems_task_wake_after(50);
		}
	}
}
