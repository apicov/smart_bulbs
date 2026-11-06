#!/usr/bin/env python3
"""Check the firmware's own C++ with clang-format and clang-tidy.

Run from an activated ESP-IDF shell, after `idf.py build`:

    python tools/lint.py [--build-dir build] [--fix-format]

Exits non-zero if anything is reported. The firmware is compiled by GCC, so
this rewrites build/compile_commands.json into a form clang-tidy understands.
"""
import argparse
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

FIRMWARE = Path(__file__).resolve().parent.parent
SOURCE_GLOBS = ["components/**/*.cpp", "components/**/*.hpp", "main/**/*.cpp", "tests/host/**/*.cpp"]
GCC = "riscv32-esp-elf-g++"

# GCC options clang rejects, plus warning settings that belong to the real build.
DROP = re.compile(
    r"-fstrict-volatile-bitfields|-fno-tree-switch-conversion|-fno-shrink-wrap|-fno-jump-tables"
    r"|-fmacro-prefix-map=.*|-fdiagnostics-color.*|-W.*|-specs=.*|-nostartfiles|-mno-.*|-mtune=.*"
)


# picolibc's pthread.h puts [[noreturn]] where GCC tolerates it and clang doesn't.
CLANG_SHIM = """#include <sys/cdefs.h>
#undef __noreturn
#define __noreturn __attribute__((__noreturn__))
"""


def find_clang_tool(name: str) -> str:
    """The tool from PATH, or else from the ESP-IDF tools folder."""
    if found := shutil.which(name):
        return found
    tools = Path(os.environ.get("IDF_TOOLS_PATH", Path.home() / ".espressif")) / "tools" / "esp-clang"
    for candidate in sorted(tools.glob(f"*/esp-clang/bin/{name}"), reverse=True):
        return str(candidate)
    sys.exit(f"{name} not found: activate ESP-IDF, or install esp-clang with `eim`")


def compile_flags(entry: dict) -> list[str]:
    """The entry's GCC flags, with the response file IDF keeps -march and -specs in expanded."""
    tokens = shlex.split(entry["command"]) if "command" in entry else entry["arguments"]
    flags = []
    for token in tokens[1:]:
        if token.startswith("@"):
            flags += shlex.split(Path(entry["directory"], token[1:]).read_text())
        else:
            flags.append(token)
    return flags


def gcc_system_includes(entry: dict) -> list[str]:
    """GCC's search paths under the build's flags, so clang reads the same libc and libstdc++."""
    flags = [f for f in compile_flags(entry) if f.startswith(("-march=", "-specs=", "-std=", "-fno-rtti"))]
    out = subprocess.run([GCC, *flags, "-E", "-x", "c++", "-v", os.devnull],
                         capture_output=True, text=True, check=True).stderr
    block = out.split("#include <...> search starts here:")[1].split("End of search list.")[0]
    return [line.strip() for line in block.splitlines() if line.strip()]


def clang_arguments(entry: dict, includes: list[str], shim: Path) -> list[str]:
    args = ["clang++", "--target=riscv32-esp-elf", "-nostdinc++", "-nostdinc", "-include", str(shim)]
    skip_next = False
    for token in compile_flags(entry):
        if skip_next:
            skip_next = False
        elif token == "-o":
            skip_next = True
        elif not DROP.fullmatch(token):
            args.append(token)
    for path in includes:
        args += ["-isystem", path]
    return args


def run_format(sources: list[Path], fix: bool) -> bool:
    tool = find_clang_tool("clang-format")
    args = ["-i"] if fix else ["--dry-run", "--Werror"]
    return subprocess.run([tool, *args, *map(str, sources)]).returncode == 0


def run_tidy(build_dir: Path, sources: list[Path]) -> bool:
    database = json.loads((build_dir / "compile_commands.json").read_text())
    wanted = {str(path) for path in sources}
    ours = [e for e in database if e["file"] in wanted]
    if not ours:
        sys.exit(f"no firmware sources in {build_dir}/compile_commands.json: run `idf.py build` first")
    includes = gcc_system_includes(ours[0])

    tool = find_clang_tool("clang-tidy")
    ok = True
    with tempfile.TemporaryDirectory() as tmp:
        shim = Path(tmp, "clang_shim.h")
        shim.write_text(CLANG_SHIM)
        entries = [{"directory": e["directory"], "file": e["file"],
                    "arguments": clang_arguments(e, includes, shim)} for e in ours]
        Path(tmp, "compile_commands.json").write_text(json.dumps(entries))
        for entry in entries:
            print(f"clang-tidy {Path(entry['file']).relative_to(FIRMWARE)}", flush=True)
            result = subprocess.run([tool, "-p", tmp, "--quiet", "--warnings-as-errors=*", entry["file"]],
                                    stderr=subprocess.DEVNULL)
            ok = ok and result.returncode == 0
    return ok


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--build-dir", type=Path, default=FIRMWARE / "build")
    parser.add_argument("--fix-format", action="store_true", help="reformat files instead of only checking")
    args = parser.parse_args()

    sources = sorted({path for glob in SOURCE_GLOBS for path in FIRMWARE.glob(glob)
                      if "build" not in path.relative_to(FIRMWARE).parts})
    format_ok = run_format(sources, args.fix_format)
    tidy_ok = run_tidy(args.build_dir.resolve(), sources)
    print(f"clang-format: {'ok' if format_ok else 'FAILED'}\nclang-tidy:   {'ok' if tidy_ok else 'FAILED'}")
    return 0 if format_ok and tidy_ok else 1


if __name__ == "__main__":
    sys.exit(main())
