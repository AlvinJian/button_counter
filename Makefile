obj-m += btn_count.o

all:
	make -C /lib/modules/$(shell uname -r)/build/ M=$(PWD) ARCH=arm modules

clean:
	make -C /lib/modules/$(shell uname -r)/build/ M=$(PWD) clean
