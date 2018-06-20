HowTo use Debugger with GRMON on NEXYS3 Board 
=============================================

1. Build
2. Start GRMON:
`/opt/grmon-eval-2.0.83/linux64/bin/grmon -uart /dev/tty_rcn0001_dsu -stack 0x40fffff0 -baud 460800 -gdb`
3. Start GDB on different console:
`sparc-rtems4.11-gdb <path-to-elf>` (e.g. `build/it_debug/it/office_model2/office.elf`)
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


