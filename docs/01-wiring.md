# 1. Hardware & wiring

## Parts (one bulb)

| Part | Notes |
|------|-------|
| Waveshare **ESP32-C6-Zero** | USB-C is used for power and flashing |
| **WS2812B** strip, 24 LEDs | 5V "NeoPixel" strip |
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

- **Power.** Each LED draws up to ~60 mA at full white, so 24 LEDs can draw ~1.4 A. A laptop USB port gives about 500 mA, so on the bench cap the strip at **450 mA** with `tools/provision.py --max-ma 450`. For a real lamp, feed the strip from its own 5V supply, connect all the GNDs together, and raise the cap to match the supply.
- **Data level.** The C6 outputs 3.3V, while WS2812 strips officially expect about 3.5V at 5V supply. Short wires usually work without a level shifter. If the first LED flickers or shows wrong colours, add a 74AHCT125 between GP18 and DIN.
- **Avoid these pins:** GP8 (the onboard RGB LED), GP9 and GP15 (they decide the boot mode), and GP12/GP13 (USB).
- **PIR placement.** Keep the PIR away from the board's antenna and from the LEDs. WiFi transmissions and heat can both cause false triggers. After power-up, the sensor needs 10–60 s to settle.
- To use **other pins**, run `idf.py menuconfig` and open "Pico Bulb". For a **different LED count**, use `tools/provision.py --led-count`.
