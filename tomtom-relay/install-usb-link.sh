#!/bin/sh
set -eu

NETWORK_FILE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/usb-link.network
INTERFACE=${1:-}

if [ -z "$INTERFACE" ] || [ ! -d "/sys/class/net/$INTERFACE" ]; then
	echo "Usage: $0 <TomTom USB Ethernet interface>" >&2
	exit 2
fi

PROPERTIES=$(udevadm info --query=property --path="/sys/class/net/$INTERFACE")
printf '%s\n' "$PROPERTIES" | grep -Fxq \
	'ID_VENDOR=Linux_2.6.13-LeddaZ_s3c24xx_udc' || {
	echo "$INTERFACE is not the TomTom USB Ethernet gadget." >&2
	exit 1
}
PATH_ID=$(printf '%s\n' "$PROPERTIES" | sed -n 's/^ID_PATH=//p')
EXPECTED_PATH=$(sed -n 's/^Path=//p' "$NETWORK_FILE")
if [ -z "$PATH_ID" ] || [ "$PATH_ID" != "$EXPECTED_PATH" ]; then
	echo "USB path differs from the reviewed profile ($PATH_ID)." >&2
	echo "Review $NETWORK_FILE before installing it." >&2
	exit 1
fi

sudo install -D -m 0644 "$NETWORK_FILE" \
	/etc/systemd/network/20-tomtom-usb.network
sudo networkctl reload
sudo networkctl reconfigure "$INTERFACE"

ip -brief address show dev "$INTERFACE"
ip route get 192.168.101.115
