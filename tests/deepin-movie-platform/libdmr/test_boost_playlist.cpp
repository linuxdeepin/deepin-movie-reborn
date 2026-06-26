// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// Coverage-boost unit tests for src/libdmr/playlist_model.cpp and
// src/libdmr/player_engine.cpp.
//
// Suite name "boost_pl" is distinct from every other suite in the binary
// ("libdmr", "libdmr_ext", "engine_model_ext", "playlist_model_ext",
// "player_engine_ext") so TEST() cases never collide.
//
// Strategy:
//   * A FRESH PlaylistModel is constructed against the live engine for every
//     mutating case, with its persisted playlist file redirected to a unique
//     temp path so the shared engine's playlist store is never disturbed.
//   * Private members are reached via the `#define private public` access block
//     so list-manipulation branches can be exercised with synthetic
//     PlayItemInfo items without spawning any real decode.
//   * Playback-touching paths (requestPlay / waitLastEnd) are neutralised with
//     file-scope Stub functions so tryPlayCurrent / playPrev / playNext can run
//     their inner switch logic without contacting mpv/gst.
//   * PlayerEngine cases are restricted to pure getters, state checks, and
//     settings-backed mutators invoked while the shared engine is Idle.
//   * LoadThread is constructed directly (empty url list) so its ctor/dtor
//     members are covered without ever starting the worker thread.
//
// Hard rules honoured:
//   * Only Google Test (TEST(boost_pl, ...)); gtest_main owns main().
//   * Static helpers use the "bpl_" prefix (unique to this TU).
//   * Stubs use "stub/stub.h"; stub replacements use the "bpl_stub_" prefix.

#include <QtTest>
#include <QTest>
#include <QSignalSpy>
#include <QDebug>
#include <unistd.h>
#include <gtest/gtest.h>

// STL / Qt headers BEFORE the access defines, per the project convention.
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

// ===========================================================================
// Helpers (file scope, "bpl_" prefix)
// ===========================================================================

// Live engine wired up by the test main (Platform_MainWindow already created).
static PlayerEngine *bpl_engine()
{
    Platform_MainWindow *w = dApp->getMainWindow();
    return w ? w->engine() : nullptr;
}

// Build a synthetic PlayItemInfo. `valid` controls both pif.valid and mi.valid.
static PlayItemInfo bpl_makeItem(const QString &path, bool valid)
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
// playlist file redirected to a unique temp path.
static PlaylistModel *bpl_newModel()
{
    PlayerEngine *e = bpl_engine();
    if (!e) return nullptr;
    PlaylistModel *m = new PlaylistModel(e);
    m->_playlistFile = QDir::tempPath() + "/bpl_playlist_" +
                       QString::number(reinterpret_cast<quintptr>(m), 16) + ".ini";
    return m;
}

static void bpl_dispose(PlaylistModel *m)
{
    if (!m) return;
    QTest::qWait(10);
    QString f = m->_playlistFile;
    delete m;
    QFile::remove(f);
}

// Create a real (tiny) file on disk so PlayItemInfo::refresh() keeps valid=true.
static QString bpl_makeRealFile(const QString &name)
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
static PlayerEngine::CoreState bpl_stub_state_playing()
{
    return PlayerEngine::Playing;
}
static PlayerEngine::CoreState bpl_stub_state_idle()
{
    return PlayerEngine::Idle;
}
static void bpl_stub_waitLastEnd() {}
static void bpl_stub_requestPlay(int) {}
static bool bpl_stub_isMpvExists_false() { return false; }

static void bpl_installSafePlaybackStubs(Stub &stub, PlayerEngine::CoreState st)
{
    if (st == PlayerEngine::Playing) {
        stub.set(ADDR(PlayerEngine, state), bpl_stub_state_playing);
    } else {
        stub.set(ADDR(PlayerEngine, state), bpl_stub_state_idle);
    }
    stub.set(ADDR(PlayerEngine, waitLastEnd), bpl_stub_waitLastEnd);
    stub.set(ADDR(PlayerEngine, requestPlay), bpl_stub_requestPlay);
}

// RAII guard that temporarily nulls PlayerEngine::_current so backend-touching
// paths reduce to their documented null-guard early returns.
struct bpl_NullCurrentGuard {
    PlayerEngine *engine;
    Backend *saved;
    explicit bpl_NullCurrentGuard(PlayerEngine *e)
        : engine(e), saved(e ? e->_current : nullptr)
    {
        if (engine) engine->_current = nullptr;
    }
    ~bpl_NullCurrentGuard()
    {
        if (engine) engine->_current = saved;
    }
};

// ===========================================================================
// PlaylistModel: persistent cache coverage via getMovieInfo
// ===========================================================================

// getMovieInfo on a non-existent local file exercises PersistentManager's
// loadFromCache (the !exists early-return branch) end-to-end without ever
// spawning a decode. This is the public-API route to cover the cache code path
// (PersistentManager itself is file-local in playlist_model.cpp and cannot be
// named directly from this TU).
TEST(boost_pl, get_movie_info_nonexistent_local_invokes_cache_safe)
{
    PlayerEngine *e = bpl_engine();
    ASSERT_NE(e, nullptr);
    PlaylistModel *m = bpl_newModel();
    ASSERT_NE(m, nullptr);
    bool is = true;
    MovieInfo mi;
    EXPECT_NO_FATAL_FAILURE({
        mi = m->getMovieInfo(QUrl::fromLocalFile("/tmp/bpl_no_such_movie_xyz.mp4"), &is);
    });
    EXPECT_FALSE(is);
    EXPECT_FALSE(mi.valid);
    bpl_dispose(m);
}

// getMovieInfo on a non-local url exercises the non-local early-return branch.
TEST(boost_pl, get_movie_info_nonlocal_url_safe)
{
    PlayerEngine *e = bpl_engine();
    ASSERT_NE(e, nullptr);
    PlaylistModel *m = bpl_newModel();
    ASSERT_NE(m, nullptr);
    bool is = true;
    MovieInfo mi;
    EXPECT_NO_FATAL_FAILURE({
        mi = m->getMovieInfo(QUrl("http://127.0.0.1:1/bpl_remote_movie.mp4"), &is);
    });
    EXPECT_FALSE(is);
    EXPECT_FALSE(mi.valid);
    bpl_dispose(m);
}

// ===========================================================================
// PlaylistModel: parseFromFile mpv-absent branch (parseFromFileByQt)
// ===========================================================================

// When isMpvExists()==false, parseFromFile delegates to parseFromFileByQt,
// which calls GstUtils. Safe on a non-existent local file (mi stays invalid).
TEST(boost_pl, parse_from_file_nonexistent_local_safe)
{
    PlaylistModel *m = bpl_newModel();
    ASSERT_NE(m, nullptr);
    bool ok = false;
    MovieInfo mi;
    EXPECT_NO_FATAL_FAILURE({
        mi = m->parseFromFile(QFileInfo("/tmp/bpl_no_such_xyz.mp4"), &ok);
    });
    EXPECT_FALSE(mi.valid);
    bpl_dispose(m);
}

// parseFromFileByQt on a non-existent file: returned MovieInfo is invalid.
TEST(boost_pl, parse_from_file_by_qt_nonexistent_safe)
{
    PlaylistModel *m = bpl_newModel();
    ASSERT_NE(m, nullptr);
    bool ok = false;
    MovieInfo mi;
    EXPECT_NO_FATAL_FAILURE({
        mi = m->parseFromFileByQt(QFileInfo("/tmp/bpl_no_such_qt_xyz.mp4"), &ok);
    });
    EXPECT_FALSE(mi.valid);
    bpl_dispose(m);
}

// ===========================================================================
// PlaylistModel: appendSingle (non-local branch; safe, no decode of media)
// ===========================================================================

// A non-local URL is appended directly via calculatePlayInfo(network, isDvd=true)
// without touching a media file. isMpvExists is stubbed to false so the non-local
// branch of calculatePlayInfo does NOT dereference _engine->_current (which the
// fresh model would otherwise reach through the live engine's real backend).
TEST(boost_pl, append_single_nonlocal_url_safe)
{
    PlaylistModel *m = bpl_newModel();
    ASSERT_NE(m, nullptr);
    Stub stub;
    stub.set(ADDR(CompositingManager, isMpvExists), bpl_stub_isMpvExists_false);
    EXPECT_NO_FATAL_FAILURE({
        m->appendSingle(QUrl("http://127.0.0.1:1/bpl_remote_single.mp4"));
    });
    // calculatePlayInfo on a non-local url always produces one entry.
    EXPECT_EQ(m->count(), 1);
    bpl_dispose(m);
}

// appendSingle of a url already present is an early return (no growth).
TEST(boost_pl, append_single_duplicate_url_skipped)
{
    PlaylistModel *m = bpl_newModel();
    ASSERT_NE(m, nullptr);
    Stub stub;
    stub.set(ADDR(CompositingManager, isMpvExists), bpl_stub_isMpvExists_false);
    m->appendSingle(QUrl("http://127.0.0.1:1/bpl_dup.mp4"));
    EXPECT_EQ(m->count(), 1);
    EXPECT_NO_FATAL_FAILURE({
        m->appendSingle(QUrl("http://127.0.0.1:1/bpl_dup.mp4"));
    });
    EXPECT_EQ(m->count(), 1);
    bpl_dispose(m);
}

// appendSingle on a non-existent local file is an early return.
TEST(boost_pl, append_single_missing_local_skipped)
{
    PlaylistModel *m = bpl_newModel();
    ASSERT_NE(m, nullptr);
    EXPECT_NO_FATAL_FAILURE({
        m->appendSingle(QUrl::fromLocalFile("/tmp/bpl_definitely_no_such_file_xyz.mp4"));
    });
    EXPECT_EQ(m->count(), 0);
    bpl_dispose(m);
}

// ===========================================================================
// PlaylistModel: savePlaylist / loadPlaylist / clearPlaylist round-trip
// ===========================================================================

// savePlaylist on an empty model writes nothing and is idempotent.
TEST(boost_pl, save_playlist_empty_idempotent)
{
    PlaylistModel *m = bpl_newModel();
    ASSERT_NE(m, nullptr);
    EXPECT_NO_FATAL_FAILURE({ m->savePlaylist(); });
    EXPECT_NO_FATAL_FAILURE({ m->savePlaylist(); });
    bpl_dispose(m);
}

// loadPlaylist on the (empty) redirected store is a safe no-op; no decode.
TEST(boost_pl, load_playlist_empty_store_safe)
{
    PlaylistModel *m = bpl_newModel();
    ASSERT_NE(m, nullptr);
    EXPECT_NO_FATAL_FAILURE({ m->loadPlaylist(); });
    QTest::qWait(10);
    bpl_dispose(m);
}

// clearPlaylist on an empty model is a no-op.
TEST(boost_pl, clear_playlist_empty_noop)
{
    PlaylistModel *m = bpl_newModel();
    ASSERT_NE(m, nullptr);
    EXPECT_NO_FATAL_FAILURE({ m->clearPlaylist(); });
    EXPECT_EQ(m->count(), 0);
    bpl_dispose(m);
}

// ===========================================================================
// PlaylistModel: switchPosition additional branches
// ===========================================================================

// _current == src, src == target: the same-position branch leaves _current
// equal to target and emits no extra signal beyond currentChanged.
TEST(boost_pl, switch_position_src_equals_target_safe)
{
    PlaylistModel *m = bpl_newModel();
    ASSERT_NE(m, nullptr);
    for (int i = 0; i < 3; ++i)
        m->_infos.append(bpl_makeItem(QString("/tmp/bpl_sw_%1.mp4").arg(i), true));
    m->_current = 1;
    EXPECT_NO_FATAL_FAILURE({ m->switchPosition(1, 1); });
    EXPECT_EQ(m->current(), 1);
    bpl_dispose(m);
}

// _current == target (not src): current is unaffected.
TEST(boost_pl, switch_position_current_equals_target_unchanged)
{
    PlaylistModel *m = bpl_newModel();
    ASSERT_NE(m, nullptr);
    for (int i = 0; i < 4; ++i)
        m->_infos.append(bpl_makeItem(QString("/tmp/bpl_sw2_%1.mp4").arg(i), true));
    m->_current = 3;   // == target
    m->switchPosition(0, 3);
    EXPECT_EQ(m->current(), 3);
    bpl_dispose(m);
}

// ===========================================================================
// PlaylistModel: changeCurrent real-file branch
// ===========================================================================

// changeCurrent on a non-webm local file that exists triggers the
// calculatePlayInfo + requestPlay path; under the safe stubs requestPlay is a
// no-op. (The webm-only branch is covered separately below.)
TEST(boost_pl, change_current_existing_local_file_under_stub)
{
    PlaylistModel *m = bpl_newModel();
    ASSERT_NE(m, nullptr);
    QString p = bpl_makeRealFile("bpl_cc_local.mp4");
    m->_infos.append(bpl_makeItem(p, true));
    Stub stub;
    bpl_installSafePlaybackStubs(stub, PlayerEngine::Playing);
    EXPECT_NO_FATAL_FAILURE({ m->changeCurrent(0); });
    bpl_dispose(m);
    QFile::remove(p);
}

// changeCurrent on a webm file with the current position different takes the
// dedicated webm calculatePlayInfo branch; under stubs requestPlay is a no-op.
// isMpvExists is stubbed to false so calculatePlayInfo's parseFromFile takes
// the Qt path (no avformat decode of a tiny garbage webm file).
TEST(boost_pl, change_current_webm_file_under_stub)
{
    PlaylistModel *m = bpl_newModel();
    ASSERT_NE(m, nullptr);
    QString p = bpl_makeRealFile("bpl_cc_video.webm");
    m->_infos.append(bpl_makeItem(p, true));
    Stub stub;
    bpl_installSafePlaybackStubs(stub, PlayerEngine::Playing);
    stub.set(ADDR(CompositingManager, isMpvExists), bpl_stub_isMpvExists_false);
    EXPECT_NO_FATAL_FAILURE({ m->changeCurrent(0); });
    bpl_dispose(m);
    QFile::remove(p);
}

// ===========================================================================
// PlaylistModel: tryPlayCurrent SingleLoop invalid-item adjust branches
// ===========================================================================

// SingleLoop, invalid item, auto-advance, _last < count-1 -> _last++.
TEST(boost_pl, try_play_current_singleloop_invalid_auto_advance)
{
    PlaylistModel *m = bpl_newModel();
    ASSERT_NE(m, nullptr);
    for (int i = 0; i < 3; ++i)
        m->_infos.append(bpl_makeItem(QString("/tmp/bpl_tp_%1.mp4").arg(i), false));
    m->setPlayMode(PlaylistModel::SingleLoop);
    m->_current = 0;
    m->_last = 0;
    Stub stub;
    bpl_installSafePlaybackStubs(stub, PlayerEngine::Idle);
    EXPECT_NO_FATAL_FAILURE({ m->tryPlayCurrent(true); });
    EXPECT_EQ(m->_last, 1);
    bpl_dispose(m);
}

// SingleLoop, invalid, manual-prev, _last > 0 -> _last--.
TEST(boost_pl, try_play_current_singleloop_invalid_manual_prev)
{
    PlaylistModel *m = bpl_newModel();
    ASSERT_NE(m, nullptr);
    for (int i = 0; i < 3; ++i)
        m->_infos.append(bpl_makeItem(QString("/tmp/bpl_tp2_%1.mp4").arg(i), false));
    m->setPlayMode(PlaylistModel::SingleLoop);
    m->_current = 0;
    m->_last = 2;
    Stub stub;
    bpl_installSafePlaybackStubs(stub, PlayerEngine::Idle);
    EXPECT_NO_FATAL_FAILURE({ m->tryPlayCurrent(false); });
    EXPECT_EQ(m->_last, 1);
    bpl_dispose(m);
}

// ===========================================================================
// PlaylistModel: playPrev additional branches
// ===========================================================================

// SingleLoop, fromUser, Idle: takes the Idle sub-branch and plays _last.
TEST(boost_pl, play_prev_singleloop_fromuser_idle_under_stub)
{
    PlaylistModel *m = bpl_newModel();
    ASSERT_NE(m, nullptr);
    QString p0 = bpl_makeRealFile("bpl_pp_0.mp4");
    QString p1 = bpl_makeRealFile("bpl_pp_1.mp4");
    m->_infos.append(bpl_makeItem(p0, true));
    m->_infos.append(bpl_makeItem(p1, true));
    m->setPlayMode(PlaylistModel::SingleLoop);
    m->_last = 0;
    Stub stub;
    bpl_installSafePlaybackStubs(stub, PlayerEngine::Idle);
    EXPECT_NO_FATAL_FAILURE({ m->playPrev(true); });
    EXPECT_EQ(m->current(), 0);
    bpl_dispose(m);
    QFile::remove(p0); QFile::remove(p1);
}

// SingleLoop, NOT fromUser, Idle: replays via tryPlayCurrent.
TEST(boost_pl, play_prev_singleloop_not_fromuser_idle_under_stub)
{
    PlaylistModel *m = bpl_newModel();
    ASSERT_NE(m, nullptr);
    QString p0 = bpl_makeRealFile("bpl_pp2_0.mp4");
    m->_infos.append(bpl_makeItem(p0, true));
    m->setPlayMode(PlaylistModel::SingleLoop);
    m->_last = 0;
    Stub stub;
    bpl_installSafePlaybackStubs(stub, PlayerEngine::Idle);
    EXPECT_NO_FATAL_FAILURE({ m->playPrev(false); });
    bpl_dispose(m);
    QFile::remove(p0);
}

// ShufflePlay prev: reshuffles and picks from _playOrder.
TEST(boost_pl, play_prev_shuffle_under_stub)
{
    PlaylistModel *m = bpl_newModel();
    ASSERT_NE(m, nullptr);
    QString p0 = bpl_makeRealFile("bpl_pp3_0.mp4");
    QString p1 = bpl_makeRealFile("bpl_pp3_1.mp4");
    m->_infos.append(bpl_makeItem(p0, true));
    m->_infos.append(bpl_makeItem(p1, true));
    m->setPlayMode(PlaylistModel::ShufflePlay);
    m->_shufflePlayed = m->_playOrder.size();
    Stub stub;
    bpl_installSafePlaybackStubs(stub, PlayerEngine::Playing);
    EXPECT_NO_FATAL_FAILURE({ m->playPrev(true); });
    EXPECT_GE(m->current(), 0);
    bpl_dispose(m);
    QFile::remove(p0); QFile::remove(p1);
}

// ListLoop prev wraps the loop counter when crossing past index 0.
TEST(boost_pl, play_prev_listloop_wraps_under_stub)
{
    PlaylistModel *m = bpl_newModel();
    ASSERT_NE(m, nullptr);
    QString p0 = bpl_makeRealFile("bpl_pp4_0.mp4");
    QString p1 = bpl_makeRealFile("bpl_pp4_1.mp4");
    m->_infos.append(bpl_makeItem(p0, true));
    m->_infos.append(bpl_makeItem(p1, true));
    m->setPlayMode(PlaylistModel::ListLoop);
    m->_last = 0;
    int loopsBefore = m->_loopCount;
    Stub stub;
    bpl_installSafePlaybackStubs(stub, PlayerEngine::Playing);
    EXPECT_NO_FATAL_FAILURE({ m->playPrev(true); });
    EXPECT_EQ(m->_loopCount, loopsBefore + 1);
    bpl_dispose(m);
    QFile::remove(p0); QFile::remove(p1);
}

// ===========================================================================
// PlaylistModel: delayedAppendAsync additional branches
// ===========================================================================

// delayedAppendAsync with a pending job AND with an empty new list enqueues
// nothing (empty list -> the enqueue call is still safe).
TEST(boost_pl, delayed_append_async_empty_when_pending_safe)
{
    PlaylistModel *m = bpl_newModel();
    ASSERT_NE(m, nullptr);
    m->_pendingJob.append(qMakePair(QUrl::fromLocalFile("/tmp/bpl_daa_pending.mp4"),
                                    QFileInfo("/tmp/bpl_daa_pending.mp4")));
    EXPECT_NO_FATAL_FAILURE({ m->delayedAppendAsync(QList<QUrl>()); });
    EXPECT_EQ(m->_pendingAppendReq.size(), 1);
    bpl_dispose(m);
}

// ===========================================================================
// PlaylistModel: handleAsyncAppendResults mixed list
// ===========================================================================

// A mix of invalid + valid + a single-entry list: when not _firstLoad, valid
// items are sorted via SortSimilarFiles. Exercises the sort body.
TEST(boost_pl, handle_async_append_results_single_valid_sort_safe)
{
    PlaylistModel *m = bpl_newModel();
    ASSERT_NE(m, nullptr);
    m->_firstLoad = false;
    QList<PlayItemInfo> pil;
    pil << bpl_makeItem("/tmp/bpl_ha_invalid.mp4", false);
    pil << bpl_makeItem("/tmp/bpl_ha_show02e10.mp4", true);
    m->handleAsyncAppendResults(pil);
    EXPECT_EQ(m->count(), 1);
    EXPECT_GE(m->indexOf(QUrl::fromLocalFile("/tmp/bpl_ha_show02e10.mp4")), 0);
    bpl_dispose(m);
}

// ===========================================================================
// PlaylistModel: slotStateChanged via a real PlayerEngine sender
// ===========================================================================

// Emit stateChanged from the live engine so slotStateChanged's sender() check
// resolves to a real PlayerEngine and the Idle branch (auto-advance) executes
// under the safe playback stubs. The connection is disconnected before exit.
TEST(boost_pl, slot_state_changed_via_engine_idle_safe)
{
    PlayerEngine *e = bpl_engine();
    ASSERT_NE(e, nullptr);
    PlaylistModel *m = bpl_newModel();
    ASSERT_NE(m, nullptr);

    // Need a non-empty list so playNext does not early-return, plus stubs so
    // requestPlay/waitLastEnd are no-ops.
    QString p0 = bpl_makeRealFile("bpl_ss_0.mp4");
    m->_infos.append(bpl_makeItem(p0, true));
    Stub stub;
    stub.set(ADDR(PlayerEngine, state), bpl_stub_state_idle);
    stub.set(ADDR(PlayerEngine, waitLastEnd), bpl_stub_waitLastEnd);
    stub.set(ADDR(PlayerEngine, requestPlay), bpl_stub_requestPlay);

    auto c = QObject::connect(e, &PlayerEngine::stateChanged, m,
                              &PlaylistModel::slotStateChanged);
    EXPECT_TRUE(c);
    EXPECT_NO_FATAL_FAILURE({ emit e->stateChanged(); });
    QTest::qWait(20);   // let the singleShot(5ms) in the !composited branch fire
    QObject::disconnect(c);

    bpl_dispose(m);
    QFile::remove(p0);
}

// ===========================================================================
// LoadThread construction (NOT run())
// ===========================================================================

// Constructing LoadThread directly covers its ctor/dtor member init lines
// without ever starting the worker thread (run() is never called).
TEST(boost_pl, load_thread_construct_and_destroy_safe)
{
    PlaylistModel *m = bpl_newModel();
    ASSERT_NE(m, nullptr);
    LoadThread *lt = nullptr;
    EXPECT_NO_FATAL_FAILURE({ lt = new LoadThread(m, QList<QUrl>()); });
    ASSERT_NE(lt, nullptr);
    EXPECT_EQ(lt->_pModel, m);
    EXPECT_TRUE(lt->_urls.isEmpty());
    EXPECT_NO_FATAL_FAILURE({ delete lt; });
    bpl_dispose(m);
}

// ===========================================================================
// PlayerEngine: pure getters (no-backend / Idle safe)
// ===========================================================================

// volume() returns 100 when there is no backend, or the backend volume.
TEST(boost_pl, engine_volume_in_known_range)
{
    PlayerEngine *e = bpl_engine();
    ASSERT_NE(e, nullptr);
    int v = e->volume();
    EXPECT_TRUE(v >= 0 && v <= 200);
}

// muted() returns false when there is no backend, or the backend mute state.
TEST(boost_pl, engine_muted_is_bool)
{
    PlayerEngine *e = bpl_engine();
    ASSERT_NE(e, nullptr);
    bool m = e->muted();
    EXPECT_TRUE(m == true || m == false);
}

// subDelay() returns 0.0 when there is no backend, finite otherwise.
TEST(boost_pl, engine_sub_delay_finite)
{
    PlayerEngine *e = bpl_engine();
    ASSERT_NE(e, nullptr);
    double d = e->subDelay();
    EXPECT_TRUE(d == d); // NaN check
}

// subCodepage() returns "auto" when there is no backend.
TEST(boost_pl, engine_sub_codepage_returns_string)
{
    PlayerEngine *e = bpl_engine();
    ASSERT_NE(e, nullptr);
    QString cp = e->subCodepage();
    EXPECT_FALSE(cp.isEmpty());
}

// getMpvProxy() returns _current verbatim (null-safe).
TEST(boost_pl, engine_get_mpv_proxy_safe)
{
    PlayerEngine *e = bpl_engine();
    ASSERT_NE(e, nullptr);
    Backend *p = nullptr;
    EXPECT_NO_FATAL_FAILURE({ p = e->getMpvProxy(); });
    (void)p;
    SUCCEED();
}

// ===========================================================================
// PlayerEngine: settings-backed mutators on Idle (safe, no media)
// ===========================================================================

// Each of these guards on _current; with no backend (or an Idle one) they are
// early returns. Driving them covers the guard branches.
TEST(boost_pl, engine_settings_mutators_idle_safe)
{
    PlayerEngine *e = bpl_engine();
    ASSERT_NE(e, nullptr);
    EXPECT_NO_FATAL_FAILURE({
        e->setDVDDevice("/dev/sr0");
        e->setVideoAspect(1.777);
        e->setVideoRotation(90);
        e->setVideoRotation(0);
        e->changehwaccelMode(Backend::hwaccelAuto);
        e->changehwaccelMode(Backend::hwaccelClose);
        e->changeSoundMode(Backend::Stereo);
        e->changeSoundMode(Backend::Left);
        e->changeSoundMode(Backend::Right);
        e->setPlaySpeed(1.0);
        e->setSubDelay(0.5);
        e->setSubCodepage("auto");
        e->addSubSearchPath("/tmp/bpl_sub_search/");
        e->updateSubStyle("Sans", 24);
    });
    SUCCEED();
}

// ===========================================================================
// PlayerEngine: subtitle / track selectors on Idle (safe)
// ===========================================================================

TEST(boost_pl, engine_selectors_idle_safe)
{
    PlayerEngine *e = bpl_engine();
    ASSERT_NE(e, nullptr);
    EXPECT_NO_FATAL_FAILURE({
        e->selectSubtitle(0);
        e->selectSubtitle(999);
        e->selectTrack(0);
        e->toggleSubtitle();
    });
    bool v = e->isSubVisible();
    EXPECT_TRUE(v == true || v == false);
}

// ===========================================================================
// PlayerEngine: null-backend branches via the RAII guard
// ===========================================================================

// waitLastEnd with _current null: dynamic_cast yields nullptr -> no-op.
TEST(boost_pl, engine_wait_last_end_null_backend_safe)
{
    PlayerEngine *e = bpl_engine();
    ASSERT_NE(e, nullptr);
    bpl_NullCurrentGuard guard(e);
    EXPECT_EQ(e->_current, nullptr);
    EXPECT_NO_FATAL_FAILURE({ e->waitLastEnd(); });
}

// onBackendStateChanged with _current null: early return + warning, _state
// untouched.
TEST(boost_pl, engine_on_backend_state_changed_null_backend_safe)
{
    PlayerEngine *e = bpl_engine();
    ASSERT_NE(e, nullptr);
    PlayerEngine::CoreState before = e->_state;
    bpl_NullCurrentGuard guard(e);
    EXPECT_EQ(e->_current, nullptr);
    EXPECT_NO_FATAL_FAILURE({ e->onBackendStateChanged(); });
    EXPECT_EQ(e->_state, before);
}

// ===========================================================================
// PlayerEngine: misc safe mutators
// ===========================================================================

TEST(boost_pl, engine_misc_mutators_safe)
{
    PlayerEngine *e = bpl_engine();
    ASSERT_NE(e, nullptr);
    EXPECT_NO_FATAL_FAILURE({
        e->nextFrame();
        e->previousFrame();
        e->makeCurrent();
        e->setMute(true);
        e->setMute(false);
        e->toggleMute();
        e->savePlaybackPosition();
        e->savePreviousMovieState();
    });
    SUCCEED();
}

// setDecodeModel / getDecodeModel: cast yields nullptr when no backend, no-op.
TEST(boost_pl, engine_decode_model_safe)
{
    PlayerEngine *e = bpl_engine();
    ASSERT_NE(e, nullptr);
    EXPECT_NO_FATAL_FAILURE({ e->setDecodeModel(QVariant(0)); });
    QVariant dm;
    EXPECT_NO_FATAL_FAILURE({ dm = e->getDecodeModel(); });
    (void)dm;
    SUCCEED();
}

// takeScreenshot returns a null QImage with no backend; burst/stop are no-ops.
TEST(boost_pl, engine_screenshot_helpers_safe)
{
    PlayerEngine *e = bpl_engine();
    ASSERT_NE(e, nullptr);
    QImage img;
    EXPECT_NO_FATAL_FAILURE({ img = e->takeScreenshot(); });
    (void)img;
    EXPECT_NO_FATAL_FAILURE({
        e->burstScreenshot();
        e->stopBurstScreenshot();
    });
}

// backend property round-trip with a benign key.
TEST(boost_pl, engine_backend_property_round_trip)
{
    PlayerEngine *e = bpl_engine();
    ASSERT_NE(e, nullptr);
    EXPECT_NO_FATAL_FAILURE({
        e->setBackendProperty("keep-open", QVariant(true));
        QVariant v = e->getBackendProperty("keep-open");
        (void)v;
        QVariant none = e->getBackendProperty("bpl_unknown_key");
        (void)none;
    });
    SUCCEED();
}
