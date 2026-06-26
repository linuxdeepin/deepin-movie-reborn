// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Extended unit tests for dmr::MovieConfiguration (movie_configuration.cpp).
// Suite name "movieconfig_ext" avoids overlap with the existing
// "libdmr" suite in test_dmr.cpp.

#include <QtTest>
#include <QTest>
#include <QDebug>
#include <unistd.h>
#include <gtest/gtest.h>
#include "application.h"
#include "movie_configuration.h"
#include "stub/stub.h"

using namespace dmr;

// ---------------------------------------------------------------------------
// Helpers (static + unique names to avoid ODR clashes with other TUs).
// ---------------------------------------------------------------------------

// Stable, unique URL pool so cases don't collide with each other or with the
// pre-existing "libdmr" suite. Each TEST clears its own url(s) before/after.
static const QUrl mc_ext_url_a = QUrl("movieconfig_ext://case-a");
static const QUrl mc_ext_url_b = QUrl("movieconfig_ext://case-b");
static const QUrl mc_ext_url_empty = QUrl("");
static const QUrl mc_ext_url_local =
    QUrl::fromLocalFile("/data/source/deepin-movie-reborn/movie/demo.mp4");

// Get the singleton once. The application + backend are already initialised
// by the time tests run (QApplication/MainWindow live in test_qtestmain.cpp).
static MovieConfiguration &mc_ext_cfg()
{
    return MovieConfiguration::get();
}

// Wipe everything before each case so tests are order-independent.
static void mc_ext_reset_db()
{
    mc_ext_cfg().clear();
}

// ===========================================================================
// knownKey2String: pure static mapping, covers every enum branch + default.
// ===========================================================================
TEST(movieconfig_ext, KnownKey2String_AllBranches)
{
    using K = MovieConfiguration::KnownKey;

    EXPECT_EQ(MovieConfiguration::knownKey2String(K::SubDelay).toStdString(),
              "sub-delay");
    EXPECT_EQ(MovieConfiguration::knownKey2String(K::SubCodepage).toStdString(),
              "sub-codepage");
    EXPECT_EQ(MovieConfiguration::knownKey2String(K::SubId).toStdString(),
              "sid");
    EXPECT_EQ(MovieConfiguration::knownKey2String(K::StartPos).toStdString(),
              "start");
    EXPECT_EQ(MovieConfiguration::knownKey2String(K::ExternalSubs).toStdString(),
              "external-subs");

    // Out-of-range enum value must hit the default branch (defensive).
    EXPECT_TRUE(MovieConfiguration::knownKey2String(
                    static_cast<K>(9999)).isEmpty());
}

// ===========================================================================
// urlExists: not-existing, existing, and after clear / remove.
// ===========================================================================
TEST(movieconfig_ext, UrlExists_BasicLifecycle)
{
    mc_ext_reset_db();
    auto &mc = mc_ext_cfg();

    // Empty db: nothing exists.
    EXPECT_FALSE(mc.urlExists(mc_ext_url_a));

    // updateUrl with any key creates the url row.
    mc.updateUrl(mc_ext_url_a, "volume", 50);
    EXPECT_TRUE(mc.urlExists(mc_ext_url_a));

    // A different url is still absent.
    EXPECT_FALSE(mc.urlExists(mc_ext_url_b));

    mc.clear();
    EXPECT_FALSE(mc.urlExists(mc_ext_url_a));
}

TEST(movieconfig_ext, UrlExists_AfterRemoveUrl)
{
    mc_ext_reset_db();
    auto &mc = mc_ext_cfg();

    mc.updateUrl(mc_ext_url_a, "k", 1);
    ASSERT_TRUE(mc.urlExists(mc_ext_url_a));

    mc.removeUrl(mc_ext_url_a);
    EXPECT_FALSE(mc.urlExists(mc_ext_url_a));
}

// ===========================================================================
// getByUrl (string-key overload): hit, miss, empty value, types round-trip.
// ===========================================================================
TEST(movieconfig_ext, GetByUrl_String_HitAndMiss)
{
    mc_ext_reset_db();
    auto &mc = mc_ext_cfg();

    EXPECT_FALSE(mc.getByUrl(mc_ext_url_a, "nope").isValid());

    mc.updateUrl(mc_ext_url_a, "volume", 30);
    EXPECT_EQ(mc.getByUrl(mc_ext_url_a, "volume").toInt(), 30);

    // Existing url but wrong key returns invalid/empty QVariant.
    EXPECT_FALSE(mc.getByUrl(mc_ext_url_a, "missing-key").isValid());
}

TEST(movieconfig_ext, GetByUrl_EmptyKeyAndEmptyUrl)
{
    mc_ext_reset_db();
    auto &mc = mc_ext_cfg();

    // Empty key on a stored url -> invalid.
    mc.updateUrl(mc_ext_url_a, "k", QVariant(1));
    EXPECT_FALSE(mc.getByUrl(mc_ext_url_a, "").isValid());

    // Empty url -> url not found -> invalid.
    EXPECT_FALSE(mc.getByUrl(mc_ext_url_empty, "k").isValid());
}

TEST(movieconfig_ext, GetByUrl_TypeRoundTrip)
{
    mc_ext_reset_db();
    auto &mc = mc_ext_cfg();

    // int
    mc.updateUrl(mc_ext_url_a, "int", QVariant(42));
    EXPECT_EQ(mc.getByUrl(mc_ext_url_a, "int").toInt(), 42);

    // double (sub-delay is fractional)
    mc.updateUrl(mc_ext_url_a, "dbl", QVariant(-2.5));
    EXPECT_DOUBLE_EQ(mc.getByUrl(mc_ext_url_a, "dbl").toDouble(), -2.5);

    // string with special chars
    const QString special = QString::fromUtf8("中文/字幕 UTF-8 \\ \" ' ;");
    mc.updateUrl(mc_ext_url_a, "str", QVariant(special));
    EXPECT_EQ(mc.getByUrl(mc_ext_url_a, "str").toString(), special);

    // overwrite updates the same key (replace into infos)
    mc.updateUrl(mc_ext_url_a, "int", QVariant(7));
    EXPECT_EQ(mc.getByUrl(mc_ext_url_a, "int").toInt(), 7);
}

// ===========================================================================
// getByUrl (KnownKey overload) + updateUrl (KnownKey overload): the enum
// variants are translated via knownKey2String before hitting the backend.
// ===========================================================================
TEST(movieconfig_ext, UpdateAndGet_KnownKeyOverloads)
{
    mc_ext_reset_db();
    auto &mc = mc_ext_cfg();
    using K = MovieConfiguration::KnownKey;

    mc.updateUrl(mc_ext_url_a, K::SubDelay, QVariant(1.25));
    EXPECT_DOUBLE_EQ(mc.getByUrl(mc_ext_url_a, K::SubDelay).toDouble(), 1.25);

    // Reading via the string key must agree (validates the enum->string map).
    EXPECT_DOUBLE_EQ(
        mc.getByUrl(mc_ext_url_a, "sub-delay").toDouble(), 1.25);

    mc.updateUrl(mc_ext_url_a, K::StartPos, QVariant(123));
    EXPECT_EQ(mc.getByUrl(mc_ext_url_a, K::StartPos).toInt(), 123);

    // KnownKey on a url that doesn't exist -> invalid.
    EXPECT_FALSE(mc.getByUrl(mc_ext_url_b, K::SubId).isValid());
}

// ===========================================================================
// queryByUrl: returns the full key/value map for a url; empty when absent.
// ===========================================================================
TEST(movieconfig_ext, QueryByUrl_MultiKeyMap)
{
    mc_ext_reset_db();
    auto &mc = mc_ext_cfg();

    // Absent url -> empty map.
    EXPECT_TRUE(mc.queryByUrl(mc_ext_url_a).isEmpty());

    mc.updateUrl(mc_ext_url_a, "k1", QVariant(1));
    mc.updateUrl(mc_ext_url_a, "k2", QVariant(2));
    mc.updateUrl(mc_ext_url_a, "k3", QVariant(3));

    auto map = mc.queryByUrl(mc_ext_url_a);
    EXPECT_EQ(map.size(), 3);
    EXPECT_EQ(map.value("k1").toInt(), 1);
    EXPECT_EQ(map.value("k2").toInt(), 2);
    EXPECT_EQ(map.value("k3").toInt(), 3);

    // Overwriting an existing key must NOT grow the map.
    mc.updateUrl(mc_ext_url_a, "k2", QVariant(99));
    map = mc.queryByUrl(mc_ext_url_a);
    EXPECT_EQ(map.size(), 3);
    EXPECT_EQ(map.value("k2").toInt(), 99);
}

TEST(movieconfig_ext, QueryByUrl_AfterRemoveAndClear)
{
    mc_ext_reset_db();
    auto &mc = mc_ext_cfg();

    mc.updateUrl(mc_ext_url_a, "k", QVariant(1));
    mc.updateUrl(mc_ext_url_b, "k", QVariant(2));
    ASSERT_EQ(mc.queryByUrl(mc_ext_url_a).size(), 1);

    mc.removeUrl(mc_ext_url_a);
    EXPECT_TRUE(mc.queryByUrl(mc_ext_url_a).isEmpty());
    // The other url is untouched by removeUrl.
    EXPECT_EQ(mc.queryByUrl(mc_ext_url_b).size(), 1);

    mc.clear();
    EXPECT_TRUE(mc.queryByUrl(mc_ext_url_b).isEmpty());
}

// ===========================================================================
// decodeList: pure helper. Splits on ';', base64-decodes each segment.
// ===========================================================================
TEST(movieconfig_ext, DecodeList_EmptyInput)
{
    auto &mc = mc_ext_cfg();
    EXPECT_TRUE(mc.decodeList(QVariant("")).isEmpty());
    EXPECT_TRUE(mc.decodeList(QVariant()).isEmpty());          // null variant
    EXPECT_TRUE(mc.decodeList(QVariant(QString())).isEmpty());  // empty string
}

TEST(movieconfig_ext, DecodeList_SingleAndMulti_RoundTrip)
{
    auto &mc = mc_ext_cfg();

    const QString a = QString::fromUtf8("track-1.ass");
    const QString b = QString::fromUtf8("track 2 with spaces");
    const QString c = QString::fromUtf8("中文.srt");

    // Encode exactly like append2ListUrl does.
    const QString encoded =
        a.toUtf8().toBase64() + ";" +
        b.toUtf8().toBase64() + ";" +
        c.toUtf8().toBase64();

    const QStringList decoded = mc.decodeList(QVariant(encoded));
    ASSERT_EQ(decoded.size(), 3);
    EXPECT_EQ(decoded[0], a);
    EXPECT_EQ(decoded[1], b);
    EXPECT_EQ(decoded[2], c);
}

TEST(movieconfig_ext, DecodeList_MalformedSegmentsKept)
{
    auto &mc = mc_ext_cfg();

    // A non-base64 segment still decodes to *something* (best effort); we only
    // assert it doesn't crash and the list length matches segment count.
    const QStringList out = mc.decodeList(QVariant("not-base64;AAAA"));
    EXPECT_EQ(out.size(), 2);
}

// ===========================================================================
// append2ListUrl + getListByUrl: list semantics, ordering, dedup behaviour,
// multiple appends, base64 round-trip.
// ===========================================================================
TEST(movieconfig_ext, AppendAndGetListUrl_SingleThenMultiple)
{
    mc_ext_reset_db();
    auto &mc = mc_ext_cfg();
    using K = MovieConfiguration::KnownKey;

    // No list yet -> empty.
    EXPECT_TRUE(mc.getListByUrl(mc_ext_url_a, K::ExternalSubs).isEmpty());

    mc.append2ListUrl(mc_ext_url_a, K::ExternalSubs,
                      QString::fromUtf8("/tmp/sub1.ass"));
    QStringList list = mc.getListByUrl(mc_ext_url_a, K::ExternalSubs);
    ASSERT_EQ(list.size(), 1);
    EXPECT_EQ(list[0].toStdString(), "/tmp/sub1.ass");

    // Second append preserves order and the previous entry.
    mc.append2ListUrl(mc_ext_url_a, K::ExternalSubs,
                      QString::fromUtf8("/tmp/sub2.srt"));
    list = mc.getListByUrl(mc_ext_url_a, K::ExternalSubs);
    ASSERT_EQ(list.size(), 2);
    EXPECT_EQ(list[0].toStdString(), "/tmp/sub1.ass");
    EXPECT_EQ(list[1].toStdString(), "/tmp/sub2.srt");

    // UTF-8 / spaces survive the base64 round-trip.
    const QString utf8 = QString::fromUtf8("/目录/字幕 文件.ass");
    mc.append2ListUrl(mc_ext_url_a, K::ExternalSubs, utf8);
    list = mc.getListByUrl(mc_ext_url_a, K::ExternalSubs);
    ASSERT_EQ(list.size(), 3);
    EXPECT_EQ(list[2], utf8);
}

TEST(movieconfig_ext, AppendListUrl_EmptyStringStillAppended)
{
    mc_ext_reset_db();
    auto &mc = mc_ext_cfg();
    using K = MovieConfiguration::KnownKey;

    // append2ListUrl base64-encodes val and joins with ';'. An empty val
    // produces one (empty) segment, so decodeList yields one entry.
    mc.append2ListUrl(mc_ext_url_a, K::ExternalSubs, QString());
    const QStringList list = mc.getListByUrl(mc_ext_url_a, K::ExternalSubs);
    EXPECT_EQ(list.size(), 1);
}

TEST(movieconfig_ext, GetListByUrl_AbsentUrl)
{
    mc_ext_reset_db();
    auto &mc = mc_ext_cfg();
    using K = MovieConfiguration::KnownKey;

    EXPECT_TRUE(mc.getListByUrl(mc_ext_url_b, K::SubId).isEmpty());
}

// ===========================================================================
// removeFromListUrl: currently a thin wrapper around getListByUrl; ensure it
// is callable on an absent url and on a populated url without crashing.
// (The implementation only reads; we verify the read is consistent.)
// ===========================================================================
TEST(movieconfig_ext, RemoveFromListUrl_NoCrash_AbsentAndPopulated)
{
    mc_ext_reset_db();
    auto &mc = mc_ext_cfg();
    using K = MovieConfiguration::KnownKey;

    // Absent url + non-empty val: must not crash.
    mc.removeFromListUrl(mc_ext_url_a, K::ExternalSubs,
                         QString::fromUtf8("/tmp/x.ass"));

    // Populate then call.
    mc.append2ListUrl(mc_ext_url_a, K::ExternalSubs,
                      QString::fromUtf8("/tmp/y.ass"));
    EXPECT_NO_FATAL_FAILURE({
        mc.removeFromListUrl(mc_ext_url_a, K::ExternalSubs,
                             QString::fromUtf8("/tmp/y.ass"));
    });

    // Empty val (parity with the existing libdmr/movieConfiguration case).
    EXPECT_NO_FATAL_FAILURE({
        mc.removeFromListUrl(mc_ext_url_a, K::ExternalSubs, QString());
    });
}

// ===========================================================================
// updateUrl with local-file url exercises the FastFileHash branch in the
// backend (url.isLocalFile()). The demo file is provided in the test env;
// if absent the branch still runs and urlExists must become true.
// ===========================================================================
TEST(movieconfig_ext, UpdateUrl_LocalFileUrl_HashBranch)
{
    mc_ext_reset_db();
    auto &mc = mc_ext_cfg();

    mc.updateUrl(mc_ext_url_local, "playback", QVariant(QUrl("x")));
    // Either the demo file hashed successfully or hashing threw; in both
    // cases the url row should be present afterwards.
    EXPECT_TRUE(mc.urlExists(mc_ext_url_local));
    EXPECT_EQ(mc.getByUrl(mc_ext_url_local, "playback").toUrl(),
              QUrl("x"));
}

// ===========================================================================
// Stubs: verify wiring through the backend by stubbing MovieConfiguration's
// own members where feasible, and assert the public API is resilient.
//
// We can't easily stub the private MovieConfigurationBackend (its definition
// is local to the .cpp), but we CAN stub the *public* urlExists so that
// dependent callers (getByUrl/queryByUrl) take the "not found" short-circuit
// even when the row is actually present. This exercises the early-return
// branches in getByUrl/queryByUrl indirectly.
// ===========================================================================
TEST(movieconfig_ext, Stub_urlExists_ForcesNotFoundBranch)
{
    mc_ext_reset_db();
    auto &mc = mc_ext_cfg();

    // Plant a real row.
    mc.updateUrl(mc_ext_url_a, "k", QVariant(1));
    ASSERT_TRUE(mc.urlExists(mc_ext_url_a));
    ASSERT_EQ(mc.getByUrl(mc_ext_url_a, "k").toInt(), 1);

    // Force urlExists to always report false; getByUrl must then return an
    // invalid QVariant (the not-found branch of queryValueByUrlKey).
    Stub stub;
    static bool mc_ext_url_exists_stub_ret = false;
    auto mc_ext_url_exists_stub = +[](void * /*self*/, const QUrl & /*url*/) {
        return mc_ext_url_exists_stub_ret;
    };
    stub.set(ADDR(MovieConfiguration, urlExists), mc_ext_url_exists_stub);

    EXPECT_FALSE(mc.urlExists(mc_ext_url_a));
    EXPECT_FALSE(mc.getByUrl(mc_ext_url_a, "k").isValid());
    EXPECT_TRUE(mc.queryByUrl(mc_ext_url_a).isEmpty());

    // Restore the real implementation and confirm the row is still there.
    stub.reset(ADDR(MovieConfiguration, urlExists));
    EXPECT_TRUE(mc.urlExists(mc_ext_url_a));
    EXPECT_EQ(mc.getByUrl(mc_ext_url_a, "k").toInt(), 1);
}

// ===========================================================================
// Smoke test: clear() on an already-empty db is a no-op (no throw, no rows).
// ===========================================================================
TEST(movieconfig_ext, Clear_OnEmptyDb_Idempotent)
{
    mc_ext_reset_db();
    auto &mc = mc_ext_cfg();

    mc.clear();
    mc.clear();
    EXPECT_FALSE(mc.urlExists(mc_ext_url_a));
    EXPECT_TRUE(mc.queryByUrl(mc_ext_url_a).isEmpty());
}
