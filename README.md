# Description
A simple driver which counts how many times a Tactile Button switch being pressed.

### Load Driver
After ```make``` and ```./install.sh```, load driver by

```sudo modprobe btn_count gpio_pin=XX```,

XX is the gpio number where the button is connected.

### Operations
```root@beaglebone:~# echo start > /dev/btn_count``` to start counting

```root@beaglebone:~# echo stop > /dev/btn_count``` to stop counting

```root@beaglebone:~# cat /dev/btn_count``` to read current counting; -1 means the driver is in stop state.

### Note
This driver is developed and tested on beaglebone black with Debian 8 and linux kernel 4.1.13-ti-r37.
