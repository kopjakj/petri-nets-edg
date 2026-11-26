#!/usr/bin/env python3
"""Fetch the RP2040 B1 ROM module from the pinned rp2040js demo release.

The ROM is not redistributed in this Overleaf project; review the upstream
boot-ROM licenses before downloading or using it.
"""
from pathlib import Path
from urllib.request import urlopen

url = "https://raw.githubusercontent.com/wokwi/rp2040js/v1.4.2/demo/bootrom.ts"
source = urlopen(url, timeout=30).read().decode("utf-8")
needle = "export const bootromB1"
if source.count(needle) != 1:
    raise SystemExit("Unexpected upstream bootrom.ts format")
target = Path(__file__).with_name("bootrom.cjs")
target.write_text(source.replace(needle, "const bootromB1", 1) +
                  "\nmodule.exports = { bootromB1 };\n")
print(f"Fetched {url} -> {target}")
