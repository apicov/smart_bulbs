#!/usr/bin/env python3
"""Write a bulb's identity and secrets to its `bulbcfg` flash partition, over USB.

Run from an activated ESP-IDF shell:

    python tools/provision.py --id living-room --wifi-ssid my-wifi \
        --mqtt-uri mqtts://mqtt.example.com:8883 [--max-ma 450] [--led-count 24] [--port /dev/ttyACM0]

Passwords are prompted for, so they stay out of your shell history. To reuse
settings kept in the old format instead:

    python tools/provision.py --from sdkconfig.defaults.local

Saved lamp state and alarms live in another partition and aren't touched.
"""
import argparse
import getpass
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

# Must match partitions.csv.
PARTITION_OFFSET = "0x3E0000"
PARTITION_SIZE = "0x6000"
NAMESPACE = "bulb"

OLD_FORMAT_KEYS = {
    "id": "CONFIG_BULB_ID",
    "wifi_ssid": "CONFIG_BULB_WIFI_SSID",
    "wifi_password": "CONFIG_BULB_WIFI_PASSWORD",
    "mqtt_uri": "CONFIG_BULB_MQTT_URI",
    "mqtt_password": "CONFIG_BULB_MQTT_PASSWORD",
    "max_ma": "CONFIG_BULB_MAX_MILLIAMPS",
    "led_count": "CONFIG_BULB_LED_COUNT",
}


def read_old_format(path: Path) -> dict:
    """Read the settings from an sdkconfig-style file."""
    text = path.read_text()
    values = {}
    for name, key in OLD_FORMAT_KEYS.items():
        match = re.search(rf'^{key}=(?:"(.*)"|(\d+))$', text, re.MULTILINE)
        if match:
            values[name] = match.group(1) if match.group(1) is not None else match.group(2)
    return values


def problem_with(values: dict) -> str | None:
    """Same rules as config_problem() in BulbConfig.hpp, plus each field's size. Never prints secrets."""
    if not re.fullmatch(r"[a-z0-9-]{1,32}", values.get("id") or ""):
        return "id must be 1 to 32 characters, using only a-z, 0-9 and -"
    if not 1 <= len((values.get("wifi_ssid") or "").encode()) <= 32:
        return "WiFi SSID must be 1 to 32 bytes"
    if not 8 <= len((values.get("wifi_password") or "").encode()) <= 64:
        return "WiFi password must be 8 to 64 bytes"
    uri = values.get("mqtt_uri") or ""
    if not uri.startswith(("mqtt://", "mqtts://")) or len(uri.encode()) > 127:
        return "MQTT URI must start with mqtt:// or mqtts:// and be at most 127 bytes"
    if not 1 <= len((values.get("mqtt_password") or "").encode()) <= 64:
        return "MQTT password must be 1 to 64 bytes"
    if values.get("led_count") is not None and not 1 <= int(values["led_count"]) <= 1024:
        return "LED count must be 1 to 1024"
    if values.get("max_ma") is not None and not 0 <= int(values["max_ma"]) <= 0xFFFFFFFF:
        return "current limit is out of range"
    return None


def generate_image(values: dict, folder: Path) -> Path:
    """Build the partition image. Strings go through files, so commas in a password are safe."""
    rows = ["key,type,encoding,value", f"{NAMESPACE},namespace,,"]
    for key, field in [("id", "id"), ("wifi_ssid", "wifi_ssid"), ("wifi_pass", "wifi_password"),
                       ("mqtt_uri", "mqtt_uri"), ("mqtt_pass", "mqtt_password")]:
        value_file = folder / key
        value_file.write_text(values[field])
        rows.append(f"{key},file,string,{value_file}")
    if values.get("max_ma") is not None:
        rows.append(f"max_ma,data,u32,{int(values['max_ma'])}")
    if values.get("led_count") is not None:
        rows.append(f"led_count,data,u16,{int(values['led_count'])}")

    csv_file = folder / "bulbcfg.csv"
    csv_file.write_text("\n".join(rows) + "\n")
    image = folder / "bulbcfg.bin"
    generator = (Path(os.environ["IDF_PATH"]) / "components" / "nvs_flash" / "nvs_partition_generator"
                 / "nvs_partition_gen.py")
    subprocess.run([sys.executable, str(generator), "generate", str(csv_file), str(image), PARTITION_SIZE],
                   check=True, stdout=subprocess.DEVNULL)
    return image


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--from", dest="source", type=Path, help="read settings from an sdkconfig-style file")
    parser.add_argument("--id", help="bulb id, the same as its MQTT username")
    parser.add_argument("--wifi-ssid", help="a 2.4 GHz network")
    parser.add_argument("--mqtt-uri", help="for example mqtts://mqtt.example.com:8883")
    parser.add_argument("--max-ma", type=int, help="strip current limit in mA (default: the firmware's)")
    parser.add_argument("--led-count", type=int, help="number of LEDs (default: the firmware's)")
    parser.add_argument("--port", help="serial port (default: found automatically)")
    parser.add_argument("--output", type=Path, help="save the image to this file instead of flashing it")
    args = parser.parse_args()

    if "IDF_PATH" not in os.environ:
        print("Activate ESP-IDF first: IDF_PATH isn't set.", file=sys.stderr)
        return 1

    values = read_old_format(args.source) if args.source else {}
    for name in ("id", "wifi_ssid", "mqtt_uri", "max_ma", "led_count"):
        given = getattr(args, name)
        if given is not None:
            values[name] = given
    for name, prompt in (("wifi_password", "WiFi password: "), ("mqtt_password", "MQTT password: ")):
        if not values.get(name):
            values[name] = getpass.getpass(prompt)

    problem = problem_with(values)
    if problem:
        print(f"Can't provision: {problem}.", file=sys.stderr)
        return 1

    with tempfile.TemporaryDirectory(prefix="bulbcfg-") as folder:  # private to this user, and always removed
        image = generate_image(values, Path(folder))
        if args.output:
            args.output.write_bytes(image.read_bytes())
            args.output.chmod(0o600)
            print(f"Saved to {args.output}. It holds the bulb's passwords, so delete it when you're done.")
            return 0

        command = [sys.executable, "-m", "esptool", "--chip", "esp32c6"]
        if args.port:
            command += ["--port", args.port]
        command += ["write-flash", PARTITION_OFFSET, str(image)]
        subprocess.run(command, check=True)

    print(f"Provisioned {values['id']}. It uses these settings from its next boot.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
