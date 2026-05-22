#!/usr/bin/env python3
"""
Cross-variant audio regression comparison.

Downloaded audio-regression-output-* artifacts each contain the raw
float-interleaved outputs of every test case for a single SIMD variant
under Tests/AudioRegressionTests/output/<variant>/<case>.raw.

This script compares every pair of variants we list in VARIANTS, case by
case, and fails (non-zero exit) if any pair drifts beyond TOLERANCE_DB.
We intentionally limit comparisons to variants that GitHub-hosted x64
runners are guaranteed to execute correctly. ARM64 and AVX-512/AVX10.1
output is captured for inspection but is not part of the gating diff
because the hosted runner may not actually execute those code paths.
"""

import os
import sys

import numpy as np

ARTIFACT_ROOT = "artifacts"
VARIANTS = ["sse2", "avx", "avx2"]
TOLERANCE_DB = -120.0
CASES = [
    "preamp_minus6",
    "biquad_peaking_1khz",
    "copy_crossfeed",
    "delay_512",
    "graphiceq_15band",
]


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


def variant_dir(variant: str) -> str:
    return os.path.join(ARTIFACT_ROOT, f"audio-regression-output-{variant}", "output", variant)


def main() -> int:
    pairs = [(VARIANTS[i], VARIANTS[j]) for i in range(len(VARIANTS)) for j in range(i + 1, len(VARIANTS))]
    print(f"Cross-variant compare: tolerance={TOLERANCE_DB} dBFS, variants={VARIANTS}")

    missing = [v for v in VARIANTS if not os.path.isdir(variant_dir(v))]
    if missing:
        print(f"WARNING: missing variant output directories: {missing}")

    all_ok = True
    for a, b in pairs:
        a_dir = variant_dir(a)
        b_dir = variant_dir(b)
        if not (os.path.isdir(a_dir) and os.path.isdir(b_dir)):
            print(f"  [{a:6s} <-> {b:6s}] SKIP (one or both directories missing)")
            continue
        for case in CASES:
            a_file = os.path.join(a_dir, f"{case}.raw")
            b_file = os.path.join(b_dir, f"{case}.raw")
            if not (os.path.isfile(a_file) and os.path.isfile(b_file)):
                print(f"  [{a:6s} <-> {b:6s}] {case}: SKIP (file missing)")
                continue
            arr_a = load_raw(a_file)
            arr_b = load_raw(b_file)
            ok, maxerr, idx, rmse = compare(arr_a, arr_b)
            verdict = "PASS" if ok else "FAIL"
            print(f"  [{a:6s} <-> {b:6s}] {case:24s}: {verdict}  maxAbsError={maxerr:.3e} (at {idx})  rmse={rmse:.3e}")
            if not ok:
                all_ok = False

    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
