#!/bin/sh
# Local dev broker in podman: mqtt://localhost:1883 and ws://localhost:9001
# Users: app/app, plus test bulbs living-room/bulb and kitchen/bulb.
set -eu
cd "$(dirname "$0")"
cp ../acl ./acl
IMG=docker.io/library/eclipse-mosquitto:2
if [ ! -f passwd ]; then
  : > passwd
  for u in app:app living-room:bulb kitchen:bulb; do
    podman run --rm -v "$PWD":/w:Z "$IMG" mosquitto_passwd -b /w/passwd "${u%%:*}" "${u#*:}"
  done
fi
chmod 0700 passwd acl 2>/dev/null || true
mkdir -p data
exec podman run --rm --name pico-mosquitto -p 1883:1883 -p 9001:9001 \
  -v "$PWD":/mosquitto/config:Z -v "$PWD/data":/mosquitto/data:Z "$IMG"
