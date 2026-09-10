#!/usr/bin/env python3
"""Host-side frequency and stability checks for the CH2 biquad chain."""

import cmath
import math
import struct


FS = 500.0


def float32(value):
    return struct.unpack("f", struct.pack("f", value))[0]


NOTCH = tuple(map(float32, (0.98758894, -1.43984271, 0.98758894,
                            -1.43984271, 0.97517788)))
HIGH_PASS = (
    0.995566972018, -1.991133944040, 0.995566972018,
    -1.991114292200, 0.991153595869,
)
LOW_PASS = (
    0.0461318020933, 0.0922636041866, 0.0461318020933,
    -1.3072850288500, 0.4918122372230,
)
FREQUENCIES = (1.0, 10.0, 30.0, 40.0, 60.0, 100.0)


def response(coefficients, frequency):
    b0, b1, b2, a1, a2 = coefficients
    z1 = cmath.exp(-2j * math.pi * frequency / FS)
    return (b0 + b1 * z1 + b2 * z1**2) / (1.0 + a1 * z1 + a2 * z1**2)


def db(value):
    return 20.0 * math.log10(abs(value))


def apply_biquad(coefficients, samples, initial=None):
    b0, b1, b2, a1, a2 = coefficients
    x1, x2, y1, y2 = initial or (0.0, 0.0, 0.0, 0.0)
    output = []
    for sample in samples:
        value = b0 * sample + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2
        x2, x1, y2, y1 = x1, sample, y1, value
        output.append(value)
    return output


def main():
    # RBJ/scipy-style design independently reproduces the committed constants.
    w0 = 2.0 * math.pi * 60.0 / FS
    beta = math.tan(w0 / (2.0 * 30.0))
    gain = 1.0 / (1.0 + beta)
    designed = (gain, -2.0 * math.cos(w0) * gain, gain,
                -2.0 * math.cos(w0) * gain, (1.0 - beta) * gain)
    assert max(abs(a - b) for a, b in zip(designed, NOTCH)) < 1.0e-7

    print("frequency,notch_db,chain_db")
    for frequency in FREQUENCIES:
        notch_gain = response(NOTCH, frequency)
        chain_gain = (notch_gain * response(HIGH_PASS, frequency)
                      * response(LOW_PASS, frequency))
        print(f"{frequency:g},{db(notch_gain):.6f},{db(chain_gain):.6f}")

    assert db(response(NOTCH, 60.0)) < -120.0
    assert all(abs(db(response(NOTCH, f))) < 0.03 for f in (1.0, 10.0, 30.0, 40.0))

    dc = [1_000_000.0] * 5000
    notch_dc = apply_biquad(NOTCH, dc, (dc[0], dc[0], dc[0], dc[0]))
    chain_dc = apply_biquad(LOW_PASS, apply_biquad(HIGH_PASS, notch_dc,
                                                   (dc[0], dc[0], 0.0, 0.0)))
    assert all(math.isfinite(value) for value in chain_dc)
    assert max(abs(value) for value in notch_dc) < 1_000_001.0

    for frequency in FREQUENCIES:
        samples = [1_000_000.0 * math.sin(2.0 * math.pi * frequency * n / FS)
                   for n in range(5000)]
        all_filter = apply_biquad(LOW_PASS, apply_biquad(HIGH_PASS,
                                  apply_biquad(NOTCH, samples)))
        bandpass_notch = apply_biquad(
            NOTCH,
            apply_biquad(LOW_PASS, apply_biquad(HIGH_PASS, samples)))
        assert all(math.isfinite(value) for value in all_filter)
        assert all(math.isfinite(value) for value in bandpass_notch)


if __name__ == "__main__":
    main()
