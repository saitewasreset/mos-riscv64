include ../include.mk

COMPONENTS		:= hello.x

.PHONY: all clean

all: $(COMPONENTS)

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

clean:
	rm -rf *~ *.o *.b.c *.x *.b
