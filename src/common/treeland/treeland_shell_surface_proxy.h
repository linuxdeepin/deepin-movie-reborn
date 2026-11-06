// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DMR_TREELAND_SHELL_SURFACE_PROXY_H
#define DMR_TREELAND_SHELL_SURFACE_PROXY_H

#include <QObject>
#include <QHash>
#include <QWidget>

class QWindow;
class QPlatformSurfaceEvent;

namespace dmr {

/**
 * @brief treeland (V25) 置顶代理（dde-shell v2 协议实现）
 *
 * treeland 没有实现老的 dde-shell KEEP_ABOVE 状态（_d_dwayland_staysontop
 * 走的路径），普通 xdg_toplevel 在 treeland 下无法置顶。treeland 提供的
 * 等价机制是 treeland_dde_shell_manager_v2/treeland_dde_shell_surface_v2
 * 协议：通过 set_role(overlay) 把窗口提升到 workspace overlay 层（位于所
 * 有普通 toplevel 之上、layer-shell 之下），set_role(normal) 恢复常规层级，
 * 两者可在窗口存续期间动态切换（参考 treeland examples/test_dde_shell）。
 *
 * 时序要求（关键）: treeland 只在创建 xdg_toplevel 包装（ensureXdgWrapper）
 * 时按 get_shell_surface 建立的关联应用 shell surface 角色；xdg_toplevel
 * 创建之后再发的 get_shell_surface 不会建立关联，set_role 不生效。
 *
 * Qt6 Wayland 下 xdg_toplevel 在 QWidget::setVisible(true) → show_sys() →
 * QWaylandWindow::initWindow() 中创建，而 QWidget 的 Show 事件在 setVisible
 * 内先于 show_sys() 投递（"send the show event before showing"），此时
 * wl_surface 已存在、xdg_toplevel 尚未创建。因此本类在窗口的 QEvent::Show
 * 时创建 shell surface，恰好先于 xdg_toplevel；窗口重建（迷你模式
 * setWindowFlags）时同样由新一次 Show 事件覆盖。此外监听
 * QWaylandWindow::surfaceCreated（wl_surface 重建场景）与 manager
 * activeChanged（协议全局晚绑定场景）兜底。
 *
 * 创建 shell surface 后立即 set_role(normal) 恢复普通层级（协议默认角色是
 * overlay）；此后置顶/取消置顶由 setStayOnTop 动态切换。
 *
 * 仅在 Qt6 + Wayland + treeland 提供该全局时生效；其他环境（X11、kwin
 * wayland、Qt5 构建）下所有接口为 no-op，调用方回退到原有路径。
 */
class TreelandShellSurfaceProxy
{
public:
    /// treeland_dde_shell_manager_v2 全局是否可用（当前会话是 treeland）
    static bool isTreelandActive();

    /// 在窗口显示前挂接主窗口；manager 未激活时也挂接，激活后自动补建
    static void attachWindow(QWidget *widget);

    /// 该窗口是否已挂接 shell surface（请求可以送达合成器）
    static bool isWindowManaged(QWindow *window);

    /// 该窗口当前的置顶状态（overlay 角色）
    static bool stayOnTopOf(QWindow *window);

    /// 请求置顶/取消置顶；返回是否成功送达合成器（假则调用方回退旧路径）
    static bool setStayOnTop(QWindow *window, bool on);
};

} // namespace dmr

#endif /* !DMR_TREELAND_SHELL_SURFACE_PROXY_H */
