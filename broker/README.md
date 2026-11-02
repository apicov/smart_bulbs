# Broker: Mosquitto on the Linode (Fedora)

A single broker on the Linode, with two TLS listeners:

| Port | Protocol         | Used by                     |
|------|------------------|-----------------------------|
| 8883 | MQTT over TLS    | Bulbs (`mqtts://HOST:8883`) |
| 8084 | MQTT over WSS    | App (`wss://HOST:8084`)     |

Browsers can't open raw TCP sockets, so the app has to talk to the broker over WebSockets.

**Prerequisite:** a DNS name pointing at the Linode, e.g. `mqtt.example.com`. Let's Encrypt won't issue a certificate for a bare IP address.

## Quick setup (recommended)

`setup-server.sh` does all the steps below. It's safe to re-run, it never touches the existing Apache sites, and it gets the certificate via webroot in `/var/www/acme`, so Apache keeps running:

```sh
# on the server
git clone <repo-url> pico-bulbs        # later updates: git -C pico-bulbs pull
sudo bash pico-bulbs/broker/setup-server.sh
# options: sudo DOMAIN=mqtt.apicov.xyz BULBS="living-room kitchen" bash …
```

It prints new passwords at the end and stores them in `/root/pico-bulbs-credentials.txt`. You can add more bulbs later with `sudo pico-bulbs/broker/add-bulb.sh <id>`.

The manual steps below are for reference. On this server, use `--webroot -w /var/www/acme` instead of `--standalone`, because Apache owns port 80.

## 1. Install

```sh
sudo dnf install -y mosquitto certbot
```

## 2. TLS certificate

This uses certbot's standalone mode, which needs port 80 free for a moment. If nginx or Apache already serves this host, use `--webroot` instead.

```sh
sudo firewall-cmd --add-service=http            # temporary, for the HTTP-01 challenge
sudo certbot certonly --standalone -d mqtt.example.com
sudo firewall-cmd --remove-service=http
```

Install the deploy hook. It copies the certificates to where mosquitto can read them, and runs again after every renewal:

```sh
sudo install -m 755 certbot-deploy-hook.sh /etc/letsencrypt/renewal-hooks/deploy/mosquitto.sh
sudo sed -i 's/mqtt.example.com/YOUR.DOMAIN/' /etc/letsencrypt/renewal-hooks/deploy/mosquitto.sh
sudo RENEWED_DOMAINS=YOUR.DOMAIN /etc/letsencrypt/renewal-hooks/deploy/mosquitto.sh   # first run
```

Renewal itself is handled by `certbot-renew.timer` (`systemctl enable --now certbot-renew.timer`). With standalone mode, port 80 must also be reachable at renewal time. Alternatively, add `--pre-hook`/`--post-hook` commands that open and close the firewall.

## 3. Config, users, ACL

```sh
sudo install -m 644 mosquitto.conf /etc/mosquitto/mosquitto.conf
sudo install -m 640 -g mosquitto acl /etc/mosquitto/acl

# App user (the password goes into the app's Settings page)
sudo touch /etc/mosquitto/passwd && sudo chown root:mosquitto /etc/mosquitto/passwd && sudo chmod 640 /etc/mosquitto/passwd
sudo mosquitto_passwd /etc/mosquitto/passwd app

# One user per bulb: the username must equal the bulb id (the ACL relies on it)
sudo ./add-bulb.sh living-room      # prints the lines for firmware/sdkconfig.defaults.local
```

## 4. Firewall and start

```sh
sudo firewall-cmd --permanent --add-port=8883/tcp --add-port=8084/tcp
sudo firewall-cmd --reload
sudo systemctl enable --now mosquitto
journalctl -u mosquitto -f
```

If mosquitto fails to start, or can't read the certificates or bind a port, check SELinux first:

```sh
sudo ausearch -m avc -ts recent | grep mosquitto     # any denials?
ls -Z /etc/mosquitto/certs                            # the deploy hook runs restorecon on these
sudo ausearch -m avc -ts recent | audit2allow -M mosquitto-local   # last resort: inspect .te, then semodule -i
```

## 5. Verify

Run these from any machine with `mosquitto-clients` installed:

```sh
H=mqtt.example.com
# Watch everything as the app user
mosquitto_sub -h $H -p 8883 --capath /etc/ssl/certs -u app -P '…' -t 'pico/#' -v

# Anonymous access must be refused
mosquitto_sub -h $H -p 8883 --capath /etc/ssl/certs -t '#'        # → "not authorised"

# A bulb must not be able to write to another bulb's topics (the message is silently dropped)
mosquitto_pub -h $H -p 8883 --capath /etc/ssl/certs -u living-room -P '…' \
  -t pico/bulb/kitchen/set -m '{"state":"OFF"}'

# Drive a bulb by hand
mosquitto_pub -h $H -p 8883 --capath /etc/ssl/certs -u app -P '…' \
  -t pico/bulb/living-room/set -m '{"state":"ON","color":{"r":255,"g":0,"b":0},"brightness":100}'
```

## Topic and payload reference

The full tree is `pico/bulb/<id>/…`. Payloads follow Home Assistant's MQTT **JSON** light schema, so an HA instance could control these bulbs later.

| Topic    | Direction  | Retained | Payload |
|----------|------------|----------|---------|
| `status` | bulb → all | yes      | `online` / `offline`. `offline` is the Last Will, sent if the bulb drops off |
| `state`  | bulb → all | yes      | `{"state":"ON","brightness":128,"color_mode":"rgb","color":{"r":255,"g":180,"b":100}}` |
| `set`    | app → bulb | no       | Any subset of: `"state":"ON"\|"OFF"\|"TOGGLE"`, `"brightness":0-255`, `"color":{"r","g","b"}` or `[r,g,b]`, `"transition":seconds` (default 0.4) |
| `motion` | bulb → all | yes      | `{"motion":true}` / `{"motion":false}` |
