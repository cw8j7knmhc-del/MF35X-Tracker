#!/usr/bin/env python3
"""Validate MF35X diagnostic race CSV exports.

The checker is dependency-free and understands the tracker export format
(semicolon delimiter, decimal comma, UTF-8 BOM). Real race files are not
committed to the public repository; they can be checked locally by passing
their paths on the command line.
"""

from __future__ import annotations

import argparse
import csv
import io
import sys
from collections import Counter
from pathlib import Path

REQUIRED = {
    "timestamp",
    "Drehzahl_Anzeige_Umin",
    "Geschwindigkeit_kmh",
    "GPIO11_switch_output",
    "RPM_raw_edges_total",
    "RPM_accepted_edges_total",
    "RPM_rejected_edges_total",
    "RPM_double_edges_total",
    "RPM_signal_ok",
    "Sample_ID",
    "Sample_Boot_ID",
    "Sample_Sequence",
}


def as_float(value: str | None) -> float | None:
    if value is None:
        return None
    value = value.strip()
    if not value:
        return None
    return float(value.replace(",", "."))


def as_int(value: str | None) -> int | None:
    number = as_float(value)
    if number is None:
        return None
    return int(number)


def is_yes(value: str | None) -> bool:
    return (value or "").strip().upper() in {"JA", "YES", "TRUE", "1", "EIN", "HIGH"}


def analyze_stream(
    handle,
    label: str,
    speed_enable: float,
    rpm_on: float,
    rpm_off: float,
) -> tuple[int, list[str]]:
    reader = csv.DictReader(handle, delimiter=";")
    if not reader.fieldnames:
        return 1, [f"{label}: keine Kopfzeile gefunden"]

    missing = sorted(REQUIRED - set(reader.fieldnames))
    if missing:
        return 1, [f"{label}: Pflichtspalten fehlen: {', '.join(missing)}"]

    errors: list[str] = []
    rows = 0
    sample_ids: set[str] = set()
    last_seq: dict[str, int] = {}
    last_counters: dict[str, tuple[int, int, int, int]] = {}
    diag = Counter()
    output = Counter()
    rpm_values: list[float] = []
    speed_values: list[float] = []

    for line_no, row in enumerate(reader, start=2):
        rows += 1
        if None in row:
            errors.append(f"{label}:{line_no}: zu viele Felder in Zeile")
            continue

        sample_id = (row.get("Sample_ID") or "").strip()
        boot = (row.get("Sample_Boot_ID") or "").strip()
        seq = as_int(row.get("Sample_Sequence"))

        if not sample_id:
            errors.append(f"{label}:{line_no}: Sample_ID leer")
        elif sample_id in sample_ids:
            errors.append(f"{label}:{line_no}: doppelte Sample_ID {sample_id}")
        else:
            sample_ids.add(sample_id)

        if not boot or seq is None:
            errors.append(f"{label}:{line_no}: Boot-ID oder Sequenz fehlt")
        else:
            if boot in last_seq and seq <= last_seq[boot]:
                errors.append(
                    f"{label}:{line_no}: Sample_Sequence fuer Boot {boot} nicht steigend "
                    f"({seq} <= {last_seq[boot]})"
                )
            last_seq[boot] = seq

        raw = as_int(row.get("RPM_raw_edges_total"))
        accepted = as_int(row.get("RPM_accepted_edges_total"))
        rejected = as_int(row.get("RPM_rejected_edges_total"))
        double = as_int(row.get("RPM_double_edges_total"))

        if None not in (raw, accepted, rejected) and raw != accepted + rejected:
            errors.append(
                f"{label}:{line_no}: RPM-Zaehler inkonsistent: raw={raw}, "
                f"accepted={accepted}, rejected={rejected}"
            )

        if None not in (double, rejected) and double > rejected:
            errors.append(
                f"{label}:{line_no}: double_edges ({double}) > rejected_edges ({rejected})"
            )

        if boot and None not in (raw, accepted, rejected, double):
            counters = (raw, accepted, rejected, double)
            previous = last_counters.get(boot)
            if previous and any(now < old for now, old in zip(counters, previous)):
                errors.append(
                    f"{label}:{line_no}: RPM-Kumulativzaehler fuer Boot {boot} ruecklaeufig"
                )
            last_counters[boot] = counters

        rpm = as_float(row.get("Drehzahl_Anzeige_Umin"))
        speed = as_float(row.get("Geschwindigkeit_kmh"))
        rpm_ok = is_yes(row.get("RPM_signal_ok"))
        output_on = is_yes(row.get("GPIO11_switch_output"))

        if rpm is not None:
            rpm_values.append(rpm)
        if speed is not None:
            speed_values.append(speed)

        output["ON" if output_on else "OFF"] += 1
        diagnostic = (row.get("Oeldruck_Diagnose") or "").strip() or "LEER"
        diag[diagnostic] += 1

        # Strong safety invariants. Hysteresis permits an already active output
        # between rpm_off and rpm_on, but never below rpm_off.
        if output_on:
            if speed is None or speed < speed_enable:
                errors.append(f"{label}:{line_no}: GPIO11 HIGH ohne Speed-Freigabe")
            if not rpm_ok:
                errors.append(f"{label}:{line_no}: GPIO11 HIGH ohne gueltiges RPM-Signal")
            if rpm is None or rpm < rpm_off:
                errors.append(f"{label}:{line_no}: GPIO11 HIGH unter RPM-OFF-Schwelle")

    print(f"{label}: {rows} Samples")
    if rpm_values:
        print(f"  RPM min/max: {min(rpm_values):.1f} / {max(rpm_values):.1f}")
    if speed_values:
        print(f"  Speed min/max: {min(speed_values):.1f} / {max(speed_values):.1f} km/h")
    print(f"  GPIO11: {dict(output)}")
    print(f"  Oeldruck-Diagnose: {dict(diag)}")

    if rpm_values and max(rpm_values) < rpm_on and output["ON"]:
        print(
            f"  WARNUNG: GPIO11 war HIGH, obwohl die Datei nie {rpm_on:.0f} RPM erreicht. "
            "Moeglicherweise begann die Aufzeichnung bei bereits aktivem Ausgang."
        )

    if errors:
        print("  FEHLER:")
        for error in errors[:50]:
            print(f"    - {error}")
        if len(errors) > 50:
            print(f"    - ... {len(errors) - 50} weitere")
        return 1, errors

    print("  Struktur- und Sicherheitsinvarianten: OK")
    return 0, []


def analyze_file(path: Path, speed_enable: float, rpm_on: float, rpm_off: float) -> int:
    with path.open("r", encoding="utf-8-sig", newline="") as handle:
        rc, _ = analyze_stream(handle, path.name, speed_enable, rpm_on, rpm_off)
        return rc


def self_test() -> int:
    header = [
        "timestamp",
        "Drehzahl_Anzeige_Umin",
        "Geschwindigkeit_kmh",
        "GPIO11_switch_output",
        "Oeldruck_Diagnose",
        "RPM_raw_edges_total",
        "RPM_accepted_edges_total",
        "RPM_rejected_edges_total",
        "RPM_double_edges_total",
        "RPM_signal_ok",
        "Sample_ID",
        "Sample_Boot_ID",
        "Sample_Sequence",
    ]
    rows = [
        ["1", "3199,0", "61,0", "NEIN", "OK", "10", "9", "1", "1", "JA", "s1", "1", "1"],
        ["2", "3200,0", "61,0", "JA", "OK", "20", "18", "2", "2", "JA", "s2", "1", "2"],
        ["3", "3150,0", "61,0", "JA", "OK", "30", "27", "3", "3", "JA", "s3", "1", "3"],
        ["4", "3149,0", "61,0", "NEIN", "OK", "40", "36", "4", "4", "JA", "s4", "1", "4"],
    ]

    stream = io.StringIO()
    writer = csv.writer(stream, delimiter=";", lineterminator="\n")
    writer.writerow(header)
    writer.writerows(rows)
    stream.seek(0)

    rc, errors = analyze_stream(stream, "self-test", 61.0, 3200.0, 3150.0)
    if rc != 0 or errors:
        print("Self-test FAILED")
        return 1
    print("Self-test OK")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Check MF35X race diagnostic CSV invariants")
    parser.add_argument("files", nargs="*", type=Path)
    parser.add_argument("--speed-enable", type=float, default=60.0)
    parser.add_argument("--rpm-on", type=float, default=3200.0)
    parser.add_argument("--rpm-off", type=float, default=3150.0)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        return self_test()
    if not args.files:
        parser.error("mindestens eine CSV-Datei angeben oder --self-test verwenden")

    result = 0
    for path in args.files:
        result |= analyze_file(path, args.speed_enable, args.rpm_on, args.rpm_off)
    return result


if __name__ == "__main__":
    sys.exit(main())
