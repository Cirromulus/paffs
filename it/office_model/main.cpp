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

#include <string.h>

using namespace outpost::nexys3;
using namespace paffs;

rtems_task
task_system_init(rtems_task_argument)
{
	//setvbuf(stdout, NULL, _IONBF, 0);
	const char* wbuf = "\nBuild: " __DATE__ " " __TIME__ "\n"
			"This is a test write. Hello world!\n";

	printf("\n\n\n\nBuild: " __DATE__ " " __TIME__ "\n");
	rtems_stack_checker_report_usage();

	ObjInfo inf;
	Obj *file;
	unsigned int br;

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

	printf("Trying to mount FS...\n");
	Result r = fs->mount();
	printf("\t %s\n", err_msg(r));

	if(r != Result::ok){
		char buf = 0;
		while(buf != 'y' && buf != 'n' && buf != 'c' && buf != '\n'){
			printf("There was no valid image found. Format?\n(y/c/N) ");
			fflush(stdout);
			scanf("%1c", &buf);
		}
		if(buf == 'y' || buf == 'c'){
			printf("You chose yes.\n");
			r = fs->format(buf == 'c');
			if(r == Result::ok){
				printf("Success.\n");
			}else{
				printf("%s!\n", err_msg(r));
				goto idle;
			}
		}else{
			printf("You chose no. \n");
			goto idle;
		}

		printf("Trying to mount FS again ...\n");
		r = fs->mount();
		printf("\t %s\n", err_msg(r));
		if(r != Result::ok)
			goto idle;
	}

	file = fs->open("/test.txt", FR | FW | FE);	//open read/write and only existing
	if(file == nullptr){
		if(fs->getLastErr() == Result::nf){
			fs->resetLastErr();
			printf("File not found, creating new...\n");
			r = fs->touch("/test.txt");
			if(r != Result::ok){
				printf("Touch error: %s\n", err_msg(r));
				goto idle;
			}
			printf("Trying to reopen file.../n");
			file = fs->open("/test.txt", FR | FE);
			if(file == nullptr || fs->getLastErr() != Result::ok){
				printf("Error opening file: %s\n", err_msg(fs->getLastErr()));
				goto idle;
			}
		}else{
			printf("Error opening file: %s\n", err_msg(fs->getLastErr()));
			goto idle;
		}
	}

	printf("File was found.\n");
	fs->getObjInfo("/test.txt", &inf);
	printf("Size: %lu Byte\n", inf.size);

	if(inf.size != 0){
		printf("Reading contents of file:\n");
		char rbuf[inf.size];
		r = fs->read(file, rbuf, inf.size, &br);
		if(r != Result::ok){
			printf("Error reading file: %s\n", err_msg(r));
			goto idle;
		}
		if(br != inf.size){
			printf("Error reading file, size differs (%u)\n", br);
			goto idle;
		}
		printf("----------\n%s\n----------\n", rbuf);
	}


	r = fs->write(file, wbuf, strlen(wbuf), &br);
	if(r != Result::ok){
		printf("Error writing file: %s\n", err_msg(r));
		goto idle;
	}else{
		printf("ok.\n");
	}
	r = fs->unmount();
	printf("Unmount: %s\n", err_msg(r));

idle:
	printf("Now idling.\n");
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
