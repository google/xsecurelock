#!/bin/bash

make clean

# Clang Analyzer.
scan-build make
