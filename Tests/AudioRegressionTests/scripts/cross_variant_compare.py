#!/usr/bin/env python3
"""
Cross-variant audio regression comparison.

Downloaded audio-regression-output-* artifacts each contain the raw
float-interleaved outputs of every test case for a single SIMD variant
under <variant>/<case>.raw.

This script compares every pair of gating variants, case by case, and
fails (non-zero exit) if any pair drifts beyond TOLERANCE_DB.
We intentionally limit comparisons to variants that GitHub-hosted x64
runners are guaranteed to execute correctly. ARM64 and AVX-512/AVX10.1
output is captured for inspection but is not part of the gating diff
because the hosted runner may not actually execute those code paths.

The gating variant list is derived from .github/simd-variants.psd1 (the
x64 variants with RunnerCanExecute) by .github/scripts/New-BuildMatrix.ps1
and passed in through the RUNNER_EXECUTABLE_VARIANTS environment variable
(a JSON array; see the cross-variant-compare job in build.yml). When the
variable is absent (e.g. a local run), the script falls back to
FALLBACK_VARIANTS with a warning.
"""

import json
import os
import sys

import numpy as np

ARTIFACT_ROOT = "artifacts"
# Fallback only; the authoritative list comes from RUNNER_EXECUTABLE_VARIANTS.
FALLBACK_VARIANTS = ["sse2", "avx", "avx2"]
TOLERANCE_DB = -120.0
def load_raw(path: str) -> np.ndarray:
    with open(path, "rb") as f:
        return np.frombuffer(f.read(), dtype=np.float32)


def compare(a: np.ndarray, b: np.ndarray):
    if len(a) != len(b):
        return False, float("inf"), 0, float("inf")
    diff = np.abs(a.astype(np.float64) - b.astype(np.float64))
    max_err = float(diff.max())
    rmse = float(np.sqrt(np.mean(diff ** 2)))
    tol = 10.0 ** (TOLERANCE_DB / 20.0)
    return max_err <= tol, max_err, int(diff.argmax()), rmse


def resolve_variants() -> list:
    raw = os.environ.get("RUNNER_EXECUTABLE_VARIANTS", "").strip()
    if not raw:
        print(
            "WARNING: RUNNER_EXECUTABLE_VARIANTS is not set; falling back to the "
            f"hard-coded variant list {FALLBACK_VARIANTS}. In CI this list is "
            "derived from .github/simd-variants.psd1 by New-BuildMatrix.ps1."
        )
        return FALLBACK_VARIANTS
    variants = json.loads(raw)
    if (
        not isinstance(variants, list)
        or not variants
        or not all(isinstance(v, str) and v for v in variants)
    ):
        raise ValueError(
            "RUNNER_EXECUTABLE_VARIANTS must be a non-empty JSON array of "
            f"variant names, got: {raw!r}"
        )
    return variants


def variant_dir(variant: str) -> str:
    artifact_dir = os.path.join(ARTIFACT_ROOT, f"audio-regression-output-{variant}")
    candidates = [
        os.path.join(artifact_dir, variant),
        os.path.join(artifact_dir, "output", variant),
    ]
    for candidate in candidates:
        if os.path.isdir(candidate):
            return candidate
    return candidates[0]


def load_cases(directory: str) -> list:
    manifest_path = os.path.join(directory, "cases.json")
    with open(manifest_path, "r", encoding="utf-8") as stream:
        data = json.load(stream)
    cases = data.get("cases")
    if (
        not isinstance(cases, list)
        or not cases
        or not all(isinstance(case, str) and case for case in cases)
        or len(cases) != len(set(cases))
    ):
        raise ValueError(f"invalid case manifest: {manifest_path}")
    return cases


def main() -> int:
    variants = resolve_variants()
    pairs = [(variants[i], variants[j]) for i in range(len(variants)) for j in range(i + 1, len(variants))]
    print(f"Cross-variant compare: tolerance={TOLERANCE_DB} dBFS, variants={variants}")

    dirs = {v: variant_dir(v) for v in variants}
    missing = [v for v in variants if not os.path.isdir(dirs[v])]
    if missing:
        for variant in missing:
            print(f"ERROR: missing variant output directory for {variant}: {dirs[variant]}")
        return 1

    manifests = {}
    try:
        manifests = {variant: load_cases(directory) for variant, directory in dirs.items()}
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"ERROR: {error}")
        return 1
    cases = manifests[variants[0]]
    for variant in variants[1:]:
        if manifests[variant] != cases:
            print(
                f"ERROR: case manifest mismatch: {variants[0]}={cases}, "
                f"{variant}={manifests[variant]}"
            )
            return 1

    all_ok = True
    comparisons = 0
    for a, b in pairs:
        a_dir = dirs[a]
        b_dir = dirs[b]
        for case in cases:
            a_file = os.path.join(a_dir, f"{case}.raw")
            b_file = os.path.join(b_dir, f"{case}.raw")
            if not (os.path.isfile(a_file) and os.path.isfile(b_file)):
                print(f"  [{a:6s} <-> {b:6s}] {case}: FAIL (file missing)")
                all_ok = False
                continue
            arr_a = load_raw(a_file)
            arr_b = load_raw(b_file)
            ok, maxerr, idx, rmse = compare(arr_a, arr_b)
            comparisons += 1
            verdict = "PASS" if ok else "FAIL"
            print(f"  [{a:6s} <-> {b:6s}] {case:24s}: {verdict}  maxAbsError={maxerr:.3e} (at {idx})  rmse={rmse:.3e}")
            if not ok:
                all_ok = False

    if comparisons == 0:
        print("ERROR: no cross-variant comparisons were executed")
        return 1

    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
