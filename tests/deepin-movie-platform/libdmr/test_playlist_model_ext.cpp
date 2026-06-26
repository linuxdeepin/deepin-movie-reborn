// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// Extension unit tests (round 3) for src/libdmr/playlist_model.cpp whose line
// coverage still has the largest gap (~519 uncovered lines).
//
// Suite name "playlist_model_ext" is intentionally distinct from the existing
// suites ("libdmr", "libdmr_ext", "engine_model_ext") so TEST() cases never
// collide.
//
// Strategy:
//   * A FRESH PlaylistModel is constructed against the live engine (the same
//     engine the test main wires up) for every mutating case, so the shared
//     engine playlist is never disturbed. Its persisted playlist file is
//     redirected to a unique temp path to avoid cross-test contamination.
//   * Private members (`_infos`, `_current`, `_playMode`, `_pendingJob`, ...)
//     are reached through the `#define private public` access block so that
//     list-manipulation branches (remove / switchPosition / currentInfo /
//     tryPlayCurrent / reshuffle / playNext / playPrev) can be exercised with
//     synthetic PlayItemInfo items WITHOUT having to spawn a real decode
//     (calculatePlayInfo / GetThumbnail thread) to populate the list.
//   * Playback-invoking paths (_engine->requestPlay / waitLastEnd) are neutralised
//     with file-scope Stub functions so tryPlayCurrent/playNext/playPrev can run
//     their inner switch logic without touching mpv/gst decode -> no crash risk.
//   * The GetThumbnail worker is constructed directly (empty url list) so its
//     lifecycle helpers (stop / setUrls / clearItem / getThumbnailRunning) are
//     covered without ever starting a decode thread.
//
// Hard rules honoured:
//   * Only Google Test (TEST(playlist_model_ext, ...)); gtest_main supplies
//     main(), never define main() here.
//   * Helper/static prefix "plm_" is unique to this TU.
//   * Stubs use "stub/stub.h"; stub replacements are free functions with the
//     "plm_stub_" prefix.

#include <QtTest>
#include <QTest>
#include <QSignalSpy>
#include <QDebug>
#include <unistd.h>
#include <gtest/gtest.h>

// playlist_model.h is pulled in transitively by player_engine.h (included via
// application.h). To expose private members it MUST be the first inclusion of
// that header, so the access defines are in place before any include that could
// set its header guard.
#include <sstream>
#include <string>
#include <vector>
#include <list>
#include <map>
#include <algorithm>
#include <memory>

#define protected public
#define private public
#include "src/libdmr/playlist_model.h"
#include "application.h"
#include "player_widget.h"
#include "player_engine.h"
#include "compositing_manager.h"
#undef protected
#undef private
#include "utils.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryFile>
#include <QDataStream>
#include <QUrl>
#include <QStandardPaths>

#include "stub/stub.h"

using namespace dmr;

// The QDataStream serializers for MovieInfo are defined in playlist_model.cpp
// but not declared in any header; forward-declare them so the round-trip test
// below can call them.
namespace dmr {
QDataStream &operator<<(QDataStream &st, const MovieInfo &mi);
QDataStream &operator>>(QDataStream &st, MovieInfo &mi);
}

// ===========================================================================
// Helpers (file scope, "plm_" prefix)
// ===========================================================================

// Live engine wired up by the test main (Platform_MainWindow already created).
static PlayerEngine *plm_engine()
{
    Platform_MainWindow *w = dApp->getMainWindow();
    return w ? w->engine() : nullptr;
}

// Build a synthetic PlayItemInfo. `valid` controls both pif.valid and mi.valid
// so the model treats it as playable / unplayable without any real decode.
static PlayItemInfo plm_makeItem(const QString &path, bool valid)
{
    PlayItemInfo pif;
    pif.url = QUrl::fromLocalFile(path);
    pif.info = QFileInfo(path);
    pif.valid = valid;
    pif.loaded = true;
    pif.mi.valid = valid;
    pif.mi.title = QFileInfo(path).fileName();
    pif.mi.fileType = QFileInfo(path).suffix();
    pif.mi.duration = valid ? 1000 : -1;
    pif.mi.width = valid ? 640 : -1;
    pif.mi.height = valid ? 480 : -1;
    return pif;
}

// Create a fresh PlaylistModel bound to the live engine but with its persisted
// playlist file redirected to a unique temp path (so savePlaylist never touches
// the shared engine's playlist store). Returns nullptr if no engine is present.
static PlaylistModel *plm_newModel()
{
    PlayerEngine *e = plm_engine();
    if (!e) return nullptr;
    PlaylistModel *m = new PlaylistModel(e);
    m->_playlistFile = QDir::tempPath() + "/plm_playlist_" +
                       QString::number(reinterpret_cast<quintptr>(m), 16) + ".ini";
    return m;
}

static void plm_dispose(PlaylistModel *m)
{
    if (!m) return;
    // Drain any zero-delay timers the model may have scheduled so their lambdas
    // do not fire after `m` is gone.
    QTest::qWait(10);
    QString f = m->_playlistFile;
    delete m;
    QFile::remove(f);
}

// Create a real (tiny) file on disk so PlayItemInfo::refresh() keeps valid=true
// (refresh() overwrites `valid` with info.exists()). Returns the absolute path.
static QString plm_makeRealFile(const QString &name)
{
    QString path = QDir::tempPath() + "/" + name;
    QFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write("x");
        f.close();
    }
    return path;
}

// ---- Stub replacements (free functions, matching non-virtual signatures) ----
// All are no-ops / sentinels so the model's playback bookkeeping runs without
// ever touching the real mpv/gst backend.
static PlayerEngine::CoreState plm_stub_state_playing()
{
    return PlayerEngine::Playing;
}
static PlayerEngine::CoreState plm_stub_state_idle()
{
    return PlayerEngine::Idle;
}
static void plm_stub_waitLastEnd() {}
static void plm_stub_requestPlay(int) {}

// Convenience: install the "safe playback" stub set on a local Stub.
static void plm_installSafePlaybackStubs(Stub &stub, PlayerEngine::CoreState st)
{
    if (st == PlayerEngine::Playing) {
        stub.set(ADDR(PlayerEngine, state), plm_stub_state_playing);
    } else {
        stub.set(ADDR(PlayerEngine, state), plm_stub_state_idle);
    }
    stub.set(ADDR(PlayerEngine, waitLastEnd), plm_stub_waitLastEnd);
    stub.set(ADDR(PlayerEngine, requestPlay), plm_stub_requestPlay);
}

// ===========================================================================
// Construction & destructor
// ===========================================================================

TEST(playlist_model_ext, construct_fresh_model_safe)
{
    PlayerEngine *e = plm_engine();
    ASSERT_TRUE(e != nullptr);
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    EXPECT_EQ(m->count(), 0);
    EXPECT_EQ(m->current(), -1);  // ctor -> stop() resets _current
    EXPECT_EQ(m->size(), 0);
    plm_dispose(m);
}

// Destructor with a GetThumbnail present but NOT running covers the
// "deleteLater + null-out" branch of ~PlaylistModel.
TEST(playlist_model_ext, destructor_with_idle_getthumbnail_safe)
{
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    m->m_getThumbnail = new GetThumbnail(m, QList<QUrl>());
    EXPECT_NO_FATAL_FAILURE({ plm_dispose(m); });
}

// ===========================================================================
// reshuffle()
// ===========================================================================

// With items present and ShufflePlay, reshuffle populates _playOrder with every
// index (the currently-uncovered body of reshuffle()).
TEST(playlist_model_ext, reshuffle_populates_playOrder_when_items_and_shuffle)
{
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    for (int i = 0; i < 3; ++i)
        m->_infos.append(plm_makeItem(QString("/tmp/plm_a%1.mp4").arg(i), true));
    m->setPlayMode(PlaylistModel::ShufflePlay);
    EXPECT_EQ(m->_playOrder.size(), 3);
    // Same set of indices regardless of order.
    QSet<int> idx;
    for (int v : m->_playOrder) idx.insert(v);
    EXPECT_TRUE(idx.contains(0) && idx.contains(1) && idx.contains(2));
    plm_dispose(m);
}

// reshuffle is a no-op unless the mode is ShufflePlay.
TEST(playlist_model_ext, reshuffle_noop_when_not_shuffle)
{
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    m->_infos.append(plm_makeItem("/tmp/plm_b1.mp4", true));
    m->setPlayMode(PlaylistModel::OrderPlay);
    EXPECT_TRUE(m->_playOrder.isEmpty());
    plm_dispose(m);
}

// reshuffle is a no-op when the list is empty (the size()==0 guard).
TEST(playlist_model_ext, reshuffle_noop_when_empty)
{
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    m->setPlayMode(PlaylistModel::ShufflePlay);
    EXPECT_TRUE(m->_playOrder.isEmpty());
    plm_dispose(m);
}

// ===========================================================================
// clear()
// ===========================================================================

// clear() empties _infos, resets _current/_last, saves and emits.
TEST(playlist_model_ext, clear_empties_infos_and_resets_current)
{
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    for (int i = 0; i < 3; ++i)
        m->_infos.append(plm_makeItem(QString("/tmp/plm_c%1.mp4").arg(i), true));
    m->_current = 1;
    m->clear();
    EXPECT_EQ(m->count(), 0);
    EXPECT_EQ(m->current(), -1);
    EXPECT_EQ(m->_last, -1);
    plm_dispose(m);
}

// clear() emits emptied, currentChanged and countChanged.
TEST(playlist_model_ext, clear_emits_expected_signals)
{
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    m->_infos.append(plm_makeItem("/tmp/plm_d1.mp4", true));
    QSignalSpy emptied(m, &PlaylistModel::emptied);
    QSignalSpy cur(m, &PlaylistModel::currentChanged);
    QSignalSpy cnt(m, &PlaylistModel::countChanged);
    m->clear();
    EXPECT_EQ(emptied.count(), 1);
    EXPECT_EQ(cur.count(), 1);
    EXPECT_EQ(cnt.count(), 1);
    plm_dispose(m);
}

// ===========================================================================
// remove()  (Idle engine branches)
// ===========================================================================

// remove at the current position while Idle resets _current/_last.
TEST(playlist_model_ext, remove_idle_current_pos_resets)
{
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    for (int i = 0; i < 3; ++i)
        m->_infos.append(plm_makeItem(QString("/tmp/plm_e%1.mp4").arg(i), true));
    m->_current = 1;
    m->remove(1);
    EXPECT_EQ(m->count(), 2);
    EXPECT_EQ(m->current(), -1);   // _current == pos -> reset
    plm_dispose(m);
}

// remove of an item that is NOT the current one (Idle) leaves _current as-is.
TEST(playlist_model_ext, remove_idle_non_current_keeps_current_pointer)
{
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    for (int i = 0; i < 3; ++i)
        m->_infos.append(plm_makeItem(QString("/tmp/plm_f%1.mp4").arg(i), true));
    m->_current = 1;
    m->remove(2);   // not current, last index
    EXPECT_EQ(m->count(), 2);
    EXPECT_EQ(m->current(), 1);
    plm_dispose(m);
}

// remove at out-of-range positions is a guarded no-op.
TEST(playlist_model_ext, remove_out_of_range_safe)
{
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    EXPECT_NO_FATAL_FAILURE({ m->remove(-1); });
    EXPECT_NO_FATAL_FAILURE({ m->remove(99999); });
    EXPECT_EQ(m->count(), 0);
    plm_dispose(m);
}

// remove() with the engine reporting Playing covers the currently-unreachable
// "_current == pos" reset inside the !=Idle branch, plus pos<_current adjust.
TEST(playlist_model_ext, remove_playing_branch_with_stubs)
{
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    for (int i = 0; i < 3; ++i)
        m->_infos.append(plm_makeItem(QString("/tmp/plm_g%1.mp4").arg(i), true));

    Stub stub;
    plm_installSafePlaybackStubs(stub, PlayerEngine::Playing);

    // pos < _current  -> _current decremented.
    m->_current = 2;
    m->remove(0);
    EXPECT_EQ(m->count(), 2);
    EXPECT_EQ(m->current(), 1);

    // _current == pos (the !=Idle reset path).
    m->_current = 0;
    m->remove(0);
    EXPECT_EQ(m->current(), -1);
    plm_dispose(m);
}

// ===========================================================================
// switchPosition()
// ===========================================================================

// _current == src -> _current becomes target.
TEST(playlist_model_ext, switchPosition_src_is_current_moves_to_target)
{
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    for (int i = 0; i < 3; ++i)
        m->_infos.append(plm_makeItem(QString("/tmp/plm_h%1.mp4").arg(i), true));
    m->_current = 0;
    m->switchPosition(0, 2);
    EXPECT_EQ(m->current(), 2);
    EXPECT_EQ(m->count(), 3);
    plm_dispose(m);
}

// _current strictly between min/max, src < target -> _current decremented.
TEST(playlist_model_ext, switchPosition_src_less_target_decrements_current)
{
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    for (int i = 0; i < 4; ++i)
        m->_infos.append(plm_makeItem(QString("/tmp/plm_i%1.mp4").arg(i), true));
    m->_current = 2;
    m->switchPosition(0, 3);   // min=0, max=3, src=0<target=3
    EXPECT_EQ(m->current(), 1);
    plm_dispose(m);
}

// _current strictly between min/max, src > target -> _current incremented.
TEST(playlist_model_ext, switchPosition_src_greater_target_increments_current)
{
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    for (int i = 0; i < 4; ++i)
        m->_infos.append(plm_makeItem(QString("/tmp/plm_j%1.mp4").arg(i), true));
    m->_current = 1;
    m->switchPosition(3, 0);   // min=0, max=3, src=3>target=0
    EXPECT_EQ(m->current(), 2);
    plm_dispose(m);
}

// _current outside the [min,max] range -> current unchanged, no signal.
TEST(playlist_model_ext, switchPosition_current_outside_range_unchanged)
{
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    for (int i = 0; i < 3; ++i)
        m->_infos.append(plm_makeItem(QString("/tmp/plm_k%1.mp4").arg(i), true));
    m->_current = -1;
    QSignalSpy spy(m, &PlaylistModel::currentChanged);
    m->switchPosition(0, 2);
    EXPECT_EQ(m->current(), -1);
    EXPECT_EQ(spy.count(), 0);
    plm_dispose(m);
}

// ===========================================================================
// currentInfo() non-const fallbacks
// ===========================================================================

// _current >= 0 -> returns _infos[_current].
TEST(playlist_model_ext, currentInfo_returns_current)
{
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    m->_infos.append(plm_makeItem("/tmp/plm_l0.mp4", true));
    m->_infos.append(plm_makeItem("/tmp/plm_l1.mp4", true));
    m->_current = 1;
    EXPECT_EQ(m->currentInfo().url, QUrl::fromLocalFile("/tmp/plm_l1.mp4"));
    plm_dispose(m);
}

// _current < 0, _last valid -> falls back to _infos[_last].
TEST(playlist_model_ext, currentInfo_falls_back_to_last)
{
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    m->_infos.append(plm_makeItem("/tmp/plm_m0.mp4", true));
    m->_infos.append(plm_makeItem("/tmp/plm_m1.mp4", true));
    m->_current = -1;
    m->_last = 1;
    EXPECT_EQ(m->currentInfo().url, QUrl::fromLocalFile("/tmp/plm_m1.mp4"));
    plm_dispose(m);
}

// _current < 0 and _last invalid -> falls back to _infos[0].
TEST(playlist_model_ext, currentInfo_falls_back_to_zero)
{
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    m->_infos.append(plm_makeItem("/tmp/plm_n0.mp4", true));
    m->_infos.append(plm_makeItem("/tmp/plm_n1.mp4", true));
    m->_current = -1;
    m->_last = -1;
    EXPECT_EQ(m->currentInfo().url, QUrl::fromLocalFile("/tmp/plm_n0.mp4"));
    plm_dispose(m);
}

// ===========================================================================
// handleAsyncAppendResults() / onAsyncUpdate() / SortSimilarFiles
// ===========================================================================

// Empty input is an early return; _infos is untouched.
TEST(playlist_model_ext, handleAsyncAppendResults_empty_returns_early)
{
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    QList<PlayItemInfo> empty;
    m->handleAsyncAppendResults(empty);
    EXPECT_EQ(m->count(), 0);
    plm_dispose(m);
}

// Invalid items are filtered out; valid ones survive and (when !_firstLoad)
// are sorted via SortSimilarFiles.
TEST(playlist_model_ext, handleAsyncAppendResults_filters_invalid_and_sorts)
{
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    m->_firstLoad = false;
    QList<PlayItemInfo> pil;
    pil << plm_makeItem("/tmp/plm_o_invalid.mp4", false);
    pil << plm_makeItem("/tmp/plm_o_show01e02.mp4", true);
    pil << plm_makeItem("/tmp/plm_o_show01e01.mp4", true);
    m->handleAsyncAppendResults(pil);
    EXPECT_EQ(m->count(), 2);
    // Both valid items were retained (order may differ post-sort).
    EXPECT_GE(m->indexOf(QUrl::fromLocalFile("/tmp/plm_o_show01e01.mp4")), 0);
    EXPECT_GE(m->indexOf(QUrl::fromLocalFile("/tmp/plm_o_show01e02.mp4")), 0);
    plm_dispose(m);
}

// _firstLoad == true -> appends without sorting.
TEST(playlist_model_ext, handleAsyncAppendResults_firstload_appends_unsorted)
{
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    m->_firstLoad = true;
    QList<PlayItemInfo> pil;
    pil << plm_makeItem("/tmp/plm_p_zeta.mp4", true);
    pil << plm_makeItem("/tmp/plm_p_alpha.mp4", true);
    m->handleAsyncAppendResults(pil);
    EXPECT_EQ(m->count(), 2);
    EXPECT_FALSE(m->_firstLoad);   // cleared after the call
    plm_dispose(m);
}

// onAsyncUpdate with an invalid MovieInfo filters the item out.
TEST(playlist_model_ext, onAsyncUpdate_filters_invalid_mi)
{
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    m->_firstLoad = true;
    m->onAsyncUpdate(plm_makeItem("/tmp/plm_q_invalid.mp4", false));
    EXPECT_EQ(m->count(), 0);
    plm_dispose(m);
}

// onAsyncUpdate with a valid item appends it and clears _firstLoad.
TEST(playlist_model_ext, onAsyncUpdate_appends_valid_firstload)
{
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    m->_firstLoad = true;
    QSignalSpy appended(m, &PlaylistModel::itemsAppended);
    QSignalSpy cnt(m, &PlaylistModel::countChanged);
    m->onAsyncUpdate(plm_makeItem("/tmp/plm_r_valid.mp4", true));
    EXPECT_EQ(m->count(), 1);
    EXPECT_FALSE(m->_firstLoad);
    EXPECT_EQ(appended.count(), 1);
    EXPECT_EQ(cnt.count(), 1);
    plm_dispose(m);
}

// ===========================================================================
// GetThumbnail worker lifecycle (no decode thread spawned)
// ===========================================================================

// getThumbnailRunning() returns false when m_getThumbnail is null.
TEST(playlist_model_ext, getThumbnailRunning_null_returns_false)
{
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    m->m_getThumbnail = nullptr;
    EXPECT_FALSE(m->getThumbnailRunning());
    plm_dispose(m);
}

// getThumbnailRunning() returns false for an idle (never-started) worker.
TEST(playlist_model_ext, getThumbnailRunning_idle_worker_returns_false)
{
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    m->m_getThumbnail = new GetThumbnail(m, QList<QUrl>());
    EXPECT_FALSE(m->getThumbnailRunning());
    // Direct lifecycle helpers are safe with no thread running.
    EXPECT_NO_FATAL_FAILURE({ m->m_getThumbnail->stop(); });
    EXPECT_NO_FATAL_FAILURE({ m->m_getThumbnail->setUrls(QList<QUrl>()); });
    EXPECT_NO_FATAL_FAILURE({ m->m_getThumbnail->clearItem(); });
    plm_dispose(m);
}

// onAsyncFinished() with an empty m_tempList only calls clearItem on the worker.
TEST(playlist_model_ext, onAsyncFinished_empty_templist_safe)
{
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    m->m_getThumbnail = new GetThumbnail(m, QList<QUrl>());
    m->m_tempList.clear();
    EXPECT_NO_FATAL_FAILURE({ m->onAsyncFinished(); });
    EXPECT_TRUE(m->m_tempList.isEmpty());
    plm_dispose(m);
}

// ===========================================================================
// slotStateChanged()
// ===========================================================================

// Calling slotStateChanged directly -> sender() is null -> early return.
TEST(playlist_model_ext, slotStateChanged_null_sender_returns_early)
{
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    EXPECT_NO_FATAL_FAILURE({ m->slotStateChanged(); });
    plm_dispose(m);
}

// ===========================================================================
// tryPlayCurrent()  (invalid-item safe branch + valid branch under stub)
// ===========================================================================

// All items invalid -> tryPlayCurrent takes the else branch, canPlay stays false,
// no playback is attempted.
TEST(playlist_model_ext, tryPlayCurrent_invalid_items_safe)
{
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    for (int i = 0; i < 2; ++i)
        m->_infos.append(plm_makeItem(QString("/tmp/plm_s_%1.mp4").arg(i), false));
    m->_current = 0;
    EXPECT_NO_FATAL_FAILURE({ m->tryPlayCurrent(true); });
    EXPECT_EQ(m->current(), -1);   // invalid -> _current reset
    plm_dispose(m);
}

// Valid item under the safe-playback stubs -> requestPlay is a no-op and
// currentChanged is emitted. The file must exist on disk so refresh() keeps
// pif.valid == true and the valid branch is actually taken.
TEST(playlist_model_ext, tryPlayCurrent_valid_item_under_stub)
{
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    QString p = plm_makeRealFile("plm_t_valid.mp4");
    m->_infos.append(plm_makeItem(p, true));
    m->_current = 0;
    Stub stub;
    plm_installSafePlaybackStubs(stub, PlayerEngine::Playing);
    QSignalSpy cur(m, &PlaylistModel::currentChanged);
    EXPECT_NO_FATAL_FAILURE({ m->tryPlayCurrent(true); });
    EXPECT_EQ(cur.count(), 1);
    plm_dispose(m);
    QFile::remove(p);
}

// ===========================================================================
// playNext() / playPrev()
// ===========================================================================

TEST(playlist_model_ext, playNext_empty_returns_early)
{
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    EXPECT_NO_FATAL_FAILURE({ m->playNext(false); });
    EXPECT_NO_FATAL_FAILURE({ m->playNext(true); });
    plm_dispose(m);
}

TEST(playlist_model_ext, playPrev_empty_returns_early)
{
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    EXPECT_NO_FATAL_FAILURE({ m->playPrev(false); });
    EXPECT_NO_FATAL_FAILURE({ m->playPrev(true); });
    plm_dispose(m);
}

// OrderPlay, auto-advance at end of list -> the "reached end" break (no
// tryPlayCurrent), no playback.
TEST(playlist_model_ext, playNext_orderplay_auto_at_end_breaks)
{
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    m->_infos.append(plm_makeItem("/tmp/plm_u_0.mp4", false));
    m->_infos.append(plm_makeItem("/tmp/plm_u_1.mp4", false));
    m->setPlayMode(PlaylistModel::OrderPlay);
    m->_last = 1;   // == count()-1
    EXPECT_NO_FATAL_FAILURE({ m->playNext(false); });
    // _last was incremented to count() then decremented back.
    EXPECT_EQ(m->_last, 1);
    plm_dispose(m);
}

// SinglePlay auto-advance is explicitly ignored.
TEST(playlist_model_ext, playNext_singleplay_auto_ignored)
{
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    m->_infos.append(plm_makeItem("/tmp/plm_v_0.mp4", true));
    m->setPlayMode(PlaylistModel::SinglePlay);
    m->_last = 0;
    Stub stub;
    plm_installSafePlaybackStubs(stub, PlayerEngine::Idle);
    EXPECT_NO_FATAL_FAILURE({ m->playNext(false); });
    plm_dispose(m);
}

// playPrev SinglePlay, auto -> no-op.
TEST(playlist_model_ext, playPrev_singleplay_auto_noop)
{
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    m->_infos.append(plm_makeItem("/tmp/plm_w_0.mp4", true));
    m->setPlayMode(PlaylistModel::SinglePlay);
    m->_last = 0;
    Stub stub;
    plm_installSafePlaybackStubs(stub, PlayerEngine::Idle);
    EXPECT_NO_FATAL_FAILURE({ m->playPrev(false); });
    plm_dispose(m);
}

// OrderPlay, user next at end wraps to 0 and tries the (stubbed) play.
TEST(playlist_model_ext, playNext_orderplay_fromuser_wraps_under_stub)
{
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    QString p0 = plm_makeRealFile("plm_x_0.mp4");
    QString p1 = plm_makeRealFile("plm_x_1.mp4");
    m->_infos.append(plm_makeItem(p0, true));
    m->_infos.append(plm_makeItem(p1, true));
    m->setPlayMode(PlaylistModel::OrderPlay);
    m->_last = 1;
    Stub stub;
    plm_installSafePlaybackStubs(stub, PlayerEngine::Playing);
    EXPECT_NO_FATAL_FAILURE({ m->playNext(true); });
    EXPECT_EQ(m->current(), 0);
    plm_dispose(m);
    QFile::remove(p0); QFile::remove(p1);
}

// ListLoop wraps and increments the loop counter.
TEST(playlist_model_ext, playNext_listloop_wraps_under_stub)
{
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    QString p0 = plm_makeRealFile("plm_y_0.mp4");
    QString p1 = plm_makeRealFile("plm_y_1.mp4");
    m->_infos.append(plm_makeItem(p0, true));
    m->_infos.append(plm_makeItem(p1, true));
    m->setPlayMode(PlaylistModel::ListLoop);
    m->_last = 1;
    int loopsBefore = m->_loopCount;
    Stub stub;
    plm_installSafePlaybackStubs(stub, PlayerEngine::Playing);
    EXPECT_NO_FATAL_FAILURE({ m->playNext(false); });
    EXPECT_EQ(m->_loopCount, loopsBefore + 1);
    EXPECT_EQ(m->current(), 0);
    plm_dispose(m);
    QFile::remove(p0); QFile::remove(p1);
}

// ShufflePlay auto-advance: reshuffles when exhausted, picks from _playOrder.
TEST(playlist_model_ext, playNext_shuffle_under_stub)
{
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    QString p0 = plm_makeRealFile("plm_z_0.mp4");
    QString p1 = plm_makeRealFile("plm_z_1.mp4");
    m->_infos.append(plm_makeItem(p0, true));
    m->_infos.append(plm_makeItem(p1, true));
    m->setPlayMode(PlaylistModel::ShufflePlay);
    m->_shufflePlayed = m->_playOrder.size();   // force reshuffle
    Stub stub;
    plm_installSafePlaybackStubs(stub, PlayerEngine::Playing);
    EXPECT_NO_FATAL_FAILURE({ m->playNext(false); });
    EXPECT_GE(m->current(), 0);
    plm_dispose(m);
    QFile::remove(p0); QFile::remove(p1);
}

// SingleLoop, user next while engine Playing -> advances to next slot.
TEST(playlist_model_ext, playNext_singleloop_fromuser_playing_under_stub)
{
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    QString p0 = plm_makeRealFile("plm_aa_0.mp4");
    QString p1 = plm_makeRealFile("plm_aa_1.mp4");
    m->_infos.append(plm_makeItem(p0, true));
    m->_infos.append(plm_makeItem(p1, true));
    m->setPlayMode(PlaylistModel::SingleLoop);
    m->_last = 0;
    Stub stub;
    plm_installSafePlaybackStubs(stub, PlayerEngine::Playing);
    EXPECT_NO_FATAL_FAILURE({ m->playNext(true); });
    EXPECT_EQ(m->current(), 1);
    plm_dispose(m);
    QFile::remove(p0); QFile::remove(p1);
}

// playPrev OrderPlay under stub wraps from first to last.
TEST(playlist_model_ext, playPrev_orderplay_wraps_under_stub)
{
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    QString p0 = plm_makeRealFile("plm_ab_0.mp4");
    QString p1 = plm_makeRealFile("plm_ab_1.mp4");
    m->_infos.append(plm_makeItem(p0, true));
    m->_infos.append(plm_makeItem(p1, true));
    m->setPlayMode(PlaylistModel::OrderPlay);
    m->_last = 0;
    Stub stub;
    plm_installSafePlaybackStubs(stub, PlayerEngine::Playing);
    EXPECT_NO_FATAL_FAILURE({ m->playPrev(true); });
    EXPECT_EQ(m->current(), 1);
    plm_dispose(m);
    QFile::remove(p0); QFile::remove(p1);
}

// ===========================================================================
// changeCurrent()
// ===========================================================================

// changeCurrent to the current position (non-webm) is an early return.
TEST(playlist_model_ext, changeCurrent_same_position_early_return)
{
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    m->_infos.append(plm_makeItem("/tmp/plm_ac_0.mp4", true));
    m->_current = 0;
    QSignalSpy cur(m, &PlaylistModel::currentChanged);
    m->changeCurrent(0);
    EXPECT_EQ(cur.count(), 0);   // early return, no signal
    plm_dispose(m);
}

// changeCurrent out-of-range is a no-op.
TEST(playlist_model_ext, changeCurrent_out_of_range_safe)
{
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    EXPECT_NO_FATAL_FAILURE({ m->changeCurrent(-1); });
    EXPECT_NO_FATAL_FAILURE({ m->changeCurrent(99999); });
    plm_dispose(m);
}

// ===========================================================================
// collectionJob() / delayedAppendAsync() / appendAsync()
// ===========================================================================

// collectionJob dedups, skips invalid urls, and records local + non-local jobs.
TEST(playlist_model_ext, collectionJob_dedups_and_records)
{
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    m->_firstLoad = true;   // so the !exists/isFile guard is skipped
    QList<QUrl> in;
    QList<QUrl> urls;
    urls << QUrl()                                           // invalid -> skip
         << QUrl("http://example.com/plm_ad_remote.mp4")     // non-local -> job
         << QUrl::fromLocalFile("/tmp/plm_ad_local_missing.mp4"); // local -> job
    EXPECT_NO_FATAL_FAILURE({ m->collectionJob(urls, in); });
    EXPECT_GE(m->_pendingJob.size(), 2);
    EXPECT_FALSE(in.isEmpty());
    plm_dispose(m);
}

// collectionJob skips a url already present in _infos.
TEST(playlist_model_ext, collectionJob_skips_existing_url)
{
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    QUrl existing = QUrl::fromLocalFile("/tmp/plm_ae_present.mp4");
    m->_infos.append(plm_makeItem(existing.toLocalFile(), true));
    QList<QUrl> in;
    QList<QUrl> urls; urls << existing;
    m->collectionJob(urls, in);
    EXPECT_TRUE(m->_pendingJob.isEmpty());
    EXPECT_TRUE(in.isEmpty());
    plm_dispose(m);
}

// delayedAppendAsync enqueues when a job is already pending (no thread spawn).
TEST(playlist_model_ext, delayedAppendAsync_enqueues_when_pending)
{
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    // Pretend a job is in flight.
    m->_pendingJob.append(qMakePair(QUrl::fromLocalFile("/tmp/plm_af_pending.mp4"),
                                    QFileInfo("/tmp/plm_af_pending.mp4")));
    QList<QUrl> next; next << QUrl::fromLocalFile("/tmp/plm_af_next.mp4");
    EXPECT_NO_FATAL_FAILURE({ m->delayedAppendAsync(next); });
    EXPECT_EQ(m->_pendingAppendReq.size(), 1);
    plm_dispose(m);
}

// appendAsync on an empty list is safe and does not spawn a worker. The init
// flags are pre-set so the initFFmpeg/initThumb guards take their (skipped)
// false branch without actually loading the shared thumbnailer/ffmpeg globals.
TEST(playlist_model_ext, appendAsync_empty_safe)
{
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    m->m_initFFmpeg = true;
    m->m_bInitThumb = true;
    EXPECT_NO_FATAL_FAILURE({ m->appendAsync(QList<QUrl>()); });
    QTest::qWait(10);
    plm_dispose(m);
}

// ===========================================================================
// Pure helpers / parse paths
// ===========================================================================

// getMusicPix on a non-existent file returns false (the !fi.exists() guard).
TEST(playlist_model_ext, getMusicPix_nonexistent_returns_false)
{
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    QPixmap pm;
    bool ok = true;
    EXPECT_NO_FATAL_FAILURE({ ok = m->getMusicPix(QFileInfo("/tmp/plm_ag_no_such.mp3"), pm); });
    EXPECT_FALSE(ok);
    plm_dispose(m);
}

// parseFromFile on a non-existent file yields an invalid MovieInfo. The *ok
// out-parameter is only written on the mpv-present branch (set to false) or the
// Qt branch when the file exists, so we only assert mi.valid here.
TEST(playlist_model_ext, parseFromFile_nonexistent_file)
{
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    bool ok = false;
    MovieInfo mi;
    EXPECT_NO_FATAL_FAILURE({
        mi = m->parseFromFile(QFileInfo("/tmp/plm_ah_no_such.mp4"), &ok);
    });
    EXPECT_FALSE(mi.valid);
    plm_dispose(m);
}

// parseFromFileByQt on a non-existent file returns a default (invalid) MovieInfo.
TEST(playlist_model_ext, parseFromFileByQt_nonexistent_file)
{
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    bool ok = false;
    MovieInfo mi;
    EXPECT_NO_FATAL_FAILURE({
        mi = m->parseFromFileByQt(QFileInfo("/tmp/plm_ai_no_such.mp4"), &ok);
    });
    EXPECT_FALSE(mi.valid);
    plm_dispose(m);
}

// getUrlFileTotalSize clamps tryTimes<=0 to 1 and fails fast on an unused port.
TEST(playlist_model_ext, getUrlFileTotalSize_clamps_and_fails_safe)
{
    PlaylistModel *m = plm_newModel();
    ASSERT_TRUE(m != nullptr);
    qint64 sz = -42;
    EXPECT_NO_FATAL_FAILURE({
        sz = m->getUrlFileTotalSize(QUrl("http://127.0.0.1:1/plm_aj_none"), 0);
    });
    EXPECT_TRUE(sz == -1 || sz >= 0);
    plm_dispose(m);
}

// ===========================================================================
// MovieInfo serialization (operator<</operator>>) round-trip
// ===========================================================================

// The QDataStream serializers for MovieInfo round-trip a populated struct.
TEST(playlist_model_ext, MovieInfo_stream_operators_roundtrip)
{
    MovieInfo src;
    src.valid = true;
    src.title = "plm_title";
    src.fileType = "mp4";
    src.resolution = "640x480";
    src.filePath = "/tmp/plm_ak.mp4";
    src.creation = "2024-01-01";
    src.raw_rotate = 90;
    src.fileSize = 12345;
    src.duration = 9999;
    src.width = 640;
    src.height = 480;
    src.vCodecID = 12;
    src.vCodeRate = 5000;
    src.fps = 25;
    src.proportion = 1.33f;
    src.aCodeID = 86018;
    src.aCodeRate = 128000;
    src.aDigit = 0;
    src.channels = 2;
    src.sampling = 44100;

    QByteArray bytes;
    {
        QDataStream out(&bytes, QIODevice::WriteOnly);
        out << src;
    }
    ASSERT_FALSE(bytes.isEmpty());

    MovieInfo dst;
    {
        QDataStream in(&bytes, QIODevice::ReadOnly);
        in >> dst;
    }
    EXPECT_TRUE(dst.valid);
    EXPECT_EQ(dst.title.toStdString(), "plm_title");
    EXPECT_EQ(dst.fileType.toStdString(), "mp4");
    EXPECT_EQ(dst.resolution.toStdString(), "640x480");
    EXPECT_EQ(dst.filePath.toStdString(), "/tmp/plm_ak.mp4");
    EXPECT_EQ(dst.fileSize, 12345);
    EXPECT_EQ(dst.duration, 9999);
    EXPECT_EQ(dst.width, 640);
    EXPECT_EQ(dst.height, 480);
    EXPECT_EQ(dst.vCodecID, 12);
    EXPECT_EQ(dst.fps, 25);
    EXPECT_EQ(dst.channels, 2);
    EXPECT_EQ(dst.sampling, 44100);
}

// MovieInfo::sizeStr covers all four size bands.
TEST(playlist_model_ext, MovieInfo_sizeStr_all_bands)
{
    const qint64 K = 1024, M = 1024 * K, G = 1024 * M;
    MovieInfo mi;
    mi.fileSize = 500;            EXPECT_EQ(mi.sizeStr().toStdString(), "500");
    mi.fileSize = 2 * K;          EXPECT_TRUE(mi.sizeStr().endsWith('K'));
    mi.fileSize = 5 * M;          EXPECT_TRUE(mi.sizeStr().endsWith('M'));
    mi.fileSize = 3 * G;          EXPECT_TRUE(mi.sizeStr().endsWith('G'));
}
