#!/bin/sh
# mkaudio.sh <res/raw dir> <out dir>: copies WAV sounds, converts OGG/MP3 to IMA ADPCM WAV (cheap to decode on the Vita)
src="$1"; out="$2"; mkdir -p "$out"
for f in "$src"/s[0-9]*; do
  b=$(basename "$f"); n="${b%.*}"
  case "$b" in
    *.wav) cp "$f" "$out/$b" ;;
    *) ffmpeg -v error -y -i "$f" -c:a adpcm_ima_wav "$out/$n.wav" ;;
  esac
done
