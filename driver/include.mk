user_dir    := ../../user
tools_dir   := ../../tools
INCLUDES    := -I../../include -I$(user_dir)/include

USERLIB              := entry.o \
						syscall_wrap.o \
						debugf.o \
						libos.o \
						fork.o \
						syscall_lib.o \
						ipc.o

USERLIB 	:= $(addprefix lib/, $(USERLIB))
USERLIB     := $(addprefix $(user_dir)/, $(USERLIB)) $(wildcard ../../lib/*.o)
USERAPPS    := $(addprefix $(user_dir)/, $(USERAPPS))

.PRECIOUS: %.b %.b.c

%.x: %.b.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.b.c: %.b
	$(tools_dir)/bintoc -f $< -o $@ -p user

%.b: %.o $(USERLIB)
	$(LD) -o $@ $(LDFLAGS) -T ../driver.lds $^

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

%.o: %.S
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

%.o: lib.h