#!/bin/sh

make clean
make CC=/usr/bin/iwyu -k 2>&1 | tee iwyu.log
