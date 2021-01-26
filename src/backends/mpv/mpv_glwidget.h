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
#include <mpv/render.h>
#include <mpv/render_gl.h>
#include <mpv_proxy.h>
#undef Bool
#include "../../vendor/qthelper.hpp"
#include <DGuiApplicationHelper>
//DWIDGET_USE_NAMESPACE

//add by heyi
typedef void (*mpv_render_contextSet_update_callback)(mpv_render_context *ctx,
                                                      mpv_render_update_fn callback,
                                                      void *callback_ctx);
typedef void (*mpv_render_contextReport_swap)(mpv_render_context *ctx);
typedef void (*mpv_renderContext_free)(mpv_render_context *ctx);
typedef int (*mpv_renderContext_create)(mpv_render_context **res, mpv_handle *mpv,
                                        mpv_render_param *params);
typedef int (*mpv_renderContext_render)(mpv_render_context *ctx, mpv_render_param *params);
typedef uint64_t (*mpv_renderContext_update)(mpv_render_context *ctx);


namespace dmr {
class MpvGLWidget : public QOpenGLWidget
{
    Q_OBJECT
public:
    friend class MpvProxy;

    MpvGLWidget(QWidget *parent, MpvHandle h);
    virtual ~MpvGLWidget();

    /*
     * rounded clipping consumes a lot of resources, and performs bad on 4K video
     */
    void toggleRoundedClip(bool val);
    //add by heyi
    /**
     * @brief setHandle 设置句柄
     * @param h 传入的句柄
     */
    void setHandle(MpvHandle h);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void setPlaying(bool);
    void setMiniMode(bool);
    //add by heyi
    /**
     * @brief initMpvFuns 第一次播放需要初库始化函数指针
     */
    void initMpvFuns();

protected slots:
    void onNewFrame();
    void onFrameSwapped();

private:
    MpvHandle _handle;
    mpv_render_context *_render_ctx {nullptr};

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

    //add by heyi
    mpv_render_contextSet_update_callback m_callback{nullptr};
    mpv_render_contextReport_swap m_context_report{nullptr};
    mpv_renderContext_free m_renderContex{nullptr};
    mpv_renderContext_create m_renderCreat{nullptr};
    mpv_renderContext_render m_renderContexRender{nullptr};
    mpv_renderContext_update m_renderContextUpdate{nullptr};
};

}

#endif /* ifndef _DMR_MPV_GLWIDGET_H */

