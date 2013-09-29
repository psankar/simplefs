obj-m := simplefs.o
simplefs-objs := simple.o
ccflags-y := -DSIMPLEFS_DEBUG

all: ko mkfs-simplefs

ko:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

mkfs-simplefs_SOURCES:
	mkfs-simplefs.c simple.h

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm mkfs-simplefs
