#!/bin/sh
# Fetch the ported source this build compiles.
#
# It is not vendored here.  The upstream project states that no licence has been
# selected for its source, so redistributing a copy of it from this repository
# is not something it permits.  Forking on GitHub is permitted, so the build
# pulls a pinned fork instead and this repository carries only its own work.
set -e
REPO=${SIMTOWER_UPSTREAM:-https://github.com/yomei-o/simtower-native-windows-port.git}
# Pinned to the fork's floor-edges branch: the recovered floor-edge rectangles
# had their width and height swapped and took the standard edge from a bank of
# hotel rooms instead of the emergency stairs.  See that commit for the detail.
SHA=588f1c186f32c6808a5ea2197931129f28c05c23
DEST=${1:-upstream}

if [ -d "$DEST/.git" ]; then
  git -C "$DEST" fetch --depth 1 origin "$SHA"
else
  git init -q "$DEST"
  git -C "$DEST" remote add origin "$REPO"
  git -C "$DEST" fetch --depth 1 origin "$SHA"
fi
git -C "$DEST" checkout -q FETCH_HEAD
echo "upstream at $SHA in $DEST"
