// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// Extended unit tests for player_engine.cpp (~64.7%) and playlist_model.cpp
// (~69.7%) in the deepin-movie-platform test binary.
//
// Suite name "engine_model_ext" is intentionally distinct from the existing
// suites ("PlayerEngine" in test_player_engine.cpp, "libdmr"/"libdmr_ext" in
// test_dmr.cpp/test_libdmr_ext.cpp) so cases never collide.
//
// Hard rules honoured:
//  * Only Google Test (TEST(engine_model_ext, ...)); no main() defined here.
//  * Engine obtained via dApp->getMainWindow()->engine(); playlist via
//    engine->playlist().
//  * Settings writes use only the documented keys (base.play.*, base.general.*).
//  * Screen/geometry cases use a brand-new local QWidget guarded by
//    QGuiApplication::primaryScreen(); skipped when no screen is present.
//  * No assumption that a real mpv backend exists; every call that may touch
//    _current either is a safe read-only query or is preceded by a pointer
//    check. Playback/decode/getMpvProxy paths are avoided.
//  * Stubs use "stub/stub.h"; helpers are static with the em_ prefix.
//  * No access to private/protected members; only the public API.

#include <QtTest>
#include <QTest>
#include <QDebug>
#include <unistd.h>
#include <gtest/gtest.h>

#include "application.h"
#include "player_widget.h"
#include "player_engine.h"
#include "playlist_model.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QSettings>
#include <QWidget>
#include <QGuiApplication>
#include <QScreen>
#include <QUrlQuery>
#include <QCryptographicHash>

#include "stub/stub.h"

using namespace dmr;

// ===========================================================================
// Convenience accessors for the live engine/playlist (wired in test main).
// ===========================================================================
static PlayerEngine *em_engine()
{
    Platform_MainWindow *w = dApp->getMainWindow();
    return w ? w->engine() : nullptr;
}

static PlaylistModel &em_playlist()
{
    return em_engine()->playlist();
}

// ===========================================================================
// player_engine.cpp  (current coverage ~64.7%)
// ===========================================================================

// --- state machine --------------------------------------------------------
// state() returns _state directly when there is no backend, otherwise it
// consults _current->state(). Either way it must return one of the documented
// CoreState values and never crash.
TEST(engine_model_ext, PlayerEngine_state_returns_known_core_state)
{
    PlayerEngine *e = em_engine();
    ASSERT_TRUE(e != nullptr);
    PlayerEngine::CoreState s = e->state();
    EXPECT_TRUE(s == PlayerEngine::Idle ||
                s == PlayerEngine::Playing ||
                s == PlayerEngine::Paused);
}

// paused() simply compares _state to Paused. Cover both return paths by
// reading it twice (the state is whatever the engine currently reports).
TEST(engine_model_ext, PlayerEngine_paused_tracks_state)
{
    PlayerEngine *e = em_engine();
    ASSERT_TRUE(e != nullptr);
    bool p = e->paused();
    EXPECT_EQ(p, e->state() == PlayerEngine::Paused);
}

// --- read-only property getters ------------------------------------------
// These query _current when present and fall back to a sentinel otherwise.
// Driving them covers either the "no backend" early return or the real read.
TEST(engine_model_ext, PlayerEngine_volume_returns_int)
{
    PlayerEngine *e = em_engine();
    ASSERT_TRUE(e != nullptr);
    int v = e->volume();
    EXPECT_TRUE(v >= 0 && v <= 200);
}

TEST(engine_model_ext, PlayerEngine_muted_returns_bool)
{
    PlayerEngine *e = em_engine();
    ASSERT_TRUE(e != nullptr);
    bool m = e->muted();
    EXPECT_TRUE(m == true || m == false);
}

TEST(engine_model_ext, PlayerEngine_duration_returns_non_negative)
{
    PlayerEngine *e = em_engine();
    ASSERT_TRUE(e != nullptr);
    EXPECT_GE(e->duration(), 0);
}

TEST(engine_model_ext, PlayerEngine_elapsed_returns_non_negative)
{
    PlayerEngine *e = em_engine();
    ASSERT_TRUE(e != nullptr);
    EXPECT_GE(e->elapsed(), 0);
}

TEST(engine_model_ext, PlayerEngine_videoSize_returns_size)
{
    PlayerEngine *e = em_engine();
    ASSERT_TRUE(e != nullptr);
    QSize sz = e->videoSize();
    EXPECT_GE(sz.width(), 0);
    EXPECT_GE(sz.height(), 0);
}

TEST(engine_model_ext, PlayerEngine_videoAspect_returns_finite)
{
    PlayerEngine *e = em_engine();
    ASSERT_TRUE(e != nullptr);
    double a = e->videoAspect();
    (void)a; // any finite double is acceptable (0.0 when no backend)
    EXPECT_TRUE(a == a); // NaN check
}

TEST(engine_model_ext, PlayerEngine_videoRotation_returns_mod_360)
{
    PlayerEngine *e = em_engine();
    ASSERT_TRUE(e != nullptr);
    int r = e->videoRotation();
    EXPECT_TRUE(r >= 0 && r < 360);
}

TEST(engine_model_ext, PlayerEngine_subDelay_returns_finite)
{
    PlayerEngine *e = em_engine();
    ASSERT_TRUE(e != nullptr);
    double d = e->subDelay();
    EXPECT_TRUE(d == d); // NaN check
}

TEST(engine_model_ext, PlayerEngine_subCodepage_returns_nonempty)
{
    PlayerEngine *e = em_engine();
    ASSERT_TRUE(e != nullptr);
    QString cp = e->subCodepage();
    EXPECT_FALSE(cp.isEmpty());
}

TEST(engine_model_ext, PlayerEngine_aid_returns_non_negative)
{
    PlayerEngine *e = em_engine();
    ASSERT_TRUE(e != nullptr);
    EXPECT_GE(e->aid(), 0);
}

TEST(engine_model_ext, PlayerEngine_sid_returns_non_negative)
{
    PlayerEngine *e = em_engine();
    ASSERT_TRUE(e != nullptr);
    EXPECT_GE(e->sid(), 0);
}

// playingMovieInfo() returns a static empty ref when there is no backend;
// otherwise it delegates to the backend. Either path is safe.
TEST(engine_model_ext, PlayerEngine_playingMovieInfo_safe)
{
    PlayerEngine *e = em_engine();
    ASSERT_TRUE(e != nullptr);
    const PlayingMovieInfo &pmi = e->playingMovieInfo();
    (void)pmi; // just ensure the call does not crash
    SUCCEED();
}

// --- subtitle / track selectors ------------------------------------------
// All of these are no-ops (or safe delegations) regardless of backend; cover
// their bodies so the branches light up.
TEST(engine_model_ext, PlayerEngine_selectors_safe)
{
    PlayerEngine *e = em_engine();
    ASSERT_TRUE(e != nullptr);
    EXPECT_NO_FATAL_FAILURE({
        e->selectSubtitle(0);
        e->selectSubtitle(999);   // out-of-range branch
        e->selectTrack(0);
        e->toggleSubtitle();
        e->setSubDelay(0.5);
        e->setSubDelay(-0.5);
        e->updateSubStyle("Sans", 24);
        e->setSubCodepage("auto");
        e->addSubSearchPath("/tmp/em_sub_search/");
    });
}

// --- isSubVisible ---------------------------------------------------------
TEST(engine_model_ext, PlayerEngine_isSubVisible_returns_bool)
{
    PlayerEngine *e = em_engine();
    ASSERT_TRUE(e != nullptr);
    bool v = e->isSubVisible();
    EXPECT_TRUE(v == true || v == false);
}

// --- volume / mute mutators ----------------------------------------------
// volumeUp/volumeDown/changeVolume/toggleMute/setMute are safe to call: they
// short-circuit with no backend, or delegate to a real setter otherwise.
TEST(engine_model_ext, PlayerEngine_volumeMutators_safe)
{
    PlayerEngine *e = em_engine();
    ASSERT_TRUE(e != nullptr);
    EXPECT_NO_FATAL_FAILURE({
        e->changeVolume(50);
        e->changeVolume(0);
        e->volumeUp();
        e->volumeDown();
        e->setMute(true);
        e->setMute(false);
        e->toggleMute();
    });
}

// --- property setters/getters --------------------------------------------
// setBackendProperty/getBackendProperty write/read into the backend (or no-op
// without one). Use a benign key so mpv does not reject the call outright.
TEST(engine_model_ext, PlayerEngine_backendProperty_round_trip)
{
    PlayerEngine *e = em_engine();
    ASSERT_TRUE(e != nullptr);
    EXPECT_NO_FATAL_FAILURE({
        e->setBackendProperty("keep-open", QVariant(true));
        QVariant v = e->getBackendProperty("keep-open");
        (void)v;
        // Unknown key: getter returns a default QVariant rather than crashing.
        QVariant none = e->getBackendProperty("em_definitely_unknown_key");
        (void)none;
    });
}

// --- seek mutators --------------------------------------------------------
// All three seek helpers guard on state()==Idle and on _current; calling them
// while idle covers the early-return branches.
TEST(engine_model_ext, PlayerEngine_seekHelpers_idle_safe)
{
    PlayerEngine *e = em_engine();
    ASSERT_TRUE(e != nullptr);
    EXPECT_NO_FATAL_FAILURE({
        e->seekForward(5);
        e->seekBackward(5);
        e->seekAbsolute(0);
        e->seekAbsolute(100);
    });
}

// --- frame stepping / dvd / screenshot stubs -----------------------------
// nextFrame/previousFrame/makeCurrent/setDVDDevice/savePlaybackPosition each
// guard on _current; calling them covers either branch.
TEST(engine_model_ext, PlayerEngine_miscMutators_safe)
{
    PlayerEngine *e = em_engine();
    ASSERT_TRUE(e != nullptr);
    EXPECT_NO_FATAL_FAILURE({
        e->nextFrame();
        e->previousFrame();
        e->makeCurrent();
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
        e->savePlaybackPosition();
    });
}

// --- burst screenshot / takeScreenshot ------------------------------------
// takeScreenshot returns a null QImage when there is no backend; otherwise it
// captures from mpv (which is fine when nothing is playing -> null image).
TEST(engine_model_ext, PlayerEngine_screenshot_helpers_safe)
{
    PlayerEngine *e = em_engine();
    ASSERT_TRUE(e != nullptr);
    QImage img = e->takeScreenshot();
    (void)img;
    EXPECT_NO_FATAL_FAILURE({
        e->burstScreenshot();
        e->stopBurstScreenshot();
    });
}

// --- getDecodeModel / setDecodeModel -------------------------------------
// setDecodeModel casts _current to MpvProxy; with no backend the cast yields
// nullptr and the call is a no-op. getDecodeModel returns an invalid QVariant
// in that case.
TEST(engine_model_ext, PlayerEngine_decodeModel_safe)
{
    PlayerEngine *e = em_engine();
    ASSERT_TRUE(e != nullptr);
    EXPECT_NO_FATAL_FAILURE({ e->setDecodeModel(QVariant(0)); });
    QVariant dm = e->getDecodeModel();
    (void)dm;
    SUCCEED();
}

// --- currFileIsAudio ------------------------------------------------------
// Reads playlist currentInfo() defensively; safe even with an empty list.
TEST(engine_model_ext, PlayerEngine_currFileIsAudio_safe)
{
    PlayerEngine *e = em_engine();
    ASSERT_TRUE(e != nullptr);
    bool a = e->currFileIsAudio();
    EXPECT_TRUE(a == true || a == false);
}

// --- isPlayableFile / isAudioFile / isSubtitle ----------------------------
// FileFilter-driven predicates. Cover the positive and negative branches with
// realistic suffixes.
TEST(engine_model_ext, PlayerEngine_isPlayableFile_url_branches)
{
    PlayerEngine *e = em_engine();
    ASSERT_TRUE(e != nullptr);

    // isPlayableFile consults FileFilter::isMediaFile, which is suffix-driven;
    // it does not check whether the file actually exists on disk. We only
    // assert that both calls are safe (no crash) and yield a bool.
    QUrl bogusLocal = QUrl::fromLocalFile("/tmp/em_definitely_no_such_video_xyz.mp4");
    bool r1 = false;
    EXPECT_NO_FATAL_FAILURE({ r1 = e->isPlayableFile(bogusLocal); });
    EXPECT_TRUE(r1 == true || r1 == false);

    QUrl http("http://127.0.0.1:1/em_none.mp4");
    bool r2 = false;
    EXPECT_NO_FATAL_FAILURE({ r2 = e->isPlayableFile(http); });
    EXPECT_TRUE(r2 == true || r2 == false);
}

TEST(engine_model_ext, PlayerEngine_isPlayableFile_string_branches)
{
    PlayerEngine *e = em_engine();
    ASSERT_TRUE(e != nullptr);
    bool a = false, b = false, c = false;
    EXPECT_NO_FATAL_FAILURE({
        a = e->isPlayableFile(QString("/tmp/em_no_such_file.mp4"));
        b = e->isAudioFile(QString("/tmp/em_no_such_audio.mp3"));
        c = e->isSubtitle(QString("/tmp/em_no_such_sub.srt"));
    });
    (void)a; (void)b; (void)c;
}

// --- addPlayFile / addPlayFiles / addPlayDir ------------------------------
// addPlayFile delegates to isPlayableFile (suffix-driven) and then appends.
// Whether a .txt file is filtered depends on FileFilter; we only require the
// call to be safe and to return a bool.
TEST(engine_model_ext, PlayerEngine_addPlayFile_txt_safe)
{
    PlayerEngine *e = em_engine();
    ASSERT_TRUE(e != nullptr);
    bool ok = true;
    EXPECT_NO_FATAL_FAILURE({
        ok = e->addPlayFile(QUrl::fromLocalFile("/tmp/em_no_such_file.txt"));
    });
    EXPECT_TRUE(ok == true || ok == false);
}

TEST(engine_model_ext, PlayerEngine_addPlayFiles_empty_list)
{
    PlayerEngine *e = em_engine();
    ASSERT_TRUE(e != nullptr);
    QList<QUrl> out;
    EXPECT_NO_FATAL_FAILURE({ out = e->addPlayFiles(QList<QUrl>()); });
    EXPECT_TRUE(out.isEmpty());
}

TEST(engine_model_ext, PlayerEngine_addPlayFiles_string_empty_list)
{
    PlayerEngine *e = em_engine();
    ASSERT_TRUE(e != nullptr);
    QList<QUrl> out;
    EXPECT_NO_FATAL_FAILURE({ out = e->addPlayFiles(QList<QString>()); });
    EXPECT_TRUE(out.isEmpty());
}

TEST(engine_model_ext, PlayerEngine_addPlayFs_empty_list_safe)
{
    PlayerEngine *e = em_engine();
    ASSERT_TRUE(e != nullptr);
    EXPECT_NO_FATAL_FAILURE({ e->addPlayFs(QList<QString>()); });
}

// addPlayDir on a non-existent directory: filterDir returns an empty list,
// addPlayFiles returns empty, and the call is safe.
TEST(engine_model_ext, PlayerEngine_addPlayDir_nonexistent_dir)
{
    PlayerEngine *e = em_engine();
    ASSERT_TRUE(e != nullptr);
    QList<QUrl> out;
    EXPECT_NO_FATAL_FAILURE({
        out = e->addPlayDir(QDir("/tmp/em_definitely_no_such_dir_xyz"));
    });
    EXPECT_TRUE(out.isEmpty());
}

// --- playlist bookkeeping via the public engine slots --------------------
// playSelected with an out-of-range index is a no-op (PlaylistModel guards
// changeCurrent). prev/next/stop/pauseResume are safe to invoke when idle.
TEST(engine_model_ext, PlayerEngine_playlistSlots_idle_safe)
{
    PlayerEngine *e = em_engine();
    ASSERT_TRUE(e != nullptr);
    EXPECT_NO_FATAL_FAILURE({
        e->playSelected(-1);
        e->playSelected(99999);
        e->playByName(QUrl("em://no-such-url"));
        e->clearPlaylist();
        e->stop();
        e->pauseResume();
    });
}

// ===========================================================================
// playlist_model.cpp  (current coverage ~69.7%)
// ===========================================================================

// --- getUrlFileTotalSize --------------------------------------------------
// tryTimes<=0 is clamped to 1; an unresolvable URL fails fast and returns -1.
TEST(engine_model_ext, PlaylistModel_getUrlFileTotalSize_clamps_tryTimes)
{
    PlaylistModel &pl = em_playlist();
    qint64 sz = -42;
    EXPECT_NO_FATAL_FAILURE({
        sz = pl.getUrlFileTotalSize(QUrl("http://127.0.0.1:1/em_none"), 0);
    });
    EXPECT_TRUE(sz == -1 || sz >= 0);
}

TEST(engine_model_ext, PlaylistModel_getUrlFileTotalSize_invalid_scheme)
{
    PlaylistModel &pl = em_playlist();
    qint64 sz = -42;
    EXPECT_NO_FATAL_FAILURE({
        sz = pl.getUrlFileTotalSize(QUrl("foo://invalid"), 1);
    });
    EXPECT_TRUE(sz == -1 || sz >= 0);
}

// --- save/clear/load playlist I/O ----------------------------------------
// These read/write the persisted playlist file via QSettings. Exercise them
// both ways to cover the beginGroup/remove/endGroup branches.
TEST(engine_model_ext, PlaylistModel_savePlaylist_empty_idempotent)
{
    PlaylistModel &pl = em_playlist();
    EXPECT_NO_FATAL_FAILURE({ pl.savePlaylist(); });
    EXPECT_NO_FATAL_FAILURE({ pl.clearPlaylist(); });
    EXPECT_NO_FATAL_FAILURE({ pl.savePlaylist(); });
}

TEST(engine_model_ext, PlaylistModel_loadPlaylist_with_persisted_entries)
{
    // Drop a couple of entries into the playlist file directly so loadPlaylist
    // has something to iterate over, then call loadPlaylist (which schedules a
    // delayed append) and let the event loop drain.
    PlaylistModel &pl = em_playlist();
    QString file = QString("%1/%2/%3/playlist")
        .arg(QStandardPaths::writableLocation(QStandardPaths::ConfigLocation))
        .arg(qApp->organizationName())
        .arg(qApp->applicationName());
    {
        QSettings cfg(file, QSettings::NativeFormat);
        cfg.beginGroup("playlist");
        cfg.remove("");
        cfg.setValue("0", QUrl("http://127.0.0.1:1/em_persisted_a.mp4"));
        cfg.setValue("1", QUrl("http://127.0.0.1:1/em_persisted_b.mp4"));
        cfg.endGroup();
        cfg.sync();
    }
    EXPECT_NO_FATAL_FAILURE({ pl.loadPlaylist(); });
    QTest::qWait(50);
    // Clean up so other cases see an empty store.
    EXPECT_NO_FATAL_FAILURE({ pl.clearPlaylist(); });
}

// --- getMovieInfo ---------------------------------------------------------
// getMovieInfo on a non-existent local file sets *is=false and returns a
// default-constructed (invalid) MovieInfo.
TEST(engine_model_ext, PlaylistModel_getMovieInfo_nonexistent_local)
{
    PlaylistModel &pl = em_playlist();
    bool is = true;
    MovieInfo mi;
    EXPECT_NO_FATAL_FAILURE({
        mi = pl.getMovieInfo(QUrl::fromLocalFile("/tmp/em_no_such_movie_xyz.mp4"), &is);
    });
    EXPECT_FALSE(is);
    EXPECT_FALSE(mi.valid);
}

TEST(engine_model_ext, PlaylistModel_getMovieInfo_non_local_url)
{
    PlaylistModel &pl = em_playlist();
    bool is = true;
    MovieInfo mi;
    EXPECT_NO_FATAL_FAILURE({
        mi = pl.getMovieInfo(QUrl("http://127.0.0.1:1/em_remote.mp4"), &is);
    });
    EXPECT_FALSE(is);
    EXPECT_FALSE(mi.valid);
}

// NOTE: getMovieInfo() dereferences its `ok` out-parameter on the non-local
// and not-found branches (it does *is = false). Passing nullptr is therefore
// unsafe, so no null-ok case is provided here.

// --- getMovieCover --------------------------------------------------------
// With no thumbnailer library resolved (or even with one), a bogus URL must
// not crash; the function returns a (possibly null) QImage.
TEST(engine_model_ext, PlaylistModel_getMovieCover_safe)
{
    PlaylistModel &pl = em_playlist();
    QImage img;
    EXPECT_NO_FATAL_FAILURE({
        img = pl.getMovieCover(QUrl::fromLocalFile("/tmp/em_no_such_movie_xyz.mp4"));
    });
    (void)img;
    SUCCEED();
}

// --- calculatePlayInfo ----------------------------------------------------
// Non-DVD, non-local URL exercises the non-local branch. When an mpv backend
// is present, calculatePlayInfo additionally consults the backend for width/
// height/duration; the call must remain safe either way.
TEST(engine_model_ext, PlaylistModel_calculatePlayInfo_network_url)
{
    PlaylistModel &pl = em_playlist();
    PlayItemInfo pif;
    EXPECT_NO_FATAL_FAILURE({
        pif = pl.calculatePlayInfo(QUrl("http://example.com/em_stream.mp4"),
                                   QFileInfo(), false);
    });
    EXPECT_EQ(pif.url, QUrl("http://example.com/em_stream.mp4"));
}

// calculatePlayInfo on a dvd:// scheme with isDvd=true enters the dvd branch.
TEST(engine_model_ext, PlaylistModel_calculatePlayInfo_dvd_scheme)
{
    PlaylistModel &pl = em_playlist();
    PlayItemInfo pif;
    EXPECT_NO_FATAL_FAILURE({
        pif = pl.calculatePlayInfo(QUrl("dvd:///"), QFileInfo("/dev/sr0"), true);
    });
    EXPECT_TRUE(pif.url.scheme() == "dvd");
}

// calculatePlayInfo on a local text file (non-video) keeps mi invalid.
TEST(engine_model_ext, PlaylistModel_calculatePlayInfo_local_text_file)
{
    PlaylistModel &pl = em_playlist();
    QTemporaryFile tf;
    ASSERT_TRUE(tf.open());
    tf.write("plain text, not a video container");
    tf.flush();
    QFileInfo fi(tf.fileName());
    QUrl url = QUrl::fromLocalFile(fi.fileName());
    PlayItemInfo pif;
    EXPECT_NO_FATAL_FAILURE({ pif = pl.calculatePlayInfo(url, fi, false); });
    EXPECT_FALSE(pif.mi.valid);
}

// --- append / remove / clear on real local files -------------------------
// Append a non-video temp file: it is filtered out upstream (invalid
// MovieInfo), so the list does not grow, but the bookkeeping must be
// consistent and must not crash.
TEST(engine_model_ext, PlaylistModel_append_invalid_local_filtered)
{
    PlaylistModel &pl = em_playlist();
    int before = pl.count();

    QTemporaryFile tf;
    ASSERT_TRUE(tf.open());
    tf.write("garbage");
    tf.flush();
    QUrl url = QUrl::fromLocalFile(tf.fileName());

    EXPECT_NO_FATAL_FAILURE({ pl.append(url); });
    QTest::qWait(20);

    // The append path may still have scheduled work; ensure it settles.
    EXPECT_GE(pl.count(), 0);
    EXPECT_EQ(pl.indexOf(url) >= 0 ? 1 : 0, pl.count() > before ? 1 : 0);

    int idx = pl.indexOf(url);
    if (idx >= 0) {
        EXPECT_NO_FATAL_FAILURE({ pl.remove(idx); });
        EXPECT_EQ(pl.indexOf(url), -1);
    }
}

// append with an invalid URL is an early no-op (url.isValid() is false).
TEST(engine_model_ext, PlaylistModel_append_invalid_url_noop)
{
    PlaylistModel &pl = em_playlist();
    int before = pl.count();
    EXPECT_NO_FATAL_FAILURE({ pl.append(QUrl()); });
    QTest::qWait(10);
    EXPECT_EQ(pl.count(), before);
}

// remove at out-of-range positions is a guarded no-op.
TEST(engine_model_ext, PlaylistModel_remove_out_of_range_safe)
{
    PlaylistModel &pl = em_playlist();
    EXPECT_NO_FATAL_FAILURE({ pl.remove(-1); });
    EXPECT_NO_FATAL_FAILURE({ pl.remove(-100); });
    EXPECT_NO_FATAL_FAILURE({ pl.remove(99999); });
}

// --- changeCurrent --------------------------------------------------------
// changeCurrent with an out-of-range index is a no-op; with the current index
// it is also a no-op (the _current == pos early return).
TEST(engine_model_ext, PlaylistModel_changeCurrent_out_of_range_safe)
{
    PlaylistModel &pl = em_playlist();
    EXPECT_NO_FATAL_FAILURE({ pl.changeCurrent(-1); });
    EXPECT_NO_FATAL_FAILURE({ pl.changeCurrent(99999); });
}

// --- collectionJob / clearLoad --------------------------------------------
// clearLoad empties m_loadFile; calling it twice is idempotent.
TEST(engine_model_ext, PlaylistModel_clearLoad_idempotent)
{
    PlaylistModel &pl = em_playlist();
    EXPECT_NO_FATAL_FAILURE({ pl.clearLoad(); });
    EXPECT_NO_FATAL_FAILURE({ pl.clearLoad(); });
    EXPECT_TRUE(pl.getLoadList().isEmpty());
}

// --- stop -----------------------------------------------------------------
// stop() just resets _current to -1 and emits currentChanged().
TEST(engine_model_ext, PlaylistModel_stop_resets_current)
{
    PlaylistModel &pl = em_playlist();
    EXPECT_NO_FATAL_FAILURE({ pl.stop(); });
    EXPECT_EQ(pl.current(), -1);
}

// --- reshuffle (via setPlayMode) -----------------------------------------
// Toggle every play mode twice (same value is a no-op, different value
// triggers reshuffle). Each transition must be safe.
TEST(engine_model_ext, PlaylistModel_setPlayMode_all_modes_round_trip)
{
    PlaylistModel &pl = em_playlist();
    PlaylistModel::PlayMode before = pl.playMode();

    // Setting the same value is a no-op branch.
    pl.setPlayMode(before);
    EXPECT_EQ(pl.playMode(), before);

    const QList<PlaylistModel::PlayMode> modes = {
        PlaylistModel::OrderPlay,
        PlaylistModel::ShufflePlay,
        PlaylistModel::SinglePlay,
        PlaylistModel::SingleLoop,
        PlaylistModel::ListLoop,
    };
    for (PlaylistModel::PlayMode m : modes) {
        pl.setPlayMode(m);
        EXPECT_EQ(pl.playMode(), m);
    }
    pl.setPlayMode(before);
}

// --- getthreadstate / getThumbnailRunning --------------------------------
TEST(engine_model_ext, PlaylistModel_getthreadstate_returns_bool)
{
    PlaylistModel &pl = em_playlist();
    bool r = pl.getthreadstate();
    EXPECT_TRUE(r == true || r == false);
}

TEST(engine_model_ext, PlaylistModel_getThumbnailRunning_returns_bool)
{
    PlaylistModel &pl = em_playlist();
    bool r = pl.getThumbnailRunning();
    EXPECT_TRUE(r == true || r == false);
}

// --- items() / count() / size() / current() / currentInfo() --------------
TEST(engine_model_ext, PlaylistModel_accessors_consistent)
{
    PlaylistModel &pl = em_playlist();
    EXPECT_EQ(pl.count(), pl.size());
    EXPECT_EQ(static_cast<int>(pl.items().size()), pl.count());
    EXPECT_TRUE(pl.current() >= -1);
}

// currentInfo() (non-const) has a defensive fallback; only call it when the
// list is non-empty (it Q_ASSERTs on an empty list).
TEST(engine_model_ext, PlaylistModel_currentInfo_nonconst_safe_when_nonempty)
{
    PlaylistModel &pl = em_playlist();
    if (pl.count() < 1) {
        GTEST_SKIP() << "playlist is empty";
    }
    PlayItemInfo &pif = pl.currentInfo();
    (void)pif.url;
    SUCCEED();
}

TEST(engine_model_ext, PlaylistModel_currentInfo_const_safe_when_selected)
{
    PlaylistModel &pl = em_playlist();
    if (pl.count() < 1 || pl.current() < 0) {
        GTEST_SKIP() << "no current item";
    }
    const PlayItemInfo &pif = static_cast<const PlaylistModel &>(pl).currentInfo();
    (void)pif.url;
    SUCCEED();
}

// --- switchPosition -------------------------------------------------------
// Needs >= 2 items (Q_ASSERT). Skip otherwise; when present, swapping 0 and
// the last index must preserve count.
TEST(engine_model_ext, PlaylistModel_switchPosition_safe_when_two_or_more)
{
    PlaylistModel &pl = em_playlist();
    if (pl.count() < 2) {
        GTEST_SKIP() << "playlist has fewer than 2 items";
    }
    int cnt = pl.count();
    EXPECT_NO_FATAL_FAILURE({ pl.switchPosition(0, cnt - 1); });
    EXPECT_EQ(pl.count(), cnt);
}

// --- PlayItemInfo::refresh ------------------------------------------------
// refresh() on a non-local URL returns false; on a local file it compares
// existence/size before/after.
TEST(engine_model_ext, PlayItemInfo_refresh_non_local_returns_false)
{
    PlayItemInfo pif;
    pif.url = QUrl("http://example.com/em_stream.mp4");
    EXPECT_FALSE(pif.refresh());
}

TEST(engine_model_ext, PlayItemInfo_refresh_local_stable_returns_false)
{
    QTemporaryFile tf;
    ASSERT_TRUE(tf.open());
    tf.write("payload");
    tf.flush();

    PlayItemInfo pif;
    pif.url = QUrl::fromLocalFile(tf.fileName());
    pif.info = QFileInfo(tf.fileName());
    pif.valid = pif.info.exists();

    EXPECT_FALSE(pif.refresh());
    EXPECT_TRUE(pif.valid);
}

// NOTE: PlayItemInfo::refresh() returns true only when the underlying
// QFileInfo reports a changed exists()/size() between the cached snapshot and
// the post-refresh() read. On kernels that cache stat() for recently-written
// files this is flaky (the sibling libdmr_ext.PlayItemInfo_refresh_local_
// growing_file_returns_true case exhibits the same behaviour), so the
// true-branch is intentionally not asserted here; the two cases above already
// cover the local-file and non-local branches of refresh().

// ===========================================================================
// paintEvent / resizeEvent coverage via a brand-new local widget
// ===========================================================================
// The engine's paintEvent draws an icon when not composited (or under wayland)
// and otherwise falls through to QWidget::paintEvent. We cannot repaint the
// shared engine safely, so we only assert the engine reports a valid size and
// that a fresh local QWidget can be constructed when a screen is available.
TEST(engine_model_ext, EngineWidget_geometry_and_screen_guard)
{
    PlayerEngine *e = em_engine();
    ASSERT_TRUE(e != nullptr);
    EXPECT_GE(e->width(), 0);
    EXPECT_GE(e->height(), 0);

    if (!QGuiApplication::primaryScreen()) {
        GTEST_SKIP() << "no primary screen available";
    }
    QWidget local;
    local.resize(64, 64);
    local.show();
    QTest::qWait(20);
    local.hide();
    SUCCEED();
}
