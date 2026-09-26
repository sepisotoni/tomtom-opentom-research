#! /bin/sh

export DIST=/mnt/sdcard/opentom
export HOME=$DIST

export FRAMEBUFFER=/dev/fb

export TSLIB_CONSOLEDEVICE=none
export TSLIB_FBDEVICE=/dev/fb
export TSLIB_TSDEVICE=/dev/input/event0
export TSLIB_CONFFILE=$DIST/etc/ts.conf
export TSLIB_PLUGINDIR=$DIST/lib/ts
export TSLIB_CALIBFILE=$DIST/etc/pointercal

export PATH=$PATH:$DIST/bin
export LD_LIBRARY_PATH=$DIST/lib
# Europe/Paris timezone with full daylight saving rules:
# Standard time is CET (UTC+1, denoted CET-1 in POSIX), Daylight time is CEST (UTC+2).
# Transition to CEST on last Sunday of March at 02:00 local time (M3.5.0/2).
# Transition back to CET on last Sunday of October at 03:00 local time (M10.5.0/3).
if [ -f /usr/share/zoneinfo/Europe/Paris ]; then
	export TZ="Europe/Paris"
	[ ! -e /etc/localtime ] && ln -sf /usr/share/zoneinfo/Europe/Paris /etc/localtime
elif [ -f $DIST/usr/share/zoneinfo/Europe/Paris ]; then
	export TZ="Europe/Paris"
	[ ! -e /etc/localtime ] && ln -sf $DIST/usr/share/zoneinfo/Europe/Paris /etc/localtime
else
	export TZ='CET-1CEST,M3.5.0/2,M10.5.0/3'
fi

ln -s $DIST/lib/libz.so.1 /lib/libz.so

if [ ! -e /etc/profile ]; then ln -s $DIST/etc/profile /etc/profile; fi

echo "Disabling BT"
stop_bt -s

ifconfig usb0 192.168.101.115 netmask 255.255.255.0 up
ifconfig lo 127.0.0.1 up

cd /dev
ln -s fb fb0
export NANOX_YRES=`fbset -s | grep geometry | if read x x yres x; then echo $yres; fi`

cd $DIST

# Suspend when the power button is pressed or the battery is low
power_button -b bin/suspend bin/suspend &

while /bin/true
do
	sleep 1
	pidof nano-X || { 
		nice -n -10 nano-X &
		nanowm &
	}
	sleep 1
	nxmenu $DIST/etc/nxmenu.cfg >$DIST/logs/nxmenu.log 2>&1
done
