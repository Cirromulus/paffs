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
#include <paffs/paffs.hpp>

rtems_task
task_system_init(rtems_task_argument)
{
	printf("WORSTBRART\n");
	std::vector<paffs::Driver*> drv;
	drv.push_back(paffs::getDriver(0));
	paffs::Paffs fs(drv);
}
