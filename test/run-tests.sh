#!/bin/sh

rm -f *.log
for test in *.xdo; do
  startx \
    /bin/sh "$PWD"/run-test.sh "$test" \
    -- \
    "$(which Xephyr)" :42 -retro -screen 640x480 \
    2>&1 |\
  tee "$test.log" |\
  grep "^Test $test status: "
done
