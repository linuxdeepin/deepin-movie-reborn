#!/bin/bash
# SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Robust line-coverage collector for deepin-movie-platform-test.
#
# Why this exists: the suite is driven by a single RUN_ALL_TESTS() inside one
# QApplication process, and several cases are state/timing-fragile and SIGSEGV /
# SIGABRT. A crash aborts the whole process, dropping coverage for every later
# case. Running each suite in a FRESH process (via DMR_GTEST_FILTER) isolates
# crashes so only the failing suite's own tail is lost, and gcov accumulates the
# .gcda counters across runs. For suites with a known crashing case we also run a
# second pass that EXCLUDES that case, recovering the rest of the suite.
#
# Usage:  bash tests/deepin-movie-platform/ut-batched-coverage.sh
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BDIR="$ROOT/build-ut/tests/deepin-movie-platform"
BIN="$BDIR/deepin-movie-platform-test"
GCDA_DIR="$BDIR/CMakeFiles/deepin-movie-platform-test.dir"
OUT="$ROOT/build-ut"

[ -x "$BIN" ] || { echo "binary not found: $BIN (build first)" >&2; exit 1; }

cd "$BDIR" || exit 1

# Fresh runtime data so stamps match the current .gcno (avoids lcov aborting on
# "stamp mismatch" mid-capture).
find "$GCDA_DIR" -name '*.gcda' -delete 2>/dev/null

SUITES="dmrsettings_ext engine_model_ext filefilter_ext libdmr libdmr_ext logic_ext MainWindow mircast_ext MovieApp movieconfig_ext PadMode platform_mw_ext platform_mw_ext2 platform_mw_ext3 platform_mw_ext4 platform_mw_ext5 platform_mw_ext6 platform_tb_ext platform_tb_ext3 platform_tb_ext4 platform_widgets_ext platform_pw_ext mircastwidget_ext PlayerEngine player_engine_ext playlist_model_ext compositing_ext online_sub_ext Presenter requestAction Settings ToolBox qhttp_ext mpv_proxy_ext mpv_proxy_ext2 utils_ext Wayland dlna_ext mircastwidget_ext2 small_widgets_ext"

# Suites with a known crashing case: run once normally (covers up to the crash),
# then again excluding that case (covers the tail). gcov accumulates both.
declare -A EXCLUDE
EXCLUDE[engine_model_ext]="engine_model_ext.PlayerEngine_miscMutators_safe"
EXCLUDE[platform_widgets_ext]="platform_widgets_ext.volume_keyPressEvent_up_increments:platform_widgets_ext.volume_keyPressEvent_down_decrements"
EXCLUDE[platform_mw_ext3]="platform_mw_ext3.Event_Paint_DelegatesToBase"
EXCLUDE[mircast_ext]="mircast_ext.slotExitMircast_whenConnecting_resetsState"
EXCLUDE[platform_mw_ext5]="platform_mw_ext5.RequestAction_TogglePlaylist_ShortcutClearsFocus"
EXCLUDE[platform_tb_ext4]="platform_tb_ext4.eventFilter_listBtnRightClick_x86NoPlaylist_isSafe:platform_tb_ext4.slotUpdateMircast_nonZeroState_enablesFsBtn:platform_tb_ext4.slotUpdateMircast_emitsSignalWithMessage:platform_tb_ext4.slotSliderReleased_mircastScreening_routesToSeekMircast"
EXCLUDE[ToolBox]="ToolBox.playListWidget"

run_suite () {
  local filt="$1"
  DMR_GTEST_FILTER="$filt" timeout 240 "$BIN" >/tmp/cov_run.log 2>&1
  local code=$?
  local ran; ran=$(grep -c "\[ RUN      \]" /tmp/cov_run.log 2>/dev/null)
  printf '  %-34s exit=%-4s ran=%s\n' "$filt" "$code" "$ran"
}

for s in $SUITES; do
  run_suite "$s.*"
  [ -n "${EXCLUDE[$s]:-}" ] && run_suite "$s.*-${EXCLUDE[$s]}"
done

echo "=== capturing merged coverage ==="
lcov --directory . --capture --output-file "$OUT/platform_cov.info" \
     --ignore-errors gcov,source,graph --no-checksum 2>/tmp/cov_cap.log | tail -2

lcov --remove "$OUT/platform_cov.info" \
  '*/usr/include/*' '*/tests/*' '/usr/local/*' \
  '*/src/common/utility_x11.*' '*/src/common/settings_translation.cpp' '*/src/common/event_monitor.cpp' \
  '*/src/widgets/videoboxbutton.cpp' '*/src/backends/mpv/mpv_glwidget.cpp' \
  '*/src/common/platform/thumbnail_worker.*' '*/src/common/thumbnail_worker.*' \
  '*/src/common/mainwindow.*' '*/src/common/dbus_adpator.*' '*/src/common/dmr_setting.*' \
  '*/src/widgets/animationlabel.*' '*/src/widgets/movie_progress_indicator.*' \
  '*/src/widgets/notification_widget.*' '*/src/widgets/playlist_widget.*' \
  '*/src/widgets/toolbox_proxy.*' '*/src/widgets/volumeslider.*' \
  '*/src/common/hwdec_probe.*' '*/src/common/filter.*' '*/src/common/event_relayer.*' \
  --ignore-errors gcov,source,graph \
  -o "$OUT/platform_cov_filtered.info" 2>/dev/null

echo "=== RESULT ==="
lcov --summary "$OUT/platform_cov_filtered.info" 2>/dev/null | grep -E "lines|functions"
