#!/bin/sh
if [ -f btn_count.ko ]; then
  sudo mkdir -p /lib/modules/$(uname -r)/kernel/drivers/misc/btn_count/
  sudo cp btn_count.ko /lib/modules/$(uname -r)/kernel/drivers/misc/btn_count/
  sudo depmod -a
else
  echo 'btn_count.ko not found!'
fi
