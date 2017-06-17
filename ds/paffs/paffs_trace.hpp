/*
 * paffs_trace.h
 *
 *  Created on: 10.07.2016
 *      Author: urinator
 */

#ifndef DS_PAFFS_PAFFS_TRACE_H_
#define DS_PAFFS_PAFFS_TRACE_H_

namespace paffs{
	extern unsigned int traceMask;
}

#include <string.h>
#include <stdio.h>
#include <signal.h>

#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

#define PAFFS_DBG(mask, msg, ...) do {\
		if(mask & traceMask || mask & PAFFS_TRACE_ALWAYS){\
			fprintf(stderr, "paffs: " msg "\n\t-line %d, file %s\n", ##__VA_ARGS__,  __LINE__, __FILENAME__);\
			if(mask & PAFFS_TRACE_BUG)\
				raise(SIGINT);\
		}\
	} while(0)

#define PAFFS_DBG_S(mask, msg, ...) do {\
		if(mask & traceMask || mask & PAFFS_TRACE_ALWAYS){\
			fprintf(stderr, "paffs: " msg "\n", ##__VA_ARGS__);\
		}\
	} while(0)

#define PAFFS_TRACE_INFO		0x00000001
#define PAFFS_TRACE_OS			0x00000002
#define PAFFS_TRACE_ALLOCATE	0x00000004
#define PAFFS_TRACE_SCAN		0x00000008
#define PAFFS_TRACE_BAD_BLOCKS	0x00000010
#define PAFFS_TRACE_ERASE		0x00000020
#define PAFFS_TRACE_GC			0x00000040
#define PAFFS_TRACE_WRITE		0x00000080
#define PAFFS_TRACE_TRACING		0x00000100
#define PAFFS_TRACE_DELETION	0x00000200
#define PAFFS_TRACE_BUFFERS		0x00000400
#define PAFFS_TRACE_NANDACCESS	0x00000800
#define PAFFS_TRACE_GC_DETAIL	0x00001000
#define PAFFS_TRACE_SCAN_DEBUG	0x00002000
#define PAFFS_TRACE_AREA		0x00004000
#define PAFFS_TRACE_PACACHE		0x00008000

#define PAFFS_TRACE_VERIFY_TC	0x00010000
#define PAFFS_TRACE_VERIFY_NAND	0x00020000
#define PAFFS_TRACE_VERIFY_AS	0x00040000
#define PAFFS_WRITE_VERIFY_AS	0x00080000 //only debug hack, may collide with ECC
#define PAFFS_TRACE_VERIFY_ALL	0x00070000

#define PAFFS_TRACE_SUPERBLOCK	0x00100000
#define PAFFS_TRACE_TREECACHE	0x00200000
#define PAFFS_TRACE_TREE		0x00400000
#define PAFFS_TRACE_MOUNT		0x00800000
#define PAFFS_TRACE_ASCACHE		0x01000000
#define PAFFS_TRACE_VERBOSE 	0x02000000
#define PAFFS_TRACE_READ		0x04000000

#define PAFFS_TRACE_ERROR		0x40000000
#define PAFFS_TRACE_BUG			0x80000000
#define PAFFS_TRACE_ALWAYS		0xf0000000
#define PAFFS_TRACE_ALL			0xfff7ffff
#define PAFFS_TRACE_SOME		0xC0050071




#endif /* DS_PAFFS_PAFFS_TRACE_H_ */
