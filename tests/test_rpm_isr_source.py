#!/usr/bin/env python3
"""Host-side tests for the exact V5.9.19 robust RPM ISR.

The production function mf35xStabileRpmISR() is extracted verbatim from
rpm_stable_override.hpp and compiled with deterministic micros() and FreeRTOS
critical-section stubs. This lets CI replay precise W-signal edge timings.
"""

from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "esp32" / "MF35X_Livetracker" / "rpm_stable_override.hpp"
SIGNATURE = "void IRAM_ATTR mf35xStabileRpmISR()"


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
    raise RuntimeError("Funktionsende nicht gefunden")


def build_test_source(function_source: str) -> str:
    return r'''
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#define IRAM_ATTR
#define portENTER_CRITICAL_ISR(x) do { (void)(x); } while (0)
#define portEXIT_CRITICAL_ISR(x) do { (void)(x); } while (0)

constexpr uint32_t RPM_SIGNAL_TIMEOUT_US = 500000UL;
constexpr uint32_t MF35X_RPM_MIN_PULSABSTAND_US = 500UL;
constexpr uint8_t MF35X_RPM_PERIODEN_ANZAHL = 21;
constexpr uint32_t MF35X_RPM_PLAUS_MIN_PROZENT = 92UL;
constexpr uint32_t MF35X_RPM_PLAUS_MAX_PROZENT = 108UL;
constexpr uint8_t MF35X_RPM_MAX_LUECKENFAKTOR = 4;
constexpr uint8_t MF35X_RPM_NEUERFASSUNG_NACH_FEHLERN = 12;
constexpr uint8_t MF35X_RPM_NEUERFASSUNG_BESTAETIGUNGEN = 7;
constexpr uint32_t MF35X_RPM_NEUERFASSUNG_MIN_PROZENT = 88UL;
constexpr uint32_t MF35X_RPM_NEUERFASSUNG_MAX_PROZENT = 112UL;
constexpr uint32_t MF35X_RPM_DOPPEL_MIN_PROZENT = 42UL;
constexpr uint32_t MF35X_RPM_DOPPEL_MAX_PROZENT = 58UL;

int mf35xRpmMux = 0;
uint32_t simulatedMicros = 0;
uint32_t micros() { return simulatedMicros; }

volatile uint32_t mf35xRpmLetzterImpulsUs = 0;
volatile uint32_t mf35xRpmPeriodenUs[MF35X_RPM_PERIODEN_ANZAHL] = {};
volatile uint8_t mf35xRpmPeriodenIndex = 0;
volatile uint8_t mf35xRpmPeriodenCount = 0;
volatile uint32_t mf35xRpmReferenzPeriodeUs = 0;
volatile uint8_t mf35xRpmFehlerInFolge = 0;
volatile bool mf35xRpmNeuerfassungAktiv = false;
volatile uint32_t mf35xRpmNeuerfassungAlteReferenzUs = 0;
volatile uint32_t mf35xRpmNeuerfassungLetzteRohflankeUs = 0;
volatile uint32_t mf35xRpmNeuerfassungKandidatUs = 0;
volatile uint8_t mf35xRpmNeuerfassungTreffer = 0;
volatile uint32_t mf35xRpmRohImpulseGesamt = 0;
volatile uint32_t mf35xRpmAkzeptierteImpulseGesamt = 0;
volatile uint32_t mf35xRpmVerworfeneImpulse = 0;
volatile uint32_t mf35xRpmDoppelImpulse = 0;
volatile uint32_t mf35xRpmNeuerfassungen = 0;
volatile uint32_t rpmImpulse = 0;
volatile uint32_t letzterRpmImpulsUs = 0;

''' + function_source + r'''

int failures = 0;
int checks = 0;

void resetState(uint32_t referenceUs = 2000, uint32_t anchorUs = 100000) {
  simulatedMicros = anchorUs;
  mf35xRpmLetzterImpulsUs = anchorUs;
  for (auto &value : mf35xRpmPeriodenUs) value = 0;
  mf35xRpmPeriodenIndex = 0;
  mf35xRpmPeriodenCount = 0;
  mf35xRpmReferenzPeriodeUs = referenceUs;
  mf35xRpmFehlerInFolge = 0;
  mf35xRpmNeuerfassungAktiv = false;
  mf35xRpmNeuerfassungAlteReferenzUs = 0;
  mf35xRpmNeuerfassungLetzteRohflankeUs = 0;
  mf35xRpmNeuerfassungKandidatUs = 0;
  mf35xRpmNeuerfassungTreffer = 0;
  mf35xRpmRohImpulseGesamt = 0;
  mf35xRpmAkzeptierteImpulseGesamt = 0;
  mf35xRpmVerworfeneImpulse = 0;
  mf35xRpmDoppelImpulse = 0;
  mf35xRpmNeuerfassungen = 0;
  rpmImpulse = 0;
  letzterRpmImpulsUs = anchorUs;
}

void edgeAfter(uint32_t deltaUs) {
  simulatedMicros += deltaUs;
  mf35xStabileRpmISR();
}

void expectTrue(const std::string& name, bool condition) {
  ++checks;
  if (!condition) {
    ++failures;
    std::cerr << "FAIL: " << name << "\n";
  } else {
    std::cout << "PASS: " << name << "\n";
  }
}

void testStablePulse() {
  resetState();
  edgeAfter(2000);
  expectTrue("stable 2000 us pulse accepted", mf35xRpmAkzeptierteImpulseGesamt == 1);
  expectTrue("stable pulse stored as period", mf35xRpmPeriodenCount == 1 && mf35xRpmPeriodenUs[0] == 2000);
  expectTrue("stable pulse leaves zero consecutive errors", mf35xRpmFehlerInFolge == 0);
}

void testDoubleEdge() {
  resetState();
  edgeAfter(1000);
  expectTrue("0.5x edge rejected", mf35xRpmVerworfeneImpulse == 1 && mf35xRpmAkzeptierteImpulseGesamt == 0);
  expectTrue("0.5x edge classified as double", mf35xRpmDoppelImpulse == 1);
  expectTrue("double edge does not advance accepted anchor", mf35xRpmLetzterImpulsUs == 100000);
  expectTrue("double edge does not build reacquisition error chain", mf35xRpmFehlerInFolge == 0);
  edgeAfter(1000);
  expectTrue("following real edge still accepted", mf35xRpmAkzeptierteImpulseGesamt == 1 && mf35xRpmPeriodenUs[0] == 2000);
}

void testMissingPulseNormalization() {
  resetState();
  edgeAfter(4000);
  expectTrue("2x missing-pulse gap accepted", mf35xRpmAkzeptierteImpulseGesamt == 1 && mf35xRpmPeriodenUs[0] == 2000);

  resetState();
  edgeAfter(6000);
  expectTrue("3x missing-pulse gap accepted", mf35xRpmAkzeptierteImpulseGesamt == 1 && mf35xRpmPeriodenUs[0] == 2000);

  resetState();
  edgeAfter(8000);
  expectTrue("4x missing-pulse gap accepted", mf35xRpmAkzeptierteImpulseGesamt == 1 && mf35xRpmPeriodenUs[0] == 2000);
}

void feedPeriod(uint32_t periodUs, int count) {
  for (int i = 0; i < count; ++i) edgeAfter(periodUs);
}

void testSustainedRealFrequencyStep(uint32_t newPeriodUs, const std::string& label) {
  resetState(2000, 100000);
  feedPeriod(newPeriodUs, 100);

  // A sustained new period is not a missing-pulse event. After enough
  // consecutive edges the protected reacquisition must learn the new period.
  const uint32_t low = newPeriodUs * 85UL / 100UL;
  const uint32_t high = newPeriodUs * 115UL / 100UL;
  const bool referenceMoved =
      mf35xRpmReferenzPeriodeUs >= low && mf35xRpmReferenzPeriodeUs <= high;

  expectTrue(label + " triggers protected reacquisition", mf35xRpmNeuerfassungen >= 1);
  expectTrue(label + " learns new reference", referenceMoved);
}

int main() {
  testStablePulse();
  testDoubleEdge();
  testMissingPulseNormalization();

  // Regression tests for aliasing between a real frequency step and the
  // 2x/3x/4x missing-pulse normalization.
  testSustainedRealFrequencyStep(1500, "2000->1500 us step");
  testSustainedRealFrequencyStep(2500, "2000->2500 us step");

  std::cout << "RPM ISR source tests: " << (checks - failures)
            << "/" << checks << " passed\n";
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
'''


def main() -> int:
    source = SOURCE.read_text(encoding="utf-8")
    function_source = extract_function(source, SIGNATURE)
    cpp = build_test_source(function_source)

    with tempfile.TemporaryDirectory(prefix="mf35x-rpm-isr-test-") as temp:
        temp_path = Path(temp)
        source_path = temp_path / "test_rpm_isr.cpp"
        binary_path = temp_path / "test_rpm_isr"
        source_path.write_text(cpp, encoding="utf-8")

        compile_result = subprocess.run(
            ["g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", str(source_path), "-o", str(binary_path)],
            text=True,
            capture_output=True,
        )
        if compile_result.returncode != 0:
            print("Host-Kompilierung des echten RPM-ISR fehlgeschlagen.", file=sys.stderr)
            print(compile_result.stdout, file=sys.stderr)
            print(compile_result.stderr, file=sys.stderr)
            return compile_result.returncode

        run_result = subprocess.run([str(binary_path)], text=True)
        return run_result.returncode


if __name__ == "__main__":
    sys.exit(main())
