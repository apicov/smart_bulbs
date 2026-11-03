# Pico Bulbs: setup guide

This guide takes you from nothing to a working smart bulb. Follow the steps in order.

| # | Guide | Result |
|---|-------|--------|
| 1 | [Hardware & wiring](01-wiring.md) | A C6-Zero with LED strip and PIR sensor on the bench |
| 2 | [Broker on the server](02-broker.md) | Mosquitto with TLS at `mqtt.apicov.xyz` |
| 3 | [Firmware: configure, flash, test](03-firmware.md) | A bulb that you control with `mosquitto_pub` |
| 4 | App | *(coming next)* |

## How it fits together

```mermaid
flowchart LR
  subgraph Home
    B1[Bulb<br/>ESP32-C6 + LEDs + PIR]
    B2[Bulb …]
  end
  subgraph Linode["Linode (Fedora)"]
    M[(Mosquitto)]
  end
  P[Phone / browser app]
  B1 -- "mqtts :8883" --> M
  B2 -- "mqtts :8883" --> M
  P -- "wss :8084" --> M
```

- **Bulbs work on their own.** Fades, saved state and (later) effects, schedules and night-off all run on the bulb itself. The app only sends commands such as "red, 60%" and shows each bulb's state.
- **MQTT topics:** `pico/bulb/<id>/status | state | set | motion`. Payloads use Home Assistant's JSON light schema; see [the topic reference](../broker/README.md#topic-and-payload-reference).
- **Without internet** the bulbs still light up and keep their last setting, but the app can't reach them. That's because the broker lives on the Linode.
