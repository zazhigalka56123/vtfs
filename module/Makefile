obj-m += vtfs.o
vtfs-objs := vtfs_main.o inode_ops.o dir_ops.o storage.o file_ops.o http.o

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

.PHONY: all clean
