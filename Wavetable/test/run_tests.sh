#!/bin/sh
# Builds a minimal SynthEdit-like host around the Wavetable modules and runs it (Linux/macOS, g++ or clang++).
# It loads the modules through MP_GetFactory like SynthEdit, drives the DSP and GUI parts and records drawing calls.
# Needs: a folder "cycles" with some single-cycle .wav files and "serum16.wav" next to the binary (see harness.cpp).
set -e
cd "$(dirname "$0")"
R=../..
${CXX:-g++} -std=c++17 -g -O1 -D_DEBUG -Wno-unknown-pragmas -I$R/se_sdk3 -I$R/shared -I.. -I. \
  harness.cpp ../WavetableEngine.cpp ../WavetableOsc.cpp ../WavetableDisplay.cpp ../WavetableDisplayGui.cpp \
  $R/se_sdk3/mp_sdk_audio.cpp $R/se_sdk3/mp_sdk_common.cpp $R/se_sdk3/mp_sdk_gui.cpp -o harness -lpthread
./harness .
