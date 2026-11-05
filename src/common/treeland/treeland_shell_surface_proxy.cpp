// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "treeland_shell_surface_proxy.h"

#include "ddlog.h"

#include "qwayland-treeland-wine-window-management-unstable-v1.h"

#include <QGuiApplication>
#include <QPlatformSurfaceEvent>
#include <QPointer>
#include <QWindow>
#include <memory>
#include <QtWaylandClient/qwaylandclientextension.h>
#include <QtGui/qpa/qplatformnativeinterface.h>

namespace dmr {

/**
 * @brief treeland_wine_window_manager_v1 全局的客户端绑定
 *
 * treeland 0.11.0 实测（V25 10.8.7.203）：
 * - treeland_dde_shell_manager_v2 的 set_role(overlay) 不会改变窗口 z 序
 *   （独立测试客户端验证，overlay 角色窗口仍会被激活窗口覆盖）；
 * - treeland_wine_window_manager_v1 的 set_z_order(hwnd_topmost) 走
 *   SurfaceWrapper::setAlwaysOnTop 路径（与 treeland 自带窗口菜单
 *   "Always on Top" 相同），可稳定保持窗口置顶。
 * 因此置顶走 wine-window-management 协议。
 */
class TreelandWineWindowManager
    : public QWaylandClientExtensionTemplate<TreelandWineWindowManager>
    , public QtWayland::treeland_wine_window_manager_v1
{
public:
    TreelandWineWindowManager()
        : QWaylandClientExtensionTemplate<TreelandWineWindowManager>(1)
    {
        // initialize() 默认被排队到事件循环执行，这里同步调用：QWaylandDisplay
        // 在平台插件初始化时已 roundtrip 拿到全部 global，构造完成后 isActive()
        // 即可靠。
        initialize();
    }

    ~TreelandWineWindowManager() override
    {
        if (isInitialized()) {
            destroy();
        }
    }
};

Q_GLOBAL_STATIC(TreelandWineWindowManager, treelandWineWindowManager)

/// 仅在 wayland 平台上返回激活的 manager；X11 等环境绝不触碰该全局，
/// 避免 QWaylandClientExtension 误建 QtWayland 集成对象
static TreelandWineWindowManager *managerIfAvailable()
{
    if (!QGuiApplication::instance()
            || !QGuiApplication::platformName().contains(QLatin1String("wayland"))) {
        return nullptr;
    }

    auto *manager = treelandWineWindowManager();
    return (manager && manager->isActive()) ? manager : nullptr;
}

/**
 * @brief 单个窗口的置顶代理，生命周期跟随所属窗口
 *
 * 协议时序：get_window_control 需要 xdg_toplevel 已存在（wl_surface 创建
 * 时机早于 xdg_toplevel），因此控制对象在窗口首次 Expose（收到过 configure，
 * toplevel 必定已创建）或首次 applyStayOnTop 时惰性创建。
 *
 * 迷你模式切换会带 X11BypassWindowManagerHint 做 setWindowFlags，xdg_toplevel
 * 可能被销毁重建，wine control 随之失活；这里在 Expose/Show 时比对
 * nativeResourceForWindow("xdg_toplevel") 指针，发现变化即重建控制对象并按
 * 上次请求的置顶状态回设，保证窗口重建后置顶状态不丢。
 */
class TreelandShellSurfaceObject : public QObject
{
public:
    static TreelandShellSurfaceObject *attach(QWidget *widget);

    ~TreelandShellSurfaceObject() override;

    bool managed() const { return m_control != nullptr; }
    bool stayOnTop() const { return m_stayOnTop; }
    bool applyStayOnTop(bool on);

private:
    explicit TreelandShellSurfaceObject(QWidget *widget);

    bool eventFilter(QObject *watched, QEvent *event) override;
    void hookWindow(QWindow *window);
    bool tryCreateControl();
    void releaseControl();
    struct ::wl_surface *nativeSurface() const;
    struct ::xdg_toplevel *nativeToplevel() const;

    QPointer<QWindow> m_window;
    std::unique_ptr<QtWayland::treeland_wine_window_control_v1> m_control;
    void *m_toplevelProxy = nullptr;
    bool m_stayOnTop = false;
};

// window -> proxy，窗口销毁时由 QPointer 自动失效
static QHash<QWindow *, QPointer<TreelandShellSurfaceObject>> s_windowProxies;

TreelandShellSurfaceObject::TreelandShellSurfaceObject(QWidget *widget)
    : QObject(widget)
{
    // 先挂在 widget 上等 WinIdChange；若 QWindow 已存在则直接挂 QWindow
    qCDebug(appMovie) << "[treeland] object created for widget"
                      << widget << "windowHandle="
                      << widget->windowHandle();
    widget->installEventFilter(this);
    if (auto *window = widget->windowHandle())
        hookWindow(window);
}

TreelandShellSurfaceObject::~TreelandShellSurfaceObject()
{
    if (m_window) {
        // 窗口还在时先销毁控制对象；窗口已销毁的场景由
        // SurfaceAboutToBeDestroyed 事件处理过
        releaseControl();
        if (m_window)
            m_window->removeEventFilter(this);
    }
}

TreelandShellSurfaceObject *TreelandShellSurfaceObject::attach(QWidget *widget)
{
    if (!widget)
        return nullptr;

    // 按所属 widget 去重：同一 xdg_toplevel 重复 get_window_control 会触发
    // 服务端 error_toplevel_already_controlled 协议错误（客户端被踢），
    // 绝不能重复挂接（窗口重建、构造函数重入等场景都靠这里兜底）
    for (auto it = s_windowProxies.begin(); it != s_windowProxies.end(); ++it) {
        if (it.value() && it.value()->parent() == widget)
            return it.value();
    }

    return new TreelandShellSurfaceObject(widget);
}

bool TreelandShellSurfaceObject::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::WinIdChange: {
        // QWidget 的平台窗口创建完成，挂接真正的 QWindow
        auto *widget = qobject_cast<QWidget *>(watched);
        qCDebug(appMovie) << "[treeland] WinIdChange on" << widget;
        if (widget && widget->windowHandle())
            hookWindow(widget->windowHandle());
        break;
    }
    case QEvent::PlatformSurface: {
        if (watched != m_window)
            break;
        auto surfaceEvent = static_cast<QPlatformSurfaceEvent *>(event);
        if (surfaceEvent->surfaceEventType() == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed) {
            // wl_surface/xgd_toplevel 即将销毁，控制对象随之失活
            releaseControl();
        }
        break;
    }
    case QEvent::Expose:
    case QEvent::Show: {
        if (watched != m_window)
            break;
        // 首次显示/每次重现时确保控制对象存在；xdg_toplevel 若被销毁重建
        // （迷你模式切换），这里按指针变化重建并回设置顶状态
        qCDebug(appMovie) << "[treeland] window event" << event->type()
                          << "-> tryCreateControl";
        tryCreateControl();
        break;
    }
    default:
        break;
    }

    return false;
}

void TreelandShellSurfaceObject::hookWindow(QWindow *window)
{
    if (!window || window == m_window)
        return;

    qCDebug(appMovie) << "[treeland] hookWindow" << window;
    m_window = window;
    window->installEventFilter(this);
    connect(window, &QObject::destroyed, this, [window] {
        s_windowProxies.remove(window);
    });
    s_windowProxies.insert(window, this);
}

struct ::wl_surface *TreelandShellSurfaceObject::nativeSurface() const
{
    QWindow *window = m_window.data();
    if (!window || !window->handle())
        return nullptr;

    auto *nativeInterface = QGuiApplication::platformNativeInterface();
    if (!nativeInterface)
        return nullptr;

    return static_cast<struct ::wl_surface *>(
        nativeInterface->nativeResourceForWindow(QByteArrayLiteral("surface"), window));
}

struct ::xdg_toplevel *TreelandShellSurfaceObject::nativeToplevel() const
{
    QWindow *window = m_window.data();
    if (!window || !window->handle())
        return nullptr;

    auto *nativeInterface = QGuiApplication::platformNativeInterface();
    if (!nativeInterface)
        return nullptr;

    return static_cast<struct ::xdg_toplevel *>(
        nativeInterface->nativeResourceForWindow(QByteArrayLiteral("xdg_toplevel"), window));
}

bool TreelandShellSurfaceObject::tryCreateControl()
{
    if (m_control && m_toplevelProxy == nativeToplevel())
        return true;

    qCDebug(appMovie) << "[treeland] tryCreateControl, have_control="
                      << (m_control != nullptr);

    // toplevel 变化（重建）或尚无控制对象：旧的先销毁（失活对象销毁安全）
    releaseControl();

    auto *manager = managerIfAvailable();
    if (!manager) {
        qCWarning(appMovie) << "[treeland] wine window manager not active, skip control";
        return false;
    }

    if (!nativeSurface()) {
        qCDebug(appMovie) << "[treeland] no native surface yet";
        return false;
    }

    struct ::xdg_toplevel *toplevel = nativeToplevel();
    if (!toplevel) {
        // xdg_toplevel 尚未创建（窗口未显示完），等下一次 Expose/Show 重试
        qCDebug(appMovie) << "[treeland] no xdg_toplevel yet";
        return false;
    }

    struct ::treeland_wine_window_control_v1 *control =
        manager->get_window_control(toplevel);
    if (!control) {
        qCWarning(appMovie) << "[treeland] get_window_control failed";
        return false;
    }

    m_control.reset(new QtWayland::treeland_wine_window_control_v1(control));
    m_toplevelProxy = toplevel;
    qCInfo(appMovie) << "[treeland] wine window control created for window" << m_window.data();

    // 恢复期望置顶状态：窗口重建后合成器按普通窗口处理，必须按上次
    // 请求的状态显式回设，避免重建后与应用的置顶状态不一致
    if (m_stayOnTop) {
        m_control->set_z_order(
            QtWayland::treeland_wine_window_control_v1::z_order_op_hwnd_topmost, 0);
        qCInfo(appMovie) << "[treeland] re-apply stay on top after window rebuild";
    }
    return true;
}

void TreelandShellSurfaceObject::releaseControl()
{
    if (!m_control)
        return;

    m_control->destroy();
    m_control.reset();
    m_toplevelProxy = nullptr;
}

bool TreelandShellSurfaceObject::applyStayOnTop(bool on)
{
    if (!tryCreateControl()) {
        qCWarning(appMovie) << "[treeland] no wine window control, cannot apply stay on top";
        return false;
    }

    m_stayOnTop = on;
    m_control->set_z_order(on ? QtWayland::treeland_wine_window_control_v1::z_order_op_hwnd_topmost
                              : QtWayland::treeland_wine_window_control_v1::z_order_op_hwnd_notopmost,
                          0);
    qCInfo(appMovie) << "[treeland] stay on top" << on << "for window" << m_window.data();
    return true;
}

bool TreelandShellSurfaceProxy::isTreelandActive()
{
    return managerIfAvailable() != nullptr;
}

void TreelandShellSurfaceProxy::attachWindow(QWidget *widget)
{
    if (!widget)
        return;

    const QString platform = QGuiApplication::instance()
        ? QGuiApplication::platformName()
        : QStringLiteral("<no-app>");
    if (!managerIfAvailable()) {
        qCDebug(appMovie) << "[treeland] manager inactive, skip attach"
                          << "platform=" << platform
                          << "manager=" << static_cast<void *>(treelandWineWindowManager())
                          << "active="
                          << (treelandWineWindowManager()
                                  ? treelandWineWindowManager()->isActive()
                                  : false);
        return;
    }

    qCDebug(appMovie) << "[treeland] attachWindow proceeding, platform="
                      << platform;
    TreelandShellSurfaceObject::attach(widget);
}

bool TreelandShellSurfaceProxy::isWindowManaged(QWindow *window)
{
    if (!window)
        return false;

    auto it = s_windowProxies.find(window);
    return it != s_windowProxies.end() && !it.value().isNull() && it.value()->managed();
}

bool TreelandShellSurfaceProxy::stayOnTopOf(QWindow *window)
{
    if (!window)
        return false;

    auto it = s_windowProxies.find(window);
    if (it == s_windowProxies.end() || it.value().isNull())
        return false;

    return it.value()->stayOnTop();
}

bool TreelandShellSurfaceProxy::setStayOnTop(QWindow *window, bool on)
{
    if (!window)
        return false;

    auto it = s_windowProxies.find(window);
    if (it == s_windowProxies.end() || it.value().isNull())
        return false;

    return it.value()->applyStayOnTop(on);
}

} // namespace dmr
