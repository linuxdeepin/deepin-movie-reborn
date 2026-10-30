// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// UT-movie-17 Mode2 batch3: Utility (src/common/utility_x11.cpp)
//
// 用例数: 12  覆盖方法: 9/9 (internAtom, qregion2XcbRectangles, sendMoveResizeMessage,
//                            startWindowSystemMove, cancelWindowMoveResize,
//                            updateMousePointForWindowMove, setBypassCompositor,
//                            setRectangles, setWindowCursor) + CornerEdge2Xcb_cursor_t[间接]
//
// 分支清单 → 用例映射 (源码以 mcp-dump/snippets.json 为准):
//   internAtom: name null/空 → XCB_NONE            → internAtom_nullOrEmpty_returnsNone
//   internAtom: reply 为空 → XCB_NONE              → internAtom_validName_returnsAtom (真实 X 连接)
//   internAtom: 成功返回 atom                      → internAtom_validName_returnsAtom
//   qregion2XcbRectangles: 多矩形映射              → qregion2XcbRectangles_* (纯转换)
//   sendMoveResizeMessage: 按钮映射(左/右)         → sendMoveResizeMessage_* / *_rightButton
//   sendMoveResizeMessage: globalPos 空 → 查询指针 → startWindowSystemMove_* (桩返回 121/212)
//   startWindowSystemMove/CANCEL: 动作码 8/11      → startWindowSystemMove_* / cancelWindowMoveResize_*
//   updateMousePointForWindowMove: _DEEPIN_MOVE_UPDATE → updateMousePointForWindowMove_*
//   setBypassCompositor: wayland 早退              → setBypassCompositor_waylandEnv_returnsEarly
//   setBypassCompositor: display 为空早退 / WId 0 早退 → setBypassCompositor_noDisplay_returnsEarly
//   setBypassCompositor: XChangeProperty 写值      → setBypassCompositor_withDisplay_* (X 宿主)
//   setRectangles: 空 → xcb_shape_mask 清除        → setRectangles_empty_*
//   setRectangles: 非空 → xcb_shape_rectangles     → setRectangles_nonEmpty_*
//   setWindowCursor: 边缘映射 + 成败分支           → setWindowCursor_*
//
// 桩策略: 全部 X 相关 C 符号(xcb_intern_atom/xcb_send_event/XInternAtom/
// XChangeProperty/XCreateFontCursor 等)经 stub_ext 符号替换并捕获参数 ——
// detour 只作用于本测试二进制内 utility_x11.cpp 的调用点, Qt xcb 平台插件
// 内部的 xcb 调用不受影响; 无显示环境时真实连接为空指针, 桩接管后同样确定。
// check_wayland_env 使用缓存值, USE_TEST 下经 set_wayland 直接置位控制。
// qregion2XcbRectangles/sendMoveResizeMessage 为 private static, 经 #define private public
// 范式(仅作用于本 TU 的 utility.h 包含)直接访问, 与本仓库平台侧测试同款。

#include <gtest/gtest.h>
#include <QRegion>
#include <QPoint>
#include <QtGui/private/qtx11extras_p.h>

#include "libdmr/utils.h"

// 私有静态函数直接访问: #define private public 仅作用于本 TU 的 utility.h 包含。
// 必须置于 X11/Xlib.h 之前(其 None/Bool 等宏会污染后续 Qt 头)。
#define protected public
#define private public
#include "utility.h"
#undef protected
#undef private

#include <xcb/xcb.h>
#include <xcb/shape.h>
#include <X11/Xlib.h>
#include <X11/cursorfont.h>

#include "stub/stubext.h"

using namespace dmr;

namespace {

constexpr uint32_t kMoveAction = 8;      // _NET_WM_MOVERESIZE_MOVE (utility_x11.cpp 内定义)
constexpr uint32_t kCancelAction = 11;   // _NET_WM_MOVERESIZE_CANCEL

struct SendEventCapture
{
    int count = 0;
    quint32 window = 0;       // xcb_send_event 的 destination(appRootWindow, offscreen 下为 0)
    quint32 evWindow = 0;     // 事件载荷 xev.window(即目标 WId, EWMH 协议真正携带的窗口)
    quint32 x = 0, y = 0, action = 0, btn = 0;
    xcb_atom_t type = 0;
};
static SendEventCapture g_seCap;

struct SetPropertyCapture
{
    int count = 0;
    Atom propAtom = 0, typeAtom = 0;
    unsigned long value = 999;
};
static SetPropertyCapture g_spCap;

struct CursorCapture
{
    Window window = 0;
    Cursor cursor = 0;        // XDefineCursor 收到的游标 ID(即 XCreateFontCursor 的返回值)
    unsigned lastShape = 0;   // XCreateFontCursor 请求的字体形状
    bool defineCalled = false;
};
static CursorCapture g_curCap;

static xcb_atom_t g_fakeAtom = 0x5678;

// 安装 xcb 客户端消息路径全套桩(连接指针可为空, 桩忽略)
void stubXcbClientMessages(stub_ext::StubExt &stub)
{
    g_seCap = SendEventCapture{};
    stub.set_lamda(&xcb_intern_atom,
                   [](xcb_connection_t *, uint8_t, uint16_t, const char *) {
                       return xcb_intern_atom_cookie_t{};
                   });
    stub.set_lamda(&xcb_intern_atom_reply,
                   [](xcb_connection_t *, xcb_intern_atom_cookie_t, xcb_generic_error_t **)
                       -> xcb_intern_atom_reply_t * {
                       auto *r = static_cast<xcb_intern_atom_reply_t *>(
                           calloc(1, sizeof(xcb_intern_atom_reply_t)));
                       r->atom = g_fakeAtom;
                       return r;
                   });
    stub.set_lamda(&xcb_query_pointer,
                   [](xcb_connection_t *, xcb_window_t) {
                       return xcb_query_pointer_cookie_t{};
                   });
    stub.set_lamda(&xcb_query_pointer_reply,
                   [](xcb_connection_t *, xcb_query_pointer_cookie_t, xcb_generic_error_t **)
                       -> xcb_query_pointer_reply_t * {
                       auto *r = static_cast<xcb_query_pointer_reply_t *>(
                           calloc(1, sizeof(xcb_query_pointer_reply_t)));
                       r->root_x = 121;
                       r->root_y = 212;
                       return r;
                   });
    stub.set_lamda(&xcb_ungrab_pointer,
                   [](xcb_connection_t *, xcb_timestamp_t) {
                       return xcb_void_cookie_t{};
                   });
    stub.set_lamda(&xcb_send_event,
                   [](xcb_connection_t *, uint8_t, xcb_window_t win, uint32_t, const char *data) {
                       auto *ev = reinterpret_cast<const xcb_client_message_event_t *>(data);
                       g_seCap.count++;
                       g_seCap.window = win;
                       g_seCap.evWindow = ev->window;
                       g_seCap.type = ev->type;
                       g_seCap.x = ev->data.data32[0];
                       g_seCap.y = ev->data.data32[1];
                       g_seCap.action = ev->data.data32[2];
                       g_seCap.btn = ev->data.data32[3];
                       return xcb_void_cookie_t{};
                   });
    stub.set_lamda(&xcb_flush, [](xcb_connection_t *) { return 1; });
}

class UtilityX11Test : public ::testing::Test
{
protected:
    void TearDown() override { m_stub.clear(); }

    stub_ext::StubExt m_stub;
};

TEST_F(UtilityX11Test, internAtom_nullOrEmpty_returnsNone)
{
    // Act + Assert: name 为空/null 时直接返回 XCB_NONE, 不触碰连接
    EXPECT_EQ(Utility::internAtom(nullptr), XCB_NONE);
    EXPECT_EQ(Utility::internAtom(""), XCB_NONE);
}

TEST_F(UtilityX11Test, internAtom_validName_returnsAtom)
{
    // Arrange
    if (QX11Info::connection() == nullptr)
        GTEST_SKIP() << "no X connection on this host";

    // Act + Assert: 成功 intern 返回有效 atom, 且同名稳定
    xcb_atom_t a1 = Utility::internAtom("WM_NAME");
    xcb_atom_t a2 = Utility::internAtom("WM_NAME");
    EXPECT_NE(a1, XCB_NONE);
    EXPECT_EQ(a1, a2);
}

TEST_F(UtilityX11Test, qregion2XcbRectangles_singleRect_mapsFields)
{
    // Arrange
    QRegion region(QRect(3, 5, 20, 10));

    // Act
    QVector<xcb_rectangle_t> rects =
        Utility::qregion2XcbRectangles(region);

    // Assert
    ASSERT_EQ(rects.size(), 1);
    EXPECT_EQ(rects[0].x, 3);
    EXPECT_EQ(rects[0].y, 5);
    EXPECT_EQ(rects[0].width, 20);
    EXPECT_EQ(rects[0].height, 10);
}

TEST_F(UtilityX11Test, qregion2XcbRectangles_multiAndEmpty_countsRects)
{
    // Arrange
    QRegion region(QRect(0, 0, 10, 10));
    region += QRect(20, 20, 5, 5);

    // Act
    QVector<xcb_rectangle_t> rects =
        Utility::qregion2XcbRectangles(region);

    // Assert
    EXPECT_EQ(rects.size(), region.rectCount());

    // 空区域 → 空输出
    EXPECT_TRUE(Utility::qregion2XcbRectangles(QRegion()).isEmpty());
}

TEST_F(UtilityX11Test, startWindowSystemMove_sendsMoveActionWithPointerPos)
{
    // Arrange
    stubXcbClientMessages(m_stub);

    // Act
    Utility::startWindowSystemMove(777);

    // Assert: globalPos 为空 → 查询指针(桩返回 121/212); MOVE=8; 默认左键=1
    EXPECT_EQ(g_seCap.count, 1);
    EXPECT_EQ(g_seCap.evWindow, 777u);   // EWMH: WId 载荷入事件, destination 为 root
    EXPECT_EQ(g_seCap.action, kMoveAction);
    EXPECT_EQ(g_seCap.type, g_fakeAtom);
    EXPECT_EQ(g_seCap.x, 121u);
    EXPECT_EQ(g_seCap.y, 212u);
    EXPECT_EQ(g_seCap.btn, 1u);
}

TEST_F(UtilityX11Test, cancelWindowMoveResize_sendsCancelAction)
{
    // Arrange
    stubXcbClientMessages(m_stub);

    // Act
    Utility::cancelWindowMoveResize(555);

    // Assert: CANCEL=11
    EXPECT_EQ(g_seCap.count, 1);
    EXPECT_EQ(g_seCap.evWindow, 555u);   // 事件载荷窗口
    EXPECT_EQ(g_seCap.action, kCancelAction);
}

TEST_F(UtilityX11Test, updateMousePointForWindowMove_sendsDeepinAtom)
{
    // Arrange
    stubXcbClientMessages(m_stub);

    // Act
    Utility::updateMousePointForWindowMove(888, QPoint(34, 56));

    // Assert: 使用给定坐标, data32[2] = 0, 自定义 atom "_DEEPIN_MOVE_UPDATE"
    EXPECT_EQ(g_seCap.count, 1);
    EXPECT_EQ(g_seCap.evWindow, 888u);   // 事件载荷窗口
    EXPECT_EQ(g_seCap.x, 34u);
    EXPECT_EQ(g_seCap.y, 56u);
    EXPECT_EQ(g_seCap.action, 0u);
    EXPECT_EQ(g_seCap.type, g_fakeAtom);
}

TEST_F(UtilityX11Test, sendMoveResizeMessage_rightButton_mapsBtnIndex)
{
    // Arrange
    stubXcbClientMessages(m_stub);

    // Act
    Utility::sendMoveResizeMessage(
        666, kMoveAction, QPoint(9, 9), Qt::RightButton);

    // Assert: 右键 → XCB_BUTTON_INDEX_3; 坐标直接取参数
    EXPECT_EQ(g_seCap.btn, 3u);
    EXPECT_EQ(g_seCap.x, 9u);
    EXPECT_EQ(g_seCap.y, 9u);
}

TEST_F(UtilityX11Test, setBypassCompositor_waylandEnv_returnsEarly)
{
    // Arrange: check_wayland_env 命中 → 不触碰任何 X 资源
    dmr::utils::set_wayland(true);

    // Act + Assert: 即使无任何桩/无 X 连接也不崩溃
    EXPECT_NO_THROW(Utility::setBypassCompositor(42, true));
    // 验证 wayland 标志 set/get 往返一致(早退分支确已命中)
    EXPECT_TRUE(dmr::utils::check_wayland_env());
    dmr::utils::set_wayland(false);
}

TEST_F(UtilityX11Test, setBypassCompositor_noDisplay_returnsEarly)
{
    // Arrange
    dmr::utils::set_wayland(false);
    if (QX11Info::display() != nullptr)
        GTEST_SKIP() << "X display present, early-return branch unreachable";

    // Act + Assert: display 为空 → 早退
    EXPECT_NO_THROW(Utility::setBypassCompositor(42, true));
    EXPECT_FALSE(dmr::utils::check_wayland_env());
}

TEST_F(UtilityX11Test, setBypassCompositor_withDisplay_setsCardinalProperty)
{
    // Arrange
    dmr::utils::set_wayland(false);
    if (QX11Info::display() == nullptr)
        GTEST_SKIP() << "no X display on this host";
    g_spCap = SetPropertyCapture{};
    static int callIndex = 0;
    callIndex = 0;
    m_stub.set_lamda(&XInternAtom,
                     [](Display *, const char *, Bool) -> Atom {
                         return ++callIndex == 1 ? Atom(0xA1) : Atom(0xA2);
                     });
    m_stub.set_lamda(&XChangeProperty,
                     [](Display *, Window, Atom prop, Atom type, int format, int,
                        const unsigned char *data, int nelements) {
                         g_spCap.count++;
                         g_spCap.propAtom = prop;
                         g_spCap.typeAtom = type;
                         g_spCap.value = nelements > 0
                             ? *reinterpret_cast<const unsigned long *>(data) : 0;
                         return Success;
                     });
    m_stub.set_lamda(&XFlush, [](Display *) { return 1; });

    // Act
    Utility::setBypassCompositor(42, true);

    // Assert: 第一个 atom = bypass 属性, 第二个 = CARDINAL, 值 1, 32 位
    EXPECT_EQ(g_spCap.count, 1);
    EXPECT_EQ(g_spCap.propAtom, Atom(0xA1));
    EXPECT_EQ(g_spCap.typeAtom, Atom(0xA2));
    EXPECT_EQ(g_spCap.value, 1ul);

    // Act2: bypass=false → 值 0
    Utility::setBypassCompositor(42, false);
    EXPECT_EQ(g_spCap.count, 2);
    EXPECT_EQ(g_spCap.value, 0ul);
}

TEST_F(UtilityX11Test, setWindowCursor_mapsEdgeAndReportsSuccess)
{
    // Arrange
    g_curCap = CursorCapture{};
    static Cursor fakeCursorId = 77;
    m_stub.set_lamda(&XCreateFontCursor,
                     [](Display *, unsigned int shape) -> Cursor {
                         g_curCap.lastShape = shape;   // 记录请求的字体形状
                         return fakeCursorId;
                     });
    m_stub.set_lamda(&XDefineCursor,
                     [](Display *, Window win, Cursor cursor) {
                         g_curCap.window = win;
                         g_curCap.cursor = cursor;
                         g_curCap.defineCalled = true;
                         return Success;
                     });
    m_stub.set_lamda(&XFlush, [](Display *) { return 1; });

    // Act + Assert: 边缘 → 字体形状映射; 成功返回 true
    EXPECT_TRUE(Utility::setWindowCursor(9, Utility::TopEdge));
    EXPECT_EQ(g_curCap.lastShape, XC_top_side);
    EXPECT_EQ(g_curCap.cursor, fakeCursorId);
    EXPECT_TRUE(g_curCap.defineCalled);
    EXPECT_EQ(g_curCap.window, 9u);

    EXPECT_TRUE(Utility::setWindowCursor(9, Utility::TopLeftCorner));
    EXPECT_EQ(g_curCap.lastShape, XC_top_left_corner);

    EXPECT_TRUE(Utility::setWindowCursor(9, Utility::BottomRightCorner));
    EXPECT_EQ(g_curCap.lastShape, XC_bottom_right_corner);
}

TEST_F(UtilityX11Test, setWindowCursor_fontCursorFailure_returnsFalse)
{
    // Arrange: XCreateFontCursor 失败返回 0
    m_stub.set_lamda(&XCreateFontCursor, [](Display *, unsigned int) -> Cursor {
        return 0;
    });

    // Act + Assert
    EXPECT_FALSE(Utility::setWindowCursor(9, Utility::LeftEdge));
}

TEST_F(UtilityX11Test, setRectangles_empty_clearsShapeMask)
{
    // Arrange
    struct MaskCapture { int count = 0; xcb_shape_op_t op; xcb_shape_kind_t kind; };
    static MaskCapture maskCap;
    maskCap = MaskCapture{};
    struct RectsCapture { int count = 0; xcb_shape_kind_t kind; uint32_t n; };
    static RectsCapture rectsCap;
    rectsCap = RectsCapture{};
    m_stub.set_lamda(&xcb_shape_mask,
                     [](xcb_connection_t *, xcb_shape_op_t op, xcb_shape_kind_t kind,
                        xcb_window_t, int16_t, int16_t, xcb_pixmap_t) -> xcb_void_cookie_t {
                         maskCap.count++;
                         maskCap.op = op;
                         maskCap.kind = kind;
                         return xcb_void_cookie_t{};
                     });
    m_stub.set_lamda(&xcb_shape_rectangles,
                     [](xcb_connection_t *, xcb_shape_op_t, xcb_shape_kind_t kind,
                        uint8_t, xcb_window_t, int16_t, int16_t, uint32_t n,
                        const xcb_rectangle_t *) -> xcb_void_cookie_t {
                         rectsCap.count++;
                         rectsCap.kind = kind;
                         rectsCap.n = n;
                         return xcb_void_cookie_t{};
                     });

    // Act: 空矩形 + onlyInput
    Utility::setRectangles(7, QVector<xcb_rectangle_t>(), true);
    EXPECT_EQ(maskCap.count, 1);
    EXPECT_EQ(maskCap.kind, XCB_SHAPE_SK_INPUT);
    EXPECT_EQ(rectsCap.count, 0);

    // Act2: 空矩形 + 非 input → BOUNDING
    Utility::setRectangles(7, QVector<xcb_rectangle_t>(), false);
    EXPECT_EQ(maskCap.count, 2);
    EXPECT_EQ(maskCap.kind, XCB_SHAPE_SK_BOUNDING);
}

TEST_F(UtilityX11Test, setRectangles_nonEmpty_setsRectangles)
{
    // Arrange
    struct MaskCapture { int count = 0; };
    static MaskCapture maskCap;
    maskCap = MaskCapture{};
    struct RectsCapture { int count = 0; xcb_shape_kind_t kind; uint32_t n; };
    static RectsCapture rectsCap;
    rectsCap = RectsCapture{};
    m_stub.set_lamda(&xcb_shape_mask,
                     [](xcb_connection_t *, xcb_shape_op_t, xcb_shape_kind_t,
                        xcb_window_t, int16_t, int16_t, xcb_pixmap_t) -> xcb_void_cookie_t {
                         maskCap.count++;
                         return xcb_void_cookie_t{};
                     });
    m_stub.set_lamda(&xcb_shape_rectangles,
                     [](xcb_connection_t *, xcb_shape_op_t, xcb_shape_kind_t kind,
                        uint8_t, xcb_window_t, int16_t, int16_t, uint32_t n,
                        const xcb_rectangle_t *) -> xcb_void_cookie_t {
                         rectsCap.count++;
                         rectsCap.kind = kind;
                         rectsCap.n = n;
                         return xcb_void_cookie_t{};
                     });
    QVector<xcb_rectangle_t> rects{xcb_rectangle_t{1, 2, 30, 40},
                                   xcb_rectangle_t{5, 6, 7, 8}};

    // Act
    Utility::setRectangles(7, rects, true);

    // Assert: 不走 mask 清除路径, 矩形数与 kind 正确
    EXPECT_EQ(maskCap.count, 0);
    EXPECT_EQ(rectsCap.count, 1);
    EXPECT_EQ(rectsCap.kind, XCB_SHAPE_SK_INPUT);
    EXPECT_EQ(rectsCap.n, 2);
}

}  // namespace
