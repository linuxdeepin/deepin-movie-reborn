///*
// * (c) 2017, Deepin Technology Co., Ltd. <support@deepin.org>
// *
// * This program is free software; you can redistribute it and/or
// * modify it under the terms of the GNU General Public License as
// * published by the Free Software Foundation; either version 3 of the
// * License, or (at your option) any later version.
// *
// * This program is distributed in the hope that it will be useful, but
// * is provided AS IS, WITHOUT ANY WARRANTY; without even the implied
// * warranty of MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, and
// * NON-INFRINGEMENT.  See the GNU General Public License for more details.
// *
// * You should have received a copy of the GNU General Public License
// * along with this program; if not, see <http://www.gnu.org/licenses/>.
// *
// * In addition, as a special exception, the copyright holders give
// * permission to link the code of portions of this program with the
// * OpenSSL library under certain conditions as described in each
// * individual source file, and distribute linked combinations
// * including the two.
// * You must obey the GNU General Public License in all respects
// * for all of the code used other than OpenSSL.  If you modify
// * file(s) with this exception, you may extend this exception to your
// * version of the file(s), but you are not obligated to do so.  If you
// * do not wish to do so, delete this exception statement from your
// * version.  If you delete this exception statement from all source
// * files in the program, then also delete it here.
// */
//#include "config.h"

//#include "mpv_proxy.h"
//#include "mpv_glwidget.h"

//#include <QtX11Extras/QX11Info>

//#include <dthememanager.h>
//#include <DApplication>
//DWIDGET_USE_NAMESPACE


//static const char *vs_blend = R"(
//attribute vec2 position;
//attribute vec2 vTexCoord;

//varying vec2 texCoord;

//void main() {
//    gl_Position = vec4(position, 0.0, 1.0);
//    texCoord = vTexCoord;
//}
//)";

//static const char* fs_blend = R"(
//varying vec2 texCoord;

//uniform sampler2D movie;

//void main() {
//     gl_FragColor = texture2D(movie, texCoord);
//}
//)";

//static const char* vs_blend_corner = R"(
//attribute vec2 position;
//attribute vec2 maskTexCoord;
//attribute vec2 vTexCoord;

//varying vec2 maskCoord;
//varying vec2 texCoord;

//void main() {
//    gl_Position = vec4(position, 0.0, 1.0);
//    texCoord = vTexCoord;
//    maskCoord = maskTexCoord;
//}
//)";

//static const char* fs_blend_corner = R"(
//varying vec2 maskCoord;
//varying vec2 texCoord;

//uniform sampler2D movie;
//uniform sampler2D mask;

//void main() {
//     gl_FragColor = texture2D(movie, texCoord) * texture2D(mask, maskCoord).a;
//}
//)";



//static const char* vs_code = R"(
//attribute vec2 position;
//attribute vec2 vTexCoord;

//varying vec2 texCoord;

//void main() {
//    gl_Position = vec4(position, 0.0, 1.0);
//    texCoord = vTexCoord;
//}
//)";

//static const char* fs_code = R"(
//varying vec2 texCoord;

//uniform sampler2D sampler;
//uniform vec4 bg;

//void main() {
//    vec4 s = texture2D(sampler, texCoord);
//    gl_FragColor = vec4(s.rgb * s.a + bg.rgb * (1.0 - s.a), 1.0);
//}
//)";

//static const char* fs_corner_code = R"(
//varying vec2 texCoord;

//uniform sampler2D corner;
//uniform vec4 bg;

//void main() {
//    vec4 s = texture2D(corner, texCoord);
//    gl_FragColor = s.a * bg;
//}
//)";

//namespace dmr {
//    static void* GLAPIENTRY glMPGetNativeDisplay(const char* name) {
//        qWarning() << __func__ << name;
//        if (!strcmp(name, "x11") || !strcmp(name, "X11")) {
//            return (void*)QX11Info::display();
//        }
//        return NULL;
//    }

//    static void *get_proc_address(void *ctx, const char *name) {
//        Q_UNUSED(ctx);
//        QOpenGLContext *glctx = QOpenGLContext::currentContext();
//        if (!glctx)
//            return NULL;

//        if (!strcmp(name, "glMPGetNativeDisplay")) {
//            return (void*)glMPGetNativeDisplay;
//        }
//        return (void *)glctx->getProcAddress(QByteArray(name));
//    }

//    static void gl_update_callback(void *cb_ctx)
//    {
//        MpvGLWidget *w = static_cast<MpvGLWidget*>(cb_ctx);
//        QMetaObject::invokeMethod(w, "onNewFrame");
//    }

//    void MpvGLWidget::onNewFrame()
//    {
//        //qDebug() << __func__;
//        if (window()->isMinimized()) {
//            makeCurrent();
//            paintGL();
//            context()->swapBuffers(context()->surface());
//            doneCurrent();
//        } else {
//            mpv_render_context_update(_render_ctx);
//            update();
//        }
//    }

//    void MpvGLWidget::onFrameSwapped()
//    {
//        //qDebug() << "frame swapped";
//        mpv_render_context_report_swap(_render_ctx);
//    }

//    MpvGLWidget::MpvGLWidget(QWidget *parent, mpv::qt::Handle h)
//        :QOpenGLWidget(parent), _handle(h) {
//        setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);

//        connect(this, &QOpenGLWidget::frameSwapped,
//                this, &MpvGLWidget::onFrameSwapped, Qt::DirectConnection);

//        //auto fmt = QSurfaceFormat::defaultFormat();
//        //fmt.setAlphaBufferSize(8);
//        //this->setFormat(fmt);
//    }

//    MpvGLWidget::~MpvGLWidget()
//    {
//        makeCurrent();
//        if (_darkTex) {
//            _darkTex->destroy();
//            delete _darkTex;
//        }
////        if (_darkTexbac) {
////            _darkTexbac->destroy();
////            delete _darkTexbac;
////        }
//        if (_lightTex) {
//            _lightTex->destroy();
//            delete _lightTex;
//        }

//        for (auto mask: _cornerMasks) {
//            if (mask) mask->destroy();
//        }

//        _vbo.destroy();

//        for (int i = 0; i < 4; i++) {
//            _vboCorners[i].destroy();
//        }

//        _vao.destroy();
//        _vaoBlend.destroy();
//        _vaoCorner.destroy();

//        if (_fbo) delete _fbo;

//        if (_render_ctx) mpv_render_context_set_update_callback(_render_ctx, NULL, NULL);
//        // Until this call is done, we need to make sure the player remains
//        // alive. This is done implicitly with the mpv::qt::Handle instance
//        // in this class.
//        mpv_render_context_free(_render_ctx);
//        doneCurrent();
//    }

//    void MpvGLWidget::setupBlendPipe()
//    {
//        updateMovieFbo();

//        _vaoBlend.create();
//        _vaoBlend.bind();
//        updateVboBlend();

//        _glProgBlend = new QOpenGLShaderProgram();
//        _glProgBlend->addShaderFromSourceCode(QOpenGLShader::Vertex, vs_blend);
//        _glProgBlend->addShaderFromSourceCode(QOpenGLShader::Fragment, fs_blend);
//        if (!_glProgBlend->link()) {
//            qDebug() << "link failed";
//        }
//        _glProgBlend->bind();
//        _vboBlend.bind();

//        int vLocBlend = _glProgBlend->attributeLocation("position");
//        int coordLocBlend = _glProgBlend->attributeLocation("vTexCoord");
//        _glProgBlend->enableAttributeArray(vLocBlend);
//        _glProgBlend->setAttributeBuffer(vLocBlend, GL_FLOAT, 0, 2, 6*sizeof(GLfloat));
//        _glProgBlend->enableAttributeArray(coordLocBlend);
//        _glProgBlend->setAttributeBuffer(coordLocBlend, GL_FLOAT, 2*sizeof(GLfloat), 2, 6*sizeof(GLfloat));
//        _glProgBlend->setUniformValue("movie", 0);
//        _glProgBlend->release();
//        _vaoBlend.release();

//        _glProgBlendCorners = new QOpenGLShaderProgram();
//        _glProgBlendCorners->addShaderFromSourceCode(QOpenGLShader::Vertex, vs_blend_corner);
//        _glProgBlendCorners->addShaderFromSourceCode(QOpenGLShader::Fragment, fs_blend_corner);
//        if (!_glProgBlendCorners->link()) {
//            qDebug() << "link failed";
//        }
//    }

//    void MpvGLWidget::setupIdlePipe()
//    {
//        _vao.create();
//        _vao.bind();

////        _darkTexbac = new QOpenGLTexture(bg_dark_bac, QOpenGLTexture::DontGenerateMipMaps);
////        _darkTexbac->setMinificationFilter(QOpenGLTexture::Linear);
//        _darkTex = new QOpenGLTexture(bg_dark, QOpenGLTexture::DontGenerateMipMaps);
//        _darkTex->setMinificationFilter(QOpenGLTexture::Linear);
//        _lightTex = new QOpenGLTexture(bg_light, QOpenGLTexture::DontGenerateMipMaps);
//        _lightTex->setMinificationFilter(QOpenGLTexture::Linear);

//        updateVbo();
//        _vbo.bind();

//        _glProg = new QOpenGLShaderProgram();
//        _glProg->addShaderFromSourceCode(QOpenGLShader::Vertex, vs_code);
//        _glProg->addShaderFromSourceCode(QOpenGLShader::Fragment, fs_code);
//        if (!_glProg->link()) {
//            qDebug() << "link failed";
//        }
//        _glProg->bind();

//        int vertexLoc = _glProg->attributeLocation("position");
//        int coordLoc = _glProg->attributeLocation("vTexCoord");
//        _glProg->enableAttributeArray(vertexLoc);
//        _glProg->setAttributeBuffer(vertexLoc, GL_FLOAT, 0, 2, 4*sizeof(GLfloat));
//        _glProg->enableAttributeArray(coordLoc);
//        _glProg->setAttributeBuffer(coordLoc, GL_FLOAT, 2*sizeof(GLfloat), 2, 4*sizeof(GLfloat));
//        _glProg->setUniformValue("sampler", 0);
//        _glProg->release();
//        _vao.release();

//        {
//            _vaoCorner.create();
//            _vaoCorner.bind();

//            // setting up corners
//            updateVboCorners();
//            updateCornerMasks();

//            _glProgCorner = new QOpenGLShaderProgram();
//            _glProgCorner->addShaderFromSourceCode(QOpenGLShader::Vertex, vs_code);
//            _glProgCorner->addShaderFromSourceCode(QOpenGLShader::Fragment, fs_corner_code);
//            if (!_glProgCorner->link()) {
//                qDebug() << "link failed";
//            }
//            _vaoCorner.release();
//        }
//    }

//    void MpvGLWidget::prepareSplashImages()
//    {
////        bg_dark = utils::LoadHiDPIImage(":/resources/icons/dark/init-splash.svg");
////        bg_light = utils::LoadHiDPIImage(":/resources/icons/light/init-splash.svg");

//        QPixmap pixmap/*(":/resources/icons/dark/init-splash-bac.svg")*/;
//        QImage img=utils::LoadHiDPIImage(":/resources/icons/dark/init-splash-bac.svg");
//        pixmap=pixmap.fromImage(img);
//        QPixmap pixmap2/*(":/resources/icons/dark/init-splash.svg")*/;
//        QImage img1=utils::LoadHiDPIImage(":/resources/icons/dark/init-splash.svg");
//        pixmap2=pixmap2.fromImage(img1);
//        QPainter p(&pixmap);
////        p.drawPixmap((pixmap.width()-pixmap2.width())/2,(pixmap.height()-pixmap2.height())/2,pixmap2);
//        p.drawPixmap(98,127,pixmap2);
//        bg_dark=pixmap.toImage();
//        bg_dark.setDevicePixelRatio(qApp->devicePixelRatio());

//        QPixmap pixmap3;
//        QImage image(pixmap.size(),QImage::Format_Alpha8);
//        image.fill(QColor(0, 0, 0, 0));
//        image.setDevicePixelRatio(qApp->devicePixelRatio());
////        QImage image=utils::LoadHiDPIImage(":/resources/icons/light/init-light-bac.svg");
//        pixmap3=pixmap3.fromImage(image);
//        QPixmap pixmap4/*(":/resources/icons/dark/init-splash.svg")*/;
//        QImage img2=utils::LoadHiDPIImage(":/resources/icons/dark/init-splash.svg");
//        pixmap4=pixmap4.fromImage(img2);
//        QPainter p1(&pixmap3);
////        p1.drawPixmap((pixmap.width()-pixmap4.width())/2,(pixmap.height()-pixmap4.height())/2,pixmap4);
//        p1.drawPixmap(98,127,pixmap4);
//        bg_light=pixmap3.toImage();
//        bg_light.setDevicePixelRatio(qApp->devicePixelRatio());
//    }

//    void MpvGLWidget::initializeGL()
//    {
//        QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
//        //f->glEnable(GL_BLEND);
//        //f->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
//        float a = 16.0 / 255.0;

////        if (qApp->theme() != "dark") a = 252.0 / 255.0;
//        if (DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType()){
//            a = 252.0 / 255.0;
//        }
//        f->glClearColor(a, a, a, 1.0);


//        prepareSplashImages();
//        setupIdlePipe();
//        setupBlendPipe();

//#ifdef _LIBDMR_
//        toggleRoundedClip(false);
//#else
//#ifndef USE_DXCB
//        connect(window()->windowHandle(), &QWindow::windowStateChanged, [=]() {
//            auto top = this->topLevelWidget();
//            bool rounded = !top->isFullScreen() && !top->isMaximized();
//            toggleRoundedClip(rounded);
//        });
//#endif
//#endif

//        mpv_opengl_init_params gl_init_params = { get_proc_address, NULL, NULL };
//        int adv_control = 1;
//        mpv_render_param params[] = {
//            {MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL)},
//            {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl_init_params},

//            /*
//             *    which saves a copy per video frame ("vd-lavc-dr" option
//             *    needs to be enabled, and the rendering backend as well as the
//             *    underlying GPU API/driver needs to have support for it).
//             **/
//            //{MPV_RENDER_PARAM_ADVANCED_CONTROL, &adv_control},
//            {MPV_RENDER_PARAM_X11_DISPLAY, reinterpret_cast<void*>(QX11Info::display())},
//            {MPV_RENDER_PARAM_INVALID, nullptr}
//        };
//        if (mpv_render_context_create(&_render_ctx, _handle, params) < 0) {
//            std::runtime_error("can not init mpv gl");
//        }

//        mpv_render_context_set_update_callback(_render_ctx, gl_update_callback,
//                reinterpret_cast<void*>(this));
//    }

//    void MpvGLWidget::updateMovieFbo()
//    {
//        if (!_doRoundedClipping) return;

//        auto desiredSize = size() * qApp->devicePixelRatio();

//        if (_fbo) {
//            if (_fbo->size() == desiredSize) {
//                return;
//            }
//            _fbo->release();
//            delete _fbo;
//        }
//        _fbo = new QOpenGLFramebufferObject(desiredSize);
//    }

//    void MpvGLWidget::updateCornerMasks()
//    {
//        if (!_doRoundedClipping) return;

//        for (int i = 0; i < 4; i++) {
//            QSize sz(RADIUS, RADIUS);
//            QImage img(sz, QImage::Format_ARGB32);
//            img.fill(Qt::transparent);

//            QPainter p;
//            p.begin(&img);
//            p.setRenderHint(QPainter::Antialiasing);
//            QPainterPath pp;
//            switch (i) {
//                case 0:
//                    pp.moveTo({0, (qreal)sz.height()});
//                    pp.arcTo(QRectF(0, 0, RADIUS*2, RADIUS*2), 180.0, -90.0);
//                    pp.lineTo(RADIUS, RADIUS);
//                    pp.closeSubpath();
//                    break;

//                case 1:
//                    pp.moveTo({0, 0});
//                    pp.arcTo(QRectF(-RADIUS, 0, RADIUS*2, RADIUS*2), 90.0, -90.0);
//                    pp.lineTo(0, RADIUS);
//                    pp.closeSubpath();
//                    break;

//                case 2:
//                    pp.moveTo({(qreal)sz.width(), 0});
//                    pp.arcTo(QRectF(-RADIUS, -RADIUS, RADIUS*2, RADIUS*2), 0.0, -90.0);
//                    pp.lineTo(0, 0);
//                    pp.closeSubpath();
//                    break;

//                case 3:
//                    pp.moveTo({(qreal)sz.width(), (qreal)sz.height()});
//                    pp.arcTo(QRectF(0, -RADIUS, RADIUS*2, RADIUS*2), 270.0, -90.0);
//                    pp.lineTo(RADIUS, 0);
//                    pp.closeSubpath();
//                    break;
//                default: return;
//            }

//            p.setPen(Qt::red);
//            p.setBrush(Qt::red);
//            p.drawPath(pp);
//            p.end();

//            if (_cornerMasks[i] == nullptr) {
//                _cornerMasks[i] = new QOpenGLTexture(img, QOpenGLTexture::DontGenerateMipMaps);
//                _cornerMasks[i]->setMinificationFilter(QOpenGLTexture::Linear);
//                _cornerMasks[i]->setWrapMode(QOpenGLTexture::ClampToEdge);
//            }
//        }
//    }

//    void MpvGLWidget::updateVboBlend()
//    {
//        if (!_vboBlend.isCreated()) {
//            _vboBlend.create();
//        }

//        GLfloat x1 = -1.0f;
//        GLfloat x2 =  1.0f;
//        GLfloat y1 =  1.0f;
//        GLfloat y2 = -1.0f;

//        GLfloat s1 = 0.0f;
//        GLfloat t1 = 1.0f;
//        GLfloat s2 = 1.0f;
//        GLfloat t2 = 0.0f;

//        GLfloat vdata[] = {
//            x1, y1, s1, t1, 0.0f, 1.0f,
//            x2, y1, s2, t1, 1.0f, 1.0f,
//            x2, y2, s2, t2, 1.0f, 0.0f,

//            x1, y1, s1, t1, 0.0f, 1.0f,
//            x2, y2, s2, t2, 1.0f, 0.0f,
//            x1, y2, s1, t2, 0.0f, 0.0f
//        };

//        _vboBlend.bind();
//        _vboBlend.allocate(vdata, sizeof(vdata));
//        _vboBlend.release();
//    }

//    void MpvGLWidget::updateVboCorners()
//    {
//        auto vp = rect().size();
//        auto tex_sz = QSize(RADIUS, RADIUS);
//        auto r = QRect(0, 0, vp.width(), vp.height());

//        QPoint pos[4] = {
//            {0, r.height() - tex_sz.height()}, //top left
//            {r.width() - tex_sz.width(), r.height() - tex_sz.height()}, //top right
//            {r.width() - tex_sz.width(), 0}, //bottom right
//            {0, 0}, //bottom left
//        };

//        for (int i = 0; i < 4; i++) {
//            if (!_vboCorners[i].isCreated()) {
//                _vboCorners[i].create();
//            }

//            auto r2 = QRect(pos[i], tex_sz);

//            GLfloat x1 = (float)r2.left() / r.width();
//            GLfloat x2 = (float)(r2.right()+1) / r.width();
//            GLfloat y1 = (float)r2.top() / r.height();
//            GLfloat y2 = (float)(r2.bottom()+1) / r.height();

//            x1 = x1 * 2.0 - 1.0;
//            x2 = x2 * 2.0 - 1.0;
//            y1 = y1 * 2.0 - 1.0;
//            y2 = y2 * 2.0 - 1.0;

//            // for video tex coord
//            GLfloat s1 = (float)r2.left() / r.width();
//            GLfloat s2 = (float)(r2.right()+1) / r.width();
//            GLfloat t2 = (float)(r2.top()) / r.height();
//            GLfloat t1 = (float)(r2.bottom()+1) / r.height();

//            // corner(and video) coord, corner-tex-coord, and video-as-tex-coord
//            GLfloat vdata[] = {
//                x1, y1,  0.0f, 1.0f,  s1, t2,
//                x2, y1,  1.0f, 1.0f,  s2, t2,
//                x2, y2,  1.0f, 0.0f,  s2, t1,

//                x1, y1,  0.0f, 1.0f,  s1, t2,
//                x2, y2,  1.0f, 0.0f,  s2, t1,
//                x1, y2,  0.0f, 0.0f,  s1, t1,
//            };
//            _vboCorners[i].bind();
//            _vboCorners[i].allocate(vdata, sizeof(vdata));
//            _vboCorners[i].release();
//        }
//    }

//    void MpvGLWidget::updateVbo()
//    {
//        if (!_vbo.isCreated()) {
//            _vbo.create();
//        }
//        //HACK: we assume if any of width or height is 380, then we are in mini mode
//        auto vp = rect().size();

//        auto bg_size = QSizeF(bg_dark.size()) / devicePixelRatioF();
//        _inMiniMode = vp.width() <= 380 || vp.height() <= 380;
//        auto tex_sz = _inMiniMode ? bg_size/2 : bg_size;

//        auto r = QRectF(0, 0, vp.width(), vp.height());
//        auto r2 = QRectF(r.center() - QPointF(tex_sz.width()/2, tex_sz.height()/2), tex_sz);

//        GLfloat x1 = (float)r2.left() / r.width();
//        GLfloat x2 = (float)(r2.right()+1) / r.width();
//        GLfloat y1 = (float)r2.top() / r.height();
//        GLfloat y2 = (float)(r2.bottom()+1) / r.height();

//        x1 = x1 * 2.0 - 1.0;
//        x2 = x2 * 2.0 - 1.0;
//        y1 = y1 * 2.0 - 1.0;
//        y2 = y2 * 2.0 - 1.0;

//        GLfloat vdata[] = {
//            x1, y1, 0.0f, 1.0f,
//            x2, y1, 1.0f, 1.0f,
//            x2, y2, 1.0f, 0.0f,

//            x1, y1, 0.0f, 1.0f,
//            x2, y2, 1.0f, 0.0f,
//            x1, y2, 0.0f, 0.0f
//        };
//        _vbo.bind();
//        _vbo.allocate(vdata, sizeof(vdata));
//        _vbo.release();
//    }

//    void MpvGLWidget::resizeGL(int w, int h)
//    {
//        QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();

//        updateMovieFbo();
//        updateVbo();
//        if (_doRoundedClipping)
//            updateVboCorners();

//        qDebug() << "GL resize" << w << h;
//        QOpenGLWidget::resizeGL(w, h);
//    }

//    void MpvGLWidget::toggleRoundedClip(bool val)
//    {
//        _doRoundedClipping = val;
//        makeCurrent();
//        updateMovieFbo();
//        update();
//    }

//    void MpvGLWidget::paintGL()
//    {
//        QOpenGLFunctions *f = QOpenGLContext::currentContext()->functions();
//        if (_playing) {

//            auto dpr = qApp->devicePixelRatio();
//            QSize scaled = size() * dpr;
//            int flip = 1;

//            if (!_doRoundedClipping) {
//                mpv_opengl_fbo fbo {
//                    static_cast<int>(defaultFramebufferObject()), scaled.width(), scaled.height(), 0
//                };

//                mpv_render_param params[] = {
//                    {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
//                    {MPV_RENDER_PARAM_FLIP_Y, &flip},
//                    {MPV_RENDER_PARAM_INVALID, nullptr}
//                };

//                mpv_render_context_render(_render_ctx, params);
//            } else {
//                f->glEnable(GL_BLEND);
//                f->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

//                _fbo->bind();

//                mpv_opengl_fbo fbo {
//                    static_cast<int>(_fbo->handle()), scaled.width(), scaled.height(), 0
//                };

//                mpv_render_param params[] = {
//                    {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
//                    {MPV_RENDER_PARAM_FLIP_Y, &flip},
//                    {MPV_RENDER_PARAM_INVALID, nullptr}
//                };

//                mpv_render_context_render(_render_ctx, params);

//                _fbo->release();

//                {
//                    QOpenGLVertexArrayObject::Binder vaoBind(&_vaoBlend);
//                    _glProgBlend->bind();
//                    f->glActiveTexture(GL_TEXTURE0);
//                    f->glBindTexture(GL_TEXTURE_2D, _fbo->texture());
//                    f->glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
//                    f->glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
//                    f->glDrawArrays(GL_TRIANGLES, 0, 6);
//                    _glProgBlend->release();
//                }

//                {
//                    f->glBlendFunc(GL_SRC_ALPHA, GL_ZERO);
//                    // blend corners
//                    //QOpenGLVertexArrayObject::Binder vaoBind(&_vaoCorner);

//                    for (int i = 0; i < 4; i++) {
//                        _glProgBlendCorners->bind();
//                        _vboCorners[i].bind();

//                        int vertexLoc = _glProgBlendCorners->attributeLocation("position");
//                        int maskLoc = _glProgBlendCorners->attributeLocation("maskTexCoord");
//                        int coordLoc = _glProgBlendCorners->attributeLocation("vTexCoord");
//                        _glProgBlendCorners->enableAttributeArray(vertexLoc);
//                        _glProgBlendCorners->setAttributeBuffer(vertexLoc, GL_FLOAT, 0, 2, 6*sizeof(GLfloat));
//                        _glProgBlendCorners->enableAttributeArray(maskLoc);
//                        _glProgBlendCorners->setAttributeBuffer(maskLoc, GL_FLOAT, 2*sizeof(GLfloat), 2, 6*sizeof(GLfloat));
//                        _glProgBlendCorners->enableAttributeArray(coordLoc);
//                        _glProgBlendCorners->setAttributeBuffer(coordLoc, GL_FLOAT, 4*sizeof(GLfloat), 2, 6*sizeof(GLfloat));
//                        _glProgBlendCorners->setUniformValue("movie", 0);
//                        _glProgBlendCorners->setUniformValue("mask", 1);

//                        f->glActiveTexture(GL_TEXTURE0);
//                        f->glBindTexture(GL_TEXTURE_2D, _fbo->texture());

//                        f->glActiveTexture(GL_TEXTURE1);
//                        _cornerMasks[i]->bind();

//                        f->glDrawArrays(GL_TRIANGLES, 0, 6);

//                        _cornerMasks[i]->release();
//                        _glProgBlendCorners->release();
//                        _vboCorners[i].release();
//                    }
//                }

//                f->glDisable(GL_BLEND);
//            }

//        } else {
//            f->glEnable(GL_BLEND);
//            f->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
//            auto clr = QColor(37, 37, 37, 255);
//            float a = 37.0 / 255.0;
////            if (qApp->theme() != "dark") {
////                clr = QColor(252, 252, 252, 255);
////                a = 252.0 / 255.0;
////            }
//            if (DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType()){
//                clr = QColor(252, 252, 252, 255);
//                a = 252.0 / 255.0;
//            }
//            f->glClearColor(a, a, a, 1.0);
//            f->glClear(GL_COLOR_BUFFER_BIT);

//            for(int i = 0;i < 2 ;i ++){
//                {
//                    QOpenGLVertexArrayObject::Binder vaoBind(&_vao);
//                    _vbo.bind();
//                    _glProg->bind();
//                    _glProg->setUniformValue("bg", clr);

//                    QOpenGLTexture *tex = _lightTex;
//                    DGuiApplicationHelper::ColorType themeType = DGuiApplicationHelper::instance()->themeType();
//                    //                if (qApp->theme() == "dark") {
//                    if (themeType == DGuiApplicationHelper::DarkType) {
//                        tex = _darkTex;
//                    }
//                    tex->bind();
//                    f->glActiveTexture(GL_TEXTURE0);
//                    f->glDrawArrays(GL_TRIANGLES, 0, 6);
//                    tex->release();
//                    _glProg->release();
//                    _vbo.release();
//                }
//            }

//            if (_doRoundedClipping) {
//                f->glBlendFunc(GL_SRC_ALPHA, GL_ZERO);
//                // blend corners
//                QOpenGLVertexArrayObject::Binder vaoBind(&_vaoCorner);

//                for (int i = 0; i < 4; i++) {
//                    _glProgCorner->bind();
//                    _vboCorners[i].bind();

//                    int vertexLoc = _glProgCorner->attributeLocation("position");
//                    int coordLoc = _glProgCorner->attributeLocation("vTexCoord");
//                    _glProgCorner->enableAttributeArray(vertexLoc);
//                    _glProgCorner->setAttributeBuffer(vertexLoc, GL_FLOAT, 0, 2, 6*sizeof(GLfloat));
//                    _glProgCorner->enableAttributeArray(coordLoc);
//                    _glProgCorner->setAttributeBuffer(coordLoc, GL_FLOAT, 2*sizeof(GLfloat), 2, 6*sizeof(GLfloat));
//                    _glProgCorner->setUniformValue("bg", clr);

//                    f->glActiveTexture(GL_TEXTURE0);
//                    _cornerMasks[i]->bind();

//                    f->glDrawArrays(GL_TRIANGLES, 0, 6);

//                    _cornerMasks[i]->release();
//                    _glProgCorner->release();
//                    _vboCorners[i].release();
//                }
//            }

//            f->glDisable(GL_BLEND);

//        }
//    }

//    void MpvGLWidget::setPlaying(bool val)
//    {
//        if (_playing != val) {
//            _playing = val;
//        }
//        updateVbo();
//        updateVboCorners();
//        updateMovieFbo();
//        update();
//    }

//    void MpvGLWidget::setMiniMode(bool val)
//    {
//        if (_inMiniMode != val) {
//            _inMiniMode = val;
//            updateVbo();
//            updateVboCorners();
//            update();
//        }
//    }
//}

