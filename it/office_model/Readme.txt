/opt/grmon-eval-2.0.83/linux64/bin/grmon -uart /dev/cobc_dsu_3 -stack 0x40fffff0 -baud 460800 -gdb

picocom /dev/cobc_console_3 -b38400



grmon2> run
  IU in error mode (tt = 0x2B, data store error)
  0x400508e8: c2004000  ld  [%g1], %g1  <_ZN7outpost24SerializeBigEndianTraitsIhE5storeERPhh+36>
  
grmon2> bt
  
       %pc          %sp 
  #0   0x400508e8   0x4010bc50   <_ZN7outpost24SerializeBigEndianTraitsIhE5storeERPhh+0x24>
  #1   0x40050cf8   0x4010bcb0   <_ZN7outpost9Serialize5storeIhEEvT_+0x24>
  #2   0x4004ff8c   0x4010bd10   <_ZN7outpost3iff4Amap11writeHeaderENS_5SliceIhEENS0_9OperationEmj+0x48>
  #3   0x4004f3c0   0x4010bd88   <_ZN7outpost3iff4Amap5writeEmjNS_4time8DurationERNS0_12WriteHandlerE+0x134>
  #4   0x4004f258   0x4010be58   <_ZN7outpost3iff4Amap5writeEmPKhjNS_4time8DurationE+0x5c>
  #5   0x40051134   0x4010bec8   <_ZN4Nand23enableLatchUpProtectionEv+0xa0>
  #6   0x4004dd28   0x4010bf50   <_ZN5paffs23OfficeModelNexys3Driver14initializeNandEv+0x1c>
  #7   0x4000593c   0x4010bfb0   <_ZN5paffs6Device3mntEb+0x1ac>
  #8   0x40003b3c   0x4010c020   <_ZN5paffs5Paffs5mountEb+0x70>
  #9   0x40001914   0x4010c090   <task_system_init+0x1d4>
  #10  0x400b756c   0x4010c160   <_Thread_Handler+0xd0>
  #11  0x400b749c   0x4010c1c0   <_Thread_Handler+0>
