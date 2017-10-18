#ifndef _DMR_MPV_GLWIDGET_H
#define _DMR_MPV_GLWIDGET_H 

#include <QtWidgets>
#include <mpv/opengl_cb.h>
#undef Bool
#include <mpv/qthelper.hpp>

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

public slots:
    void onNewFrame();

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
    QOpenGLTexture *_darkMiniTex {nullptr};
    QOpenGLTexture *_lightMiniTex {nullptr};
    QOpenGLShaderProgram *_glProg {nullptr};

    QOpenGLVertexArrayObject _vaoBlend;
    QOpenGLBuffer _vboBlend;
    QOpenGLShaderProgram *_glProgBlend {nullptr};
    QOpenGLTexture *_texMask {nullptr};
    QOpenGLFramebufferObject *_fbo {nullptr};

    //textures for corner
    QOpenGLVertexArrayObject _vaoCorner;
    QOpenGLTexture *_cornerMasks[4] {nullptr,};
    QOpenGLBuffer _vboCorners[4];
    QOpenGLShaderProgram *_glProgCorner {nullptr};

    QImage bg_dark {":/resources/icons/dark/init-splash.png"};
    QImage bg_light {":/resources/icons/light/init-splash.png"};

    QImage bg_dark_mini {":/resources/icons/dark/mini-init-splash.png"};
    QImage bg_light_mini {":/resources/icons/light/mini-init-splash.png"};

    void updateVbo();
    void updateVboCorners();
    void updateVboBlend();

    void updateBlendMask();
    void updateCornerMasks();

    void setupBlendPipe();
    void setupIdlePipe();

};

}

#endif /* ifndef _DMR_MPV_GLWIDGET_H */

