include ../include.mk

COMPONENT	:= char_queue.o
DRIVER		:= serial.x
.PHONY: all clean

.PRECIOUS: %.b %.b.c

%.x: %.b.c
	$(CC) $(CFLAGS) -c -o $@ $<

%.b.c: %.b
	$(tools_dir)/bintoc -f $< -o $@ -p user

%.b: %.o $(USERLIB) $(COMPONENT)
	$(LD) -o $@ $(LDFLAGS) -T ../driver.lds $^

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

%.o: %.S
	$(CC) $(CFLAGS) $(INCLUDES) -c -o $@ $<

%.o: lib.h

all: $(DRIVER)

clean:
	rm -rf *~ *.o *.b.c *.x *.b
