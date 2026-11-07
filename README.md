# Pico Bulbs

DIY smart bulbs: an **ESP32-C6-Zero** drives a NeoPixel strip and reads a PIR sensor. Each bulb is controlled over **MQTT** (Mosquitto on a Linode) from a **Capacitor + React + MUI** app.

**New here? Start with the [setup guide](docs/README.md)** (wiring → broker → firmware → app).

| Folder      | What |
|-------------|------|
| `firmware/` | ESP-IDF 6.0 project for the bulb: light engine (fades, gamma, current limit), PIR sensor, MQTT over TLS, state saved in NVS |
| `broker/`   | Mosquitto config, ACL and `setup-server.sh` for the Fedora server; `dev/` runs a local test broker |
| `app/`      | Capacitor + React + MUI control app (web and Android): bulb list, power, brightness, palette, colour picker |
| `docs/`     | Step-by-step setup guide and wiring |

## MQTT topics

`pico/bulb/<id>/status | state | set | motion`. Payloads use Home Assistant's JSON light schema; see [broker/README.md](broker/README.md#topic-and-payload-reference).

## Firmware quick start

```sh
cd firmware
idf.py set-target esp32c6
python -m espsecure generate-signing-key --version 2 --scheme rsa3072 keys/signing.pem   # once; back it up
idf.py build flash
python tools/provision.py --id living-room --wifi-ssid my-wifi --mqtt-uri mqtts://mqtt.example.com:8883
idf.py monitor
```

Create `firmware/keys/` first if it doesn't exist. See [docs/03-firmware.md](docs/03-firmware.md) for the details.

Default wiring: LED data on GPIO18, PIR on GPIO19, 24 LEDs, 1000 mA current cap. Change the pins in `idf.py menuconfig` under "Pico Bulb". The LED count and current cap can also be set per bulb with `tools/provision.py`.

## Server

On the server, clone the repo and run:

```sh
git clone <repo-url> pico-bulbs
sudo bash pico-bulbs/broker/setup-server.sh
```
