cmd_/home/user/ics2023/nemu/build/obj-riscv32-nemu-interpreter/src/monitor/sdb/sdb.o := unused

source_/home/user/ics2023/nemu/build/obj-riscv32-nemu-interpreter/src/monitor/sdb/sdb.o := src/monitor/sdb/sdb.c

deps_/home/user/ics2023/nemu/build/obj-riscv32-nemu-interpreter/src/monitor/sdb/sdb.o := \
    $(wildcard include/config/device.h) \
  /home/user/ics2023/nemu/include/isa.h \
  /home/user/ics2023/nemu/src/isa/riscv32/include/isa-def.h \
    $(wildcard include/config/rve.h) \
    $(wildcard include/config/rv64.h) \
  /home/user/ics2023/nemu/include/common.h \
    $(wildcard include/config/target/am.h) \
    $(wildcard include/config/mbase.h) \
    $(wildcard include/config/msize.h) \
    $(wildcard include/config/isa64.h) \
  /home/user/ics2023/nemu/include/macro.h \
  /home/user/ics2023/nemu/include/debug.h \
  /home/user/ics2023/nemu/include/utils.h \
    $(wildcard include/config/target/native/elf.h) \
  /home/user/ics2023/nemu/include/cpu/cpu.h \
  src/monitor/sdb/sdb.h \

/home/user/ics2023/nemu/build/obj-riscv32-nemu-interpreter/src/monitor/sdb/sdb.o: $(deps_/home/user/ics2023/nemu/build/obj-riscv32-nemu-interpreter/src/monitor/sdb/sdb.o)

$(deps_/home/user/ics2023/nemu/build/obj-riscv32-nemu-interpreter/src/monitor/sdb/sdb.o):
