#!/usr/bin/env python3
"""Host-side unit test for the exact GPIO11 control function in firmware.

The script extracts schaltausgangAktualisieren() verbatim from the production
core, compiles that exact function with small hardware stubs, and executes
boundary/fail-safe tests on a normal Linux runner. No ESP32 hardware is needed.
"""

from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
CORE = ROOT / "esp32" / "MF35X_Livetracker" / "MF35X_Livetracker_core.hpp"
SIGNATURE = "void schaltausgangAktualisieren()"


def extract_function(source: str, signature: str) -> str:
    # Only accept a real definition. The core also contains a forward
    # declaration `void schaltausgangAktualisieren();` which must not match.
    definition = signature + " {"
    start = source.find(definition)
    if start < 0:
        raise RuntimeError(f"Funktionsdefinition nicht gefunden: {definition}")

    brace = source.find("{", start + len(signature))
    if brace < 0:
        raise RuntimeError("Oeffnende Klammer der Funktion nicht gefunden")

    depth = 0
    for pos in range(brace, len(source)):
        char = source[pos]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[start : pos + 1]

    raise RuntimeError("Funktionsende nicht gefunden")


def build_test_source(function_source: str) -> str:
    return r'''
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>

struct GpsSnapshot {
  bool speedValid = false;
  double speedKmh = 0.0;
};

struct OutputConfig {
  float speedEnableKmh;
  float rpmOn;
  float rpmOff;
};

GpsSnapshot testGps;
bool testGpsFixCurrent = false;
OutputConfig outputConfig = {60.0f, 3200.0f, 3150.0f};
int outputConfigMux = 0;
volatile bool rpmSignalOk = false;
volatile float rpm = 0.0f;
volatile bool schaltausgangAktiv = false;
constexpr int SCHALTAUSGANG_PIN = 11;
constexpr int HIGH = 1;
constexpr int LOW = 0;
int lastDigitalWritePin = -1;
int lastDigitalWriteValue = -1;

#define portENTER_CRITICAL(x) do { (void)(x); } while (0)
#define portEXIT_CRITICAL(x) do { (void)(x); } while (0)

GpsSnapshot gpsSnapshotLesen() { return testGps; }
bool gpsFixAktuell(const GpsSnapshot&) { return testGpsFixCurrent; }
void digitalWrite(int pin, int value) {
  lastDigitalWritePin = pin;
  lastDigitalWriteValue = value;
}

''' + function_source + r'''

int failures = 0;
int checks = 0;

void setInputs(
    bool previousOn,
    bool gpsFix,
    bool speedValid,
    double speedKmh,
    bool rpmOk,
    float rpmValue,
    float speedEnable = 60.0f,
    float rpmOn = 3200.0f,
    float rpmOff = 3150.0f) {
  schaltausgangAktiv = previousOn;
  testGpsFixCurrent = gpsFix;
  testGps.speedValid = speedValid;
  testGps.speedKmh = speedKmh;
  rpmSignalOk = rpmOk;
  rpm = rpmValue;
  outputConfig = {speedEnable, rpmOn, rpmOff};
  lastDigitalWritePin = -1;
  lastDigitalWriteValue = -1;
}

void expectState(const std::string& name, bool expected) {
  ++checks;
  schaltausgangAktualisieren();
  const bool actual = schaltausgangAktiv;
  const int expectedWrite = expected ? HIGH : LOW;

  if (actual != expected ||
      lastDigitalWritePin != SCHALTAUSGANG_PIN ||
      lastDigitalWriteValue != expectedWrite) {
    ++failures;
    std::cerr << "FAIL: " << name
              << " state=" << actual << " expected=" << expected
              << " pin=" << lastDigitalWritePin
              << " write=" << lastDigitalWriteValue
              << " expected_write=" << expectedWrite << "\n";
  } else {
    std::cout << "PASS: " << name << "\n";
  }
}

int main() {
  // Fail-safe conditions must always force LOW.
  setInputs(true, false, true, 80.0, true, 4000.0f);
  expectState("GPS fix lost forces LOW", false);

  setInputs(true, true, false, 80.0, true, 4000.0f);
  expectState("invalid speed forces LOW", false);

  setInputs(true, true, true, 80.0, false, 4000.0f);
  expectState("RPM signal lost forces LOW", false);

  // Speed threshold: exactly threshold is allowed; below is not.
  setInputs(false, true, true, 59.9, true, 4000.0f, 60.0f);
  expectState("59.9 below 60 speed threshold", false);

  setInputs(false, true, true, 60.0, true, 3200.0f, 60.0f);
  expectState("60.0 equals speed threshold", true);

  // Explicitly verify the alternative 61 km/h configuration used at times.
  setInputs(false, true, true, 60.9, true, 4000.0f, 61.0f);
  expectState("60.9 below configured 61 speed threshold", false);

  setInputs(false, true, true, 61.0, true, 3200.0f, 61.0f);
  expectState("61.0 equals configured speed threshold", true);

  // RPM ON boundary: 3200 turns on; 3199 does not.
  setInputs(false, true, true, 70.0, true, 3199.0f);
  expectState("3199 does not switch ON", false);

  setInputs(false, true, true, 70.0, true, 3200.0f);
  expectState("3200 switches ON", true);

  setInputs(false, true, true, 70.0, true, 3201.0f);
  expectState("3201 switches ON", true);

  // Hysteresis: when already ON, 3150 remains ON; below 3150 turns OFF.
  setInputs(true, true, true, 70.0, true, 3150.0f);
  expectState("3150 remains ON", true);

  setInputs(true, true, true, 70.0, true, 3149.0f);
  expectState("3149 switches OFF", false);

  // Inside hysteresis band, previous state must be retained.
  setInputs(false, true, true, 70.0, true, 3175.0f);
  expectState("3175 retains previous OFF", false);

  setInputs(true, true, true, 70.0, true, 3175.0f);
  expectState("3175 retains previous ON", true);

  // Sequence test: ON -> hysteresis hold -> RPM OFF -> cannot re-enable below ON.
  setInputs(false, true, true, 70.0, true, 3200.0f);
  schaltausgangAktualisieren();
  rpm = 3175.0f;
  schaltausgangAktualisieren();
  rpm = 3149.0f;
  schaltausgangAktualisieren();
  ++checks;
  if (schaltausgangAktiv) {
    ++failures;
    std::cerr << "FAIL: sequence should be OFF after dropping below 3150\n";
  } else {
    std::cout << "PASS: ON-hold-OFF sequence\n";
  }

  rpm = 3199.0f;
  schaltausgangAktualisieren();
  ++checks;
  if (schaltausgangAktiv) {
    ++failures;
    std::cerr << "FAIL: output re-enabled below 3200 after OFF\n";
  } else {
    std::cout << "PASS: no re-enable below 3200\n";
  }

  // Losing the speed permission after ON must immediately disable output.
  setInputs(true, true, true, 70.0, true, 3300.0f);
  schaltausgangAktualisieren();
  testGps.speedKmh = 59.9;
  ++checks;
  schaltausgangAktualisieren();
  if (schaltausgangAktiv) {
    ++failures;
    std::cerr << "FAIL: speed loss did not force LOW\n";
  } else {
    std::cout << "PASS: speed loss after ON forces LOW\n";
  }

  std::cout << "GPIO11 source tests: " << (checks - failures)
            << "/" << checks << " passed\n";
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
'''


def main() -> int:
    source = CORE.read_text(encoding="utf-8")
    function_source = extract_function(source, SIGNATURE)
    cpp = build_test_source(function_source)

    with tempfile.TemporaryDirectory(prefix="mf35x-output-test-") as temp:
        temp_path = Path(temp)
        source_path = temp_path / "test_output_control.cpp"
        binary_path = temp_path / "test_output_control"
        source_path.write_text(cpp, encoding="utf-8")

        compile_result = subprocess.run(
            ["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", str(source_path), "-o", str(binary_path)],
            text=True,
            capture_output=True,
        )
        if compile_result.returncode != 0:
            print("Host-Kompilierung der echten GPIO11-Funktion fehlgeschlagen.", file=sys.stderr)
            print(compile_result.stdout, file=sys.stderr)
            print(compile_result.stderr, file=sys.stderr)
            return compile_result.returncode

        run_result = subprocess.run([str(binary_path)], text=True)
        return run_result.returncode


if __name__ == "__main__":
    sys.exit(main())
