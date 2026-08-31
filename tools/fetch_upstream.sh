#!/bin/sh
# Fetch the ported source this build compiles.
#
# It is not vendored here.  The upstream project states that no licence has been
# selected for its source, so redistributing a copy of it from this repository
# is not something it permits.  Forking on GitHub is permitted, so the build
# pulls a pinned fork instead and this repository carries only its own work.
set -e
REPO=${SIMTOWER_UPSTREAM:-https://github.com/yomei-o/simtower-native-windows-port.git}
SHA=9c2685ae99fee51a818595decf4d7b9babeb4338
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
