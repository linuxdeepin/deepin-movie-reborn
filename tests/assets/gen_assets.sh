#!/bin/bash
# SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Synthesize the small media assets consumed by the unit tests.
#
# The test suites only need *valid* streams whose codec/length properties they
# can observe; none of them depend on real content, so every file below is
# generated locally with ffmpeg instead of shipping binary blobs in git.
#
# Usage: ./gen_assets.sh   (requires ffmpeg on PATH)

set -e
cd "$(dirname "$0")"

gen_mp4() {
    # 320x240, 2s, H.264 + AAC sine audio -- enough to enter the real decode
    # path without touching hardware or large payloads.
    ffmpeg -y -loglevel error \
        -f lavfi -i "testsrc2=duration=2:size=320x240:rate=25" \
        -f lavfi -i "sine=frequency=440:duration=2" \
        -c:v libx264 -preset ultrafast -pix_fmt yuv420p \
        -c:a aac -shortest "$1"
}

gen_mp3() {
    # 2s sine-wave MP3 for audio-only playback tests.
    ffmpeg -y -loglevel error \
        -f lavfi -i "sine=frequency=523:duration=2" \
        -codec:a libmp3lame -qscale:a 9 "$1"
}

gen_h264() {
    # Raw Annex-B H.264 elementary stream (no container) used by the
    # stream-probe / codec-detection tests.
    ffmpeg -y -loglevel error \
        -f lavfi -i "testsrc2=duration=1:size=160x120:rate=25" \
        -c:v libx264 -preset ultrafast -pix_fmt yuv420p \
        -an -f h264 "$1"
}

gen_ass() {
    # Minimal ASS subtitle; only parsed for timing/section handling.
    cat > "Hachiko.A.Dog's.Story.ass" <<'EOF'
[Script Info]
ScriptType: v4.00+
PlayResX: 384
PlayResY: 288

[V4+ Styles]
Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding
Style: Default,Arial,16,&Hffffff,&Hffffff,&H0,&H0,0,0,0,0,100,100,0,0,1,1,0,2,10,10,10,1

[Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
Dialogue: 0,0:00:00.00,0:00:02.00,Default,,0,0,0,,Hachiko test subtitle
EOF
}

gen_mp4 "demo.mp4"
gen_mp4 "天空之眼 高清1080P.mp4"
gen_mp3 "demo.mp3"
cp -f "demo.mp3" "bensound-sunny.mp3"
gen_h264 "test.264"
gen_ass

echo "assets generated:"
ls -la
