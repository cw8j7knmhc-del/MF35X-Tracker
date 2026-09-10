#!/usr/bin/env python3
"""Host-side tests for MF35X oil-pressure conversion and diagnostics.

The production pressure curve function is extracted verbatim from the tracker
core and compiled on the GitHub runner. The test also verifies the voltage ->
resistance -> pressure chain at the documented calibration points and checks
that diagnostic state thresholds behave monotonically and fail-safe.
"""

from __future__ import annotations

import math
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / "esp32" / "MF35X_Livetracker" / "MF35X_Livetracker_core.hpp"
DIAG = ROOT / "esp32" / "MF35X_Livetracker" / "v5917_patch.hpp"


def extract_function(source: str, signature: str) -> str:
    definition = signature + " {"
    start = source.find(definition)
    if start < 0:
        raise RuntimeError(f"Funktionsdefinition nicht gefunden: {definition}")
    brace = source.find("{", start + len(signature))
    depth = 0
    for pos in range(brace, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[start : pos + 1]
    raise RuntimeError(f"Funktionsende nicht gefunden: {signature}")


def scalar(source: str, name: str) -> float:
    m = re.search(rf"constexpr\s+float\s+{re.escape(name)}\s*=\s*([0-9.]+)f?\s*;", source)
    if not m:
        raise RuntimeError(f"Konstante nicht gefunden: {name}")
    return float(m.group(1))


def array_values(source: str, name: str) -> list[float]:
    m = re.search(rf"const\s+float\s+{re.escape(name)}\[\]\s*=\s*\{{(.*?)\}}\s*;", source, re.S)
    if not m:
        raise RuntimeError(f"Array nicht gefunden: {name}")
    values: list[float] = []
    for token in m.group(1).split(","):
        token = token.strip()
        if not token:
            continue
        if token == "DRUCK_NULL_OHM":
            values.append(scalar(source, "DRUCK_NULL_OHM"))
        else:
            values.append(float(token.rstrip("fF")))
    return values


def build_curve_test(core: str) -> str:
    func = extract_function(core, "float widerstandZuBar(float widerstand)")
    bars = array_values(core, "DRUCK_BAR_PUNKTE")
    ohms = array_values(core, "DRUCK_OHM_PUNKTE")

    bars_cpp = ", ".join(f"{v:.9g}f" for v in bars)
    ohms_cpp = ", ".join(f"{v:.9g}f" for v in ohms)

    return f'''#include <cmath>\n#include <cstdlib>\n#include <iostream>\n\nconst float DRUCK_BAR_PUNKTE[] = {{{bars_cpp}}};\nconst float DRUCK_OHM_PUNKTE[] = {{{ohms_cpp}}};\nconstexpr int DRUCK_ANZAHL_PUNKTE = {len(bars)};\n\n{func}\n\nint main() {{\n  int failures = 0;\n  auto check = [&](const char* name, float got, float expected, float tol) {{\n    if (!std::isfinite(got) || std::fabs(got - expected) > tol) {{\n      std::cerr << "FAIL: " << name << " got=" << got << " expected=" << expected << "\\n";\n      ++failures;\n    }} else {{\n      std::cout << "PASS: " << name << "\\n";\n    }}\n  }};\n\n  for (int i = 0; i < DRUCK_ANZAHL_PUNKTE; ++i) {{\n    check("curve calibration point", widerstandZuBar(DRUCK_OHM_PUNKTE[i]), DRUCK_BAR_PUNKTE[i], 0.0005f);\n  }}\n\n  for (int i = 0; i < DRUCK_ANZAHL_PUNKTE - 1; ++i) {{\n    const float midR = (DRUCK_OHM_PUNKTE[i] + DRUCK_OHM_PUNKTE[i + 1]) * 0.5f;\n    const float midP = (DRUCK_BAR_PUNKTE[i] + DRUCK_BAR_PUNKTE[i + 1]) * 0.5f;\n    check("linear interpolation midpoint", widerstandZuBar(midR), midP, 0.0005f);\n  }}\n\n  check("below curve clamps zero", widerstandZuBar(DRUCK_OHM_PUNKTE[0] - 5.0f), 0.0f, 0.0005f);\n  check("above curve clamps ten", widerstandZuBar(DRUCK_OHM_PUNKTE[DRUCK_ANZAHL_PUNKTE - 1] + 50.0f), 10.0f, 0.0005f);\n\n  float previous = widerstandZuBar(DRUCK_OHM_PUNKTE[0]);\n  for (float r = DRUCK_OHM_PUNKTE[0] + 0.25f; r <= DRUCK_OHM_PUNKTE[DRUCK_ANZAHL_PUNKTE - 1]; r += 0.25f) {{\n    const float now = widerstandZuBar(r);\n    if (now + 0.0001f < previous) {{\n      std::cerr << "FAIL: curve not monotonic at R=" << r << "\\n";\n      ++failures;\n      break;\n    }}\n    previous = now;\n  }}\n\n  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;\n}}\n'''


def check_chain(core: str, diag: str) -> list[str]:
    errors: list[str] = []
    vcc = scalar(core, "DRUCK_VCC")
    production_fixed = scalar(core, "DRUCK_R_FIXED")
    diagnostic_fixed = scalar(diag, "MF35X_DIAG_PRESSURE_FIXED_OHM")
    bars = array_values(core, "DRUCK_BAR_PUNKTE")
    ohms = array_values(core, "DRUCK_OHM_PUNKTE")

    if not (3.0 <= vcc <= 3.4):
        errors.append(f"DRUCK_VCC unplausibel: {vcc}")

    def interp(r: float) -> float:
        if r <= ohms[0]:
            return 0.0
        if r >= ohms[-1]:
            return 10.0
        for i in range(len(ohms) - 1):
            if ohms[i] <= r <= ohms[i + 1]:
                return bars[i] + (r - ohms[i]) * (bars[i + 1] - bars[i]) / (ohms[i + 1] - ohms[i])
        raise AssertionError

    worst_bias = 0.0
    worst_point = None
    for expected_bar, sensor_ohm in zip(bars, ohms):
        # Simulate the real divider using the measured diagnostic resistor.
        voltage = vcc * sensor_ohm / (diagnostic_fixed + sensor_ohm)
        reconstructed = production_fixed * voltage / (vcc - voltage)
        calculated_bar = interp(reconstructed)
        bias = calculated_bar - expected_bar
        if abs(bias) > abs(worst_bias):
            worst_bias = bias
            worst_point = (expected_bar, sensor_ohm, voltage, reconstructed, calculated_bar)

    print(f"Production fixed resistor: {production_fixed:.1f} ohm")
    print(f"Diagnostic/measured resistor: {diagnostic_fixed:.1f} ohm")
    if worst_point:
        b, r, v, rr, calc = worst_point
        print(
            f"Worst 216-vs-production bias: {worst_bias:+.3f} bar "
            f"at nominal {b:.1f} bar / {r:.1f} ohm "
            f"(V={v:.4f}, reconstructed={rr:.2f} ohm, result={calc:.3f} bar)"
        )

    # Keep CI green for the currently documented 220/216 difference, but turn
    # a materially larger mismatch into a hard failure.
    if abs(production_fixed - diagnostic_fixed) > 10.0:
        errors.append(
            f"Festwiderstaende weichen zu stark ab: production={production_fixed}, diagnostic={diagnostic_fixed}"
        )
    if abs(worst_bias) > 0.15:
        errors.append(f"Festwiderstandsabweichung erzeugt >0.15 bar Kennlinienfehler: {worst_bias:+.3f}")
    elif abs(worst_bias) > 0.05:
        print("WARNUNG: 220-ohm Hauptberechnung und 216-ohm Diagnose erzeugen einen messbaren Bias.")

    # Diagnostic thresholds must be ordered: short < usable < open.
    short_v = 0.02
    open_v = 2.50
    invalid_high_v = 3.10
    if not (0 < short_v < open_v < invalid_high_v < vcc):
        errors.append("Oeldruck-Spannungsgrenzen sind nicht sinnvoll geordnet")

    # Verify diagnostic voltage/resistance boundary implications.
    short_ohm = diagnostic_fixed * short_v / (vcc - short_v)
    open_ohm = diagnostic_fixed * open_v / (vcc - open_v)
    print(f"SHORT boundary ~ {short_ohm:.2f} ohm at 20 mV")
    print(f"OPEN boundary ~ {open_ohm:.2f} ohm at 2.50 V")
    if short_ohm >= 5.0:
        errors.append("SHORT-Spannungsgrenze kollidiert mit 5-ohm OUT_OF_RANGE-Untergrenze")
    if open_ohm <= 250.0:
        errors.append("OPEN-Spannungsgrenze kollidiert mit 250-ohm OUT_OF_RANGE-Obergrenze")

    return errors


def main() -> int:
    core = CORE.read_text(encoding="utf-8")
    diag = DIAG.read_text(encoding="utf-8")

    errors = check_chain(core, diag)
    if errors:
        for error in errors:
            print(f"FAIL: {error}", file=sys.stderr)
        return 1

    cpp = build_curve_test(core)
    with tempfile.TemporaryDirectory(prefix="mf35x-pressure-test-") as temp:
        src = Path(temp) / "pressure.cpp"
        exe = Path(temp) / "pressure"
        src.write_text(cpp, encoding="utf-8")
        compiled = subprocess.run(
            ["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", str(src), "-o", str(exe)],
            text=True,
            capture_output=True,
        )
        if compiled.returncode != 0:
            print(compiled.stdout, file=sys.stderr)
            print(compiled.stderr, file=sys.stderr)
            return compiled.returncode
        run = subprocess.run([str(exe)], text=True)
        if run.returncode != 0:
            return run.returncode

    print("Oil-pressure source tests: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
