#!/bin/sh
# Builds a minimal SynthEdit-like host around the SXE Waveshaper modules and runs it (Linux/macOS, g++ or clang++).
# It loads the modules through MP_GetFactory like SynthEdit, drives the DSP and GUI parts and records drawing calls.
# Also runs the formula engine unit tests. "python3 render_ops.py out.png" turns the recorded GUI calls into an image.
set -e
cd "$(dirname "$0")"
R=../..
${CXX:-g++} -std=c++17 -O1 -Wall -Wno-unknown-pragmas -Wno-unused-parameter formula_test.cpp ../FormulaEngine.cpp -o formula_test
./formula_test > formula_test.log && tail -1 formula_test.log
${CXX:-g++} -std=c++17 -g -O1 -D_DEBUG -Wno-unknown-pragmas -I$R/se_sdk3 -I$R/shared -I.. -I. \
  harness.cpp ../FormulaEngine.cpp ../Waveshaper.cpp ../WaveshaperDisplay.cpp ../WaveshaperDisplayGui.cpp \
  $R/se_sdk3/mp_sdk_audio.cpp $R/se_sdk3/mp_sdk_common.cpp $R/se_sdk3/mp_sdk_gui.cpp -o harness -lpthread
./harness .
