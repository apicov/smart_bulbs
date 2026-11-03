#!/bin/bash
# Render the app icons and splash screens from the SVGs in this folder.
#   icon.svg            full icon (web, legacy Android launcher)
#   icon-round.svg      round legacy Android launcher icon
#   icon-foreground.svg Android 8+ adaptive icon foreground (bulb inside the safe zone, transparent)
# Needs rsvg-convert and ImageMagick (dnf install librsvg2-tools ImageMagick). Re-run after editing an SVG.
set -euo pipefail
cd "$(dirname "$0")"
BG='#1c1510'                      # adaptive icon background + splash background
RES=../android/app/src/main/res
png() { rsvg-convert -w "$2" -h "$2" "$1" -o "$3"; }

# Web
png icon.svg 192 ../public/icons/icon-192.png
png icon.svg 512 ../public/icons/icon-512.png
png icon.svg 180 ../public/icons/apple-touch-icon.png

# Android launcher: legacy 48dp, adaptive foreground 108dp
for d in mdpi:1 hdpi:1.5 xhdpi:2 xxhdpi:3 xxxhdpi:4; do
  name=${d%%:*}; k=${d##*:}
  legacy=$(python3 -c "print(round(48*$k))"); fg=$(python3 -c "print(round(108*$k))")
  png icon.svg "$legacy" "$RES/mipmap-$name/ic_launcher.png"
  png icon-round.svg "$legacy" "$RES/mipmap-$name/ic_launcher_round.png"
  png icon-foreground.svg "$fg" "$RES/mipmap-$name/ic_launcher_foreground.png"
done
sed -i "s|<color name=\"ic_launcher_background\">.*</color>|<color name=\"ic_launcher_background\">$BG</color>|" \
  "$RES/values/ic_launcher_background.xml"

# Splash screens: keep each existing file's size, bulb centred at ~40% of the short side
tmp=$(mktemp -d)
for f in "$RES"/drawable*/splash.png; do
  read -r w h < <(magick identify -format '%w %h\n' "$f")
  s=$(( (w < h ? w : h) * 2 / 5 ))
  png icon-foreground.svg "$s" "$tmp/b.png"
  magick -size "${w}x${h}" "xc:$BG" "$tmp/b.png" -gravity center -composite "$f"
done
rm -rf "$tmp"
echo "✅ icons and splash screens written"
