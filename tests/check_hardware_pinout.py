#!/usr/bin/env python3
"""Verify that firmware hardware constants match hardware/pinout.csv.

This test is intentionally simple and dependency-free so it can run locally and
in GitHub Actions. It does not change firmware behavior; it only fails when the
canonical hardware definition and the firmware drift apart.
"""

from __future__ import annotations

import csv
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / "esp32" / "MF35X_Livetracker" / "MF35X_Livetracker_core.hpp"
PINOUT = ROOT / "hardware" / "pinout.csv"

CONST_RE = re.compile(r"constexpr\s+int\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(-?\d+)\s*;")


def fail(message: str) -> None:
    print(f"ERROR: {message}")
    raise SystemExit(1)


def main() -> int:
    if not CORE.exists():
        fail(f"Firmware core not found: {CORE}")
    if not PINOUT.exists():
        fail(f"Pinout file not found: {PINOUT}")

    source = CORE.read_text(encoding="utf-8")
    firmware_constants = {name: int(value) for name, value in CONST_RE.findall(source)}

    rows: list[dict[str, str]] = []
    with PINOUT.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        required_columns = {"category", "symbol", "value", "connection", "notes"}
        if not reader.fieldnames or not required_columns.issubset(reader.fieldnames):
            fail(f"pinout.csv must contain columns: {sorted(required_columns)}")
        rows = list(reader)

    errors: list[str] = []
    checked = 0

    for row in rows:
        category = row["category"].strip().upper()
        symbol = row["symbol"].strip()
        try:
            expected = int(row["value"].strip())
        except ValueError:
            errors.append(f"{symbol}: invalid numeric value {row['value']!r}")
            continue

        if category in {"GPIO", "ADS"}:
            checked += 1
            actual = firmware_constants.get(symbol)
            if actual is None:
                errors.append(f"{symbol}: missing from firmware core")
            elif actual != expected:
                errors.append(f"{symbol}: firmware={actual}, pinout.csv={expected}")

    gpio_rows = [row for row in rows if row["category"].strip().upper() == "GPIO"]
    used_by_gpio: dict[int, list[str]] = {}
    for row in gpio_rows:
        try:
            pin = int(row["value"].strip())
        except ValueError:
            continue
        used_by_gpio.setdefault(pin, []).append(row["symbol"].strip())

    for pin, symbols in sorted(used_by_gpio.items()):
        if len(symbols) > 1:
            errors.append(f"GPIO{pin} assigned more than once: {', '.join(symbols)}")

    reserved_pins = {
        int(row["value"].strip())
        for row in rows
        if row["category"].strip().upper() == "RESERVED" and row["value"].strip().lstrip("-").isdigit()
    }
    for pin in sorted(reserved_pins):
        if pin in used_by_gpio:
            errors.append(
                f"Reserved GPIO{pin} is assigned to: {', '.join(used_by_gpio[pin])}"
            )

    # The source explicitly documents GPIO0 as permanently free. Keep that
    # requirement machine-checked even if the CSV is edited incorrectly later.
    if 0 in used_by_gpio:
        errors.append("GPIO0 must remain unused")

    if errors:
        print("Hardware pinout check FAILED:")
        for error in errors:
            print(f"  - {error}")
        return 1

    print(f"Hardware pinout check OK: {checked} firmware mappings verified.")
    print("GPIO uniqueness OK; reserved pins are unused.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
