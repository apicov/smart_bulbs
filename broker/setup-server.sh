#!/usr/bin/env bash
# Pico Bulbs: one-shot, re-runnable Mosquitto setup for the Linode (Fedora + Apache).
#
# Run on the server from a clone of the repo:
#   git clone <repo-url> pico-bulbs
#   sudo bash pico-bulbs/broker/setup-server.sh
#
# Optional env (pass after sudo, e.g. sudo DOMAIN=x BULBS="a b" bash ...):
#   DOMAIN  broker hostname, must already resolve to this server (default mqtt.apicov.xyz)
#   BULBS   space-separated bulb ids to create MQTT users for (default "living-room")
#   EMAIL   only needed if certbot has no account on this server yet
#
# What it does (each step is skipped if already done):
#   1. install mosquitto
#   2. add an Apache :80 vhost for $DOMAIN serving ACME challenges from /var/www/acme
#      (same pattern as books-http.conf); existing sites are not touched
#   3. get a Let's Encrypt cert via webroot (Apache keeps running)
#   4. install mosquitto.conf + acl, create users app + $BULBS with random passwords
#   5. install the certbot deploy hook (copies certs, restarts mosquitto on renewal)
#   6. open 8883/8084 in firewalld (if active), start mosquitto
#   7. add an Apache HTTPS vhost for $DOMAIN serving firmware from /var/www/pico-firmware
#      at /firmware/, writable by the user who ran sudo; existing sites are not touched
#   8. self-test: TLS login works, anonymous is refused, the firmware URL answers
# New passwords are printed at the end and kept in /root/pico-bulbs-credentials.txt (0600).
set -euo pipefail

DOMAIN="${DOMAIN:-mqtt.apicov.xyz}"
BULBS="${BULBS:-living-room}"
HERE="$(cd "$(dirname "$0")" && pwd)"
CREDS=/root/pico-bulbs-credentials.txt
PASSWD=/etc/mosquitto/passwd
ACME_ROOT=/var/www/acme
FIRMWARE_ROOT=/var/www/pico-firmware
NEW_CREDS=()

step() { printf '\n\033[1;34m==> %s\033[0m\n' "$*"; }
ok()   { printf '    \033[32m✓\033[0m %s\n' "$*"; }
die()  { printf '\n\033[1;31m✗ %s\033[0m\n' "$*" >&2; exit 1; }
randpw() { head -c 18 /dev/urandom | base64 | tr -d '/+='; }

install_vhost() {  # install_vhost <new file> <destination>: test the config, or put the old one back
  local tmp=$1 vhost=$2 backup=""
  if cmp -s "$tmp" "$vhost"; then
    rm -f "$tmp"; ok "vhost unchanged"
    return
  fi
  [[ -f $vhost ]] && { backup="$vhost.bak.$(date +%s)"; cp -a "$vhost" "$backup"; }
  install -m 644 "$tmp" "$vhost"; rm -f "$tmp"; restorecon "$vhost" || true
  if apachectl configtest 2>&1 | grep -q 'Syntax OK'; then
    systemctl reload httpd; ok "vhost installed, httpd reloaded"
  else
    apachectl configtest || true
    if [[ -n $backup ]]; then mv -f "$backup" "$vhost"; else rm -f "$vhost"; fi
    die "Apache config test failed — reverted $vhost, httpd untouched"
  fi
}

[[ $EUID -eq 0 ]] || die "run with sudo"
for f in mosquitto.conf acl certbot-deploy-hook.sh; do
  [[ -f "$HERE/$f" ]] || die "missing $HERE/$f — run it from a clone of the repo"
done

# ---------------------------------------------------------------- preflight
step "Preflight: DNS for $DOMAIN"
resolved="$(getent ahostsv4 "$DOMAIN" | awk 'NR==1{print $1}')"
[[ -n "$resolved" ]] || die "$DOMAIN does not resolve yet — add an A record to this server's IP and retry"
ip -4 -o addr show | grep -qw "$resolved" \
  || die "$DOMAIN resolves to $resolved, which is not an address of this server"
ok "$DOMAIN -> $resolved (this server)"

# ---------------------------------------------------------------- 1. packages
step "1. Packages"
if rpm -q mosquitto >/dev/null; then ok "mosquitto already installed"
else dnf install -y mosquitto; ok "mosquitto installed"; fi
rpm -q certbot >/dev/null || dnf install -y certbot

# ---------------------------------------------------------------- 2. apache acme vhost
step "2. Apache ACME vhost for $DOMAIN"
VHOST=/etc/httpd/conf.d/mqtt-http.conf
if grep -rqsE "ServerName\s+$DOMAIN\b" /etc/httpd/conf.d/ && [[ ! -f $VHOST ]]; then
  ok "an existing vhost already serves $DOMAIN — leaving Apache alone"
else
  tmp="$(mktemp)"
  cat >"$tmp" <<EOF
# Pico Bulbs MQTT broker: only used for Let's Encrypt HTTP-01 challenges.
# MQTT itself runs in mosquitto on 8883 (mqtts) and 8084 (wss).
<VirtualHost *:80>
    ServerName $DOMAIN
    Alias /.well-known/acme-challenge/ $ACME_ROOT/.well-known/acme-challenge/
    <Directory $ACME_ROOT>
        Require all granted
    </Directory>
    RewriteEngine on
    RewriteCond %{REQUEST_URI} !^/\.well-known/acme-challenge/
    RewriteRule ^ - [R=404,L]
</VirtualHost>
EOF
  install_vhost "$tmp" "$VHOST"
fi
install -d -m 755 "$ACME_ROOT/.well-known/acme-challenge"

# ---------------------------------------------------------------- 3. certificate
step "3. Let's Encrypt certificate"
if [[ -f /etc/letsencrypt/live/$DOMAIN/fullchain.pem ]]; then
  ok "certificate exists (renewed automatically by certbot-renew.timer)"
else
  args=(certonly --webroot -w "$ACME_ROOT" -d "$DOMAIN" --non-interactive --agree-tos --keep-until-expiring)
  if [[ -n "${EMAIL:-}" ]]; then args+=(--email "$EMAIL"); fi
  certbot "${args[@]}"
  ok "certificate issued"
fi

# ---------------------------------------------------------------- 4. config + users
step "4. Mosquitto config, ACL, users"
for f in mosquitto.conf acl; do
  dst=/etc/mosquitto/$f
  if [[ -f $dst ]] && ! cmp -s "$HERE/$f" "$dst"; then
    cp -a "$dst" "$dst.bak.$(date +%s)"; ok "backed up previous $dst"
  fi
done
install -m 644 -o root -g root "$HERE/mosquitto.conf" /etc/mosquitto/mosquitto.conf
install -m 600 -o mosquitto -g mosquitto "$HERE/acl" /etc/mosquitto/acl
install -d -m 750 -o mosquitto -g mosquitto /var/lib/mosquitto
[[ -f $PASSWD ]] || install -m 600 -o mosquitto -g mosquitto /dev/null "$PASSWD"

add_user() {  # create user only if missing, remember its new password
  local u=$1 pw
  if grep -q "^$u:" "$PASSWD"; then ok "user $u exists (password unchanged)"; return; fi
  pw="$(randpw)"
  mosquitto_passwd -b "$PASSWD" "$u" "$pw"
  NEW_CREDS+=("$u  $pw")
  ok "user $u created"
}
add_user app
for b in $BULBS; do
  [[ $b =~ ^[a-z0-9-]+$ ]] || die "bulb id '$b': use lowercase letters, digits, '-'"
  add_user "$b"
done
chown mosquitto:mosquitto "$PASSWD"; chmod 600 "$PASSWD"   # mosquitto_passwd may reset these
restorecon -R /etc/mosquitto /var/lib/mosquitto 2>/dev/null || true

# ---------------------------------------------------------------- 5. cert hook
step "5. Certbot deploy hook"
HOOK=/etc/letsencrypt/renewal-hooks/deploy/mosquitto.sh
install -d /etc/letsencrypt/renewal-hooks/deploy
sed "s/mqtt\.example\.com/$DOMAIN/" "$HERE/certbot-deploy-hook.sh" >"$HOOK.tmp"
install -m 755 "$HOOK.tmp" "$HOOK"; rm -f "$HOOK.tmp"
# First run copies the certs into /etc/mosquitto/certs and (re)starts mosquitto
RENEWED_DOMAINS="$DOMAIN" "$HOOK" || {
  journalctl -u mosquitto -n 30 --no-pager
  ausearch -m avc -ts recent 2>/dev/null | grep -i mosquitto || true
  die "mosquitto failed to start — see log above"
}
ok "hook installed, certs in /etc/mosquitto/certs"

# ---------------------------------------------------------------- 6. firewall + service
step "6. Firewall and service"
if systemctl is-active -q firewalld; then
  for p in 8883 8084; do firewall-cmd -q --permanent --add-port=$p/tcp; done
  firewall-cmd -q --reload
  ok "firewalld: 8883/tcp, 8084/tcp open"
else
  ok "firewalld not active — nothing to open (check a Linode Cloud Firewall if you use one)"
fi
systemctl enable -q mosquitto
systemctl is-active -q mosquitto || die "mosquitto is not running"
ok "mosquitto enabled and running"

# ---------------------------------------------------------------- 7. firmware vhost
step "7. Apache HTTPS vhost for firmware"
rpm -q mod_ssl >/dev/null || dnf install -y mod_ssl
# The owner uploads with tools/publish_firmware.py, without needing sudo.
install -d -m 755 -o "${SUDO_USER:-root}" "$FIRMWARE_ROOT"
restorecon -R "$FIRMWARE_ROOT" 2>/dev/null || true
tmp="$(mktemp)"
cat >"$tmp" <<EOF
# Pico Bulbs firmware. The image holds no secrets, and only a signed one will install.
<VirtualHost *:443>
    ServerName $DOMAIN
    SSLEngine on
    SSLCertificateFile /etc/letsencrypt/live/$DOMAIN/fullchain.pem
    SSLCertificateKeyFile /etc/letsencrypt/live/$DOMAIN/privkey.pem
    Alias /firmware/ $FIRMWARE_ROOT/
    <Directory $FIRMWARE_ROOT>
        Options -Indexes
        Require all granted
    </Directory>
    RewriteEngine on
    RewriteRule ^/(?!firmware/) - [R=404,L]
</VirtualHost>
EOF
install_vhost "$tmp" /etc/httpd/conf.d/mqtt-firmware.conf
if systemctl is-active -q firewalld; then
  firewall-cmd -q --permanent --add-service=https
  firewall-cmd -q --reload
  ok "firewalld: https open"
fi

# ---------------------------------------------------------------- 8. self-test
step "8. Self-test against $DOMAIN"
# Use a CA bundle file: --capath /etc/ssl/certs only works where individual
# hashed certs exist (Debian/Ubuntu). Fedora 44 dropped /etc/pki/tls/certs/ca-bundle.crt.
CA=(--capath /etc/ssl/certs)
for f in /etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem /etc/ssl/certs/ca-bundle.crt \
         /etc/pki/tls/certs/ca-bundle.crt /etc/ssl/certs/ca-certificates.crt; do
  if [[ -f $f ]]; then CA=(--cafile "$f"); break; fi
done
if ((${#NEW_CREDS[@]})); then
  { echo "# $(date -Is) pico-bulbs MQTT users (user  password)"; printf '%s\n' "${NEW_CREDS[@]}"; } >>"$CREDS"
  chmod 600 "$CREDS"
fi
app_pw="$(awk '$1=="app"{p=$2} END{print p}' "$CREDS" 2>/dev/null || true)"
if [[ -n $app_pw ]]; then
  if timeout 10 mosquitto_pub -h "$DOMAIN" -p 8883 "${CA[@]}" \
       -u app -P "$app_pw" -t pico/setup-test -m ok; then
    ok "TLS login as app on 8883 works"
  else
    die "TLS login as app on 8883 failed"
  fi
else
  ok "app password not in $CREDS — skipping login test"
fi
if timeout 10 mosquitto_pub -h "$DOMAIN" -p 8883 "${CA[@]}" -t pico/x -m x 2>/dev/null; then
  die "anonymous publish was ACCEPTED — check allow_anonymous"
else
  ok "anonymous access refused"
fi
code="$(curl -s -o /dev/null -w '%{http_code}' --max-time 10 "https://$DOMAIN/firmware/pico-bulb.bin" || true)"
case "$code" in
  200) ok "firmware URL works, and an image is published" ;;
  404) ok "firmware URL works over TLS, nothing published yet" ;;
  *)   die "firmware URL returned '$code' — check Apache and port 443" ;;
esac

# ---------------------------------------------------------------- summary
step "Done"
echo "    Bulbs: mqtts://$DOMAIN:8883      App: wss://$DOMAIN:8084"
echo "    Firmware: https://$DOMAIN/firmware/pico-bulb.bin"
if ((${#NEW_CREDS[@]})); then
  echo "    New credentials (also saved in $CREDS):"
  printf '      %s\n' "${NEW_CREDS[@]}"
  echo "    For each bulb, run from firmware/ on your PC and enter its password when asked:"
  echo "      python tools/provision.py --id <user> --wifi-ssid <network> --mqtt-uri mqtts://$DOMAIN:8883"
fi
echo "    All MQTT passwords created so far: sudo cat $CREDS"
