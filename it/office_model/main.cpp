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
	//(void) rtems_task_argument;
	printf("WORSTBRART\n");
	paffs::Paffs fs;

}
