// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "treeland_shell_surface_proxy.h"
#include "ddlog.h"
#include "qwayland-treeland-dde-shell-unstable-v2.h"

#include <QEvent>
#include <QGuiApplication>
#include <QPlatformSurfaceEvent>
#include <QPointer>
#include <QWindow>
#include <QtWaylandClient/QWaylandClientExtensionTemplate>
#include <QtWaylandClient/private/qwaylandwindow_p.h>

#include <memory>

namespace dmr {

/**
 * 全局 manager 绑定，参考 treeland examples/test_dde_shell 的
 * DDEShellManageV2 模式。QWaylandClientExtensionTemplate::initialize()
 * 默认经事件循环排队执行，这里构造时同步调用，保证 attachWindow 阶段
 * 即完成绑定（平台初轮 roundtrip 早已完成，全局表已就绪）。
 */
class TreelandShellManager
    : public QWaylandClientExtensionTemplate<TreelandShellManager>
    , public QtWayland::treeland_dde_shell_manager_v2
{
public:
    TreelandShellManager()
        : QWaylandClientExtensionTemplate<TreelandShellManager>(1)
    {
        initialize();
    }

    ~TreelandShellManager() override
    {
        if (isInitialized()) {
            destroy();
        }
    }
};

Q_GLOBAL_STATIC(TreelandShellManager, treelandShellManager)

static TreelandShellManager *managerIfAvailable()
{
    if (qGuiApp->platformName() != QLatin1String("wayland")) {
        return nullptr;
    }

    auto *manager = treelandShellManager();
    if (!manager || !manager->isActive()) {
        return nullptr;
    }
    return manager;
}

class TreelandShellSurfaceObject;

// QWindow -> 代理对象；按窗口去重，同一 wl_surface 绝不重复 get_shell_surface
static QHash<QWindow *, TreelandShellSurfaceObject *> s_windowProxies;

/**
 * 单窗口 shell surface 代理：持有 treeland_dde_shell_surface_v2 对象，
 * 负责在正确时机创建/销毁并切换角色。
 *
 * 生命周期关键点：
 *  - get_shell_surface 必须先于 xdg_toplevel 到达合成器（见类注释），
 *    由 QEvent::Show（show_sys 之前）与 QWaylandWindow::surfaceCreated
 *    （wl_surface 重建时先于 createShellSurface）两条钩子覆盖；
 *  - 对同一 wl_surface 只能 get_shell_surface 一次，重复请求会被
 *    合成器以 error_already_shell_surface 踢掉，创建前必须去重；
 *  - v2 协议要求 shell surface 先于其 wl_surface 销毁，否则合成器
 *    在 wl_surface 消亡时清理关联对象，客户端随后销毁旧对象会触发
 *    "invalid object"。由 QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed
 *    与 QWaylandWindow::surfaceDestroyed 双钩子覆盖。
 */
class TreelandShellSurfaceObject : public QObject
{
public:
    static TreelandShellSurfaceObject *attach(QWidget *widget)
    {
        if (!widget) {
            return nullptr;
        }

        auto *obj = new TreelandShellSurfaceObject(widget);
        obj->hookWindow(widget->windowHandle());
        return obj;
    }

    ~TreelandShellSurfaceObject() override
    {
        releaseShellSurface();
        if (m_window) {
            m_window->removeEventFilter(this);
        }
        if (m_widget) {
            m_widget->removeEventFilter(this);
        }
        if (m_window) {
            s_windowProxies.remove(m_window.data());
        }
    }

    bool managed() const { return m_shellSurface != nullptr; }
    bool stayOnTop() const { return m_stayOnTop; }

    bool applyStayOnTop(bool on)
    {
        if (!m_shellSurface) {
            qCWarning(appMovie) << "[treeland] no shell surface, cannot apply stay on top";
            return false;
        }

        if (m_stayOnTop == on) {
            return true;
        }

        m_shellSurface->set_role(on ? QtWayland::treeland_dde_shell_surface_v2::role_overlay
                                    : QtWayland::treeland_dde_shell_surface_v2::role_normal);
        m_stayOnTop = on;
        qCInfo(appMovie) << "[treeland] stay on top" << on << "for window" << m_window.data();
        return true;
    }

private:
    explicit TreelandShellSurfaceObject(QWidget *widget)
        : m_widget(widget)
    {
        widget->installEventFilter(this);
        if (auto *manager = managerIfAvailable()) {
            // manager 晚于 attach 激活时补建 shell surface
            connect(manager, &TreelandShellManager::activeChanged, this, [this] {
                tryCreateShellSurface();
            });
        }
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_widget) {
            // Show 事件在 setVisible 内先于 show_sys()/initWindow() 投递，
            // 此时 wl_surface 已创建、xdg_toplevel 尚未创建，是建立
            // shell surface 关联的最佳时机（含迷你模式窗口重建后的再次显示）
            if (event->type() == QEvent::Show) {
                if (!m_window && m_widget->windowHandle()) {
                    hookWindow(m_widget->windowHandle());
                }
                tryCreateShellSurface();
            } else if (event->type() == QEvent::WinIdChange) {
                hookWindow(m_widget->windowHandle());
            }
        } else if (watched == m_window && event->type() == QEvent::PlatformSurface) {
            auto *surfaceEvent = static_cast<QPlatformSurfaceEvent *>(event);
            if (surfaceEvent->surfaceEventType() == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed) {
                // 平台窗口销毁前必须先销毁 shell surface（协议时序要求）
                releaseShellSurface();
            }
        }
        return QObject::eventFilter(watched, event);
    }

    void hookWindow(QWindow *window)
    {
        if (!window || window == m_window.data()) {
            return;
        }

        if (m_window) {
            m_window->removeEventFilter(this);
            s_windowProxies.remove(m_window.data());
        }

        m_window = window;
        window->installEventFilter(this);
        s_windowProxies.insert(window, this);

        auto *waylandWindow = dynamic_cast<QtWaylandClient::QWaylandWindow *>(
            window->handle());
        if (waylandWindow) {
            // wl_surface 重建（reset 后再初始化）时，surfaceCreated 在
            // initWindow 内先于 createShellSurface 发出，同步补建
            connect(waylandWindow, &QtWaylandClient::QWaylandWindow::surfaceCreated,
                    this, &TreelandShellSurfaceObject::tryCreateShellSurface, Qt::UniqueConnection);
            connect(waylandWindow, &QtWaylandClient::QWaylandWindow::surfaceDestroyed,
                    this, &TreelandShellSurfaceObject::releaseShellSurface, Qt::UniqueConnection);
        }

        tryCreateShellSurface();
    }

    void tryCreateShellSurface()
    {
        if (m_shellSurface) {
            return;   // 同一 wl_surface 只能 get_shell_surface 一次
        }

        auto *manager = managerIfAvailable();
        if (!manager) {
            return;
        }

        struct ::wl_surface *surface = nativeSurface();
        if (!surface) {
            return;
        }

        m_shellSurface.reset(new QtWayland::treeland_dde_shell_surface_v2(manager->get_shell_surface(surface)));
        if (!m_shellSurface) {
            qCWarning(appMovie) << "[treeland] get_shell_surface failed for" << m_window.data();
            return;
        }

        // 协议默认角色是 overlay，这里显式恢复普通层级，置顶状态由
        // setStayOnTop 动态切换
        m_shellSurface->set_role(m_stayOnTop ? QtWayland::treeland_dde_shell_surface_v2::role_overlay
                                             : QtWayland::treeland_dde_shell_surface_v2::role_normal);
        qCInfo(appMovie) << "[treeland] shell surface created for" << m_window.data()
                         << "stay on top =" << m_stayOnTop;
    }

    void releaseShellSurface()
    {
        if (!m_shellSurface) {
            return;
        }

        // 必须先于 wl_surface 销毁（协议时序要求）
        m_shellSurface.reset();
        qCInfo(appMovie) << "[treeland] shell surface released for" << m_window.data();
    }

    struct ::wl_surface *nativeSurface() const
    {
        if (!m_window) {
            return nullptr;
        }

        auto *waylandWindow = dynamic_cast<QtWaylandClient::QWaylandWindow *>(m_window->handle());
        if (!waylandWindow) {
            return nullptr;
        }

        return waylandWindow->surface();
    }

    QPointer<QWidget> m_widget;
    QPointer<QWindow> m_window;
    std::unique_ptr<QtWayland::treeland_dde_shell_surface_v2> m_shellSurface;
    bool m_stayOnTop = false;
};

bool TreelandShellSurfaceProxy::isTreelandActive()
{
    return managerIfAvailable() != nullptr;
}

void TreelandShellSurfaceProxy::attachWindow(QWidget *widget)
{
    if (!widget) {
        return;
    }

    // manager 未激活时也挂接：等待 activeChanged 后自动补建 shell surface
    TreelandShellSurfaceObject::attach(widget);
}

bool TreelandShellSurfaceProxy::isWindowManaged(QWindow *window)
{
    if (!window) {
        return false;
    }

    auto it = s_windowProxies.find(window);
    return it != s_windowProxies.end() && it.value() && it.value()->managed();
}

bool TreelandShellSurfaceProxy::stayOnTopOf(QWindow *window)
{
    if (!window) {
        return false;
    }

    auto it = s_windowProxies.find(window);
    if (it == s_windowProxies.end() || !it.value()) {
        return false;
    }

    return it.value()->stayOnTop();
}

bool TreelandShellSurfaceProxy::setStayOnTop(QWindow *window, bool on)
{
    if (!window) {
        return false;
    }

    auto it = s_windowProxies.find(window);
    if (it == s_windowProxies.end() || !it.value()) {
        return false;
    }

    return it.value()->applyStayOnTop(on);
}

} // namespace dmr
