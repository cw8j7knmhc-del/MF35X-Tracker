#!/usr/bin/env python3
"""Independently verify that KiCad labels/NC markers sit on the intended pins.

Unlike check_kicad_wiring.py, this test does NOT compare the schematic with
`generator.build()` and therefore does not inherit the generator's coordinate
math. It computes KiCad sheet coordinates from symbol-local pin positions using
KiCad's rotation-0 transform: sheet_x = symbol_x + local_x,
sheet_y = symbol_y - local_y.

This specifically prevents a regression of the DRAFT-1 Y-axis mirroring bug
that initially moved labels and GPIO0/AIN3 no-connect markers onto wrong pins.
"""
from __future__ import annotations

import csv
import importlib.util
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WIRING = ROOT / "hardware" / "wiring.csv"
SCHEMATIC = ROOT / "hardware" / "kicad" / "MF35X_Tracker.kicad_sch"
GENERATOR = ROOT / "hardware" / "kicad" / "gen_schematic.py"
NC_STATES = {"NC_RESERVED", "NC_UNUSED"}


def load_generator():
    spec = importlib.util.spec_from_file_location("mf35x_kicad_anchor_model", GENERATOR)
    if spec is None or spec.loader is None:
        raise RuntimeError("KiCad generator model could not be imported")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def fmt(value: float) -> str:
    return f"{value:.2f}"


def label_token(net: str, x: float, y: float) -> str:
    return f'(global_label "{net}" (shape bidirectional) (at {fmt(x)} {fmt(y)} 0)'


def nc_token(x: float, y: float) -> str:
    return f'(no_connect (at {fmt(x)} {fmt(y)})'


def main() -> int:
    model = load_generator()
    lookup = model.pin_lookup()
    schematic = SCHEMATIC.read_text(encoding="utf-8")

    with WIRING.open(newline="", encoding="utf-8") as handle:
        rows = list(csv.DictReader(handle))

    errors: list[str] = []
    checked_labels: set[tuple[str, str]] = set()
    checked_ncs: set[str] = set()

    for line_no, row in enumerate(rows, start=2):
        net = row["net"].strip()
        status = row["status"].strip()

        for endpoint in (row["endpoint_a"].strip(), row["endpoint_b"].strip()):
            if not endpoint or endpoint not in lookup:
                continue

            sym, pin = lookup[endpoint]

            # Independent KiCad rotation-0 local -> sheet coordinate transform.
            # Do not replace this with a helper from gen_schematic.py.
            x = sym.x + pin.x
            y = sym.y - pin.y
            canonical_endpoint = f"{sym.ref}.{pin.name}"

            if status in NC_STATES:
                if canonical_endpoint in checked_ncs:
                    continue
                checked_ncs.add(canonical_endpoint)
                token = nc_token(x, y)
                if token not in schematic:
                    errors.append(
                        f"wiring.csv:{line_no}: {canonical_endpoint} {status} marker missing "
                        f"at actual KiCad pin anchor ({fmt(x)}, {fmt(y)})"
                    )
                continue

            key = (canonical_endpoint, net)
            if key in checked_labels:
                continue
            checked_labels.add(key)

            token = label_token(net, x, y)
            if token not in schematic:
                errors.append(
                    f"wiring.csv:{line_no}: net {net} is not attached to {canonical_endpoint} "
                    f"at actual KiCad pin anchor ({fmt(x)}, {fmt(y)})"
                )

    # Explicit high-risk sentinels. These are intentionally redundant so a
    # later refactor of the generic loop cannot weaken the original safety fix.
    sentinels = {
        "GPIO0 no-connect": nc_token(42.00, 62.62),
        "ADS1115 AIN3 no-connect": nc_token(120.00, 58.81),
        "ESP32 GPIO14 / TC_DO": label_token("TC_DO", 58.00, 56.27),
        "GPS RX from ESP32 TX": label_token("ESP_TX_TO_GPS_RX", 162.00, 58.81),
        "ADS1115 VDD / +3V3": label_token("+3V3", 104.00, 49.92),
        "J2 pin 1 / +5V_IN": label_token("+5V_IN", 42.00, 128.73),
    }
    for name, token in sentinels.items():
        if token not in schematic:
            errors.append(f"high-risk anchor missing: {name}")

    # Known mirrored locations from the original generator bug must never be
    # used as no-connect markers.
    forbidden_mirrored_ncs = {
        "mirrored GPIO0": nc_token(42.00, 47.38),
        "mirrored ADS AIN3": nc_token(120.00, 51.19),
    }
    for name, token in forbidden_mirrored_ncs.items():
        if token in schematic:
            errors.append(f"forbidden stale no-connect found: {name}")

    if errors:
        print("MF35X KiCad pin-anchor check: FAILED", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    print("MF35X KiCad pin-anchor check: OK")
    print(f"  net/pin anchors checked: {len(checked_labels)}")
    print(f"  explicit no-connect anchors checked: {len(checked_ncs)}")
    print("  original mirrored GPIO0/AIN3 locations are absent")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
