#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
RELAY_DIR="$ROOT/tomtom-relay"
BIN_DIR="$HOME/.local/bin"
UNIT_DIR="$HOME/.config/systemd/user"
CONFIG_DIR="$HOME/.config/tomtom-relay"
STORAGE_DIR="$HOME/TomTomStorage"

command -v cargo >/dev/null 2>&1 || {
	echo "Rust/Cargo is required to build tomtom-relay." >&2
	exit 1
}

cargo build --release --manifest-path "$RELAY_DIR/Cargo.toml"
mkdir -p "$BIN_DIR" "$UNIT_DIR" "$CONFIG_DIR" "$STORAGE_DIR"
chmod 0700 "$STORAGE_DIR" "$CONFIG_DIR"
if [ ! -e "$CONFIG_DIR/environment" ]; then
	install -m 0600 /dev/null "$CONFIG_DIR/environment"
fi
install -m 0755 "$RELAY_DIR/target/release/tomtom-relay" \
	"$BIN_DIR/tomtom-relay"
install -m 0644 "$RELAY_DIR/tomtom-relay.service" \
	"$UNIT_DIR/tomtom-relay.service"
systemctl --user daemon-reload
systemctl --user enable tomtom-relay.service
systemctl --user restart tomtom-relay.service

printf 'TomTom relay installed and enabled. Storage: %s\n' "$STORAGE_DIR"
printf 'Service state: '
systemctl --user is-active tomtom-relay.service
