# XIAO ESP32-S3 Sense node

Firmware for a Seeed **XIAO ESP32-S3 Sense** that works as one more bulb in the app. It drives a 12 V WS2811 strip, the one from the dancefloor project, and reads a PIR. On top of that it reads four I2C sensors and the Sense board's camera, and publishes the readings over MQTT.

It shares `../components` and `../tools` with the C6 bulb in `../c6`. The general guides are [wiring](../../docs/01-wiring.md#xiao-esp32-s3-sense-node) and [firmware](../../docs/03-firmware.md). This page collects what's specific to this node.

## Parts

| Part | Notes |
|------|-------|
| Seeed **XIAO ESP32-S3 Sense** | 8 MB flash, 8 MB PSRAM, camera (OV2640 or OV3660) on its expansion board |
| **WS2811** 12 V strip | One chip drives 3 LEDs. Colour order BRG |
| 12 V supply | For the strip only |
| 74AHCT125 level shifter | Lifts the 3.3 V data to 5 V. Power it from 5 V |
| 330 Ω resistor | In series with the strip's data line |
| 1000 µF / ≥16 V capacitor | Across the strip's 12 V and GND, at the strip end |
| **HC-SR505** mini PIR | Powered from 5 V. Its output is about 3.3 V, which is safe for the XIAO |
| **VEML7700** | Ambient light, I2C 0x10 |
| **TSL2591** | Ambient light with a wide range, I2C 0x29 |
| **VL53L1X** | Distance, up to about 1.3 m. Starts at I2C 0x29 and is moved to 0x30 |
| **SCD30** | CO₂, temperature and humidity, I2C 0x61 |

## Pin connections

| XIAO pad | GPIO | Connects to |
|----------|------|-------------|
| **D0** | 1 | 74AHCT125 input A. Its output Y → 330 Ω → strip **DIN** |
| **D1** | 2 | PIR **OUT** |
| **D3** | 4 | VL53L1X **XSHUT** |
| **D4** | 5 | I2C **SDA** on all four sensors |
| **D5** | 6 | I2C **SCL** on all four sensors |
| **5V** | | PIR **+**, 74AHCT125 **VCC** |
| **3V3** | | VIN or VCC on all four sensors |
| **GND** | | Every GND: the sensors, the PIR, the 74AHCT125 and the 12 V supply's **−** |

The 74AHCT125 also needs its **1OE** pin tied to GND to enable that channel. Tie the unused inputs to GND as well.

```
                XIAO ESP32-S3 (top view, USB up)
                    ┌────[USB-C]────┐
  74AHCT125 1A ─────┤ D0         5V ├───── PIR +, 74AHCT125 VCC
  PIR OUT ──────────┤ D1        GND ├───── all GNDs (incl. 12 V −)
                    ┤ D2        3V3 ├───── sensors VIN
  VL53L1X XSHUT ────┤ D3        D10 ├
  SDA (all 4) ──────┤ D4         D9 ├
  SCL (all 4) ──────┤ D5         D8 ├
                    ┤ D6         D7 ├
                    └───────────────┘

  74AHCT125 1Y ──[330 Ω]── strip DIN        strip +12V ── 12 V supply +
                                            strip GND  ── 12 V supply − ── XIAO GND
```

The camera connects to the expansion board's own connector and uses internal pins. Its SCCB runs on I2C port 1 (GPIO 39/40), away from the sensors on port 0.

### Wiring rules

- **Never connect 12 V to the XIAO.** Power the XIAO from USB-C or its 5V pad. The 12 V supply only meets the XIAO at GND, and that shared GND is required.
- **The data line needs the level shifter.** With 12 V on the strip, a 3.3 V signal is not reliable.
- **XSHUT is required with both the TSL2591 and VL53L1X fitted.** Both start at 0x29. At boot the firmware resets the VL53L1X with XSHUT and moves it to 0x30 before starting the TSL2591. If XSHUT isn't wired, the firmware leaves the VL53L1X out.
- **I2C pull-ups.** Most breakouts have their own. The firmware also turns on the chip's weak internal pull-ups, but those are only enough for short wires. The drivers talk to the VEML7700 and SCD30 at 100 kHz and to the TSL2591 at 400 kHz.
- **Pins to keep free:**
  - D6/D7 are the UART.
  - D8–D10 are shared with the Sense board's SD card slot.
  - GPIO 10–18 and 38–48 belong to the camera, microphone and PSRAM.
- **PIR placement.** Keep it away from the antenna and the strip. It needs 10–60 s to settle after power-up.

## Build and flash

The board has no USB-to-serial chip. Flashing and the console go over the S3's own USB port.

```sh
source ~/.espressif/tools/activate_idf_v6.0.1.sh     # or: get_idf
cd firmware/s3
idf.py build flash monitor                           # Ctrl+] to leave
```

If the port doesn't appear, hold **BOOT** while plugging in USB, then flash again.

Then give it its id and secrets from `firmware/`:

```sh
python tools/provision.py --project s3 --id dancefloor --wifi-ssid my-wifi \
    --mqtt-uri mqtts://mqtt.apicov.xyz:8883 --led-count 30
```

Add the bulb on the broker with `broker/add-bulb.sh`, as for a C6 bulb.

To publish an update over WiFi, commit first, then run:

```sh
(cd s3 && idf.py reconfigure build)
python tools/publish_firmware.py --project s3        # uploads pico-bulb-s3.bin
```

## Settings

| Setting | Default | Where to change it |
|---------|---------|--------------------|
| LED type | WS2811, BRG | `idf.py menuconfig` → Pico Bulb |
| LED count | 30 chips, which is 90 LEDs | `provision.py --led-count`. Count the WS2811 chips, not the LEDs |
| Current limit | Off | `provision.py --max-ma` once you know the supply |
| LED / PIR GPIO | 1 (D0) / 2 (D1) | menuconfig → Pico Bulb |
| I2C SDA / SCL | 5 (D4) / 6 (D5) | menuconfig → Pico Node sensors |
| VL53L1X XSHUT | 4 (D3), or -1 for no VL53L1X | menuconfig → Pico Node sensors |
| Sensor report period | 30 s | menuconfig → Pico Node sensors |
| OTA file | `pico-bulb-s3.bin` | menuconfig → Pico Bulb |

The 8 MB flash is split into these partitions (`partitions.csv`):

| Partition | Size |
|-----------|------|
| Each OTA slot (two) | 3 MB |
| Bulb settings | 24 KB |
| Crash report | 64 KB |

## MQTT

The node shows up as a normal bulb under `pico/bulb/<id>/`, and its `info` message includes `"chip":"esp32s3"`. Sensor readings arrive on `pico/bulb/<id>/sensors`, retained, every period:

```json
{"veml7700":{"lux":123.4},"tsl2591":{"lux":118.0},"vl53l1x":{"mm":530},
 "scd30":{"co2":612,"temp":22.4,"rh":41.0},"camera":"ok"}
```

- A sensor that isn't fitted or doesn't answer is left out of the message.
- `"mm":null` means nothing is in range of the VL53L1X.
- `camera` is `ok`, or the error from starting the camera.

Each problem is logged once, not on every read. The camera only takes one test frame at boot; it doesn't stream yet.

```sh
mosquitto_sub -h $H -p 8883 $CA -u app -P "$P" -t 'pico/bulb/+/sensors' -v   # H, CA, P as in docs/03-firmware.md
```

## First-boot checklist

1. **The strip fades up to warm white.** If it doesn't, check the level shifter, its 1OE pin and the shared GND.
2. **Colour order.** Set pure red, green and blue in the app. If two colours are swapped, your strip isn't BRG: change the order in `LightEngine.cpp` (`LedType::WS2811`).
3. **PIR.** Wave at it and look for `pir: motion detected` in the log.
4. **Sensors.** The log should show each sensor starting. Sane readings are:
   - Lux that drops when you cover the light sensors.
   - A distance that follows your hand.
   - CO₂ around 400–800 ppm indoors. Wait about a minute for the first reading.
5. **TSL2591.** Check it still reads sensible lux after the VL53L1X has been moved to 0x30.
6. **Camera.** The log should show `Camera test frame: 640x480, … bytes of JPEG`.
