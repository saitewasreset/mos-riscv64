include drivers.mk

.PHONY: all clean $(DRIVERS)

all: $(DRIVERS)

clean: $(addsuffix .clean,$(DRIVERS))

$(DRIVERS):
	$(MAKE) -C $@ all

%.clean:
	$(MAKE) -C $(basename $@) clean

