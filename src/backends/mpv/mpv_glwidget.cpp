// Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
// SPDX-FileCopyrightText: 2022 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "config.h"

#include "mpv_proxy.h"
#include "mpv_glwidget.h"
#include "sysutils.h"

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
#include <QX11Info>
#else
#include <QtGui/private/qtx11extras_p.h>
#include <QtGui/private/qtguiglobal_p.h>
#endif

#include <QLibrary>
#include <QPainterPath>

#include <dthememanager.h>
#include <DApplication>
#include <QDBusInterface>
//#include <wayland-client.h>
//#include "../../window/qplatformnativeinterface.h"
//qpa/qplatformnativeinterface.h
#if defined(_WIN32) && !defined(_WIN32_WCE) && !defined(__SCITECH_SNAP__)
/* Win32 but not WinCE */
#   define KHRONOS_APIENTRY __stdcall
#else
#   define KHRONOS_APIENTRY
#endif
DWIDGET_USE_NAMESPACE
#ifndef EGLAPIENTRY
#define EGLAPIENTRY  KHRONOS_APIENTRY
#endif

#ifndef GLAPIENTRY
#define GLAPIENTRY
#endif

static const char *vs_blend = R"(
#ifdef GL_ES
// Set default precision to medium
precision mediump int;
precision mediump float;
#endif
attribute vec2 position;
attribute vec2 vTexCoord;

varying vec2 texCoord;

void main() {
    gl_Position = vec4(position, 0.0, 1.0);
    texCoord = vTexCoord;
}
)";

static const char* fs_blend = R"(
#ifdef GL_ES
// Set default precision to medium
precision mediump int;
precision mediump float;
#endif
varying vec2 texCoord;

uniform sampler2D movie;

void main() {
     gl_FragColor = texture2D(movie, texCoord); 
}
)";

static const char* fs_blend_wayland = R"(
#ifdef GL_ES
// Set default precision to medium
precision mediump int;
precision mediump float;
#endif
varying vec2 texCoord;

uniform sampler2D movie;

void main() {
     gl_FragColor = texture2D(movie, texCoord);
}
)";

static const char* vs_blend_corner = R"(
#ifdef GL_ES
// Set default precision to medium
precision mediump int;
precision mediump float;
#endif
attribute vec2 position;
attribute vec2 maskTexCoord;
attribute vec2 vTexCoord;

varying vec2 maskCoord;
varying vec2 texCoord;

void main() {
    gl_Position = vec4(position, 0.0, 1.0);
    texCoord = vTexCoord;
    maskCoord = maskTexCoord;
}
)";

static const char* fs_blend_corner = R"(
#ifdef GL_ES
// Set default precision to medium
precision mediump int;
precision mediump float;
#endif
varying vec2 maskCoord;
varying vec2 texCoord;

uniform sampler2D movie;
uniform sampler2D mask;

void main() {
     gl_FragColor = texture2D(movie, texCoord) * texture2D(mask, maskCoord).a; 
}
)";

static const char* fs_blend_corner_wayland = R"(
#ifdef GL_ES
// Set default precision to medium
precision mediump int;
precision mediump float;
#endif
varying vec2 maskCoord;
varying vec2 texCoord;

uniform sampler2D movie;
uniform sampler2D mask;

void main() {
     gl_FragColor = texture2D(movie, texCoord) * texture2D(mask, maskCoord).a;
}
)";

static const char* vs_code = R"(
#ifdef GL_ES
// Set default precision to medium
precision mediump int;
precision mediump float;
#endif

attribute vec2 position;
attribute vec2 vTexCoord;

varying vec2 texCoord;

void main() {
    gl_Position = vec4(position, 0.0, 1.0);
    texCoord = vTexCoord;
}
)";

static const char* fs_code = R"(
#ifdef GL_ES
// Set default precision to medium
precision mediump int;
precision mediump float;
#endif
varying vec2 texCoord;

uniform sampler2D sampler;
uniform vec4 bg;

void main() {
    vec4 s = texture2D(sampler, texCoord);
    gl_FragColor = vec4(s.rgb * s.a + bg.rgb * (1.0 - s.a), 1.0);
}
)";

static const char* fs_code_wayland = R"(
#ifdef GL_ES
// Set default precision to medium
precision mediump int;
precision mediump float;
#endif
varying vec2 texCoord;

uniform sampler2D sampler;
uniform vec4 bg;

void main() {
    vec4 s = texture2D(sampler, texCoord);
    gl_FragColor = vec4(s.rgb * s.a + bg.rgb * (1.0 - s.a), 1.0);
}
)";

static const char* fs_corner_code = R"(
#ifdef GL_ES
// Set default precision to medium
precision mediump int;
precision mediump float;
#endif
varying vec2 texCoord;

uniform sampler2D corner;
uniform vec4 bg;

void main() {
    vec4 s = texture2D(corner, texCoord);
    gl_FragColor = s.a * bg;
}
)";

static const char* fs_corner_code_wayland = R"(
#ifdef GL_ES
// Set default precision to medium
precision mediump int;
precision mediump float;
#endif
varying vec2 texCoord;

uniform sampler2D corner;
uniform vec4 bg;

void main() {
    vec4 s = texture2D(corner, texCoord);
    gl_FragColor = s.a * bg;
}
)";

namespace dmr {
    static void* GLAPIENTRY glMPGetNativeDisplay(const char* name) {
        qWarning() << __func__ << name;
        if (!strcmp(name, "x11") || !strcmp(name, "X11")) {
            qDebug() << "DEBUG: X11 display detected.";
            return static_cast<void*>(QX11Info::display());
        }
        qDebug() << "DEBUG: Exiting glMPGetNativeDisplay. No native display found or handled.";
        return nullptr;
    }

    static void* EGLAPIENTRY glMPGetNativeDisplay_EGL(const char* name) {
        qWarning() << __func__ << name;
        qDebug() << "DEBUG: Entering glMPGetNativeDisplay_EGL. Name:" << name;
        //QPlatformNativeInterface* native = QGuiApplication::platformNativeInterface();
        //struct wl_display * wl_dpy = (struct wl_display*) (native->nativeResourceForWindow("display",NULL));
        if (!strcmp(name, "wayland")) {
            qDebug() << "DEBUG: Wayland display detected.";
            //return (void*)wl_dpy;
            return nullptr;
        }
        qDebug() << "DEBUG: Exiting glMPGetNativeDisplay_EGL. No Wayland display found or handled.";
        return nullptr;
    }

    static void *get_proc_address(void *pCtx, const char *pName) {
        qDebug() << "DEBUG: Entering get_proc_address. Context:" << pCtx << ", Name:" << pName;
        Q_UNUSED(pCtx);
        QOpenGLContext *pGLCtx = QOpenGLContext::currentContext();
        if (!pGLCtx) {
            qWarning() << "WARNING: OpenGL context is null, cannot get proc address.";
            return nullptr;
        }

        if (!strcmp(pName, "glMPGetNativeDisplay")) {
            if(utils::check_wayland_env()){
                qDebug() << "DEBUG: Resolved glMPGetNativeDisplay to EGL version for Wayland.";
                return (void*)glMPGetNativeDisplay_EGL;
            }else{
                qDebug() << "DEBUG: Resolved glMPGetNativeDisplay to X11 version for non-Wayland.";
                return (void*)glMPGetNativeDisplay;
            }
        }
        qDebug() << "DEBUG: Exiting get_proc_address. Resolved address for:" << pName;
        return reinterpret_cast<void*>(pGLCtx->getProcAddress(QByteArray(pName)));
    }

    static void gl_update_callback(void *pCtx)
    {
        qDebug() << "DEBUG: Entering gl_update_callback.";
        MpvGLWidget *pWid = static_cast<MpvGLWidget*>(pCtx);
        QMetaObject::invokeMethod(pWid, "onNewFrame");
        qDebug() << "DEBUG: Exiting gl_update_callback.";
    }

    //cppcheck 被QMetaObject::invokeMethod使用
    void MpvGLWidget::onNewFrame()
    {
        qDebug() << "DEBUG: Entering onNewFrame.";
        if (window()->isMinimized()) {
            qDebug() << "DEBUG: Window is minimized. Performing minimal update.";
            makeCurrent();
            paintGL();
            context()->swapBuffers(context()->surface());
            doneCurrent();
        } else {
            qDebug() << "DEBUG: Window is not minimized. Performing full render context update.";
            m_renderContextUpdate(m_pRenderCtx);
            update();
        }
        qDebug() << "DEBUG: Exiting onNewFrame.";
    }

    void MpvGLWidget::onFrameSwapped()
    {
        qDebug() << "DEBUG: Entering onFrameSwapped.";
        //qInfo() << "frame swapped";

        if(!m_context_report) {
            qDebug() << "DEBUG: m_context_report is null. Exiting onFrameSwapped.";
            return;
        }
        m_context_report(m_pRenderCtx);
        qDebug() << "DEBUG: Exiting onFrameSwapped.";
    }

    MpvGLWidget::MpvGLWidget(QWidget *parent, MpvHandle h)
        :QOpenGLWidget(parent), m_handle(h) {
        qDebug() << "DEBUG: Entering MpvGLWidget constructor.";

        initMember();

        initMpvFuns();

        setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);

        connect(this, &QOpenGLWidget::frameSwapped, 
                this, &MpvGLWidget::onFrameSwapped, Qt::DirectConnection);
        qDebug() << "DEBUG: Exiting MpvGLWidget constructor.";
    }

    MpvGLWidget::~MpvGLWidget() 
    {
        qDebug() << "DEBUG: Entering MpvGLWidget destructor.";
        makeCurrent();
        if (m_pDarkTex) {
            qDebug() << "DEBUG: Destroying dark texture.";
            m_pDarkTex->destroy();
            delete m_pDarkTex;
            m_pDarkTex = nullptr;
        }
        if (m_pLightTex) {
            qDebug() << "DEBUG: Destroying light texture.";
            m_pLightTex->destroy();
            delete m_pLightTex;
            m_pLightTex = nullptr;
        }

        for (auto mask: m_pCornerMasks) {
            if (mask) {
                qDebug() << "DEBUG: Destroying corner mask.";
                mask->destroy();
            }
        }

        m_vbo.destroy();
        qDebug() << "DEBUG: VBO destroyed.";

        for (int i = 0; i < 4; i++) {
            m_vboCorners[i].destroy();
            qDebug() << "DEBUG: Destroying VBO corner at index:" << i;

            ///指针数组m_pCornerMasks在mpv_glwidget.cpp 476行申请内存后未释放///
            delete m_pCornerMasks[i];
            m_pCornerMasks[i] = nullptr;
            qDebug() << "DEBUG: Deleted corner mask pointer at index:" << i;
        }

        m_vao.destroy();
        qDebug() << "DEBUG: VAO destroyed.";
        m_vaoBlend.destroy();
        qDebug() << "DEBUG: VAO blend destroyed.";
        m_vaoCorner.destroy();
        qDebug() << "DEBUG: VAO corner destroyed.";

        delete m_pGlProgBlend;
        m_pGlProgBlend = nullptr;
        qDebug() << "DEBUG: GL program blend deleted.";

        delete m_pGlProgBlendCorners;
        m_pGlProgBlendCorners = nullptr;;
        qDebug() << "DEBUG: GL program blend corners deleted.";

        delete m_pGlProg;
        m_pGlProg = nullptr;
        qDebug() << "DEBUG: GL program deleted.";

        delete m_pGlProgCorner;
        m_pGlProgCorner = nullptr;
        qDebug() << "DEBUG: GL program corner deleted.";

        if (m_pFbo) {
            qDebug() << "DEBUG: Deleting FBO.";
            delete m_pFbo;
            m_pFbo = nullptr;
        }
        //add by heyi
        if (m_pRenderCtx) {
            qDebug() << "DEBUG: Calling render context callback before free.";
            m_callback(m_pRenderCtx, nullptr, nullptr);
        }
        // Until this call is done, we need to make sure the player remains
        // alive. This is done implicitly with the mpv::qt::Handle instance
        // in this class.
        m_renderContex(m_pRenderCtx);
        //mpv_render_context_free(m_pRenderCtx);
        doneCurrent();
        qDebug() << "DEBUG: Exiting MpvGLWidget destructor.";
    }

    void MpvGLWidget::setupBlendPipe()
    {
        qDebug() << "DEBUG: Entering setupBlendPipe.";
        updateMovieFbo();

        m_vaoBlend.create();
        m_vaoBlend.bind();
        updateVboBlend();

        m_pGlProgBlend = new QOpenGLShaderProgram();
        m_pGlProgBlend->addShaderFromSourceCode(QOpenGLShader::Vertex, vs_blend);
        if(utils::check_wayland_env()){
            qDebug() << "DEBUG: Setting up blend pipe for Wayland environment (fs_blend_wayland).";
            m_pGlProgBlend->addShaderFromSourceCode(QOpenGLShader::Fragment, fs_blend_wayland);
        }else {
            qDebug() << "DEBUG: Setting up blend pipe for non-Wayland environment (fs_blend).";
            m_pGlProgBlend->addShaderFromSourceCode(QOpenGLShader::Fragment, fs_blend);
        }

        if (!m_pGlProgBlend->link()) {
            qInfo() << "link failed";
        }
        m_pGlProgBlend->bind();
        m_vboBlend.bind();

        int vLocBlend = m_pGlProgBlend->attributeLocation("position");
        int coordLocBlend = m_pGlProgBlend->attributeLocation("vTexCoord");
        m_pGlProgBlend->enableAttributeArray(vLocBlend);
        m_pGlProgBlend->setAttributeBuffer(vLocBlend, GL_FLOAT, 0, 2, 6*sizeof(GLfloat));
        m_pGlProgBlend->enableAttributeArray(coordLocBlend);
        m_pGlProgBlend->setAttributeBuffer(coordLocBlend, GL_FLOAT, 2*sizeof(GLfloat), 2, 6*sizeof(GLfloat));
        m_pGlProgBlend->setUniformValue("movie", 0);
        m_pGlProgBlend->release();
        m_vaoBlend.release();
        qDebug() << "DEBUG: Blend shader program released and VAO/VBO blend released.";

        m_pGlProgBlendCorners = new QOpenGLShaderProgram();
        m_pGlProgBlendCorners->addShaderFromSourceCode(QOpenGLShader::Vertex, vs_blend_corner);
        if(utils::check_wayland_env()){
            qDebug() << "DEBUG: Setting up blend pipe corners for Wayland environment (fs_blend_corner_wayland).";
            m_pGlProgBlendCorners->addShaderFromSourceCode(QOpenGLShader::Fragment, fs_blend_corner_wayland);
        }else{
            qDebug() << "DEBUG: Setting up blend pipe corners for non-Wayland environment (fs_blend_corner).";
            m_pGlProgBlendCorners->addShaderFromSourceCode(QOpenGLShader::Fragment, fs_blend_corner);
        }

        if (!m_pGlProgBlendCorners->link()) {
            qInfo() << "link failed";
        }
        qDebug() << "DEBUG: Exiting setupBlendPipe.";
    }

    void MpvGLWidget::setupIdlePipe()
    {
        qDebug() << "DEBUG: Entering setupIdlePipe.";
        m_vao.create();
        m_vao.bind();
        qDebug() << "DEBUG: VAO created and bound for idle pipe.";

        m_pDarkTex = new QOpenGLTexture(m_imgBgDark, QOpenGLTexture::DontGenerateMipMaps);
        m_pDarkTex->setMinificationFilter(QOpenGLTexture::Linear);
        m_pLightTex = new QOpenGLTexture(m_imgBgLight, QOpenGLTexture::DontGenerateMipMaps);
        m_pLightTex->setMinificationFilter(QOpenGLTexture::Linear);
        qDebug() << "DEBUG: Dark and light splash textures created.";

        updateVbo();
        m_vbo.bind();
        qDebug() << "DEBUG: VBO updated and bound for idle pipe.";

        m_pGlProg = new QOpenGLShaderProgram();
        m_pGlProg->addShaderFromSourceCode(QOpenGLShader::Vertex, vs_code);
        if(utils::check_wayland_env()){
            qDebug() << "DEBUG: Setting up idle pipe for Wayland environment (fs_code_wayland).";
            m_pGlProg->addShaderFromSourceCode(QOpenGLShader::Fragment, fs_code_wayland);
        }else{
            qDebug() << "DEBUG: Setting up idle pipe for non-Wayland environment (fs_code).";
            m_pGlProg->addShaderFromSourceCode(QOpenGLShader::Fragment, fs_code);
        }

        if (!m_pGlProg->link()) {
            qInfo() << "link failed";
        }
        m_pGlProg->bind();

        int vertexLoc = m_pGlProg->attributeLocation("position");
        int coordLoc = m_pGlProg->attributeLocation("vTexCoord");
        m_pGlProg->enableAttributeArray(vertexLoc);
        m_pGlProg->setAttributeBuffer(vertexLoc, GL_FLOAT, 0, 2, 4*sizeof(GLfloat));
        m_pGlProg->enableAttributeArray(coordLoc);
        m_pGlProg->setAttributeBuffer(coordLoc, GL_FLOAT, 2*sizeof(GLfloat), 2, 4*sizeof(GLfloat));
        m_pGlProg->setUniformValue("sampler", 0);
        m_pGlProg->release();
        m_vao.release();
        qDebug() << "DEBUG: Idle shader program released and VAO/VBO released.";

        {
            qDebug() << "DEBUG: Entering idle pipe corner setup block.";
            m_vaoCorner.create();
            m_vaoCorner.bind();
            qDebug() << "DEBUG: VAO created and bound for idle pipe corners.";

            // setting up corners
            updateVboCorners();
            updateCornerMasks();
            qDebug() << "DEBUG: VBO corners and corner masks updated.";

            m_pGlProgCorner = new QOpenGLShaderProgram();
            m_pGlProgCorner->addShaderFromSourceCode(QOpenGLShader::Vertex, vs_code);
            if(utils::check_wayland_env()){
                qDebug() << "DEBUG: Setting up idle pipe corners for Wayland environment (fs_corner_code_wayland).";
                m_pGlProgCorner->addShaderFromSourceCode(QOpenGLShader::Fragment, fs_corner_code_wayland);
            }else{
                qDebug() << "DEBUG: Setting up idle pipe corners for non-Wayland environment (fs_corner_code).";
                m_pGlProgCorner->addShaderFromSourceCode(QOpenGLShader::Fragment, fs_corner_code);
            }

            if (!m_pGlProgCorner->link()) {
                qInfo() << "link failed";
            }
            m_vaoCorner.release();
            qDebug() << "DEBUG: Idle corner shader program released and VAO released.";
        }
        qDebug() << "DEBUG: Exiting setupIdlePipe.";
    }

    void MpvGLWidget::prepareSplashImages()
    {
        qDebug() << "DEBUG: Entering prepareSplashImages.";
        QPixmap pixmap;
        QImage img=utils::LoadHiDPIImage(":/resources/icons/dark/init-splash-bac.svg");
        pixmap=pixmap.fromImage(img);
        qDebug() << "DEBUG: Loaded dark splash background image.";

        QPixmap pixmap2;
        QImage img1=QIcon::fromTheme("deepin-movie").pixmap(130, 130).toImage();
        pixmap2=pixmap2.fromImage(img1);
        qDebug() << "DEBUG: Loaded deepin-movie icon for splash.";

        QPainter painter(&pixmap);
        painter.drawPixmap(102,126,pixmap2); // 参数102 126，在界面上保持居中
        m_imgBgDark=pixmap.toImage();
        m_imgBgDark.setDevicePixelRatio(qApp->devicePixelRatio());
        qDebug() << "DEBUG: Dark splash image prepared.";

        QPixmap pixmap3;
        QImage image(pixmap.size(),QImage::Format_Alpha8);
        image.fill(QColor(0, 0, 0, 0));
        image.setDevicePixelRatio(qApp->devicePixelRatio());
        pixmap3=pixmap3.fromImage(image);
        qDebug() << "DEBUG: Created alpha channel image for light splash.";

        QPixmap pixmap4;
        QImage img2=QIcon::fromTheme("deepin-movie").pixmap(130, 130).toImage();
        pixmap4=pixmap4.fromImage(img2);
        qDebug() << "DEBUG: Loaded deepin-movie icon for light splash.";

        QPainter painter1(&pixmap3);
        painter1.drawPixmap(102,126,pixmap4); // 参数102 126，在界面上保持居中
        m_imgBgLight = pixmap3.toImage();
        m_imgBgLight.setDevicePixelRatio(qApp->devicePixelRatio());
        qDebug() << "DEBUG: Light splash image prepared.";
    }

    //cppcheck误报
    void MpvGLWidget::initializeGL()
    {
        qInfo() << "Initializing OpenGL context";
        QOpenGLFunctions *pGLFunction = QOpenGLContext::currentContext()->functions();
        if (!pGLFunction) {
            qCritical() << "CRITICAL: Failed to get OpenGL functions in initializeGL.";
            return;
        }
        float a = static_cast<float>(16.0 / 255.0);

        if (DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType()){
            a = static_cast<float>(252.0 / 255.0);
            qDebug() << "Using light theme color";
        }
        if(parent()->property("color").isValid()) {
            qDebug() << "DEBUG: Parent color property is valid.";
            QColor clr = parent()->property("color").value<QColor>();
            pGLFunction->glClearColor(clr.red()/255.f, clr.green()/255.f, clr.blue()/255.f, 1.0);
        } else {
            qDebug() << "DEBUG: Parent color property is invalid, using default clear color.";
            pGLFunction->glClearColor(a, a, a, 1.0);
        }
        qDebug() << "DEBUG: OpenGL clear color set.";

        prepareSplashImages();
        qDebug() << "Setting up OpenGL pipelines";
        setupIdlePipe();
        setupBlendPipe();

#ifndef _LIBDMR_
#ifndef USE_DXCB
        connect(window()->windowHandle(), &QWindow::windowStateChanged, [=]() {
            QWidget* pTopWid = this->topLevelWidget();
            bool rounded = !pTopWid->isFullScreen() && !pTopWid->isMaximized();
            qDebug() << "Window state changed - rounded corners:" << rounded;
            // 全屏和最大化下不裁剪圆角
            m_bDoRoundedClipping = rounded;

            //wayland
            if(utils::check_wayland_env()) {
                qDebug() << "DEBUG: Wayland environment detected - forcing rounded corners.";
                rounded = true;
                qDebug() << "Wayland environment detected - forcing rounded corners";
            }
            toggleRoundedClip(rounded);
            qDebug() << "DEBUG: Rounded clip toggled.";
        });
#endif
#endif

#if MPV_CLIENT_API_VERSION < MPV_MAKE_VERSION(2,0)
        mpv_opengl_init_params gl_init_params = { get_proc_address, nullptr, nullptr };
#else
        mpv_opengl_init_params gl_init_params = { get_proc_address, nullptr };
#endif
        qInfo() << "Initializing MPV OpenGL render context";
        mpv_render_param params[] = {
            {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL)},
            {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl_init_params},
            {MPV_RENDER_PARAM_X11_DISPLAY, reinterpret_cast<void*>(QX11Info::display())},
            {MPV_RENDER_PARAM_INVALID, nullptr}
        };

        if(utils::check_wayland_env()){
            qInfo() << "Configuring MPV for Wayland environment";
            params[2] = {MPV_RENDER_PARAM_WL_DISPLAY, nullptr};
        }

        //add by heyi
        if(!m_renderCreat) {
            qCritical() << "MPV render context creation function not found";
            return;
        }
        if (m_renderCreat(&m_pRenderCtx, m_handle, params) < 0) {
            qCritical() << "CRITICAL: Cannot initialize mpv gl context.";
            std::runtime_error("can not init mpv gl");
        }
        qInfo() << "MPV OpenGL render context initialized successfully";

        m_callback(m_pRenderCtx, gl_update_callback,
                   reinterpret_cast<void*>(this));
        qDebug() << "DEBUG: Exiting initializeGL.";
    }

    void MpvGLWidget::updateMovieFbo()
    {
        if (!m_bUseCustomFBO) {
            qDebug() << "Custom FBO not enabled, skipping update";
            return;
        }

        auto desiredSize = size() * qApp->devicePixelRatio();
        qDebug() << "Updating movie FBO with size:" << desiredSize;

        if (m_pFbo) {
            if (m_pFbo->size() == desiredSize) {
                qDebug() << "FBO size unchanged, skipping recreation";
                return;
            }
            m_pFbo->release();
            delete m_pFbo;
        }
        // 创建 FBO 格式
        QOpenGLFramebufferObjectFormat format;
        format.setAttachment(QOpenGLFramebufferObject::CombinedDepthStencil);
        format.setSamples(0);  // 禁用多重采样，避免与视频渲染冲突
        format.setTextureTarget(GL_TEXTURE_2D);
        format.setInternalTextureFormat(GL_RGBA8);  // 使用 8 位 RGBA 格式
        m_pFbo = new QOpenGLFramebufferObject(desiredSize, format);
        // 检查 FBO 是否创建成功
        if (!m_pFbo || !m_pFbo->isValid()) {
            qCritical() << "Failed to create FBO with size:" << desiredSize;
            return;
        }
        qDebug() << "FBO created successfully";
    }

    void MpvGLWidget::updateCornerMasks()
    {
        qDebug() << "updateCornerMasks";
        if (!utils::check_wayland_env() && !m_bUseCustomFBO) {
            qDebug() << "!utils::check_wayland_env() && !m_bUseCustomFBO";
            return;
        }

        for (int i = 0; i < 4; i++) {
            QSize sz(RADIUS, RADIUS);
            QImage img(sz, QImage::Format_ARGB32);
            img.fill(Qt::transparent);

            QPainter painter;
            painter.begin(&img);
            painter.setRenderHint(QPainter::Antialiasing);
            QPainterPath pp;
            switch (i) {
                case 0:
                    pp.moveTo({0, static_cast<qreal>(sz.height())});
                    pp.arcTo(QRectF(0, 0, RADIUS*2, RADIUS*2), 180.0, -90.0);
                    pp.lineTo(RADIUS, RADIUS);
                    pp.closeSubpath();
                    break;

                case 1:
                    pp.moveTo({0, 0});
                    pp.arcTo(QRectF(-RADIUS, 0, RADIUS*2, RADIUS*2), 90.0, -90.0);
                    pp.lineTo(0, RADIUS);
                    pp.closeSubpath();
                    break;

                case 2:
                    pp.moveTo({static_cast<qreal>(sz.width()), 0});
                    pp.arcTo(QRectF(-RADIUS, -RADIUS, RADIUS*2, RADIUS*2), 0.0, -90.0);
                    pp.lineTo(0, 0);
                    pp.closeSubpath();
                    break;

                case 3:
                    pp.moveTo({static_cast<qreal>(sz.width()), static_cast<qreal>(sz.height())});
                    pp.arcTo(QRectF(0, -RADIUS, RADIUS*2, RADIUS*2), 270.0, -90.0);
                    pp.lineTo(RADIUS, 0);
                    pp.closeSubpath();
                    break;
                default: return;
            }

            painter.setPen(Qt::red);
            painter.setBrush(Qt::red);
            painter.drawPath(pp);
            painter.end();

            if (m_pCornerMasks[i] == nullptr) {
                m_pCornerMasks[i] = new QOpenGLTexture(img, QOpenGLTexture::DontGenerateMipMaps);
                m_pCornerMasks[i]->setMinificationFilter(QOpenGLTexture::Linear);
                m_pCornerMasks[i]->setWrapMode(QOpenGLTexture::ClampToEdge);
            }
        }
        qDebug() << "updateCornerMasks end";
    }

    void MpvGLWidget::updateVboBlend()
    {
        qDebug() << "updateVboBlend";
        if (!m_vboBlend.isCreated()) {
            qDebug() << "m_vboBlend.create()";
            m_vboBlend.create();
        }

        GLfloat x1 = -1.0f;
        GLfloat x2 =  1.0f;
        GLfloat y1 =  1.0f;
        GLfloat y2 = -1.0f;

        GLfloat s1 = 0.0f;
        GLfloat t1 = 1.0f;
        GLfloat s2 = 1.0f;
        GLfloat t2 = 0.0f;

        GLfloat vdata[] = {
            x1, y1, s1, t1, 0.0f, 1.0f,
            x2, y1, s2, t1, 1.0f, 1.0f,
            x2, y2, s2, t2, 1.0f, 0.0f,
                                        
            x1, y1, s1, t1, 0.0f, 1.0f,
            x2, y2, s2, t2, 1.0f, 0.0f,
            x1, y2, s1, t2, 0.0f, 0.0f
        };

        m_vboBlend.bind();
        m_vboBlend.allocate(vdata, sizeof(vdata));
        m_vboBlend.release();
        qDebug() << "updateVboBlend end";
    }

    void MpvGLWidget::updateVboCorners()
    {
        qDebug() << "updateVboCorners";
        auto vp = rect().size();
        auto tex_sz = QSize(RADIUS, RADIUS);
        auto r = QRect(0, 0, vp.width(), vp.height());

        QPoint pos[4] = {
            {0, r.height() - tex_sz.height()}, //top left
            {r.width() - tex_sz.width(), r.height() - tex_sz.height()}, //top right
            {r.width() - tex_sz.width(), 0}, //bottom right
            {0, 0}, //bottom left
        };

        for (int i = 0; i < 4; i++) {
            if (!m_vboCorners[i].isCreated()) {
                qDebug() << "m_vboCorners[i].create()";
                m_vboCorners[i].create();
            }

            auto r2 = QRect(pos[i], tex_sz);

            GLfloat x1 = static_cast<float>(r2.left()) / r.width();
            GLfloat x2 = static_cast<float>(r2.right()+1) / r.width();
            GLfloat y1 = static_cast<float>(r2.top()) / r.height();
            GLfloat y2 = static_cast<float>(r2.bottom()+1) / r.height();

            x1 = static_cast<GLfloat>(static_cast<double>(x1) * 2.0 - 1.0);
            x2 = static_cast<GLfloat>(static_cast<double>(x2) * 2.0 - 1.0);
            y1 = static_cast<GLfloat>(static_cast<double>(y1) * 2.0 - 1.0);
            y2 = static_cast<GLfloat>(static_cast<double>(y2) * 2.0 - 1.0);

            // for video tex coord
            GLfloat s1 = static_cast<GLfloat>(r2.left()) / r.width();
            GLfloat s2 = static_cast<GLfloat>(r2.right()+1) / r.width();
            GLfloat t2 = static_cast<GLfloat>(r2.top()) / r.height();
            GLfloat t1 = static_cast<GLfloat>(r2.bottom()+1) / r.height();
            
            // corner(and video) coord, corner-tex-coord, and video-as-tex-coord
            GLfloat vdata[] = {
                x1, y1,  0.0f, 1.0f,  s1, t2,
                x2, y1,  1.0f, 1.0f,  s2, t2,
                x2, y2,  1.0f, 0.0f,  s2, t1,
                                             
                x1, y1,  0.0f, 1.0f,  s1, t2,
                x2, y2,  1.0f, 0.0f,  s2, t1,
                x1, y2,  0.0f, 0.0f,  s1, t1,
            };
            m_vboCorners[i].bind();
            m_vboCorners[i].allocate(vdata, sizeof(vdata));
            m_vboCorners[i].release();
        }
        qDebug() << "updateVboCorners end";
    }

    void MpvGLWidget::updateVbo()
    {
        qDebug() << "updateVbo";
        if (!m_vbo.isCreated()) {
            qDebug() << "m_vbo.create()";
            m_vbo.create();
        }
        //HACK: we assume if any of width or height is 380, then we are in mini mode
        auto vp = rect().size();

        auto bg_size = QSizeF(m_imgBgDark.size()) / devicePixelRatioF();
        m_bInMiniMode = vp.width() <= 380 || vp.height() <= 380;
        auto tex_sz = m_bInMiniMode ? bg_size/2 : bg_size;

        auto r = QRectF(0, 0, vp.width(), vp.height());
        auto r2 = QRectF(r.center() - QPointF(tex_sz.width()/2, tex_sz.height()/2), tex_sz);

        GLfloat x1 = static_cast<GLfloat>(static_cast<double>(r2.left()) / r.width());
        GLfloat x2 = static_cast<GLfloat>(static_cast<double>((r2.right()+1) / r.width()));
        GLfloat y1 = static_cast<GLfloat>(static_cast<double>(r2.top() / r.height()));
        GLfloat y2 = static_cast<GLfloat>(static_cast<double>((r2.bottom()+1) / r.height()));

        x1 = static_cast<GLfloat>(static_cast<double>(x1) * 2.0 - 1.0);
        x2 = static_cast<GLfloat>(static_cast<double>(x2) * 2.0 - 1.0);
        y1 = static_cast<GLfloat>(static_cast<double>(y1) * 2.0 - 1.0);
        y2 = static_cast<GLfloat>(static_cast<double>(y2) * 2.0 - 1.0);

        GLfloat vdata[] = {
            x1, y1, 0.0f, 1.0f,
            x2, y1, 1.0f, 1.0f,
            x2, y2, 1.0f, 0.0f,

            x1, y1, 0.0f, 1.0f,
            x2, y2, 1.0f, 0.0f,
            x1, y2, 0.0f, 0.0f
        };
        m_vbo.bind();
        m_vbo.allocate(vdata, sizeof(vdata));
        m_vbo.release();
        qDebug() << "updateVbo end";
    }

    void MpvGLWidget::resizeGL(int nWidth, int nHeight)
    {
        qDebug() << "resizeGL";
        updateMovieFbo();
        updateVbo();
        if (m_bUseCustomFBO){
            qDebug() << "updateVboCorners";
            updateVboCorners();
        }
        qInfo() << "GL resize" << nWidth << nHeight;
        QOpenGLWidget::resizeGL(nWidth, nHeight);
        qDebug() << "resizeGL end";
    }

    void MpvGLWidget::toggleRoundedClip(bool bFalse)
    {
        qDebug() << "toggleRoundedClip";
        // 设置圆角时使用自定的FBO，但在全屏和最大化时，通过
        // m_bDoRoundedClipping 设置是否实际应用圆角
        m_bUseCustomFBO = bFalse;
        makeCurrent();
        updateMovieFbo();
        update();
        qDebug() << "toggleRoundedClip end";
    }

    void MpvGLWidget::initMember()
    {
        qDebug() << "initMember";
        m_pRenderCtx = nullptr;

        m_bPlaying = false;
        m_bInMiniMode= false;

        m_bUseCustomFBO = true;
        m_bDoRoundedClipping = true;
        m_pDarkTex = nullptr;
        m_pLightTex = nullptr;
        m_pGlProg = nullptr;
        m_pGlProgBlend  = nullptr;
        m_pFbo = nullptr;
        m_pGlProgBlendCorners = nullptr;
        m_pGlProgCorner = nullptr;
        m_pCornerMasks[0] = nullptr;
        m_pCornerMasks[1] = nullptr;
        m_pCornerMasks[2] = nullptr;
        m_pCornerMasks[3] = nullptr;

        m_callback = nullptr;
        m_context_report = nullptr;
        m_renderContex = nullptr;
        m_renderCreat = nullptr;
        m_renderContexRender = nullptr;
        m_renderContextUpdate = nullptr;
        m_bRawFormat = false;
        qDebug() << "initMember end";
    }

    /*not used yet*/
    /*void MpvGLWidget::setHandle(myHandle h)
    {
        m_handle = h;
    }*/

    void MpvGLWidget::paintGL() 
    {
        qDebug() << "paintGL";
        QOpenGLFunctions *pGLFunction = QOpenGLContext::currentContext()->functions();
        if (!pGLFunction) {
            qCritical() << "Failed to get OpenGL functions in paintGL";
            return;
        }
    
        // 检查纹理是否有效
        if (m_pLightTex && !m_pLightTex->isCreated()) {
            qWarning() << "Light texture not created in paintGL";
            return;
        }
        
        if (m_bPlaying) {
            qDebug() << "Rendering video frame";
            qreal dpr = qApp->devicePixelRatio();
            QSize scaled = size() * dpr;
            int nFlip = 1;

            if (!m_bUseCustomFBO) {
                qDebug() << "Using default framebuffer for rendering";
                mpv_opengl_fbo fbo {
                    static_cast<int>(defaultFramebufferObject()), scaled.width(), scaled.height(), 0
                };

                mpv_render_param params[] = {
                    {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
                    {MPV_RENDER_PARAM_FLIP_Y, &nFlip},
                    {MPV_RENDER_PARAM_INVALID, nullptr}
                };

                m_renderContexRender(m_pRenderCtx, params);
            } else {
                qDebug() << "Using custom FBO for rendering";
                m_pFbo->bind();

                mpv_opengl_fbo fbo {
                    static_cast<int>(m_pFbo->handle()), scaled.width(), scaled.height(), 0
                };

                mpv_render_param params[] = {
                    {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
                    {MPV_RENDER_PARAM_FLIP_Y, &nFlip},
                    {MPV_RENDER_PARAM_INVALID, nullptr}
                };

                m_renderContexRender(m_pRenderCtx, params);

                m_pFbo->release();

                {
                    QOpenGLVertexArrayObject::Binder vaoBind(&m_vaoBlend);
                    m_pGlProgBlend->bind();
                    pGLFunction->glActiveTexture(GL_TEXTURE0);
                    pGLFunction->glBindTexture(GL_TEXTURE_2D, m_pFbo->texture());
                    pGLFunction->glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                    pGLFunction->glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                    pGLFunction->glDrawArrays(GL_TRIANGLES, 0, 6);
                    m_pGlProgBlend->release();
                }

                if (m_bDoRoundedClipping) {
                    pGLFunction->glBlendFunc(GL_SRC_ALPHA, GL_ZERO);
                    // blend corners
                    //QOpenGLVertexArrayObject::Binder vaoBind(&m_vaoCorner);

                    for (int i = 0; i < 4; i++) {
                        m_pGlProgBlendCorners->bind();
                        m_vboCorners[i].bind();

                        int nVertexLoc = m_pGlProgBlendCorners->attributeLocation("position");
                        int nMaskLoc = m_pGlProgBlendCorners->attributeLocation("maskTexCoord");
                        int nCoordLoc = m_pGlProgBlendCorners->attributeLocation("vTexCoord");
                        m_pGlProgBlendCorners->enableAttributeArray(nVertexLoc);
                        m_pGlProgBlendCorners->setAttributeBuffer(nVertexLoc, GL_FLOAT, 0, 2, 6*sizeof(GLfloat));
                        m_pGlProgBlendCorners->enableAttributeArray(nMaskLoc);
                        m_pGlProgBlendCorners->setAttributeBuffer(nMaskLoc, GL_FLOAT, 2*sizeof(GLfloat), 2, 6*sizeof(GLfloat));
                        m_pGlProgBlendCorners->enableAttributeArray(nCoordLoc);
                        m_pGlProgBlendCorners->setAttributeBuffer(nCoordLoc, GL_FLOAT, 4*sizeof(GLfloat), 2, 6*sizeof(GLfloat));
                        m_pGlProgBlendCorners->setUniformValue("movie", 0);
                        m_pGlProgBlendCorners->setUniformValue("mask", 1);

                        pGLFunction->glActiveTexture(GL_TEXTURE0);
                        pGLFunction->glBindTexture(GL_TEXTURE_2D, m_pFbo->texture());

                        pGLFunction->glActiveTexture(GL_TEXTURE1);
                        m_pCornerMasks[i]->bind();

                        pGLFunction->glDrawArrays(GL_TRIANGLES, 0, 6);

                        m_pCornerMasks[i]->release();
                        m_pGlProgBlendCorners->release();
                        m_vboCorners[i].release();
                    }
                }

                //pGLFunction->glDisable(GL_BLEND);
            }
#ifdef __x86_64__
            QWidget *topWidget = topLevelWidget();
            if(topWidget && (topWidget->isFullScreen())) {//全屏状态播放时更新显示进度
                QString time_text = QTime::currentTime().toString("hh:mm");
                QRect rectTime = QRect(rect().width() - 90, 0, 90, 40);
                QPainter painter;
                painter.begin(this);
                QPen pen;
                pen.setColor(QColor(255, 255, 255, 255 * .4));
                painter.setPen(pen);
                QFontMetrics fm(font());
                auto fr = fm.boundingRect(time_text);
                fr.moveCenter(rectTime.center());
                //显示系统时间
                painter.drawText(fr,time_text);
                QPoint pos((rectTime.topLeft().x() + 20), rectTime.topLeft().y() + rectTime.height() - 5);
                int pert = qMin(m_pert * 10, 10.0);
                for (int i = 0; i < 10; i++) {//显示影院视频播放进度
                    if (i >= pert) {
                        painter.fillRect(QRect(pos, QSize(3, 3)), QColor(255, 255, 255, 255 * .25));
                    } else {
                        painter.fillRect(QRect(pos, QSize(3, 3)), QColor(255, 255, 255, 255 * .5));
                    }
                    pos.rx() += 5;
                }
                QRect rectMovieTime = QRect(rect().width() - 175, 46, 175, 20);
                if(m_strPlayTime.isNull() || m_strPlayTime.isEmpty()) return;
                QPalette Palette;
                pen.setColor(Palette.color(QPalette::Text));
                if (m_bRawFormat) {
                    if (DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType()) {
                        pen.setColor(QColor(0, 0, 0, 40));
                    } else {
                        pen.setColor(QColor(255, 255, 255, 40));
                    }
                }
                painter.setPen(pen);
                fr = fm.boundingRect(m_strPlayTime);
                fr.moveCenter(rectMovieTime.center());
                //显示影院视频播放时间与总时间
                painter.drawText(fr,m_strPlayTime);
                painter.end();
            }
#endif
        } else {
            qDebug() << "Rendering idle state";
            pGLFunction->glEnable(GL_BLEND);
            pGLFunction->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            QColor color = QColor(37, 37, 37, 255);
            float fRation = 37.0f / 255.0f;
//            if (qApp->theme() != "dark") {
//                clr = QColor(252, 252, 252, 255);
//                a = 252.0 / 255.0;
//            }
            if (DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType()){
                color = QColor(252, 252, 252, 255);
                fRation = 252.0f / 255.0f;
            }
            if(parent()->property("color").isValid()) {
                QColor clr = parent()->property("color").value<QColor>();
                pGLFunction->glClearColor(clr.red()/255.f, clr.green()/255.f, clr.blue()/255.f, 1.0);
            } else {
                pGLFunction->glClearColor(fRation, fRation, fRation, 1.0);
            }
            pGLFunction->glClear(GL_COLOR_BUFFER_BIT);

            for(int i = 0;i < 2 ;i ++){
                {
                    QOpenGLVertexArrayObject::Binder vaoBind(&m_vao);
                    m_vbo.bind();
                    m_pGlProg->bind();
                    m_pGlProg->setUniformValue("bg", color);
                    prepareSplashImages();
                    QOpenGLTexture *pGLTexture;
                    m_pLightTex->setData(m_imgBgLight);
                    pGLTexture = m_pLightTex;
                    //和产品、ui商议深色主题下去除深色背景效果
//                    DGuiApplicationHelper::ColorType themeType = DGuiApplicationHelper::instance()->themeType();
//                    //                if (qApp->theme() == "dark") {
//                    if (themeType == DGuiApplicationHelper::DarkType) {
//                        pGLTexture = m_pDarkTex;
//                    }
                    pGLTexture->bind();
                    pGLFunction->glActiveTexture(GL_TEXTURE0);
                    pGLFunction->glDrawArrays(GL_TRIANGLES, 0, 6);
                    pGLTexture->release();
                    m_pGlProg->release();
                    m_vbo.release();
                }
            }

            if (m_bDoRoundedClipping) {
                pGLFunction->glBlendFunc(GL_SRC_ALPHA, GL_ZERO);
                // blend corners
                QOpenGLVertexArrayObject::Binder vaoBind(&m_vaoCorner);

                for (int i = 0; i < 4; i++) {
                    m_pGlProgCorner->bind();
                    m_vboCorners[i].bind();

                    int vertexLoc = m_pGlProgCorner->attributeLocation("position");
                    int coordLoc = m_pGlProgCorner->attributeLocation("vTexCoord");
                    m_pGlProgCorner->enableAttributeArray(vertexLoc);
                    m_pGlProgCorner->setAttributeBuffer(vertexLoc, GL_FLOAT, 0, 2, 6*sizeof(GLfloat));
                    m_pGlProgCorner->enableAttributeArray(coordLoc);
                    m_pGlProgCorner->setAttributeBuffer(coordLoc, GL_FLOAT, 2*sizeof(GLfloat), 2, 6*sizeof(GLfloat));
                    m_pGlProgCorner->setUniformValue("bg", color);
                    
                    pGLFunction->glActiveTexture(GL_TEXTURE0);
                    m_pCornerMasks[i]->bind();

                    pGLFunction->glDrawArrays(GL_TRIANGLES, 0, 6);

                    m_pCornerMasks[i]->release();
                    m_pGlProgCorner->release();
                    m_vboCorners[i].release();
                }
            }

            pGLFunction->glDisable(GL_BLEND);

        }
    }
#ifdef __x86_64__
    void MpvGLWidget::updateMovieProgress(qint64 duration, qint64 pos)
    {
        if (pos > duration)
            pos = duration;
        m_pert = (qreal)pos / duration;//更新影院播放进度
        QString sCurtime = QString("%1 %2").arg(utils::Time2str(pos)).arg("/ ");
        QString stime = QString("%1").arg(utils::Time2str(duration));
        m_strPlayTime = sCurtime + stime;//更新影院当前播放时长
    }
#endif
  
    void MpvGLWidget::setRawFormatFlag(bool bRawFormat)
    {
        m_bRawFormat = bRawFormat;
    }

    void MpvGLWidget::setPlaying(bool bFalse)
    {
        if (m_bPlaying != bFalse) {
            m_bPlaying = bFalse;
        }
        updateVbo();
        updateVboCorners();
        updateMovieFbo();
        update();
    }

    /*not used yet*/
    /*void MpvGLWidget::setMiniMode(bool val)
    {
        if (m_bInMiniMode != val) {
            m_bInMiniMode = val;
            updateVbo();
            updateVboCorners();
            update();
        }
    }*/

    void MpvGLWidget::initMpvFuns()
    {
        qInfo() << "Initializing MPV functions";
        QLibrary mpvLibrary(SysUtils::libPath("libmpv.so"));
        m_callback = reinterpret_cast<mpv_render_contextSet_update_callback>(mpvLibrary.resolve("mpv_render_context_set_update_callback"));
        m_context_report = reinterpret_cast<mpv_render_contextReport_swap>(mpvLibrary.resolve("mpv_render_context_report_swap"));
        m_renderContex = reinterpret_cast<mpv_renderContext_free>(mpvLibrary.resolve("mpv_render_context_free"));
        m_renderCreat = reinterpret_cast<mpv_renderContext_create>(mpvLibrary.resolve("mpv_render_context_create"));
        m_renderContexRender = reinterpret_cast<mpv_renderContext_render>(mpvLibrary.resolve("mpv_render_context_render"));
        m_renderContextUpdate = reinterpret_cast<mpv_renderContext_update>(mpvLibrary.resolve("mpv_render_context_update"));
    }
}
