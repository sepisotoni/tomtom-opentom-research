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
# The weather companion writes the current GPS-location UTC offset in minutes.
timezone_offset_file="$DIST/etc/weather_timezone_offset_minutes"
if [ -r "$timezone_offset_file" ]; then
	timezone_offset_minutes=`cat "$timezone_offset_file" 2>/dev/null`
	case "$timezone_offset_minutes" in
		""|"-"|*[!0-9-]*|-*-*)
			timezone_offset_minutes=""
			;;
	esac
	if [ -n "$timezone_offset_minutes" ]; then
		case "$timezone_offset_minutes" in
			-*)
				sign="+"
				absolute_offset=${timezone_offset_minutes#-}
				;;
			*)
				sign="-"
				absolute_offset=$timezone_offset_minutes
				;;
		esac
		case "$absolute_offset" in
			""|*[!0-9]*)
				absolute_offset=""
				;;
		esac
		if [ -n "$absolute_offset" ] && [ "$absolute_offset" -le 840 ]; then
			hours=`expr "$absolute_offset" / 60`
			minutes=`expr "$absolute_offset" % 60`
			if [ "$minutes" -lt 10 ]; then
				minutes="0$minutes"
			fi
			export TZ="TTM$sign$hours:$minutes"
		fi
	fi
fi
if [ -z "$TZ" ] && [ -f /usr/share/zoneinfo/Europe/Paris ]; then
	export TZ="Europe/Paris"
	[ ! -e /etc/localtime ] && ln -sf /usr/share/zoneinfo/Europe/Paris /etc/localtime
elif [ -z "$TZ" ] && [ -f "$DIST/usr/share/zoneinfo/Europe/Paris" ]; then
	export TZ="Europe/Paris"
	[ ! -e /etc/localtime ] && ln -sf "$DIST/usr/share/zoneinfo/Europe/Paris" /etc/localtime
else
	if [ -z "$TZ" ]; then
		export TZ='CET-1CEST,M3.5.0/2,M10.5.0/3'
	fi
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

# Keep the USB-only control service available for Face Studio.
if [ -x "$DIST/bin/tomtom-control" ] &&
	! pidof tt-control >/dev/null 2>&1; then
	"$DIST/bin/tomtom-control" >>"$DIST/logs/tomtom-control.log" 2>&1 &
fi
# Start the Global Locate daemon for devices with the integrated GPS receiver.
if [ -r /proc/barcelona/gldetected ] &&
	[ "`cat /proc/barcelona/gldetected`" = "1" ] &&
	[ -x "$DIST/bin/gltt" ] &&
	! pidof gltt >/dev/null 2>&1; then
	rc.gltt start 115200 >> "$DIST/logs/gps-start.log" 2>&1
fi

if [ -x "$DIST/bin/weather-sync" ] &&
	! pidof weather-sync >/dev/null 2>&1; then
	"$DIST/bin/weather-sync" >>"$DIST/logs/weather-sync.log" 2>&1 &
fi

# Suspend when the power button is pressed or the battery is low
power_button -b bin/suspend bin/suspend &

while /bin/true
do
	sleep 1
	pidof nano-X || {
		nice -n -10 nano-X &
		nanowm &
		sleep 2
	}
	if ! pidof nxmenu >/dev/null 2>&1; then
		nxmenu $DIST/etc/nxmenu.cfg >$DIST/logs/nxmenu.log 2>&1 &
	fi
	sleep 1
	if [ -x "$DIST/bin/tomtom-control" ] &&
		! pidof tt-control >/dev/null 2>&1; then
		"$DIST/bin/tomtom-control" >>"$DIST/logs/tomtom-control.log" 2>&1 &
	fi
	if [ -x "$DIST/bin/weather-sync" ] &&
		! pidof weather-sync >/dev/null 2>&1; then
		"$DIST/bin/weather-sync" >>"$DIST/logs/weather-sync.log" 2>&1 &
	fi
	if ! pidof watchface.new >/dev/null 2>&1; then
		"$DIST/bin/watchface-main" >>"$DIST/logs/watchface.log" 2>&1 &
	fi
	sleep 5
done
