OFFICE MODEL 2 integration test for PAFFS
=========================================

Test Layout
-----------

This integration test will create a small command line to interact with the filesystem PAFFS.
The implemented driver (`../src/driver/om2_artix.*`) communicates with the "first version office model" interface FPGA (OM1-IFF second port from bottom) via Spacewire Port 1 (lower Port) of the "second version office model" Leon3 (OM2 Leon). For MRAM, the Module directly connected to Leon3 processor is used.

# How to test

This test is interactive and meant to be used as a playground. You may add, delete, modify files and folders, and cut power (or just reload image). If everything is in the same state as you left, it succeeded.

# Successful tested Hardware

- OM1 Board3 with NAND 4 and OM2 S/N 1

HowTo use Debugger with GRMON on ARTIX7 Board (OM2) 
---------------------------------------------------

0. For convenience, you may install the udev rules _80-rcn___tty.rules_

1. Build
2. Start GRMON:
`/opt/grmon-eval-2.0.83/linux64/bin/grmon -uart /dev/tty_rcn0001_dsu -stack 0x40fffff0 -baud 460800 -gdb`
3. Start GDB on different console:
`sparc-rtems4.11-gdb <path-to-elf>` (e.g. `build/it_debug/it/office_model2/paffs-artix7.elf`)
4. connect GDB to GRMON, load and start program:
`target extended-remote :2222
load`
(optional: `break task_system_init`)
`run`
5. Disconnect from remote:
CTLR-C (multiple times)
y(es)
6. connect again:
`target extended-remote :2222`

(optional: see output of program in another console)
`picocom /dev/tty_rcn0001_console -b38400`

Happy debugging.


