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

varying vec2 texCoord;

void main() {
    gl_Position = vec4(position, 0.0, 1.0);
    texCoord = vTexCoord;
}
)";

static const char* fs_blend = R"(
varying vec2 texCoord;

uniform sampler2D movie;

void main() {
     gl_FragColor = texture2D(movie, texCoord); 
}
)";

static const char* vs_blend_corner = R"(
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
varying vec2 maskCoord;
varying vec2 texCoord;

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

static const char* fs_corner_code = R"(
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

    void MpvGLWidget::setupBlendPipe()
    {
        updateMovieFbo();

        _vaoBlend.create();
        _vaoBlend.bind();
        updateVboBlend();

        _glProgBlend = new QOpenGLShaderProgram();
        _glProgBlend->addShaderFromSourceCode(QOpenGLShader::Vertex, vs_blend);
        _glProgBlend->addShaderFromSourceCode(QOpenGLShader::Fragment, fs_blend);
        if (!_glProgBlend->link()) {
            qDebug() << "link failed";
        }
        _glProgBlend->bind();
        _vboBlend.bind();

        int vLocBlend = _glProgBlend->attributeLocation("position");
        int coordLocBlend = _glProgBlend->attributeLocation("vTexCoord");
        _glProgBlend->enableAttributeArray(vLocBlend);
        _glProgBlend->setAttributeBuffer(vLocBlend, GL_FLOAT, 0, 2, 6*sizeof(GLfloat));
        _glProgBlend->enableAttributeArray(coordLocBlend);
        _glProgBlend->setAttributeBuffer(coordLocBlend, GL_FLOAT, 2*sizeof(GLfloat), 2, 6*sizeof(GLfloat));
        _glProgBlend->setUniformValue("movie", 0);
        _glProgBlend->release();
        _vaoBlend.release();

        _glProgBlendCorners = new QOpenGLShaderProgram();
        _glProgBlendCorners->addShaderFromSourceCode(QOpenGLShader::Vertex, vs_blend_corner);
        _glProgBlendCorners->addShaderFromSourceCode(QOpenGLShader::Fragment, fs_blend_corner);
        if (!_glProgBlendCorners->link()) {
            qDebug() << "link failed";
        }
    }

    void MpvGLWidget::setupIdlePipe()
    {
        _vao.create();
        _vao.bind();

        _darkTex = new QOpenGLTexture(bg_dark, QOpenGLTexture::DontGenerateMipMaps);
        _darkTex->setMinificationFilter(QOpenGLTexture::Linear);
        _lightTex = new QOpenGLTexture(bg_light, QOpenGLTexture::DontGenerateMipMaps);
        _lightTex->setMinificationFilter(QOpenGLTexture::Linear);

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

        {
            _vaoCorner.create();
            _vaoCorner.bind();

            // setting up corners
            updateVboCorners();
            updateCornerMasks();

            _glProgCorner = new QOpenGLShaderProgram();
            _glProgCorner->addShaderFromSourceCode(QOpenGLShader::Vertex, vs_code);
            _glProgCorner->addShaderFromSourceCode(QOpenGLShader::Fragment, fs_corner_code);
            if (!_glProgCorner->link()) {
                qDebug() << "link failed";
            }
            _vaoCorner.release();
        }
    }

    void MpvGLWidget::initializeGL() 
    {
        QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
        //f->glEnable(GL_BLEND);
        //f->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        float a = 16.0 / 255.0;
        if (qApp->theme() != "dark") a = 252.0 / 255.0;
        f->glClearColor(a, a, a, 1.0);


        setupIdlePipe();
        setupBlendPipe();


        connect(window()->windowHandle(), &QWindow::windowStateChanged, [=]() {
            auto top = this->topLevelWidget();
            bool rounded = !top->isFullScreen() && !top->isMaximized();
            toggleRoundedClip(rounded);
        });

        //if (mpv_opengl_cb_init_gl(_gl_ctx, NULL, get_proc_address, NULL) < 0)
        if (mpv_opengl_cb_init_gl(_gl_ctx, "GL_MP_MPGetNativeDisplay", get_proc_address, NULL) < 0)
            throw std::runtime_error("could not initialize OpenGL");
    }

    void MpvGLWidget::updateMovieFbo()
    {
        if (!_doRoundedClipping) return;

        if (_fbo) {
            _fbo->release();
            delete _fbo;
        }
        _fbo = new QOpenGLFramebufferObject(size() * qApp->devicePixelRatio());
    }

    void MpvGLWidget::updateCornerMasks()
    {
        if (!_doRoundedClipping) return;

        for (int i = 0; i < 4; i++) {
            QSize sz(RADIUS, RADIUS);
            QImage img(sz, QImage::Format_ARGB32);
            img.fill(Qt::transparent);

            QPainter p;
            p.begin(&img);
            p.setRenderHint(QPainter::Antialiasing);
            QPainterPath pp;
            switch (i) {
                case 0:
                    pp.moveTo({0, (qreal)sz.height()});
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
                    pp.moveTo({(qreal)sz.width(), 0});
                    pp.arcTo(QRectF(-RADIUS, -RADIUS, RADIUS*2, RADIUS*2), 0.0, -90.0);
                    pp.lineTo(0, 0);
                    pp.closeSubpath();
                    break;

                case 3:
                    pp.moveTo({(qreal)sz.width(), (qreal)sz.height()});
                    pp.arcTo(QRectF(0, -RADIUS, RADIUS*2, RADIUS*2), 270.0, -90.0);
                    pp.lineTo(RADIUS, 0);
                    pp.closeSubpath();
                    break;
                default: return;
            }

            p.setPen(Qt::red);
            p.setBrush(Qt::red);
            p.drawPath(pp);
            p.end();

            if (_cornerMasks[i] == nullptr) {
                _cornerMasks[i] = new QOpenGLTexture(img, QOpenGLTexture::DontGenerateMipMaps);
                _cornerMasks[i]->setMinificationFilter(QOpenGLTexture::Linear);
                _cornerMasks[i]->setWrapMode(QOpenGLTexture::ClampToEdge);
            }
        }
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

    void MpvGLWidget::updateVboCorners()
    {
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
            if (!_vboCorners[i].isCreated()) {
                _vboCorners[i].create();
            }

            auto r2 = QRect(pos[i], tex_sz);

            GLfloat x1 = (float)r2.left() / r.width();
            GLfloat x2 = (float)(r2.right()+1) / r.width();
            GLfloat y1 = (float)r2.top() / r.height();
            GLfloat y2 = (float)(r2.bottom()+1) / r.height();

            x1 = x1 * 2.0 - 1.0;
            x2 = x2 * 2.0 - 1.0;
            y1 = y1 * 2.0 - 1.0;
            y2 = y2 * 2.0 - 1.0;

            // for video tex coord
            GLfloat s1 = (float)r2.left() / r.width();
            GLfloat s2 = (float)(r2.right()+1) / r.width();
            GLfloat t2 = (float)(r2.top()) / r.height();
            GLfloat t1 = (float)(r2.bottom()+1) / r.height();
            
            // corner(and video) coord, corner-tex-coord, and video-as-tex-coord
            GLfloat vdata[] = {
                x1, y1,  0.0f, 1.0f,  s1, t2,
                x2, y1,  1.0f, 1.0f,  s2, t2,
                x2, y2,  1.0f, 0.0f,  s2, t1,
                                             
                x1, y1,  0.0f, 1.0f,  s1, t2,
                x2, y2,  1.0f, 0.0f,  s2, t1,
                x1, y2,  0.0f, 0.0f,  s1, t1,
            };
            _vboCorners[i].bind();
            _vboCorners[i].allocate(vdata, sizeof(vdata));
            _vboCorners[i].release();
        }
    }

    void MpvGLWidget::updateVbo()
    {
        if (!_vbo.isCreated()) {
            _vbo.create();
        }
        //HACK: we assume if any of width or height is 380, then we are in mini mode
        auto vp = rect().size();

        _inMiniMode = vp.width() <= 380 || vp.height() <= 380;
        auto tex_sz = _inMiniMode ? bg_dark.size()/2 : bg_dark.size();

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

        if (_playing) {
            updateMovieFbo();
        } else {
            updateVbo();
            updateVboCorners();
        }

        QOpenGLWidget::resizeGL(w, h);
    }

    void MpvGLWidget::toggleRoundedClip(bool val)
    {
        _doRoundedClipping = val;
        makeCurrent();
        updateMovieFbo();
        update();
    }

    void MpvGLWidget::paintGL() 
    {
        QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
        if (_playing) {

            auto dpr = qApp->devicePixelRatio();
            QSize scaled = size() * dpr;

            if (!_doRoundedClipping) {
                mpv_opengl_cb_draw(_gl_ctx, defaultFramebufferObject(), scaled.width(), -scaled.height());
            } else {
                f->glEnable(GL_BLEND);
                f->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

                _fbo->bind();
                mpv_opengl_cb_draw(_gl_ctx, _fbo->handle(), scaled.width(), -scaled.height());
                _fbo->release();

                {
                    QOpenGLVertexArrayObject::Binder vaoBind(&_vaoBlend);
                    _glProgBlend->bind();
                    f->glActiveTexture(GL_TEXTURE0);
                    f->glBindTexture(GL_TEXTURE_2D, _fbo->texture());
                    f->glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                    f->glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                    f->glDrawArrays(GL_TRIANGLES, 0, 6);
                    _glProgBlend->release();
                }

                if (_doRoundedClipping) {
                    f->glBlendFunc(GL_SRC_ALPHA, GL_ZERO);
                    // blend corners
                    //QOpenGLVertexArrayObject::Binder vaoBind(&_vaoCorner);

                    for (int i = 0; i < 4; i++) {
                        _glProgBlendCorners->bind();
                        _vboCorners[i].bind();

                        int vertexLoc = _glProgBlendCorners->attributeLocation("position");
                        int maskLoc = _glProgBlendCorners->attributeLocation("maskTexCoord");
                        int coordLoc = _glProgBlendCorners->attributeLocation("vTexCoord");
                        _glProgBlendCorners->enableAttributeArray(vertexLoc);
                        _glProgBlendCorners->setAttributeBuffer(vertexLoc, GL_FLOAT, 0, 2, 6*sizeof(GLfloat));
                        _glProgBlendCorners->enableAttributeArray(maskLoc);
                        _glProgBlendCorners->setAttributeBuffer(maskLoc, GL_FLOAT, 2*sizeof(GLfloat), 2, 6*sizeof(GLfloat));
                        _glProgBlendCorners->enableAttributeArray(coordLoc);
                        _glProgBlendCorners->setAttributeBuffer(coordLoc, GL_FLOAT, 4*sizeof(GLfloat), 2, 6*sizeof(GLfloat));
                        _glProgBlendCorners->setUniformValue("movie", 0);
                        _glProgBlendCorners->setUniformValue("mask", 1);

                        f->glActiveTexture(GL_TEXTURE0);
                        f->glBindTexture(GL_TEXTURE_2D, _fbo->texture());

                        f->glActiveTexture(GL_TEXTURE1);
                        _cornerMasks[i]->bind();

                        f->glDrawArrays(GL_TRIANGLES, 0, 6);

                        _cornerMasks[i]->release();
                        _glProgBlendCorners->release();
                        _vboCorners[i].release();
                    }
                }

                f->glDisable(GL_BLEND);
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

            {
                QOpenGLVertexArrayObject::Binder vaoBind(&_vao);
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
                _glProg->release();
                _vbo.release();
            }

            if (_doRoundedClipping) {
                f->glBlendFunc(GL_SRC_ALPHA, GL_ZERO);
                // blend corners
                QOpenGLVertexArrayObject::Binder vaoBind(&_vaoCorner);

                for (int i = 0; i < 4; i++) {
                    _glProgCorner->bind();
                    _vboCorners[i].bind();

                    int vertexLoc = _glProgCorner->attributeLocation("position");
                    int coordLoc = _glProgCorner->attributeLocation("vTexCoord");
                    _glProgCorner->enableAttributeArray(vertexLoc);
                    _glProgCorner->setAttributeBuffer(vertexLoc, GL_FLOAT, 0, 2, 6*sizeof(GLfloat));
                    _glProgCorner->enableAttributeArray(coordLoc);
                    _glProgCorner->setAttributeBuffer(coordLoc, GL_FLOAT, 2*sizeof(GLfloat), 2, 6*sizeof(GLfloat));
                    _glProgCorner->setUniformValue("bg", clr);
                    
                    f->glActiveTexture(GL_TEXTURE0);
                    _cornerMasks[i]->bind();

                    f->glDrawArrays(GL_TRIANGLES, 0, 6);

                    _cornerMasks[i]->release();
                    _glProgCorner->release();
                    _vboCorners[i].release();
                }
            }

            f->glDisable(GL_BLEND);

        }
    }

    void MpvGLWidget::setPlaying(bool val)
    {
        if (_playing != val) {
            _playing = val;
        }
        updateVbo();
        updateVboCorners();
        update();
    }

    void MpvGLWidget::setMiniMode(bool val)
    {
        if (_inMiniMode != val) {
            _inMiniMode = val;
            updateVbo();
            updateVboCorners();
            update();
        }
    }
}

