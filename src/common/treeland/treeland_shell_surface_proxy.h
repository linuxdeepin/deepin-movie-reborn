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
 * @brief treeland (V25) 置顶代理
 *
 * treeland 没有实现老的 dde-shell KEEP_ABOVE 状态（_d_dwayland_staysontop
 * 走的路径），普通 xdg_toplevel 在 treeland 下无法置顶。实测 treeland
 * 0.11.0（V25）：treeland_dde_shell_manager_v2 的 set_role(overlay) 不改
 * 变窗口 z 序（独立测试客户端验证无效）；treeland_wine_window_manager_v1
 * 的 set_z_order(hwnd_topmost/hwnd_notopmost) 走 SurfaceWrapper::
 * setAlwaysOnTop 路径（与 treeland 自带窗口菜单 "Always on Top" 相同），
 * 可稳定保持窗口置顶。因此本代理用 wine-window-management 协议实现置顶。
 *
 * 时序要求：get_window_control 需要 xdg_toplevel 已存在，控制对象在窗口
 * 首次 Expose 或首次置顶请求时惰性创建。迷你模式切换（X11BypassWindow
 * ManagerHint）可能销毁重建 xdg_toplevel，代理在 Expose/Show 时比对
 * toplevel 指针，变化即重建控制对象并回设上次请求的置顶状态。
 *
 * 仅在 Qt6 + Wayland + treeland 提供该全局时生效；其他环境（X11、kwin
 * wayland、Qt5 构建）下所有接口为 no-op，调用方回退到原有路径。
 */
class TreelandShellSurfaceProxy
{
public:
    /// treeland_dde_shell_manager_v2 全局是否可用（当前会话是 treeland）
    static bool isTreelandActive();

    /// 在窗口显示前挂接主窗口；manager 未激活时不做任何事
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
