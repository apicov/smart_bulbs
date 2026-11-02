#!/bin/sh
# Usage (on the server, as root): ./add-bulb.sh <bulb-id>
# Creates/updates an MQTT user for a bulb with a random password and prints it.
# Put the password in the bulb's firmware/sdkconfig.defaults.local.
set -eu
ID="${1:?usage: add-bulb.sh <bulb-id>}"
case "$ID" in *[!a-z0-9-]*|"") echo "bulb id: lowercase letters, digits, '-' only" >&2; exit 1 ;; esac
PW="$(head -c 18 /dev/urandom | base64 | tr -d '/+=')"
mosquitto_passwd -b /etc/mosquitto/passwd "$ID" "$PW"
chown mosquitto:mosquitto /etc/mosquitto/passwd && chmod 600 /etc/mosquitto/passwd
systemctl reload mosquitto 2>/dev/null || systemctl restart mosquitto
echo "CONFIG_BULB_ID=\"$ID\""
echo "CONFIG_BULB_MQTT_PASSWORD=\"$PW\""
