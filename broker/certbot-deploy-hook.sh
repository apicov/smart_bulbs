#!/bin/sh
# Install as /etc/letsencrypt/renewal-hooks/deploy/mosquitto.sh (chmod +x).
# certbot runs it after every successful issue/renewal: copy the certs where
# the mosquitto user can read them and restart the broker to load them.
set -eu
DOMAIN="${MQTT_DOMAIN:-mqtt.example.com}"   # <-- set your broker hostname
SRC="/etc/letsencrypt/live/$DOMAIN"
DST="/etc/mosquitto/certs"

case "${RENEWED_DOMAINS:-$DOMAIN}" in *"$DOMAIN"*) ;; *) exit 0 ;; esac

install -d -m 750 -o root -g mosquitto "$DST"
install -m 640 -o root -g mosquitto "$SRC/fullchain.pem" "$DST/fullchain.pem"
install -m 640 -o root -g mosquitto "$SRC/privkey.pem"   "$DST/privkey.pem"
restorecon -R "$DST" 2>/dev/null || true
systemctl restart mosquitto
