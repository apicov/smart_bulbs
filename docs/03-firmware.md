# 3. Firmware: configure, flash, test

## One-time: ESP-IDF 6.0

The firmware needs ESP-IDF v6.0 with the RISC-V toolchain. Install it with Espressif's installer (`eim`). In every new shell, activate it first:

```sh
source ~/.espressif/tools/activate_idf_v6.0.1.sh
alias idf.py='python $IDF_PATH/tools/idf.py'     # if idf.py isn't on PATH
```

## Configure the bulb

Secrets live in `firmware/sdkconfig.defaults.local`. Git ignores this file, so it never ends up on GitHub.

```sh
cd firmware
cp sdkconfig.defaults.local.example sdkconfig.defaults.local
```

| Key | Value |
|-----|-------|
| `CONFIG_BULB_ID` | Bulb id, e.g. `living-room`. Must match its MQTT username |
| `CONFIG_BULB_WIFI_SSID` / `_PASSWORD` | A **2.4 GHz** network (the C6 has no 5 GHz) |
| `CONFIG_BULB_MQTT_URI` | `mqtts://mqtt.apicov.xyz:8883` |
| `CONFIG_BULB_MQTT_PASSWORD` | That bulb's password from the server |
| `CONFIG_BULB_MAX_MILLIAMPS` | `450` when powered over USB; see [wiring](01-wiring.md) |

After changing this file, delete `sdkconfig` so the build reads it again.

## Build and flash

```sh
rm -f sdkconfig            # only after editing sdkconfig.defaults.local
idf.py build flash monitor # Ctrl+] to leave the monitor
```

If the serial port isn't found, hold **BOOT**, tap **RESET**, then run the command again.

## What a healthy boot looks like

```
main: Bulb id: living-room (topics pico/bulb/living-room/*)
light_engine: Started: 24 LEDs on GPIO18, limit 450 mA     ← strip fades up to warm white
wifi_station: Got IP 192.168.x.x
mqtt_client: Connected to broker
pir: motion detected                                       ← wave at the sensor
```

## Test without the app

Install `mosquitto` (`sudo dnf install mosquitto`) to get the command-line clients. Then:

```sh
H=mqtt.apicov.xyz
CA="--cafile /etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem"
P='<app password>'

# watch everything
mosquitto_sub -h $H -p 8883 $CA -u app -P "$P" -t 'pico/#' -v

# red, 40%, 1 s fade
mosquitto_pub -h $H -p 8883 $CA -u app -P "$P" -t pico/bulb/living-room/set \
  -m '{"state":"ON","color":{"r":255,"g":0,"b":0},"brightness":100,"transition":1}'

mosquitto_pub … -m '{"state":"OFF","transition":3}'     # slow fade out
```

## Troubleshooting

| Symptom | Likely cause |
|---------|--------------|
| Strip stays dark, log shows `Started` | Check the 5V/GND/DIN wiring and the DIN arrow direction on the strip (the arrow points away from the input) |
| First LED is wrong or flickers | Data level too low: shorten the wire or add a 74AHCT125 |
| No `Got IP` | Wrong SSID or password, or the network is 5 GHz only |
| `Connection refused (check username/password/ACL)` | `CONFIG_BULB_ID` doesn't match the MQTT user, or the password is wrong |
| TLS/handshake errors | URI must be `mqtts://…:8883`; check the board can reach the internet |
| Board resets when you go to full white | Power brown-out: lower `MAX_MILLIAMPS` or use a separate supply |
