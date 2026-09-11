#!/usr/bin/env python3
"""Static ownership checks for the ADS1292R acquisition service."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
APP_THREADX = (ROOT / "Core/Src/app_threadx.c").read_text(encoding="utf-8")
ACQUISITION = (ROOT / "Core/Src/ads1292r_acquisition.c").read_text(encoding="utf-8")
MANIFEST = (ROOT / "Debug/Core/Src/subdir.mk").read_text(encoding="utf-8")
OBJECTS = (ROOT / "Debug/objects.list").read_text(encoding="utf-8")

# CubeMX's ThreadX bootstrap must not regain acquisition implementation details.
assert "ADS1292R_AcquisitionInit(memory_ptr)" in APP_THREADX
for symbol in (
    "ADS1292R_AcquisitionThread",
    "ADS1292R_RingPush",
    "ADS1292R_Record ads_ring",
    "volatile uint32_t ads_drdy_count",
    "HAL_OK",
):
    assert symbol not in APP_THREADX, f"legacy acquisition code in app_threadx.c: {symbol}"

# The dedicated module is the sole owner of the runtime acquisition machinery.
for symbol in (
    "static VOID ADS1292R_AcquisitionThread",
    "static void ADS1292R_RingPush",
    "static ADS1292R_Record ads_ring",
    "volatile uint32_t ads_drdy_count",
    "ADS1292R_ReadData(frame)",
):
    assert symbol in ACQUISITION, f"missing acquisition implementation: {symbol}"

# Checked-in managed-build files must compile and link the extracted module once.
assert MANIFEST.count("../Core/Src/ads1292r_acquisition.c") == 1
assert MANIFEST.count("./Core/Src/ads1292r_acquisition.o") == 2  # OBJS + clean
assert OBJECTS.count('"./Core/Src/ads1292r_acquisition.o"') == 1

print("ADS1292R acquisition module ownership and build manifest: PASS")
