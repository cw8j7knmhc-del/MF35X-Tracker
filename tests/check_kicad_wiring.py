#!/usr/bin/env python3
"""Validate MF35X pinout -> wiring matrix -> generated KiCad schematic.

This is intentionally strict: the schematic must be a reproducible rendering of
hardware/wiring.csv and all firmware-relevant pins must be represented.
Unconfirmed physical details remain TODO/PARTIAL instead of being silently
promoted to VERIFIED.
"""
from __future__ import annotations

import csv
import importlib.util
import re
import sys
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PINOUT = ROOT / "hardware" / "pinout.csv"
WIRING = ROOT / "hardware" / "wiring.csv"
SCHEMATIC = ROOT / "hardware" / "kicad" / "MF35X_Tracker.kicad_sch"
GENERATOR = ROOT / "hardware" / "kicad" / "gen_schematic.py"

ALLOWED_STATUS = {"VERIFIED", "DERIVED", "PARTIAL", "TODO", "NC_RESERVED", "NC_UNUSED"}

EXPECTED_PINOUT_WIRING = {
    "I2C_SDA": ("I2C_SDA", "U1.GPIO8", "U2.SDA"),
    "I2C_SCL": ("I2C_SCL", "U1.GPIO9", "U2.SCL"),
    "RPM_PIN": ("RPM_IN", "U1.GPIO10", "U5.OUTPUT_SIGNAL"),
    "SCHALTAUSGANG_PIN": ("EXT_OUTPUT_LOGIC", "U1.GPIO11", "J1.PIN1"),
    "MAX31855_CLK": ("TC_CLK", "U1.GPIO12", "U4.CLK"),
    "MAX31855_CS": ("TC_CS", "U1.GPIO13", "U4.CS"),
    "MAX31855_DO": ("TC_DO", "U4.DO", "U1.GPIO14"),
    "GPS_RX": ("GPS_TX_TO_ESP_RX", "U3.TX", "U1.GPIO16"),
    "GPS_TX": ("ESP_TX_TO_GPS_RX", "U1.GPIO17", "U3.RX"),
    "ADS_KANAL_OELTEMP": ("OIL_TEMP_SENSE", "R1.2", "U2.AIN0"),
    "ADS_KANAL_OELDRUCK": ("OIL_PRESSURE_SENSE", "R2.2", "U2.AIN1"),
    "ADS_KANAL_BATTERIE": ("BATTERY_SENSE", "R3.2", "U2.AIN2"),
}


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as handle:
        return list(csv.DictReader(handle))


def endpoints(row: dict[str, str]) -> set[str]:
    return {x for x in (row["endpoint_a"].strip(), row["endpoint_b"].strip()) if x}


def fail(errors: list[str], message: str) -> None:
    errors.append(message)


def load_generator():
    spec = importlib.util.spec_from_file_location("mf35x_kicad_generator", GENERATOR)
    if spec is None or spec.loader is None:
        raise RuntimeError("KiCad generator could not be imported")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main() -> int:
    errors: list[str] = []

    for required in (PINOUT, WIRING, SCHEMATIC, GENERATOR):
        if not required.exists():
            fail(errors, f"missing required hardware file: {required.relative_to(ROOT)}")
    if errors:
        for error in errors:
            print(f"FAIL: {error}", file=sys.stderr)
        return 1

    pinout = read_csv(PINOUT)
    wiring = read_csv(WIRING)
    sch = SCHEMATIC.read_text(encoding="utf-8")

    # 1) Matrix schema/status rules.
    status_counts = Counter()
    for line_no, row in enumerate(wiring, start=2):
        status = row["status"].strip()
        net = row["net"].strip()
        status_counts[status] += 1
        if status not in ALLOWED_STATUS:
            fail(errors, f"wiring.csv:{line_no}: invalid status {status!r}")
        if status == "TODO" and "TODO" not in net:
            fail(errors, f"wiring.csv:{line_no}: TODO connection must have a visibly TODO net name: {net}")
        if status.startswith("NC_") and row["endpoint_b"].strip():
            fail(errors, f"wiring.csv:{line_no}: no-connect row must not invent a second endpoint")
        if status == "VERIFIED" and "TODO" in net:
            fail(errors, f"wiring.csv:{line_no}: TODO net cannot be VERIFIED")

    # 2) Every firmware/pinout mapping must have the expected hardware row.
    pinout_symbols = {row["symbol"].strip(): row for row in pinout}
    for symbol, (net, a, b) in EXPECTED_PINOUT_WIRING.items():
        if symbol not in pinout_symbols:
            fail(errors, f"pinout.csv is missing firmware symbol {symbol}")
            continue
        matches = [row for row in wiring if row["net"].strip() == net and {a, b}.issubset(endpoints(row))]
        if len(matches) != 1:
            fail(errors, f"expected exactly one wiring row for {symbol}: {a} <-> {b} on {net}; got {len(matches)}")

    # 3) Reserved/unused pins must be real no-connect states, never normal nets.
    nc_expect = {
        "GPIO0_RESERVED": ("U1.GPIO0", "NC_RESERVED"),
        "ADS_AIN3_UNUSED": ("U2.AIN3", "NC_UNUSED"),
    }
    for net, (endpoint, expected_status) in nc_expect.items():
        rows = [row for row in wiring if row["net"].strip() == net]
        if len(rows) != 1:
            fail(errors, f"expected one {net} row, got {len(rows)}")
            continue
        row = rows[0]
        if row["status"].strip() != expected_status or row["endpoint_a"].strip() != endpoint:
            fail(errors, f"{net} must be {endpoint} with status {expected_status}")
        if f'(global_label "{net}"' in sch:
            fail(errors, f"{net} incorrectly appears as a KiCad net label")

    no_connect_count = sch.count("(no_connect ")
    if no_connect_count != 2:
        fail(errors, f"expected exactly 2 KiCad no-connect markers (GPIO0, AIN3), got {no_connect_count}")

    # 4) Basic file integrity before KiCad itself parses/ERCs the schematic.
    if not sch.startswith("(kicad_sch"):
        fail(errors, "schematic does not start with (kicad_sch")
    if sch.count("(") != sch.count(")"):
        fail(errors, "schematic S-expression parentheses are unbalanced")
    uuids = re.findall(r"\(uuid\s+([0-9a-fA-F-]{36})\)", sch)
    if not uuids:
        fail(errors, "no KiCad UUIDs found")
    elif len(uuids) != len(set(uuids)):
        duplicates = [u for u, count in Counter(uuids).items() if count > 1]
        fail(errors, f"duplicate KiCad UUIDs: {duplicates[:5]}")

    # 5) The committed schematic must be byte-for-byte reproducible from the matrix.
    generator = load_generator()
    expected_schematic = generator.build()
    if sch != expected_schematic:
        fail(errors, "MF35X_Tracker.kicad_sch is stale; run: python hardware/kicad/gen_schematic.py")

    # 6) Check important net label multiplicity. These counts encode topology,
    # not just names, so accidental lost endpoints are caught.
    expected_label_counts = {
        "I2C_SDA": 2,
        "I2C_SCL": 2,
        "RPM_IN": 2,
        "EXT_OUTPUT_LOGIC": 2,
        "TC_CLK": 2,
        "TC_CS": 2,
        "TC_DO": 2,
        "GPS_TX_TO_ESP_RX": 2,
        "ESP_TX_TO_GPS_RX": 2,
        "OIL_TEMP_SENSE": 3,
        "OIL_PRESSURE_SENSE": 3,
        "BATTERY_SENSE": 3,
    }
    for net, expected_count in expected_label_counts.items():
        actual = sch.count(f'(global_label "{net}"')
        if actual != expected_count:
            fail(errors, f"KiCad net {net} has {actual} labels, expected {expected_count}")

    if errors:
        print("MF35X KiCad/wiring consistency: FAILED", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    print("MF35X KiCad/wiring consistency: OK")
    print(f"  wiring rows: {len(wiring)}")
    print(f"  status counts: {dict(status_counts)}")
    print(f"  schematic UUIDs: {len(uuids)} unique")
    print("  pinout -> wiring -> KiCad mappings: 12 verified")
    print("  GPIO0 + ADS AIN3: explicit no-connect markers")
    return 0


if __name__ == "__main__":
    sys.exit(main())
