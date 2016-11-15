# THQ uDraw Game Tablet for PS3 Linux driver

After compiling with `make` and `make install` (the latter as root or through `sudo`),
you'll need to create a few files to load the driver correctly.

Create a `/etc/udev/load_hid_udraw.sh` file with the content:
```
#!/bin/bash
DRIVER=$1
DEVICE=$2
HID_DRV_PATH=/sys/bus/hid/drivers
/sbin/modprobe hid_udraw_ps3_standalone
echo ${DEVICE} > ${HID_DRV_PATH}/hid-generic/unbind
echo ${DEVICE} > ${HID_DRV_PATH}/hid-udraw-ps3/bind
```

Create a `/etc/udev/rules.d/80-udraw.rules` file with the content:
```
DRIVER=="hid-generic", ENV{MODALIAS}=="hid:b0003g*v000020D6p0000CB17", RUN+="/bin/sh /etc/udev/load_hid_udraw.sh hid-generic %k"
```

After a reboot, the driver should be loaded automatically when the dongle gets plugged in.
