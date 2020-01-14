#!/bin/sh
LF='
'
convert -size 256x128 -gravity center -pointsize 28 \
  -font /usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf \
  caption:"INCOMPATIBLE COMPOSITOR,${LF}PLEASE FIX!" \
  xbm:incompatible_compositor.xbm
