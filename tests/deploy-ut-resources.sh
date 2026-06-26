#!/bin/bash
# SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# 一键部署 deepin-movie-reborn 单元测试所需的全部外部资源文件。
#
# 用法:
#   bash tests/deploy-ut-resources.sh
#
# 说明:
#   - 会自动 sudo(仅提示一次密码)创建 /data/source/deepin-movie-reborn 目录
#   - 视频: 从 /home/uos/Videos 软链接(省 2G+ 空间)；改为复制见 LINK_VIDEOS
#   - 音频: 从 /usr/share/music/bensound-sunny.mp3 拷贝
#   - 其余文件(test.264 / test.jpg / play.conf / Hachiko 字幕)自动生成
#   - 幂等: 可重复运行，已有文件会被覆盖

set -euo pipefail

# ----------------------- 可配置项 (可用环境变量覆盖) -----------------------
BASE_DIR="${BASE_DIR:-/data/source/deepin-movie-reborn}"
VIDEOS_DIR="${VIDEOS_DIR:-/home/uos/Videos}"
AUDIO_SRC="${AUDIO_SRC:-/usr/share/music/bensound-sunny.mp3}"
LINK_VIDEOS="${LINK_VIDEOS:-true}"   # true=软链接(省空间); false=复制

MOVIE_DIR="$BASE_DIR/movie"
SRC_DEMO="$VIDEOS_DIR/demo.mp4"          # -> movie/demo.mp4
SRC_SKY="$VIDEOS_DIR/天空之眼.mp4"        # -> movie/天空之眼 高清1080P.mp4

# ----------------------- 输出辅助 -----------------------
c_blue()  { printf '\033[1;34m%s\033[0m' "$1"; }
c_green() { printf '\033[1;32m%s\033[0m' "$1"; }
c_red()   { printf '\033[1;31m%s\033[0m' "$1"; }
log()  { printf '%s %s\n' "$(c_blue '[deploy]')" "$*"; }
ok()   { printf '  %s %s\n' "$(c_green '[ok]')" "$*"; }
die()  { printf '%s %s\n' "$(c_red '[error]')" "$*" >&2; exit 1; }

# ----------------------- 需要 root: 自动 sudo 重入(只提示一次密码) -----------------------
if [ "$(id -u)" -ne 0 ]; then
    [ -r "$0" ] || die "无法读取脚本自身路径: $0"
    export _DEPLOY_ORIG_USER="${SUDO_USER:-$USER}"
    log "需要 sudo 权限以创建 ${BASE_DIR}，请在提示处输入密码："
    exec sudo -E bash "$0" "$@"
fi
ORIG_USER="${_DEPLOY_ORIG_USER:-${SUDO_USER:-$USER}}"

# ----------------------- 依赖与源文件检查 -----------------------
command -v ffmpeg >/dev/null 2>&1 || die "未找到 ffmpeg，请先安装: sudo apt install ffmpeg"
[ -f "$SRC_DEMO"  ] || die "缺少视频源: $SRC_DEMO"
[ -f "$SRC_SKY"   ] || die "缺少视频源: $SRC_SKY"
[ -f "$AUDIO_SRC" ] || die "缺少音频源: $AUDIO_SRC"

# ----------------------- 建目录 -----------------------
log "创建目录: ${BASE_DIR}/ 与 ${MOVIE_DIR}/"
mkdir -p "$MOVIE_DIR"

# ----------------------- 视频 -----------------------
place_video() {
    local src="$1" dst="$2"
    if [ "$LINK_VIDEOS" = "true" ]; then
        ln -sfn "$src" "$dst"
        ok "$(basename "$dst")  ->  $src  (软链接)"
    else
        cp -f "$src" "$dst"
        ok "$(basename "$dst")  (复制自 $src)"
    fi
}
log "放置视频 (LINK_VIDEOS=${LINK_VIDEOS}):"
place_video "$SRC_DEMO" "$MOVIE_DIR/demo.mp4"
place_video "$SRC_SKY"  "$MOVIE_DIR/天空之眼 高清1080P.mp4"

# ----------------------- 音频 (两个文件共用同一来源) -----------------------
log "放置音频:"
cp -f "$AUDIO_SRC" "$MOVIE_DIR/bensound-sunny.mp3"; ok "bensound-sunny.mp3"
cp -f "$AUDIO_SRC" "$MOVIE_DIR/demo.mp3";           ok "demo.mp3  (复用 ${AUDIO_SRC})"

# ----------------------- play.conf -----------------------
log "生成 play.conf:"
cat > "$MOVIE_DIR/play.conf" <<'EOF'
hwdec=auto
vo=xv
EOF
ok "play.conf"

# ----------------------- test.264 (从 demo.mp4 提取 5s 裸 H.264 流) -----------------------
log "生成 test.264:"
if ! ffmpeg -y -hide_banner -loglevel error -i "$SRC_DEMO" -t 5 -an \
        -c:v copy -bsf:v h264mp4toannexb -f h264 "$MOVIE_DIR/test.264" 2>/dev/null; then
    log "  copy 模式失败，改用重新编码(libx264)..."
    ffmpeg -y -hide_banner -loglevel error -i "$SRC_DEMO" -t 5 -an \
        -c:v libx264 -f h264 "$MOVIE_DIR/test.264" || die "ffmpeg 生成 test.264 失败"
fi
ok "test.264"

# ----------------------- test.jpg (从 demo.mp4 截取首帧) -----------------------
log "生成 test.jpg:"
ffmpeg -y -hide_banner -loglevel error -i "$SRC_DEMO" -vframes 1 -q:v 2 -an "$BASE_DIR/test.jpg" \
    || die "ffmpeg 生成 test.jpg 失败"
ok "test.jpg"

# ----------------------- Hachiko 字幕 (movie/ 与根目录各一份) -----------------------
log "生成 Hachiko.A.Dog's.Story.ass:"
write_ass() {
    cat > "$1" <<'EOF'
[Script Info]
ScriptType: v4.00+
PlayResX: 1920
PlayResY: 1080
WrapStyle: 0

[V4+ Styles]
Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding
Style: Default,Arial,48,&H00FFFFFF,&H000000FF,&H00000000,&H64000000,0,0,0,0,100,100,0,0,1,2,1,2,10,10,10,1

[Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
Dialogue: 0,0:00:01.00,0:00:05.00,Default,,0,0,0,,This is a generated test subtitle for unit tests.
EOF
}
write_ass "$MOVIE_DIR/Hachiko.A.Dog's.Story.ass"; ok "movie/Hachiko.A.Dog's.Story.ass"
write_ass "$BASE_DIR/Hachiko.A.Dog's.Story.ass";  ok "Hachiko.A.Dog's.Story.ass (根目录)"

# ----------------------- 归属权交回原用户 -----------------------
if [ -n "$ORIG_USER" ]; then
    chown -R "$ORIG_USER:" "$BASE_DIR" || log "(警告) chown 失败，文件将归 root 所有"
fi

# ----------------------- 校验 -----------------------
log "校验所有文件:"
EXPECTED=(
    "$MOVIE_DIR/demo.mp4"
    "$MOVIE_DIR/天空之眼 高清1080P.mp4"
    "$MOVIE_DIR/bensound-sunny.mp3"
    "$MOVIE_DIR/demo.mp3"
    "$MOVIE_DIR/play.conf"
    "$MOVIE_DIR/test.264"
    "$MOVIE_DIR/Hachiko.A.Dog's.Story.ass"
    "$BASE_DIR/test.jpg"
    "$BASE_DIR/Hachiko.A.Dog's.Story.ass"
)
miss=()
for f in "${EXPECTED[@]}"; do
    if [ -e "$f" ]; then ok "$f"; else
        miss+=("$f"); printf '  %s 缺失: %s\n' "$(c_red '[missing]')" "$f"
    fi
done
[ ${#miss[@]} -eq 0 ] || die "有 ${#miss[@]} 个文件缺失，请检查上方日志"

# ----------------------- 汇总 -----------------------
echo
ok "部署完成！共 ${#EXPECTED[@]} 个文件就位于 ${BASE_DIR}"
printf '  - 视频采用%s；如需硬拷贝请运行: LINK_VIDEOS=false bash %s\n' \
    "$([ "$LINK_VIDEOS" = true ] && echo 软链接 || echo 复制)" "$0"
echo
log "目录清单:"
ls -lh "$BASE_DIR" "$MOVIE_DIR" 2>/dev/null
