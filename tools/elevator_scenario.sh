#!/bin/sh
# Build a tower an elevator has a reason to serve, and watch the cars.
#
# An elevator with nothing above the lobby never moves, so "it does not move"
# cannot be tested by placing one.  This plays out three floors of offices and
# a shaft that reaches them, then dumps the car records - position, direction,
# passengers - so motion is a number that changed rather than a guess about a
# picture.
#
#   sh tools/elevator_scenario.sh out.png [seconds-to-run]
set -e
cd "$(dirname "$0")/.."

OUT="${1:-C:/prog/claude/scenario.png}"
RUN="${2:-40}"
EXE=C:/prog/claude/SIMTOWER.EXE

# Screen geometry.  Floor bands are 36 pixels; floor one sits at y=525 in the
# view the game opens with, so floor n is 525 - 36*(n-1).
F1=525
F2=489
F3=453

# Palette cells, from the window dump: the grid starts at y=239 with 32-pixel
# rows and two 32-pixel columns whose centres are x=150 and x=182.
GROUP=150,255          # lobby / floor / escalator / service
LIFTS=182,255          # standard / express / service elevator
OFFICE=182,287         # office / hotel single / twin
ROW1=271               # first row of a group popup, +32 per row
ROW2=287
ROW3=319

node tools/node_harness.js build/node/simtower_game_node.js "$OUT" "$EXE" 6000 \
  click:400,264 wait:2000 \
  key:0 key:3 wait:1000 \
  drag:300,$F1,600,$F1 wait:1500 \
  press:$GROUP wait:400 release:150,$ROW2 wait:400 \
  drag:300,$F2,600,$F2 wait:1200 \
  drag:300,$F3,600,$F3 wait:1200 \
  click:$OFFICE wait:400 \
  click:330,$F2 wait:400 click:390,$F2 wait:400 click:520,$F2 wait:400 \
  click:330,$F3 wait:400 click:520,$F3 wait:400 \
  press:$LIFTS wait:400 release:182,$ROW1 wait:400 \
  drag:450,$F1,450,$F3 wait:3000 \
  key:E \
  wait:$((RUN * 1000 / 4)) key:E shot:"${OUT%.png}-a.png" \
  wait:$((RUN * 1000 / 4)) key:E shot:"${OUT%.png}-b.png" \
  wait:$((RUN * 1000 / 4)) key:E shot:"${OUT%.png}-c.png" \
  wait:$((RUN * 1000 / 4)) key:E
