/* 
 * (c) 2017, Deepin Technology Co., Ltd. <support@deepin.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * is provided AS IS, WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, and
 * NON-INFRINGEMENT.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 * You must obey the GNU General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */
#ifndef _DMR_MPV_GLWIDGET_H
#define _DMR_MPV_GLWIDGET_H 

#include <QtWidgets>
#include <mpv/opengl_cb.h>
#undef Bool
#include <mpv/qthelper.hpp>
#include <DGuiApplicationHelper>
//DWIDGET_USE_NAMESPACE
namespace dmr {
class MpvGLWidget : public QOpenGLWidget
{
    Q_OBJECT
public:
    friend class MpvProxy;

    MpvGLWidget(QWidget *parent, mpv::qt::Handle h);
    virtual ~MpvGLWidget();

    /*
     * rounded clipping consumes a lot of resources, and performs bad on 4K video
     */
    void toggleRoundedClip(bool val);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void setPlaying(bool);
    void setMiniMode(bool);

protected slots:
    void onNewFrame();
    void onFrameSwapped();

private:
    mpv::qt::Handle _handle;
    mpv_opengl_cb_context *_gl_ctx {nullptr};
    bool _playing {false};
    bool _inMiniMode {false};
    bool _doRoundedClipping {true};

    QOpenGLVertexArrayObject _vao;
    QOpenGLBuffer _vbo;
    QOpenGLTexture *_darkTex {nullptr};
    QOpenGLTexture *_lightTex {nullptr};
    QOpenGLShaderProgram *_glProg {nullptr};

    QOpenGLVertexArrayObject _vaoBlend;
    QOpenGLBuffer _vboBlend;
    QOpenGLShaderProgram *_glProgBlend {nullptr};
    QOpenGLFramebufferObject *_fbo {nullptr};
    QOpenGLShaderProgram *_glProgBlendCorners {nullptr};

    //textures for corner
    QOpenGLVertexArrayObject _vaoCorner;
    QOpenGLTexture *_cornerMasks[4] {nullptr,};
    QOpenGLBuffer _vboCorners[4];
    QOpenGLShaderProgram *_glProgCorner {nullptr};

    QImage bg_dark;
    QImage bg_light;

    void updateVbo();
    void updateVboCorners();
    void updateVboBlend();

    void updateMovieFbo();
    void updateCornerMasks();

    void setupBlendPipe();
    void setupIdlePipe();

    void prepareSplashImages();

};

}

#endif /* ifndef _DMR_MPV_GLWIDGET_H */

