#!/bin/sh
# Build a tower an elevator has a reason to serve, and watch the cars.
#
# An elevator with nothing above the lobby never moves, so "it does not move"
# cannot be tested by placing one.  This plays out three floors of offices and
# a shaft that reaches them, then dumps the tower (T) and the car records (E) -
# position, direction, passengers - so motion is a number that changed rather
# than a guess about a picture.
#
#   sh tools/elevator_scenario.sh out.png [seconds-to-run]
set -e
cd "$(dirname "$0")/.."

OUT="${1:-C:/prog/claude/scenario.png}"
RUN="${2:-40}"
EXE=C:/prog/claude/SIMTOWER.EXE

# Screen geometry.  Floor bands are 36 pixels and floor one sits at y=525 in
# the view the game opens with, so floor n is 525 - 36*(n-1).
F1=525
F2=489
F3=453
F4=417

# Palette geometry, from the window dump: the tool row is at y=230, the
# catalogue grid starts at y=239 with 32-pixel rows and two columns whose
# centres are x=150 and x=182.  A group cell opens a popup on the press whose
# current choice is the row aligned with the cell, so pressing and releasing
# without moving keeps that choice, and each row below is +32.
FINGER=165,230
GROUP=150,255          # lobby / floor / escalator / service
LIFTS=182,255          # standard / express / service elevator
OFFICE=182,287         # office / hotel single / twin
SHAFT=450

node tools/node_harness.js build/node/simtower_game_node.js "$OUT" "$EXE" 6000 \
  click:400,264 wait:2000 \
  key:0 key:3 wait:1000 \
  drag:300,$F1,600,$F1 wait:1500 \
  press:$GROUP wait:400 release:150,287 wait:400 \
  drag:300,$F2,600,$F2 wait:1000 \
  drag:300,$F3,600,$F3 wait:1000 \
  drag:300,$F4,600,$F4 wait:1000 \
  press:$OFFICE wait:400 release:$OFFICE wait:400 \
  click:330,$F2 wait:300 click:390,$F2 wait:300 click:520,$F2 wait:300 \
  click:330,$F3 wait:300 click:520,$F3 wait:300 \
  click:330,$F4 wait:300 click:520,$F4 wait:300 \
  press:$LIFTS wait:400 release:$LIFTS wait:400 \
  click:$SHAFT,$F1 wait:1500 \
  click:$FINGER wait:400 \
  drag:$SHAFT,$F2,$SHAFT,$F4 wait:2000 \
  key:T key:E \
  wait:$((RUN * 1000 / 3)) key:E shot:"${OUT%.png}-a.png" \
  wait:$((RUN * 1000 / 3)) key:E shot:"${OUT%.png}-b.png" \
  wait:$((RUN * 1000 / 3)) key:T key:E
