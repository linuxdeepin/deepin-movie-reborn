#include "config.h"

#include "mpv_proxy.h"
#include "mpv_glwidget.h"

#include <QtX11Extras/QX11Info>

#include <dthememanager.h>
#include <DApplication>
DWIDGET_USE_NAMESPACE

static const char* vs_blend = R"(
attribute vec2 position;
attribute vec2 vTexCoord;
attribute vec2 maskTexCoord;

varying vec2 texCoord;
varying vec2 maskCoord;

void main() {
    gl_Position = vec4(position, 0.0, 1.0);
    texCoord = vTexCoord;
    maskCoord = maskTexCoord;
}
)";

static const char* fs_blend = R"(
varying vec2 texCoord;
varying vec2 maskCoord;

uniform sampler2D movie;
uniform sampler2D mask;

void main() {
     gl_FragColor = texture2D(movie, texCoord) * texture2D(mask, maskCoord).a; 
}
)";

static const char* vs_code = R"(
attribute vec2 position;
attribute vec2 vTexCoord;

varying vec2 texCoord;

void main() {
    gl_Position = vec4(position, 0.0, 1.0);
    texCoord = vTexCoord;
}
)";

static const char* fs_code = R"(
varying vec2 texCoord;

uniform sampler2D sampler;
uniform vec4 bg;

void main() {
    vec4 s = texture2D(sampler, texCoord);
    gl_FragColor = vec4(s.rgb * s.a + bg.rgb * (1.0 - s.a), 1.0);
}
)";

namespace dmr {
    static void* GLAPIENTRY glMPGetNativeDisplay(const char* name) {
        if (!strcmp(name, "x11") || !strcmp(name, "X11")) {
            return (void*)QX11Info::display();
        }
        return NULL;
    }

    static void *get_proc_address(void *ctx, const char *name) {
        Q_UNUSED(ctx);
        QOpenGLContext *glctx = QOpenGLContext::currentContext();
        if (!glctx)
            return NULL;

        if (!strcmp(name, "glMPGetNativeDisplay")) {
            return (void*)glMPGetNativeDisplay;
        }
        return (void *)glctx->getProcAddress(QByteArray(name));
    }

    static void gl_update_callback(void *cb_ctx)
    {
        MpvGLWidget *w = static_cast<MpvGLWidget*>(cb_ctx);
        QMetaObject::invokeMethod(w, "onNewFrame");
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
        setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);

        _gl_ctx = (mpv_opengl_cb_context*) mpv_get_sub_api(h, MPV_SUB_API_OPENGL_CB);
        if (!_gl_ctx) {
            std::runtime_error("can not init mpv gl");
        }
        mpv_opengl_cb_set_update_callback(_gl_ctx, gl_update_callback, this);
        connect(this, &QOpenGLWidget::frameSwapped, [=]() {
            //qDebug() << "frame swapped";
            mpv_opengl_cb_report_flip(_gl_ctx, 0);
        });

        auto fmt = QSurfaceFormat::defaultFormat();
        fmt.setAlphaBufferSize(8);
        this->setFormat(fmt);
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
        float a = 16.0 / 255.0;
        if (qApp->theme() != "dark") a = 252.0 / 255.0;
        f->glClearColor(a, a, a, 1.0);

        _vao.create();
        _vao.bind();

        _darkTex = new QOpenGLTexture(bg_dark, QOpenGLTexture::DontGenerateMipMaps);
        _darkTex->setMinificationFilter(QOpenGLTexture::Linear);
        _lightTex = new QOpenGLTexture(bg_light, QOpenGLTexture::DontGenerateMipMaps);
        _lightTex->setMinificationFilter(QOpenGLTexture::Linear);


        _darkMiniTex = new QOpenGLTexture(bg_dark_mini);
        _darkMiniTex->setMinificationFilter(QOpenGLTexture::LinearMipMapLinear);
        _lightMiniTex = new QOpenGLTexture(bg_light_mini);
        _lightMiniTex->setMinificationFilter(QOpenGLTexture::LinearMipMapLinear);

        updateVbo();

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
        _vao.release();

        _vaoBlend.create();
        _vaoBlend.bind();
        updateVboBlend();

        _vboBlend.bind();
        _glProgBlend = new QOpenGLShaderProgram();
        _glProgBlend->addShaderFromSourceCode(QOpenGLShader::Vertex, vs_blend);
        _glProgBlend->addShaderFromSourceCode(QOpenGLShader::Fragment, fs_blend);
        if (!_glProgBlend->link()) {
            qDebug() << "link failed";
        }
        _glProgBlend->bind();

        int vLocBlend = _glProgBlend->attributeLocation("position");
        int coordLocBlend = _glProgBlend->attributeLocation("vTexCoord");
        int maskLocBlend = _glProgBlend->attributeLocation("maskTexCoord");
        _glProgBlend->enableAttributeArray(vLocBlend);
        _glProgBlend->setAttributeBuffer(vLocBlend, GL_FLOAT, 0, 2, 6*sizeof(GLfloat));
        _glProgBlend->enableAttributeArray(coordLocBlend);
        _glProgBlend->setAttributeBuffer(coordLocBlend, GL_FLOAT, 2*sizeof(GLfloat), 2, 6*sizeof(GLfloat));
        _glProgBlend->enableAttributeArray(maskLocBlend);
        _glProgBlend->setAttributeBuffer(maskLocBlend, GL_FLOAT, 4*sizeof(GLfloat), 2, 6*sizeof(GLfloat));
        _glProgBlend->setUniformValue("movie", 0);
        _glProgBlend->setUniformValue("mask", 1);
        updateBlendMask();
        _glProgBlend->release();
        _vaoBlend.release();

        connect(window()->windowHandle(), &QWindow::windowStateChanged, [=]() {
                makeCurrent();
                updateBlendMask();
                update();
        });

        //if (mpv_opengl_cb_init_gl(_gl_ctx, NULL, get_proc_address, NULL) < 0)
        if (mpv_opengl_cb_init_gl(_gl_ctx, "GL_MP_MPGetNativeDisplay", get_proc_address, NULL) < 0)
            throw std::runtime_error("could not initialize OpenGL");
    }

    void MpvGLWidget::updateBlendMask()
    {
        if (!_doRoundedClipping) return;

        bool rounded = !window()->isFullScreen() && !window()->isMaximized();

        QImage img(size(), QImage::Format_ARGB32);
        img.fill(0);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath pp;
        auto d = 0.0f;
        if (rounded)
            pp.addRoundedRect(rect(), RADIUS+d, RADIUS+d);
        else
            pp.addRect(rect());
        p.fillPath(pp, Qt::white);
        p.end();

        if (_texMask) {
            _texMask->release();
            _texMask->destroy();
            delete _texMask;
        }
        _texMask = new QOpenGLTexture(img);
        _texMask->setMinificationFilter(QOpenGLTexture::LinearMipMapLinear);

        if (_fbo) {
            _fbo->release();
            delete _fbo;
        }
        _fbo = new QOpenGLFramebufferObject(size());
    }

    void MpvGLWidget::updateVboBlend()
    {
        if (!_vboBlend.isCreated()) {
            _vboBlend.create();
        }

        GLfloat x1 = -1.0f;
        GLfloat x2 =  1.0f;
        GLfloat y1 =  1.0f;
        GLfloat y2 = -1.0f;

        GLfloat s1 = 0.0f;
        GLfloat t1 = 1.0f;
        GLfloat s2 = 1.0f;
        GLfloat t2 = 0.0f;

        //hack: remove one pixel vertical black line (which I can not figure how to emerge)
        GLfloat s = 1.0f - 2.0f/width();
        s = 2.0f/width();
        s1+=s;
        s2-=s;

        GLfloat vdata[] = {
            x1, y1, s1, t1, 0.0f, 1.0f,
            x2, y1, s2, t1, 1.0f, 1.0f,
            x2, y2, s2, t2, 1.0f, 0.0f,
                                        
            x1, y1, s1, t1, 0.0f, 1.0f,
            x2, y2, s2, t2, 1.0f, 0.0f,
            x1, y2, s1, t2, 0.0f, 0.0f
        };

        _vboBlend.bind();
        _vboBlend.allocate(vdata, sizeof(vdata));
        _vboBlend.release();
    }

    void MpvGLWidget::updateVbo()
    {
        if (!_vbo.isCreated()) {
            _vbo.create();
        }
        //HACK: we assume if any of width or height is 380, then we are in mini mode
        auto vp = rect().size();

        _inMiniMode = vp.width() <= 380 || vp.height() <= 380;
        auto tex_sz = _inMiniMode ? bg_dark_mini.size() : bg_dark.size();

        auto r = QRect(0, 0, vp.width(), vp.height());
        auto r2 = QRect(r.center() - QPoint(tex_sz.width()/2, tex_sz.height()/2), tex_sz);

        GLfloat x1 = (float)r2.left() / r.width();
        GLfloat x2 = (float)(r2.right()+1) / r.width();
        GLfloat y1 = (float)r2.top() / r.height();
        GLfloat y2 = (float)(r2.bottom()+1) / r.height();

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
        updateVbo();

        updateBlendMask();

        QOpenGLWidget::resizeGL(w, h);
    }

    void MpvGLWidget::toggleRoundedClip(bool val)
    {
        _doRoundedClipping = val;
        makeCurrent();
        updateBlendMask();
        update();
    }

    void MpvGLWidget::paintGL() 
    {
        QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
        if (_playing) {
            if (!_doRoundedClipping) {
                mpv_opengl_cb_draw(_gl_ctx, defaultFramebufferObject(), width(), -height());
            } else {
                f->glClearColor(0.0, 0, 0, 0.0);
                f->glClear(GL_COLOR_BUFFER_BIT);

                _fbo->bind();
                mpv_opengl_cb_draw(_gl_ctx, _fbo->handle(), width(), -height());
                _fbo->release();

                _vaoBlend.bind();
                _vboBlend.bind();
                _glProgBlend->bind();

                f->glActiveTexture(GL_TEXTURE0);
                f->glBindTexture(GL_TEXTURE_2D, _fbo->texture());
                f->glActiveTexture(GL_TEXTURE1);
                _texMask->bind();

                //already set when inited
                //_glProgBlend->setUniformValue("movie", 0);
                //_glProgBlend->setUniformValue("mask", 1);

                f->glDrawArrays(GL_TRIANGLES, 0, 6);

                _glProgBlend->release();
                _vboBlend.release();
                _vaoBlend.release();
            }

        } else {
            f->glEnable(GL_BLEND);
            f->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            auto clr = QColor(16, 16, 16, 255);
            float a = 16.0 / 255.0;
            if (qApp->theme() != "dark") {
                clr = QColor(252, 252, 252, 255);
                a = 252.0 / 255.0;
            }
            f->glClearColor(a, a, a, 1.0);
            f->glClear(GL_COLOR_BUFFER_BIT);

            _vao.bind();
            _vbo.bind();
            _glProg->bind();
            _glProg->setUniformValue("bg", clr);

            QOpenGLTexture *tex = _lightTex;
            if (qApp->theme() == "dark") {
                tex = _darkTex;
            }
            f->glActiveTexture(GL_TEXTURE0);
            tex->bind();
            f->glDrawArrays(GL_TRIANGLES, 0, 6);
            tex->release();
            f->glDisable(GL_BLEND);

            _glProg->release();
            _vbo.release();
            _vao.release();
        }
    }

    void MpvGLWidget::setPlaying(bool val)
    {
        if (_playing != val) {
            _playing = val;
        }
        update();
    }

    void MpvGLWidget::setMiniMode(bool val)
    {
        if (_inMiniMode != val) {
            _inMiniMode = val;
            updateVbo();
            update();
        }
    }
}

