#!/bin/sh
# Publish the built game as the pages GitHub serves.
#
# The game is one 2 MB file.  Served from a fixed URL a browser will happily
# keep showing yesterday's build, which looks exactly like a fix that did not
# work - and asking a player to clear their cache is not a fix either.  So the
# game goes to play.html and the entry point index.html is a few hundred bytes
# that send the browser to `play.html?v=<build>`.  A new build is a new URL, so
# it can never come out of the cache; only the tiny index.html can go stale,
# and GitHub Pages revalidates that within its ten-minute max-age.
#
# The build is also stamped into the page itself, bottom left, so a screenshot
# says which build produced it.
set -e
cd "$(dirname "$0")/.."

SOURCE=docs/simtower_game.html
[ -f "$SOURCE" ] || { echo "no $SOURCE - build the simtower_game target first"; exit 1; }

REVISION="$(git rev-parse --short HEAD 2>/dev/null || echo unknown)"
STAMP="$REVISION $(date -u '+%Y-%m-%d %H:%MZ')"
VERSION="$(date -u '+%Y%m%d%H%M%S')"

sed "s/BUILD_STAMP/$STAMP/" "$SOURCE" > docs/play.html

cat > docs/index.html <<HTML
<!doctype html>
<meta charset="utf-8">
<title>SimTower on WebAssembly</title>
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta http-equiv="Cache-Control" content="no-cache, must-revalidate">
<!-- The game itself is play.html.  This page exists only to point at the
     current build: the query string is what stops a browser serving a stale
     copy of a two megabyte file.  Written by tools/deploy.sh. -->
<meta http-equiv="refresh" content="0; url=play.html?v=$VERSION">
<style>
  body { margin: 0; height: 100vh; display: grid; place-items: center;
         background: #101014; color: #c8c8d0;
         font: 14px system-ui, sans-serif; }
</style>
<p>loading build $STAMP&nbsp;<a href="play.html?v=$VERSION" style="color:#8ab4f8">&rarr;</a>
<script>location.replace('play.html?v=$VERSION');</script>
HTML

echo "deployed docs/play.html + docs/index.html  build $STAMP  v=$VERSION"
