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
main: Lowest free stack: light 2800 B, pir 1500 B, schedule 2000 B   ← your numbers will differ
bulb_ctrl: Clock set: Fri 2026-11-06 21:30:00 CET          ← should match your clock
pir: motion detected                                       ← wave at the sensor
```

The stack line repeats on every MQTT connect, and its numbers can only fall. If any of them drops below about 500 B, increase that task's `TASK_STACK_BYTES`. If it stays large after days of use, you can reduce it to save RAM.

## Clock and timezone

The wake-up alarm needs the time, so the bulb gets it from `pool.ntp.org` after connecting to WiFi. `Clock set` can appear before or after the stack line. The default timezone is Central European Time, with daylight saving handled automatically. For anywhere else, set `CONFIG_BULB_TIMEZONE` to a POSIX TZ string, such as `GMT0BST,M3.5.0/1,M10.5.0` for the UK.

After a power cut with no internet, the bulb has no clock and the alarm can't run. The app shows a warning until the bulb gets the time. The sleep timer doesn't need the clock.

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

# on topic pico/bulb/living-room/schedule_set
mosquitto_pub … -m '{"timer":{"after":60,"fade":10}}'   # off in 60 s, fading for the last 10 s
```

## Test without a board

The fade, gamma, current-limit and JSON logic has no ESP-IDF dependency, so its unit tests run on your PC. They need `g++` with C++23 support and CMake, and cJSON from `managed_components`, which appears after your first `idf.py build`. Run them in a shell where ESP-IDF is **not** activated, because its tools on `PATH` break the host compiler.

```sh
cd firmware
cmake -S tests/host -B tests/host/build
cmake --build tests/host/build
ctest --test-dir tests/host/build --output-on-failure
```

## Check the code style

After `idf.py build`, this runs `clang-format` and `clang-tidy` on the firmware's own code. Both tools come with ESP-IDF. It exits non-zero if either reports a problem, and `--fix-format` reformats the files for you.

```sh
python tools/lint.py
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
