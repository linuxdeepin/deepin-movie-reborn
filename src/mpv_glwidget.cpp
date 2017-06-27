#include "mpv_proxy.h"
#include "mpv_glwidget.h"

namespace dmr {
    static void *get_proc_address(void *ctx, const char *name) {
        Q_UNUSED(ctx);
        QOpenGLContext *glctx = QOpenGLContext::currentContext();
        if (!glctx)
            return NULL;
        return (void *)glctx->getProcAddress(QByteArray(name));
    }

    static void gl_update_callback(void *cb_ctx)
    {
        MpvGLWidget *w = static_cast<MpvGLWidget*>(cb_ctx);
        w->onNewFrame();
    }

    void MpvGLWidget::onNewFrame()
    {
        //qDebug() << __func__;
        if (window()->isMinimized()) {
            makeCurrent();
            paintGL();
            context()->swapBuffers(context()->surface());
            doneCurrent();
        } else {
            update();
        }
    }

    MpvGLWidget::MpvGLWidget(QWidget *parent, mpv::qt::Handle h)
        :QOpenGLWidget(parent), _handle(h) { 

        _gl_ctx = (mpv_opengl_cb_context*) mpv_get_sub_api(h, MPV_SUB_API_OPENGL_CB);
        if (!_gl_ctx) {
            std::runtime_error("can not init mpv gl");
        }
        mpv_opengl_cb_set_update_callback(_gl_ctx, gl_update_callback, this);
        connect(this, &QOpenGLWidget::frameSwapped, [=]() {
            //qDebug() << "frame swapped";
            mpv_opengl_cb_report_flip(_gl_ctx, 0);
        });
    }

    MpvGLWidget::~MpvGLWidget() 
    {
        makeCurrent();
        if (_gl_ctx)
            mpv_opengl_cb_set_update_callback(_gl_ctx, NULL, NULL);
        // Until this call is done, we need to make sure the player remains
        // alive. This is done implicitly with the mpv::qt::Handle instance
        // in this class.
        mpv_opengl_cb_uninit_gl(_gl_ctx);
    }

    void MpvGLWidget::initializeGL() 
    {
        QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
        f->glClearColor(0.0f, 0.0f, 0.0f, 0.5f);
        if (mpv_opengl_cb_init_gl(_gl_ctx, NULL, get_proc_address, NULL) < 0)
            throw std::runtime_error("could not initialize OpenGL");
    }

    void MpvGLWidget::resizeGL(int w, int h) 
    {
        QOpenGLWidget::resizeGL(w, h);
    }

    void MpvGLWidget::paintGL() 
    {
        QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
        if (_playing) {
            mpv_opengl_cb_draw(_gl_ctx, defaultFramebufferObject(), width(), -height());
        } else {
            f->glClear(GL_COLOR_BUFFER_BIT);
        }
    }

    void MpvGLWidget::setPlaying(bool val)
    {
        if (_playing != val) {
            _playing = val;
            update();
        }
    }
}

