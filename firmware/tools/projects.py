"""The firmware projects, one per chip, and what the tools need to know about each."""
import argparse
import csv
from dataclasses import dataclass
from pathlib import Path

FIRMWARE = Path(__file__).resolve().parent.parent
IDF_PROJECT = "pico_bulb"  # project() in each CMakeLists.txt, so also the .bin and .elf names


@dataclass(frozen=True)
class Project:
    name: str            # its folder under firmware/
    chip: str            # for esptool and esp_coredump
    published_name: str  # the file its bulbs download, as in BULB_OTA_URL
    clang_target: tuple[str, ...]

    @property
    def folder(self) -> Path:
        return FIRMWARE / self.name

    @property
    def build(self) -> Path:
        return self.folder / "build"

    @property
    def releases(self) -> Path:
        return FIRMWARE / "releases" / self.name

    def partition_offset(self, name: str) -> str:
        """A partition's offset, from this project's partitions.csv."""
        with (self.folder / "partitions.csv").open() as table:
            for row in csv.reader(line for line in table if not line.lstrip().startswith("#")):
                if row and row[0].strip() == name:
                    return row[3].strip()
        raise LookupError(f"no {name} partition in {self.folder / 'partitions.csv'}")


PROJECTS = {
    "c6": Project("c6", "esp32c6", "pico-bulb.bin", ("--target=riscv32-esp-elf",)),
    "s3": Project("s3", "esp32s3", "pico-bulb-s3.bin", ("--target=xtensa-esp-elf", "-mcpu=esp32s3")),
}


def add_project_argument(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--project", choices=PROJECTS, default="c6",
                        help="which chip's firmware: c6 (ESP32-C6 bulb) or s3 (XIAO ESP32-S3 node); "
                             "default: %(default)s")


def project_from(args: argparse.Namespace) -> Project:
    return PROJECTS[args.project]
