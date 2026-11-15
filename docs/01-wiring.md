# 1. Hardware & wiring

This covers the ESP32-C6-Zero bulb. The XIAO ESP32-S3 Sense node, with a 12 V strip and I2C sensors, is [further down](#xiao-esp32-s3-sense-node).

## Parts (one bulb)

| Part | Notes |
|------|-------|
| Waveshare **ESP32-C6-Zero** | USB-C is used for power and flashing |
| 3× 4 W **RGBW** NeoPixel LEDs | Adafruit 5163, 5V, RGBW order |
| **HC-SR505** mini PIR | Runs on 4.5–20V. Output is HIGH (~3.3V) on motion and holds for ~8 s |
| 330 Ω resistor | In series with the LED data line |
| 1000 µF / ≥6.3V capacitor | Across the strip's 5V and GND |
| *(optional)* 74AHCT125 | Lifts the 3.3V data signal to 5V; see the note below |

## Connections

![ESP32-C6-Zero pinout](https://www.waveshare.com/w/upload/2/2e/ESP32-C6-Zero_Pin.png)

| From | To | Board pin |
|------|----|-----------|
| Strip **5V** | 5V | left header, top |
| Strip **GND** | GND | left header, 2nd |
| Strip **DIN** | **GP18** via 330 Ω | right header, 5th |
| PIR **+** | 5V | |
| PIR **−** | GND | |
| PIR **OUT** | **GP19** | right header, 6th (next to GP18) |
| 1000 µF cap | across strip 5V/GND, at the strip end | mind the polarity: − to GND |

```
            ESP32-C6-Zero (top view, USB up)
              ┌──────[USB-C]──────┐
   strip 5V ──┤ 5V             TX ├
 strip GND ───┤ GND            RX ├
              ┤ 3V3          GP14 ├
              ┤ GP0          GP15 ├
              ┤ GP1          GP18 ├──[330Ω]── strip DIN
              ┤ GP2          GP19 ├────────── PIR OUT
              ┤ GP3          GP20 ├
              ┤ GP4          GP21 ├
              ┤ GP5          GP22 ├
              └───────────────────┘
   PIR + → 5V      PIR − → GND      (shared GND everywhere)
```

## Rules of thumb

- **Power.** A 4 W LED draws ~800 mA at 5V with all four channels on, ~200 mA per channel. The firmware lights whites with the white LED alone, so the most an LED draws is ~400 mA, for colours like yellow that use two full channels. Three LEDs can draw ~1.2 A. The default cap of 1800 mA suits a 2 A supply. For other LEDs, set the per-channel current in `idf.py menuconfig` (~20 mA for a WS2812B). A laptop USB port gives about 500 mA, so on the bench cap the strip at **450 mA** with `tools/provision.py --max-ma 450`. For a real lamp, feed the strip from its own 5V supply, connect all the GNDs together, and raise the cap to match the supply.
- **Data level.** The C6 outputs 3.3V, while WS2812 strips officially expect about 3.5V at 5V supply. Short wires usually work without a level shifter. If the first LED flickers or shows wrong colours, add a 74AHCT125 between GP18 and DIN.
- **Avoid these pins:** GP8 (the onboard RGB LED), GP9 and GP15 (they decide the boot mode), and GP12/GP13 (USB).
- **PIR placement.** Keep the PIR away from the board's antenna and from the LEDs. WiFi transmissions and heat can both cause false triggers. After power-up, the sensor needs 10–60 s to settle.
- To use **other pins**, run `idf.py menuconfig` and open "Pico Bulb". For a **different LED count**, use `tools/provision.py --led-count`.
- The firmware expects **RGBW NeoPixels** (SK6812 RGBW) by default. For an RGB strip such as the WS2812B, set "LED type" to RGB under "Pico Bulb" in `idf.py menuconfig`, or its colours come out wrong.

## XIAO ESP32-S3 Sense node

The `firmware/s3` project. It's a bulb like the C6 one, plus I2C sensors and the Sense board's camera.

| Part | Notes |
|------|-------|
| Seeed **XIAO ESP32-S3 Sense** | 8 MB flash, 8 MB PSRAM, camera on its expansion board |
| **WS2811** 12 V strip | One chip per 3 LEDs, wired BRG, as in the dancefloor project |
| 12 V supply | For the strip only. Share its GND with the XIAO |
| 74AHCT125, 330 Ω | Lifts the 3.3V data to 5V for the strip, powered from 5V |
| **HC-SR505** mini PIR | As on the C6 bulb |
| VEML7700, TSL2591, VL53L1X, SCD30 | I2C breakouts, powered from 3V3 |

| Pad | GPIO | Connects to |
|-----|------|-------------|
| D0 | 1 | 74AHCT125 in; its out → 330 Ω → strip **DIN** |
| D1 | 2 | PIR **OUT** |
| D3 | 4 | VL53L1X **XSHUT** |
| D4 | 5 | I2C **SDA**, all four sensors |
| D5 | 6 | I2C **SCL**, all four sensors |
| 3V3, GND | | Sensors' power. GND also to the PIR, the level shifter and the 12 V supply's GND |
| 5V | | PIR + and the level shifter's VCC |

- **Same address.** The TSL2591 and VL53L1X both start at I2C address 0x29. The firmware holds the VL53L1X in reset with XSHUT, then moves it to 0x30 before the TSL2591 starts. Without the XSHUT wire it leaves the VL53L1X out.
- **Pull-ups.** Most breakouts have their own I2C pull-ups. The firmware also turns on the chip's weak ones, which are enough only for short wires.
- **Never feed 12 V to the XIAO.** It runs from USB or 5V. The strip's 12 V only meets the XIAO at GND.
- **LED count.** For WS2811, set it to the number of chips, which is the LEDs ÷ 3, with `tools/provision.py --project s3 --led-count`. The default is 30.
- **First flash.** Set pure red, green and blue from the app and check the strip shows the same. If two are swapped, the strip is wired in a different order than the dancefloor one.
- **Current limit.** It's off on this node. Set one with `--max-ma` once you know the supply.
- To use **other pins**, run `idf.py menuconfig` in `firmware/s3` and open "Pico Bulb" and "Pico Node sensors".
