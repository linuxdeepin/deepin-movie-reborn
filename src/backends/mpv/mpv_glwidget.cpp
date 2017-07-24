#include "mpv_proxy.h"
#include "mpv_glwidget.h"

#include <dthememanager.h>
#include <DApplication>
DWIDGET_USE_NAMESPACE

const char* vs_code = R"(
attribute vec2 position;
attribute vec2 vTexCoord;

varying vec2 texCoord;

void main() {
    gl_Position = vec4(position, 0.0, 1.0);
    texCoord = vTexCoord;
}
)";

const char* fs_code = R"(
varying vec2 texCoord;

uniform sampler2D sampler;

void main() {
    gl_FragColor = texture2D(sampler, texCoord);
}
)";

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
        delete _darkTex;
        delete _lightTex;
        _vbo.destroy();
        _vao.destroy();

        if (_gl_ctx)
            mpv_opengl_cb_set_update_callback(_gl_ctx, NULL, NULL);
        // Until this call is done, we need to make sure the player remains
        // alive. This is done implicitly with the mpv::qt::Handle instance
        // in this class.
        mpv_opengl_cb_uninit_gl(_gl_ctx);
        doneCurrent();
    }

    void MpvGLWidget::initializeGL() 
    {
        QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
        f->glEnable(GL_BLEND);
        f->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        float a = 10.0 / 255.0;
        if (qApp->theme() != "dark") a = 252.0 / 255.0;
        f->glClearColor(a, a, a, 1.0);

        _vao.create();
        _vao.bind();

        static QImage bg_dark(":/resources/icons/dark/init-splash.png");
        static QImage bg_light(":/resources/icons/light/init-splash.png");
        _darkTex = new QOpenGLTexture(bg_dark);
        _lightTex = new QOpenGLTexture(bg_light);

        updateVbo(rect().size(), bg_dark.size());

        _vbo.bind();
        _glProg = new QOpenGLShaderProgram();
        _glProg->addShaderFromSourceCode(QOpenGLShader::Vertex, vs_code);
        _glProg->addShaderFromSourceCode(QOpenGLShader::Fragment, fs_code);
        if (!_glProg->link()) {
            qDebug() << "link failed";
        }
        _glProg->bind();

        int vertexLoc = _glProg->attributeLocation("position");
        int coordLoc = _glProg->attributeLocation("vTexCoord");
        _glProg->enableAttributeArray(vertexLoc);
        _glProg->setAttributeBuffer(vertexLoc, GL_FLOAT, 0, 2, 4*sizeof(GLfloat));
        _glProg->enableAttributeArray(coordLoc);
        _glProg->setAttributeBuffer(coordLoc, GL_FLOAT, 2*sizeof(GLfloat), 2, 4*sizeof(GLfloat));
        _glProg->setUniformValue("sampler", 0);
        _glProg->release();

        if (mpv_opengl_cb_init_gl(_gl_ctx, NULL, get_proc_address, NULL) < 0)
            throw std::runtime_error("could not initialize OpenGL");
    }

    void MpvGLWidget::updateVbo(const QSize& vp, const QSize& tex_sz)
    {
        if (!_vbo.isCreated()) {
            _vbo.create();
        }

        auto r = QRect(0, 0, vp.width(), vp.height());
        auto r2 = QRect(r.center() - QPoint(tex_sz.width()/2, tex_sz.height()/2), tex_sz);

        GLfloat x1 = (float)r2.left() / r.width();
        GLfloat x2 = (float)r2.right() / r.width();
        GLfloat y1 = (float)r2.top() / r.height();
        GLfloat y2 = (float)r2.bottom() / r.height();

        x1 = x1 * 2.0 - 1.0;
        x2 = x2 * 2.0 - 1.0;
        y1 = y1 * 2.0 - 1.0;
        y2 = y2 * 2.0 - 1.0;

        GLfloat vdata[] = {
            x1, y1, 0.0f, 1.0f,
            x2, y1, 1.0f, 1.0f,
            x2, y2, 1.0f, 0.0f,

            x1, y1, 0.0f, 1.0f,
            x2, y2, 1.0f, 0.0f,
            x1, y2, 0.0f, 0.0f
        };
        _vbo.bind();
        _vbo.allocate(vdata, sizeof(vdata));
        _vbo.release();
    }

    void MpvGLWidget::resizeGL(int w, int h) 
    {
        QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();

        qDebug() << size() << w << h;
        static QImage bg_dark(":/resources/icons/dark/init-splash.png");
        updateVbo(QSize(w, h), bg_dark.size());

        QOpenGLWidget::resizeGL(w, h);
    }

    void MpvGLWidget::paintGL() 
    {
        QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
        if (_playing) {
            mpv_opengl_cb_draw(_gl_ctx, defaultFramebufferObject(), width(), -height());
        } else {
            float a = 10.0 / 255.0;
            if (qApp->theme() != "dark")
                a = 252.0 / 255.0;
            f->glClearColor(a, a, a, 1.0);

            f->glClear(GL_COLOR_BUFFER_BIT);
            _vao.bind();
            _vbo.bind();
            _glProg->bind();

            QOpenGLTexture *tex = _lightTex;
            if (qApp->theme() == "dark") {
                tex = _darkTex;
            }
            tex->bind();
            f->glDrawArrays(GL_TRIANGLES, 0, 6);
            tex->release();
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

