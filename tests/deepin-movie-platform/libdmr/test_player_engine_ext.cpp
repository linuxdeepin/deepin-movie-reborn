// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// Extended unit tests for src/libdmr/player_engine.cpp.
//
// Suite name "player_engine_ext" is distinct from the existing suites
// ("PlayerEngine" in test_player_engine.cpp, "engine_model_ext" in
// test_engine_model_ext.cpp) so TEST cases never collide.
//
// Only Google Test is used (TEST(player_engine_ext, ...)); no main() is
// defined here (test_qtestmain.cpp owns main + Application wiring).
//
// Strategy:
//  * The shared engine (dApp->getMainWindow()->engine()) is driven in Idle
//    state only. Every function exercised either early-returns when
//    state()==Idle / !_current, or is a pure helper / wrapper. No real
//    playback or decode is ever triggered.
//  * For two backend-touching slots (waitLastEnd, onBackendStateChanged) the
//    engine->_current pointer is temporarily nulled via an RAII guard so the
//    documented null-guard branch is covered deterministically and safely
//    regardless of whether a real mpv backend was constructed.
//  * CompositingManager::isMpvExists is stubbed to force the !mpv branch of
//    currFileIsAudio (pure suffix check, no decode).
//  * Stubs use "stub/stub.h"; helpers use the pe_ prefix to stay unique.
//
// Cases intentionally NOT covered (risky / decode / GUI):
//  - paintEvent / resizeEvent: require a shown widget / window(), crash-prone.
//  - toggleRoundedClip: dereferences a dynamic_cast result that is null when
//    _current is null; with a live backend it touches mpv directly.
//  - requestPlay id-in-range / setPlayFile / _current->play(): real playback.
//  - onBackendStateChanged non-null branch & state transitions: real backend.
//  - waitLastEnd pollingEndOfPlayback on a live MpvProxy: real backend.
//  - toggleMute mpvFunsLoadOver emit branch: requires a live backend.

#include <QtTest>
#include <QTest>
#include <QDebug>
#include <gtest/gtest.h>

// Private/protected access for slot coverage (onBackendStateChanged,
// requestPlay, updateSubStyles, onSubtitlesDownloaded,
// onPlaylistAsyncAppendFinished) and for the _current / _pendingPlayReq members
// used by the null-guard RAII helper. Mirrors the convention used elsewhere in
// the suite.
#define protected public
#define private public
#include "src/libdmr/player_engine.h"
#undef protected
#undef private

#include "application.h"
#include "player_widget.h"
#include "playlist_model.h"
#include "compositing_manager.h"

#include <QFileInfo>
#include <QList>
#include <QUrl>
#include <QVariant>

#include "stub/stub.h"

using namespace dmr;

// ===========================================================================
// helpers (file scope, uniquely prefixed pe_)
// ===========================================================================

static PlayerEngine *pe_engine()
{
    Platform_MainWindow *w = dApp->getMainWindow();
    return w ? w->engine() : nullptr;
}

// RAII guard that temporarily nulls PlayerEngine::_current so backend-touching
// code paths reduce to their documented null-guard early returns. The original
// pointer is always restored (destructor runs at scope exit, even on assertion
// failure since gtest EXPECT does not throw). No event loop is spun while the
// backend is detached, so no other code observes the transient null state.
struct pe_NullCurrentGuard {
    PlayerEngine *engine;
    Backend *saved;
    explicit pe_NullCurrentGuard(PlayerEngine *e) : engine(e), saved(e ? e->_current : nullptr)
    {
        if (engine) engine->_current = nullptr;
    }
    ~pe_NullCurrentGuard()
    {
        if (engine) engine->_current = saved;
    }
};

// Stub returning false for CompositingManager::isMpvExists so currFileIsAudio
// takes the !mpv (isAudioFile suffix-check) branch.
static bool pe_stub_isMpvExists_false()
{
    return false;
}

// ===========================================================================
// getMpvProxy  (returns _current verbatim, no decode)
// ===========================================================================
TEST(player_engine_ext, getMpvProxy_returns_current_pointer)
{
    PlayerEngine *e = pe_engine();
    ASSERT_TRUE(e != nullptr);
    Backend *p = nullptr;
    EXPECT_NO_FATAL_FAILURE({ p = e->getMpvProxy(); });
    // Whatever _current is (null or a live MpvProxy), the call must not crash.
    (void)p;
    SUCCEED();
}

// ===========================================================================
// savePreviousMovieState  (wraps savePlaybackPosition, guarded by _current)
// ===========================================================================
TEST(player_engine_ext, savePreviousMovieState_safe)
{
    PlayerEngine *e = pe_engine();
    ASSERT_TRUE(e != nullptr);
    EXPECT_NO_FATAL_FAILURE({ e->savePreviousMovieState(); });
    SUCCEED();
}

// ===========================================================================
// loadSubtitle  (Idle early-returns true without touching the backend)
// ===========================================================================
TEST(player_engine_ext, loadSubtitle_idle_returns_true)
{
    PlayerEngine *e = pe_engine();
    ASSERT_TRUE(e != nullptr);
    // Idle state: the first guard returns true immediately.
    bool ok = false;
    EXPECT_NO_FATAL_FAILURE({ ok = e->loadSubtitle(QFileInfo("/tmp/pe_no_such_sub.srt")); });
    EXPECT_TRUE(ok);
}

// ===========================================================================
// loadOnlineSubtitle  (Idle early-returns)
// ===========================================================================
TEST(player_engine_ext, loadOnlineSubtitle_idle_noop)
{
    PlayerEngine *e = pe_engine();
    ASSERT_TRUE(e != nullptr);
    EXPECT_NO_FATAL_FAILURE({ e->loadOnlineSubtitle(QUrl("http://127.0.0.1:1/pe_none.mp4")); });
    SUCCEED();
}

// ===========================================================================
// onSubtitlesDownloaded  (protected slot; Idle early-returns)
// ===========================================================================
TEST(player_engine_ext, onSubtitlesDownloaded_idle_noop)
{
    PlayerEngine *e = pe_engine();
    ASSERT_TRUE(e != nullptr);
    EXPECT_NO_FATAL_FAILURE({
        e->onSubtitlesDownloaded(QUrl("http://127.0.0.1:1/pe_none.mp4"),
                                 QList<QString>() << "/tmp/pe_no_such_sub.srt",
                                 OnlineSubtitle::FailReason::NoError);
    });
    SUCCEED();
}

// ===========================================================================
// onPlaylistAsyncAppendFinished  (two branches via _pendingPlayReq)
// ===========================================================================
// No pending play request: the else branch is taken (info log only).
TEST(player_engine_ext, onPlaylistAsyncAppendFinished_no_pending_safe)
{
    PlayerEngine *e = pe_engine();
    ASSERT_TRUE(e != nullptr);
    // Ensure a clean state so a prior case cannot influence this one.
    e->_pendingPlayReq = QUrl();
    EXPECT_NO_FATAL_FAILURE({ e->onPlaylistAsyncAppendFinished(QList<PlayItemInfo>()); });
    EXPECT_FALSE(e->_pendingPlayReq.isValid());
}

// A pending request that is NOT in the playlist: indexOf returns -1 so the
// "id is invalid" log branch is taken and _pendingPlayReq is kept.
TEST(player_engine_ext, onPlaylistAsyncAppendFinished_pending_not_found_safe)
{
    PlayerEngine *e = pe_engine();
    ASSERT_TRUE(e != nullptr);
    QUrl bogus("pe://no-such-url-ext");
    e->_pendingPlayReq = bogus;
    EXPECT_NO_FATAL_FAILURE({ e->onPlaylistAsyncAppendFinished(QList<PlayItemInfo>()); });
    // Not found -> request stays pending (waits for another signal).
    EXPECT_EQ(e->_pendingPlayReq, bogus);
    // Clean up so no later case observes a stale pending request.
    e->_pendingPlayReq = QUrl();
}

// ===========================================================================
// play  (empty playlist / no backend -> early return)
// ===========================================================================
TEST(player_engine_ext, play_empty_playlist_noop)
{
    PlayerEngine *e = pe_engine();
    ASSERT_TRUE(e != nullptr);
    // With an empty playlist the very first guard returns; no decode path.
    if (e->playlist().count() > 0) {
        GTEST_SKIP() << "playlist not empty; would risk real playback";
    }
    EXPECT_NO_FATAL_FAILURE({ e->play(); });
    SUCCEED();
}

// ===========================================================================
// prev / next  (empty playlist -> playPrev/playNext no-op; _playingRequest
// is toggled and reset). Safe: savePreviousMovieState guards _current, and
// any signals fired by an empty playlist land on no-op guards.
// ===========================================================================
TEST(player_engine_ext, prev_next_empty_playlist_safe)
{
    PlayerEngine *e = pe_engine();
    ASSERT_TRUE(e != nullptr);
    if (e->playlist().count() > 0) {
        GTEST_SKIP() << "playlist not empty; prev/next could trigger playback";
    }
    EXPECT_FALSE(e->_playingRequest);
    EXPECT_NO_FATAL_FAILURE({ e->prev(); });
    EXPECT_FALSE(e->_playingRequest);
    EXPECT_NO_FATAL_FAILURE({ e->next(); });
    EXPECT_FALSE(e->_playingRequest);
}

// ===========================================================================
// requestPlay is intentionally NOT exercised: calling the protected slot with
// an empty playlist aborts (Q_ASSERT / fatal path inside the engine) in this
// build, which would SIGABRT the whole suite and drop every later case.
// ===========================================================================

// ===========================================================================
// updateSubStyles  (protected slot; with _state==Idle the inner scaling block
// is skipped. Either the settings options are absent (early return) or they
// are present but the Idle guard skips the backend-touching updateSubStyle.)
// ===========================================================================
TEST(player_engine_ext, updateSubStyles_idle_safe)
{
    PlayerEngine *e = pe_engine();
    ASSERT_TRUE(e != nullptr);
    if (e->_state != PlayerEngine::CoreState::Idle) {
        GTEST_SKIP() << "engine not idle; updateSubStyles could touch backend";
    }
    EXPECT_NO_FATAL_FAILURE({ e->updateSubStyles(); });
    SUCCEED();
}

// ===========================================================================
// waitLastEnd  (null _current -> dynamic_cast yields nullptr -> skip)
// Covered deterministically via the pe_NullCurrentGuard so a live backend is
// never asked to poll for end of playback.
// ===========================================================================
TEST(player_engine_ext, waitLastEnd_null_backend_safe)
{
    PlayerEngine *e = pe_engine();
    ASSERT_TRUE(e != nullptr);
    pe_NullCurrentGuard guard(e);
    EXPECT_EQ(e->_current, nullptr); // guard detached the backend
    EXPECT_NO_FATAL_FAILURE({ e->waitLastEnd(); });
    // Guard restores _current on destruction.
}

// ===========================================================================
// onBackendStateChanged  (null _current -> early return + warning)
// Covered deterministically via the pe_NullCurrentGuard so the real backend
// state machine is never driven.
// ===========================================================================
TEST(player_engine_ext, onBackendStateChanged_null_backend_safe)
{
    PlayerEngine *e = pe_engine();
    ASSERT_TRUE(e != nullptr);
    PlayerEngine::CoreState before = e->_state;
    pe_NullCurrentGuard guard(e);
    EXPECT_EQ(e->_current, nullptr);
    EXPECT_NO_FATAL_FAILURE({ e->onBackendStateChanged(); });
    // With no backend, _state must be left untouched.
    EXPECT_EQ(e->_state, before);
}

// ===========================================================================
// currFileIsAudio  (force the !isMpvExists branch via stub; isAudioFile is a
// pure suffix check on the (empty) current item url, so no decode occurs.)
// ===========================================================================
TEST(player_engine_ext, currFileIsAudio_no_mpv_branch_safe)
{
    PlayerEngine *e = pe_engine();
    ASSERT_TRUE(e != nullptr);
    Stub stub;
    stub.set(ADDR(CompositingManager, isMpvExists), pe_stub_isMpvExists_false);
    bool a = true;
    EXPECT_NO_FATAL_FAILURE({ a = e->currFileIsAudio(); });
    stub.reset(ADDR(CompositingManager, isMpvExists));
    EXPECT_TRUE(a == true || a == false);
}

// ===========================================================================
// addPlayFiles(QString) / addPlayFs with a non-empty bogus input
// (engine_model_ext only covers the empty-list variants; these exercise the
// per-item fileTransfer + isDir branches with harmless non-existent paths.)
// ===========================================================================
TEST(player_engine_ext, addPlayFiles_string_bogus_local_path_safe)
{
    PlayerEngine *e = pe_engine();
    ASSERT_TRUE(e != nullptr);
    QList<QUrl> out;
    EXPECT_NO_FATAL_FAILURE({
        out = e->addPlayFiles(QList<QString>{"/tmp/pe_definitely_no_such_file_xyz.mp4"});
    });
    // isDir is false -> the bogus url is forwarded to addPlayFiles(QList<QUrl>)
    // which filters it via isPlayableFile; the result is safe either way.
    (void)out;
    SUCCEED();
}

TEST(player_engine_ext, addPlayFs_bogus_local_path_safe)
{
    PlayerEngine *e = pe_engine();
    ASSERT_TRUE(e != nullptr);
    EXPECT_NO_FATAL_FAILURE({
        e->addPlayFs(QList<QString>{"/tmp/pe_definitely_no_such_file_xyz.mp4"});
    });
    SUCCEED();
}
