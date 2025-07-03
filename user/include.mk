lab-ge = $(shell [ "$$(echo $(lab)_ | cut -f1 -d_)" -ge $(1) ] && echo true)

INITAPPS             := tltest.x fktest.x pingpong.x serialtest.x processtest.x virtiotest.x

USERLIB              := entry.o \
			syscall_wrap.o \
			debugf.o \
			libos.o \
			fork.o \
			syscall_lib.o \
			ipc.o \
			user_interrupt.o \
			user_interrupt_wrap.o \
			serial.o \
			process.o \
			virtio.o \
			pageref.o \
			file.o \
			fsipc.o \
			fd.o \
			console.o \
			pipe.o \
			fprintf.o


USERLIB := $(addprefix lib/, $(USERLIB)) $(wildcard ../lib/*.o)
