# 4. App

The control app lives in `app/`: React 19, MUI 7 and Capacitor 7, built on the same patterns as pico-reader. It runs in the browser and as an Android app, and talks to the broker directly over `wss://`. There's no backend of its own.

## What it does

- **Bulb list:** every bulb that has connected to the broker shows up by itself. Each one has an online/offline state, its current colour and brightness, a power switch and a motion indicator. There's also an **All off** button.
- **Bulb page:** power switch, brightness slider, 12 one-tap colours (four whites and eight colours), and a custom colour picker.
- **Settings:** broker URL, username and password, plus a light/dark/system theme.

## First run

1. Get the `app` password from the server: `sudo cat /root/pico-bulbs-credentials.txt`.
2. Open **Settings**. The broker URL is already `wss://mqtt.apicov.xyz:8084` and the username `app`, so just enter the password and tap **Save & connect**.
3. Your bulbs appear in the list. If they don't, check the dot in the top bar:

| Dot | Status | Meaning |
|-----|--------|---------|
| green | Connected | OK. If no bulbs show, none has connected yet; see the bulb's serial log |
| orange | Connecting / Offline | Can't reach the broker: check the network, or open port 8084 on the server |
| red | Login refused | Wrong username or password |
| grey | Not set up | No password entered yet |

## Develop in the browser

```sh
cd app
npm install
npm run dev          # http://localhost:5173
npm test             # unit tests + app tests with a fake MQTT client
npm run lint && npm run typecheck
```

## Install on an Android phone

Turn on USB debugging on the phone, connect it, then:

```sh
./install-android.sh              # build + install + start
./install-android.sh --no-install # just build android/app/build/outputs/apk/debug/app-debug.apk
```

This needs JDK 21 (`sudo dnf install java-21-openjdk-devel`) and the Android SDK in `~/Android/Sdk`.

## How it behaves

- **Optimistic UI:** a tap shows its result straight away. The bulb then answers with its retained `state`, which confirms or corrects what's shown.
- **Dragging:** while you drag the brightness slider or the colour picker, values go out about 12 times per second (QoS 0) with 0.15 s fades, so the lamp follows your finger. The final value is sent again at QoS 1, so it can't get lost.
- **Tapping a colour turns the bulb on.** The app sends `"state":"ON"` together with the colour, because the firmware only changes power when it's told to.
- **The password is stored on the device**, in Capacitor Preferences (Android SharedPreferences, or localStorage on the web). That's fine for a personal app. Don't use a shared device with it.
