#!/usr/bin/env python3
"""Regression test for causal raw-ECG R-peak timestamp refinement."""

FS = 500
BASELINE = FS * 200 // 1000
LOWPASS = FS * 24 // 1000
MWI = FS * 150 // 1000
LOOKBACK = FS * 200 // 1000


def detector_candidate(samples):
    baseline_buffer = [0] * BASELINE
    lowpass_buffer = [0] * LOWPASS
    mwi_buffer = [0] * MWI
    highpass_history = [0] * len(samples)
    derivative_history = [0] * 4
    baseline_sum = lowpass_sum = mwi_sum = 0
    mwi_values = []

    for counter, raw in enumerate(samples, 1):
        bi = counter % BASELINE
        baseline_sum += raw - baseline_buffer[bi]
        baseline_buffer[bi] = raw
        highpass = raw - baseline_sum // BASELINE
        highpass_history[counter - 1] = highpass

        li = counter % LOWPASS
        lowpass_sum += highpass - lowpass_buffer[li]
        lowpass_buffer[li] = highpass
        bandpass = lowpass_sum // LOWPASS
        derivative = (2 * bandpass + derivative_history[0]
                      - derivative_history[2] - 2 * derivative_history[3]) // 8
        derivative_history = [bandpass] + derivative_history[:3]
        squared = (derivative // 256) ** 2
        mi = counter % MWI
        mwi_sum += squared - mwi_buffer[mi]
        mwi_buffer[mi] = squared
        mwi_values.append(mwi_sum // MWI)

    candidate = max(range(len(mwi_values)), key=mwi_values.__getitem__) + 1
    first = max(1, candidate - LOOKBACK)
    corrected = max(range(first, candidate + 1),
                    key=lambda count: abs(highpass_history[count - 1]))
    return candidate, corrected


def main():
    r_peak = 501
    samples = [0] * 1000
    samples[r_peak - 1] = 1_000_000
    candidate, corrected = detector_candidate(samples)
    assert candidate > r_peak, (candidate, r_peak)
    assert corrected == r_peak, (corrected, r_peak)
    print(f"candidate_delay_samples={candidate-r_peak}, corrected_error_samples=0")


if __name__ == "__main__":
    main()
