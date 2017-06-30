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

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void setPlaying(bool);
    void updateVbo(const QSize& vp, const QSize& tex_sz);

public slots:
    void onNewFrame();

private:
    mpv::qt::Handle _handle;
    mpv_opengl_cb_context *_gl_ctx {nullptr};
    bool _playing;

    QOpenGLVertexArrayObject _vao;
    QOpenGLBuffer _vbo;
    QOpenGLTexture *_darkTex;
    QOpenGLTexture *_lightTex;
    QOpenGLShaderProgram *_glProg;
};

}

#endif /* ifndef _DMR_MPV_GLWIDGET_H */

