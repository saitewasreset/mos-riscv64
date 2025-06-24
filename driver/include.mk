user_dir    := ../../user
tools_dir   := ../../tools
INCLUDES    := -I../../include -I$(user_dir)/include

USERLIB              := entry.o \
			syscall_wrap.o \
			debugf.o \
			libos.o \
			fork.o \
			syscall_lib.o \
			ipc.o \
			user_interrupt.o \
			user_interrupt_wrap.o

USERLIB 	:= $(addprefix lib/, $(USERLIB))
USERLIB     := $(addprefix $(user_dir)/, $(USERLIB)) $(wildcard ../../lib/*.o)
USERAPPS    := $(addprefix $(user_dir)/, $(USERAPPS))