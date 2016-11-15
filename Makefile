MODULE_NAME := hid_udraw_ps3_standalone

MODULE_FILENAME := hid-udraw-ps3

LINUXINCLUDE := -I$(PWD)/include $(LINUXINCLUDE)

$(MODULE_NAME)-y	:= $(MODULE_FILENAME).o

obj-m			+= $(MODULE_NAME).o

KDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)
default:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules

install: $(MODULE_NAME).ko $(MODULE_NAME).mod.c
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) modules_install

uninstall:
	/bin/bash restore.sh $(MODULE_NAME)

clean:
	$(MAKE) -C $(KDIR) SUBDIRS=$(PWD) clean

