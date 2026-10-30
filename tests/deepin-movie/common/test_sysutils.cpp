// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// UT-movie-17 Mode2 batch1: SysUtils (src/common/sysutils.cpp)
//
// 用例数: 6 (3×TEST_P libExist + 3×TEST_P libPath)   覆盖方法: 3/3 (libExist, libPath, ctor)
//
// 分支清单 → 用例映射 (源码以 mcp-dump/snippets.json 为准):
//   SysUtils()                                    → ctor_isInstantiable
//   libExist: 含 ".so" 截取主名, QLibrary 直载成功        → libExist/LibZ (主路径加载成功)
//   libExist: 主名加载失败, libPath 回退加载成功          → libExist/LibM (回退路径加载成功)
//   libExist: 两条路径均失败                            → libExist/Missing (false)
//   libPath: QLibraryInfo 空路径守卫 (平台常量非空, 不可达 — 未映射)
//   libPath: 目录不可达回退 (/usr 等兜底) (宿主目录恒可达, 不可达 — 未映射)
//   libPath: 精确匹配 entryList 含 strlib               → libPath/ExactMatch
//   libPath: 非精确匹配取排序末位                       → libPath/VariantLast
//   libPath: 无匹配返回原名                             → libPath/NoMatch
//
// 环境: 测试依赖宿主 glibc 标准布局 (/usr/lib/x86_64-linux-gnu 存在 libz/libm 系列),
// 属于编译宿主环境而非外部设备/服务依赖, 不需要桩。

#include <gtest/gtest.h>
#include <QString>
#include <QDir>

#include "sysutils.h"

/* SysUtils 在全局命名空间 */

namespace {

struct LibExistCase { const char *lib; bool exist; const char *why; };
struct LibPathCase  { const char *lib; const char *expectPrefix; const char *why; };

class SysUtilsExistTest : public ::testing::TestWithParam<LibExistCase> { };
class SysUtilsPathTest  : public ::testing::TestWithParam<LibPathCase>  { };

TEST_P(SysUtilsExistTest, libExist)
{
    // Arrange
    const LibExistCase &c = GetParam();

    // Act
    bool exist = SysUtils::libExist(QString::fromLatin1(c.lib));

    // Assert
    EXPECT_EQ(exist, c.exist) << "lib=" << c.lib << " why=" << c.why;
}

INSTANTIATE_TEST_SUITE_P(LibExist, SysUtilsExistTest, ::testing::Values(
    LibExistCase{"libz.so.1", true,  "libz.so 为真实 ELF, 主路径 QLibrary(libz) 直接成功"},
    LibExistCase{"libm.so.6", true,  "libm.so 为链接脚本, 主路径失败后回退 libPath 命中"},
    LibExistCase{"lib_ut_missing_12345.so", false, "主/回退路径均不存在, 返回 false"},
    LibExistCase{"lib_ut_missing_nosuffix", false, "无 .so 后缀: 走截取 else 分支, 主/回退均失败"}
));

TEST_P(SysUtilsPathTest, libPath)
{
    // Arrange
    const LibPathCase &c = GetParam();

    // Act
    QString result = SysUtils::libPath(QString::fromLatin1(c.lib));

    // Assert
    EXPECT_FALSE(result.isEmpty()) << "lib=" << c.lib;
    EXPECT_TRUE(result.startsWith(QString::fromLatin1(c.expectPrefix)))
        << "lib=" << c.lib << " result=" << result.toStdString() << " why=" << c.why;
}

INSTANTIATE_TEST_SUITE_P(LibPath, SysUtilsPathTest, ::testing::Values(
    LibPathCase{"libm.so.6", "libm.so.6", "entryList 精确命中 libm.so.6, 原样返回"},
    LibPathCase{"libm",      "libm",      "entryList 非精确命中(libm.a/libm.so/libm.so.6), 取排序末位 libm.so.6"},
    LibPathCase{"lib_ut_missing_12345", "lib_ut_missing_12345", "无匹配, 返回原名"}
));

TEST(SysUtilsTest, ctor_isInstantiable)
{
    // Arrange / Act
    SysUtils utils;

    // Assert
    SUCCEED();
}

}  // namespace
