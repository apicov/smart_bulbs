# Pico Bulbs

DIY smart bulbs: an **ESP32-C6-Zero** drives a NeoPixel strip and reads a PIR sensor. Each bulb is controlled over **MQTT** (Mosquitto on a Linode) from a **Capacitor + React + MUI** app.

| Folder      | What |
|-------------|------|
| `firmware/` | ESP-IDF 6.0 project for the bulb: light engine (fades, gamma, current limit), PIR sensor, MQTT over TLS, state saved in NVS |
| `broker/`   | Mosquitto config, ACL and `setup-server.sh` for the Fedora server; `dev/` runs a local test broker |
| `app/`      | Control app (to come) |

## MQTT topics

`pico/bulb/<id>/status | state | set | motion`. Payloads use Home Assistant's JSON light schema; see [broker/README.md](broker/README.md#topic-and-payload-reference).

## Firmware quick start

```sh
cd firmware
cp sdkconfig.defaults.local.example sdkconfig.defaults.local   # WiFi + MQTT credentials (git-ignored)
idf.py set-target esp32c6
idf.py build flash monitor
```

Default wiring: LED data on GPIO18, PIR on GPIO19, 24 LEDs, 1000 mA current cap. Change these in `idf.py menuconfig` under "Pico Bulb".

## Server

On the server, clone the repo and run:

```sh
git clone <repo-url> pico-bulbs
sudo bash pico-bulbs/broker/setup-server.sh
```
