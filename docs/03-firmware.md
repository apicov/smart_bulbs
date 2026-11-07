# 3. Firmware: configure, flash, test

## One-time: ESP-IDF 6.0

The firmware needs ESP-IDF v6.0 with the RISC-V toolchain. Install it with Espressif's installer (`eim`). In every new shell, activate it first:

```sh
source ~/.espressif/tools/activate_idf_v6.0.1.sh
alias idf.py='python $IDF_PATH/tools/idf.py'     # if idf.py isn't on PATH
```

## One-time: create your signing key

Bulbs only accept updates signed with this key. Create it once and use the same one for every bulb:

```sh
cd firmware
mkdir -p keys
python -m espsecure generate-signing-key --version 2 --scheme rsa3072 keys/signing.pem
```

Git ignores `keys/`. **Back this file up somewhere safe and private.** If you lose it, every bulb needs USB before it can take another update. If someone else gets it, they can sign firmware your bulbs will accept.

## Build and flash

```sh
idf.py build flash    # run `rm -f sdkconfig` first if you've pulled new sdkconfig.defaults settings
```

If the serial port isn't found, hold **BOOT**, tap **RESET**, then run the command again.

## Give the bulb its settings

The firmware image holds no passwords, so every bulb can run the same one. Each bulb's id and secrets go into its own flash partition over USB:

```sh
python tools/provision.py --id living-room --wifi-ssid my-wifi --mqtt-uri mqtts://mqtt.apicov.xyz:8883 --max-ma 450
idf.py monitor        # Ctrl+] to leave the monitor
```

It asks for the WiFi and MQTT passwords, so they stay out of your shell history.

| Option | Value |
|-----|-------|
| `--id` | Bulb id, e.g. `living-room`. Must match its MQTT username |
| `--wifi-ssid` | A **2.4 GHz** network (the C6 has no 5 GHz) |
| `--mqtt-uri` | `mqtts://mqtt.apicov.xyz:8883` |
| `--max-ma` | Optional. `450` when powered over USB; see [wiring](01-wiring.md) |
| `--led-count` | Optional, if the strip doesn't have 24 LEDs |
| `--port` | Optional, if the serial port isn't found automatically |

To change a setting, run it again with all the values. Saved colours and alarms aren't touched. A bulb with no settings still works as a plain lamp and logs `Not provisioned`.

**A new router or WiFi password needs USB**, so keep this in mind before sealing a bulb.

If you still have a `sdkconfig.defaults.local` from an earlier version, this copies its settings across, after which you can delete the file:

```sh
python tools/provision.py --from sdkconfig.defaults.local
```

## What a healthy boot looks like

```
main: Bulb id: living-room (topics pico/bulb/living-room/*)
light_engine: Started: 24 LEDs on GPIO18, limit 450 mA     ← strip fades up to warm white
wifi_station: Got IP 192.168.x.x
mqtt_client: Connected to broker
main: Lowest free stack: light 2800 B, pir 1500 B, schedule 2000 B, ota 7500 B   ← your numbers will differ
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

## Update over WiFi

Bulbs running this firmware can update without a cable. Changes to the partition table or bootloader still need USB. The first time only, run `sudo bash broker/setup-server.sh` on the server, as in [the broker guide](02-broker.md).

**1. Commit, build and publish.** The version comes from git, so the tool refuses uncommitted changes.

```sh
cd firmware
idf.py reconfigure build             # plain `build` can keep the old version number
python tools/publish_firmware.py     # --server <login> if you don't use tsp@mqtt.apicov.xyz
```

This changes nothing on any bulb. It keeps the `.bin` and `.elf` in `releases/<version>/`, which git ignores.

**2. Watch and update one bulb.** Check it before updating the rest.

```sh
mosquitto_sub -h $H -p 8883 $CA -u app -P "$P" -t pico/bulb/living-room/info -v
mosquitto_pub -h $H -p 8883 $CA -u app -P "$P" -t pico/bulb/living-room/cmd -m '{"cmd":"ota"}'
```

`info` shows `checking`, `downloading`, then `restarting`. After the restart it shows the new `version` with `"confirmed":false`, which changes to `true` after 30 seconds connected to the broker. Expect about a minute in all, with the download taking about 30 seconds.

The light keeps running during the download. It goes dark briefly during the restart, then fades back to the on or off state it had before. Saved settings and alarms aren't affected.

Opening the USB port restarts this board, and that counts as a power-up, so the light turns on. It also drops any MQTT command sent while the bulb is reconnecting, so wait for `online` on the `status` topic before sending one.

| `ota` in `info` | What it means |
|-----------------|---------------|
| `up_to_date` | The server has the version already running |
| `busy` | A sleep timer or alarm is running, as a restart would lose it. Send the command again later |
| `waiting` | Installed, and restarts when a timer or alarm that started during the download has finished |
| `rolled_back` | This version failed on this bulb. Publish a fixed one, or add `"force":true` if the cause was something else, such as a broker outage |
| `unconfirmed` | The running image hasn't been confirmed yet. Try again in a moment |
| `rejected` | The image failed validation, such as a missing or wrong signature. Nothing changed |
| `failed` | Download or flash error, with the reason in the log. Nothing changed |

**If a new version is bad:** it has to reach the broker and stay connected for 30 seconds. If it crashes and restarts first, or hasn't managed that within 5 minutes, the bulb goes back to the previous version and reports it in `rolled_back`. A router or broker outage during those 5 minutes also causes a rollback, which is safe: retry with `"force":true`.

**To go back on purpose:** check out the older commit, then build and publish it. Any version other than the running one installs.

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
| `Not provisioned` or `running as a plain lamp` | Run `tools/provision.py`; see above |
| Build fails looking for `keys/signing.pem` | Create or restore your signing key; see above |
| Strip stays dark, log shows `Started` | Check the 5V/GND/DIN wiring and the DIN arrow direction on the strip (the arrow points away from the input) |
| First LED is wrong or flickers | Data level too low: shorten the wire or add a 74AHCT125 |
| No `Got IP` | Wrong SSID or password, or the network is 5 GHz only |
| `Connection refused (check username/password/ACL)` | The bulb's `--id` doesn't match the MQTT user, or the password is wrong |
| TLS/handshake errors | URI must be `mqtts://…:8883`; check the board can reach the internet |
| `ota` stays `failed`, log shows `Can't get https://…` | Nothing published yet, or the server's firmware vhost isn't set up. See [the broker guide](02-broker.md) |
| Board resets when you go to full white | Power brown-out: lower `MAX_MILLIAMPS` or use a separate supply |
