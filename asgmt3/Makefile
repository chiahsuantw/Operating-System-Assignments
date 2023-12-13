obj-m += kfetch_mod.o
kfetch_mod-objs += kfetch_mod_312551014.o

ccflags-y := -w

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean