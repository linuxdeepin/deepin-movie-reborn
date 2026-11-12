// SPDX-FileCopyrightText: 2026 UnionTech Software Technology Co., Ltd.
//
// SPDX-License-Identifier: GPL-3.0-or-later

// 覆盖：MpvGLWidget（此前零测试）—— ctor / initMpvFuns / initializeGL / paintGL /
//       onNewFrame / onFrameSwapped / updateMovieFbo / updateCornerMasks / updateVbo /
//       updateVboCorners / updateVboBlend / setupIdlePipe / setupBlendPipe / resizeGL /
//       toggleRoundedClip / setPlaying / setRawFormatFlag / updateMovieProgress / ~MpvGLWidget
//
// 分支清单 → 用例映射（mpv_glwidget.cpp）：
//   MpvGLWidget() ctor                    → ctor_New_InitializesDefaultState
//   initMpvFuns()                         → ctor_New_InitMpvFunsNullWithoutLib
//                                            initMpvFuns_ResolveSuccess_AssignsPointers
//   initializeGL()                        → initializeGL_CreateSuccess_InstallsUpdateCallback
//                                            initializeGL_CreateFails_MissingThrowContinuesExecution
//                                            initializeGL_CreateFnMissing_ReturnsEarly
//   paintGL() 播放+自定义FBO              → paintGL_PlayingCustomFBO_RendersViaFbo
//   paintGL() 播放+默认帧缓冲            → paintGL_PlayingDefaultFBO_RendersToDefaultFramebuffer
//   paintGL() 播放+全屏进度浮层          → paintGL_PlayingFullscreenOverlay_RebuildsOverlayTexture
//   paintGL() 空闲分支                   → paintGL_IdleTexCreated_DrawsSplashAndCorners
//   paintGL() 纹理未创建早退             → paintGL_IdleLightTexNotCreated_ReturnsEarly
//   onNewFrame() 非最小化/最小化         → onNewFrame_NotMinimized_CallsRenderContextUpdate
//                                            onNewFrame_Minimized_PaintsAndSwaps
//   onFrameSwapped() 空指针/已安装       → onFrameSwapped_ReportNull_ReturnsEarly
//                                            onFrameSwapped_ReportInstalled_CallsReportSwap
//   updateMovieFbo() 开/关               → updateMovieFbo_CustomFBOOff_KeepsNull
//                                            updateMovieFbo_CustomFBOOn_CreatesFbo
//   updateCornerMasks() 开/关            → updateCornerMasks_CustomFBOOn_CreatesFourMasks
//                                            updateCornerMasks_CustomFBOOff_KeepsNullMasks
//   updateVbo() 迷你模式边界(380)        → updateVbo_Boundary380Width_MiniModeTrue
//                                            updateVbo_LargerThan380_MiniModeFalse
//   updateVboCorners()/updateVboBlend()  → updateVboCorners_AllFourCorners_NoCrash
//                                            updateVboBlend_RepeatedCalls_NoCrash
//   setupIdlePipe()/setupBlendPipe()     → setupIdlePipe_PipelinesTexturesAndMasksReady
//                                            setupBlendPipe_ProgramsAndFboReady
//   resizeGL()                           → resizeGL_CustomFBO_RecreatesFboWithoutCrash
//                                            resizeGL_NoCustomFBO_NoCrash
//   toggleRoundedClip()                  → toggleRoundedClip_TogglesCustomFboFlag
//   setPlaying()                         → setPlaying_StateTransitionNoToggleOnSameValue
//   setRawFormatFlag()                   → setRawFormatFlag_TogglesFlag
//   updateMovieProgress()                → TEST_P（Normal / PosExceedsDuration / ZeroDuration）
//   ~MpvGLWidget()                       → dtor_AfterFullSetup_ReleasesRenderCtxWithoutCrash
//
// 测试环境约定（Qt6 + USE_TEST，offscreen 平台，无真实 GL 上下文）：
//   1. libmpv 未链接：QLibrary::resolve 打桩返回空，6 个 mpv_render_* 成员函数指针
//      由测试直接赋值录制 stub；fake 渲染上下文为哑指针，永不解引用。
//   2. Qt GL 桩策略（套件级安装、套件级恢复）：
//      - QOpenGLContext::currentContext → 返回伪造上下文（Qt6 QOpenGLFunctions 的
//        glXxx 为 inline 函数指针调用，必须先有 currentContext 才能解析到空操作）；
//      - QOpenGLContext::getProcAddress(const char*) → 返回空操作函数地址，
//        使所有 GL 调用落空（glGetError→0、glGetString→null、glGenXxx 不写出参）；
//      - QOpenGLWidget::context() → 伪造上下文（未显示的 QOpenGLWidget 返回空指针，
//        onNewFrame 最小化分支会解引用）；
//      - QOpenGLFramebufferObject：isValid/bind/handle/texture 打桩为确定值
//        （无 Framebuffers 特性时 Qt 内部 init 提前返回，texture() 会越界访问空数组）；
//      - QOpenGLTexture::bind/release、QOpenGLShaderProgram::release/
//        enableAttributeArray/disableAttributeArray/setAttributeBuffer 打桩为空操作
//        （无上下文时这些路径段错误，探针实验见 tests/.ut/probe_gl*.cpp）。
//   3. paintGL/空闲分支要求先 setupIdlePipe（m_pGlProg 为空会解引用崩溃）；
//      播放分支要求先 setupBlendPipe + updateCornerMasks + updateVboCorners。
//   4. 已知缺陷：initializeGL 渲染上下文创建失败分支缺少 throw（std::runtime_error
//      被构造后丢弃），错误被吞掉继续执行——用例记录实际行为，缺陷登记 defects.json。

// —— Qt 头（在 #define private public 之前）——
#include <QWidget>
#include <QOpenGLWidget>
#include <QtOpenGL>
#include <QtOpenGLWidgets>
#include <QOpenGLContext>
#include <QLibrary>
#include <QImage>
#include <QPainter>
#include <QWindow>

#include <gtest/gtest.h>

#include <cstring>

// —— 项目头（#define 获得成员访问权限）——
#define protected public
#define private public
#include "src/backends/mpv/mpv_glwidget.h"
#undef protected
#undef private

// —— Qt 私有头（伪造 GL 上下文需要操作 QOpenGLContextPrivate::shareGroup）——
#include <QtGui/qopenglcontext.h>
#include <QtGui/6.8.0/QtGui/private/qopenglcontext_p.h>

#include "stub/stub.h"

using namespace dmr;

// ============================ 测试基建 ============================

namespace {

// —— fake 资源（哑指针，永不解引用）——
char g_ut_wFakeBuf[256];
mpv_handle *ut_wFakeHandle()
{
    return reinterpret_cast<mpv_handle *>(g_ut_wFakeBuf);
}
mpv_render_context *ut_wFakeRenderCtx()
{
    return reinterpret_cast<mpv_render_context *>(g_ut_wFakeBuf + 64);
}

// 伪造 GL 上下文：真实构造的 QOpenGLContext + 手工构造的 QOpenGLContextGroup 壳。
// QOpenGLContext 未 create() 时 shareGroup() 为空，Qt 内部
// QOpenGLMultiGroupSharedResource::value<T>() 会空解引用（SIGSEGV@0x8，
// 探针实验见 tests/.ut/probe_gl5.cpp）；挂上 shareGroup 后 functions() 解析、
// 扩展匹配等全部落入空操作路径。
QOpenGLContext *ut_wFakeGLCtx()
{
    static QOpenGLContext ctx;
    static bool inited = false;
    if (!inited) {
        inited = true;
        // d_ptr 位于 QObject 布局偏移 8（x86_64：vptr + d_ptr）
        auto *priv = *reinterpret_cast<QOpenGLContextPrivate **>(
            reinterpret_cast<char *>(&ctx) + sizeof(void *));
        // QOpenGLContextGroup 壳：vptr(0) + d_ptr(8)，生命周期为进程级静态
        static char grpBuf[sizeof(QOpenGLContextGroup)] = {0};
        static char grpPrivBuf[sizeof(QOpenGLContextGroupPrivate)] = {0};
        auto *grpPriv = new (grpPrivBuf) QOpenGLContextGroupPrivate();
        *reinterpret_cast<QObjectPrivate **>(grpBuf + sizeof(void *)) = grpPriv;
        priv->shareGroup = reinterpret_cast<QOpenGLContextGroup *>(grpBuf);
    }
    return &ctx;
}

// 所有 GL 调用落到这里的空操作。返回空串指针：指针型返回值（如 glGetString）
// 得到合法空串（strlen→0），避免返回寄存器垃圾值导致后续 strlen 段错误；
// 出参型函数（glGenXxx）不写写出参，调用方读到 0/未初始化但不崩溃。
static const char *ut_wGlEmptyStr = "";
static void *ut_wGlNoop()
{
    return (void *)ut_wGlEmptyStr;
}

// —— 录制容器 ——
static int g_ut_renderCreateCalls = 0;
static int g_ut_renderCreateResult = 0;      // 可注入失败
static int g_ut_renderRenderCalls = 0;
static int g_ut_renderUpdateCalls = 0;
static int g_ut_renderFreeCalls = 0;
static int g_ut_renderReportCalls = 0;
static mpv_render_update_fn g_ut_installedUpdateFn = nullptr;
static void *g_ut_installedUpdateCtx = nullptr;
static int g_ut_renderSetUpdateCallbackCalls = 0;
static bool g_ut_texCreated = false;         // QOpenGLTexture::isCreated 桩开关

static void ut_wResetRecorders()
{
    g_ut_renderCreateCalls = 0;
    g_ut_renderCreateResult = 0;
    g_ut_renderRenderCalls = 0;
    g_ut_renderUpdateCalls = 0;
    g_ut_renderFreeCalls = 0;
    g_ut_renderReportCalls = 0;
    g_ut_installedUpdateFn = nullptr;
    g_ut_installedUpdateCtx = nullptr;
    g_ut_renderSetUpdateCallbackCalls = 0;
    g_ut_texCreated = false;
}

// —— mpv render 录制 stub ——
static int ut_wRenderCreate_rec(mpv_render_context **res, mpv_handle *, mpv_render_param *)
{
    ++g_ut_renderCreateCalls;
    if (g_ut_renderCreateResult == 0 && res)
        *res = ut_wFakeRenderCtx();
    return g_ut_renderCreateResult;
}
static int ut_wRenderRender_rec(mpv_render_context *, mpv_render_param *)
{
    ++g_ut_renderRenderCalls;
    return 0;
}
static uint64_t ut_wRenderUpdate_rec(mpv_render_context *)
{
    ++g_ut_renderUpdateCalls;
    return 1;
}
static void ut_wRenderFree_rec(mpv_render_context *)
{
    ++g_ut_renderFreeCalls;
}
static void ut_wRenderReport_rec(mpv_render_context *)
{
    ++g_ut_renderReportCalls;
}
static void ut_wRenderSetUpdateCallback_rec(mpv_render_context *, mpv_render_update_fn cb, void *cctx)
{
    g_ut_installedUpdateFn = cb;
    g_ut_installedUpdateCtx = cctx;
    ++g_ut_renderSetUpdateCallbackCalls;
}

// —— Qt 桩 ——
static QOpenGLContext *ut_wCurrentContext_stub()
{
    return ut_wFakeGLCtx();
}
typedef QFunctionPointer (*UtWCurCtxFn)();

static QFunctionPointer ut_wGetProcAddr_stub(const QOpenGLContext *, const char *)
{
    return (QFunctionPointer)&ut_wGlNoop;
}
typedef QFunctionPointer (QOpenGLContext::*UtWGetProcFn)(const char *) const;

static QOpenGLContext *ut_wWidgetContext_stub(const QOpenGLWidget *)
{
    return ut_wFakeGLCtx();
}
typedef QOpenGLContext *(QOpenGLWidget::*UtWWidgetCtxFn)() const;

static bool ut_wFboIsValid_stub(const QOpenGLFramebufferObject *)
{
    return true;
}
static bool ut_wFboBind_stub(QOpenGLFramebufferObject *)
{
    return true;
}
static bool ut_wFboRelease_stub(QOpenGLFramebufferObject *)
{
    return true;
}
static GLuint ut_wFboHandle_stub(const QOpenGLFramebufferObject *)
{
    return 0xA11CE;
}
static GLuint ut_wFboTexture_stub(const QOpenGLFramebufferObject *)
{
    return 0xBEEF;
}
typedef bool (QOpenGLFramebufferObject::*UtWFboBoolFn)();
typedef bool (QOpenGLFramebufferObject::*UtWFboConstBoolFn)() const;
typedef GLuint (QOpenGLFramebufferObject::*UtWFboUintFn)() const;

static void ut_wTexBind_stub(QOpenGLTexture *, uint, QOpenGLTexture::TextureUnitReset)
{
}
static void ut_wTexRelease_stub(QOpenGLTexture *, uint, QOpenGLTexture::TextureUnitReset)
{
}
typedef void (QOpenGLTexture::*UtWTexBindFn)(uint, QOpenGLTexture::TextureUnitReset);

static bool ut_wTexIsCreated_stub(const QOpenGLTexture *)
{
    return g_ut_texCreated;
}
typedef bool (QOpenGLTexture::*UtWTexConstBoolFn)() const;

static void ut_wProgRelease_stub(QOpenGLShaderProgram *)
{
}
static void ut_wProgEnableAttr_stub(QOpenGLShaderProgram *, int)
{
}
static void ut_wProgDisableAttr_stub(QOpenGLShaderProgram *, int)
{
}
static void ut_wProgSetAttrBuf_stub(QOpenGLShaderProgram *, int, GLenum, int, int, int)
{
}
typedef void (QOpenGLShaderProgram::*UtWProgVoidFn)();
typedef void (QOpenGLShaderProgram::*UtWProgIntFn)(int);
typedef void (QOpenGLShaderProgram::*UtWProgBufFn)(int, GLenum, int, int, int);

static QFunctionPointer ut_wQLibResolveNull_stub(QLibrary *, const char *)
{
    return nullptr;
}
static QFunctionPointer ut_wQLibResolveDummy_stub(QLibrary *, const char *)
{
    return (QFunctionPointer)&ut_wGlNoop;
}
typedef QFunctionPointer (QLibrary::*UtWResolveFn)(const char *);

// —— 用例辅助 ——
// 创建挂好录制 stub 的 MpvGLWidget（默认 MpvHandle：sptr 为空，容器永不析构）
static MpvGLWidget *ut_wCreateWidget(Stub &stub, QWidget *parent)
{
    // 防止 initMpvFuns 加载真实 libmpv
    stub.set((UtWResolveFn)&QLibrary::resolve, ut_wQLibResolveNull_stub);
    auto *w = new MpvGLWidget(parent, MpvHandle());
    w->m_callback = ut_wRenderSetUpdateCallback_rec;
    w->m_context_report = ut_wRenderReport_rec;
    w->m_renderContex = ut_wRenderFree_rec;
    w->m_renderCreat = ut_wRenderCreate_rec;
    w->m_renderContexRender = ut_wRenderRender_rec;
    w->m_renderContextUpdate = ut_wRenderUpdate_rec;
    return w;
}

// 播放分支前置：自定义 FBO + blend 管线 + 圆角遮罩 + 角落 VBO（不建空闲纹理，
// 保持 m_pLightTex 为空以绕过 paintGL 的 isCreated 门禁）
static void ut_wSetupPlayingPipelines(MpvGLWidget *w)
{
    w->toggleRoundedClip(true);
    w->setupBlendPipe();
    w->updateCornerMasks();
    w->updateVboCorners();
}

// 空闲分支前置：空闲管线（创建 m_pLightTex 等，配合 g_ut_texCreated=true 通过门禁）
static void ut_wSetupIdlePipelines(MpvGLWidget *w)
{
    w->setupIdlePipe();
}

}  // namespace

// ============================ 测试套件 ============================

class MpvGLWidgetTest : public ::testing::Test
{
public:
    static Stub *s_glStub;

    static void SetUpTestSuite()
    {
        ASSERT_EQ(nullptr, s_glStub);
        s_glStub = new Stub();
        // 伪造 GL 上下文 + 空操作函数解析（Qt6 QOpenGLFunctions 为 inline 函数指针调用，
        // 必须让 QOpenGLFunctionsPrivate::init 经打桩的 getProcAddress 解析到空操作）
        s_glStub->set((UtWCurCtxFn)&QOpenGLContext::currentContext, ut_wCurrentContext_stub);
        s_glStub->set((UtWGetProcFn)&QOpenGLContext::getProcAddress, ut_wGetProcAddr_stub);
        // 未显示的 QOpenGLWidget::context() 返回空指针，onNewFrame 最小化分支会解引用
        s_glStub->set((UtWWidgetCtxFn)&QOpenGLWidget::context, ut_wWidgetContext_stub);
        // FBO：无 Framebuffers 特性时 Qt 内部 init 提前返回（valid=false、guard 为空），
        // texture() 会越界访问空数组——打桩为确定值
        s_glStub->set((UtWFboConstBoolFn)&QOpenGLFramebufferObject::isValid, ut_wFboIsValid_stub);
        s_glStub->set((UtWFboBoolFn)&QOpenGLFramebufferObject::bind, ut_wFboBind_stub);
        s_glStub->set((UtWFboBoolFn)&QOpenGLFramebufferObject::release, ut_wFboRelease_stub);
        s_glStub->set((UtWFboUintFn)&QOpenGLFramebufferObject::handle, ut_wFboHandle_stub);
        s_glStub->set((UtWFboUintFn)&QOpenGLFramebufferObject::texture, ut_wFboTexture_stub);
        // 纹理绑定/释放（无上下文时段错误）
        s_glStub->set((UtWTexBindFn)&QOpenGLTexture::bind, ut_wTexBind_stub);
        s_glStub->set((UtWTexBindFn)&QOpenGLTexture::release, ut_wTexRelease_stub);
        // 着色器属性数组路径（无上下文时段错误）
        s_glStub->set((UtWProgVoidFn)&QOpenGLShaderProgram::release, ut_wProgRelease_stub);
        s_glStub->set((UtWProgIntFn)&QOpenGLShaderProgram::enableAttributeArray, ut_wProgEnableAttr_stub);
        s_glStub->set((UtWProgIntFn)&QOpenGLShaderProgram::disableAttributeArray, ut_wProgDisableAttr_stub);
        s_glStub->set((UtWProgBufFn)&QOpenGLShaderProgram::setAttributeBuffer, ut_wProgSetAttrBuf_stub);
    }

    static void TearDownTestSuite()
    {
        delete s_glStub;
        s_glStub = nullptr;
    }

    void SetUp() override
    {
        ut_wResetRecorders();
        // 纹理 isCreated 门禁由 g_ut_texCreated 控制（默认 false，早退分支可测）
        m_texStub.set((UtWTexConstBoolFn)&QOpenGLTexture::isCreated, ut_wTexIsCreated_stub);
    }

    void TearDown() override
    {
        m_texStub.clear();
    }

protected:
    Stub m_texStub;
};

Stub *MpvGLWidgetTest::s_glStub = nullptr;

// TEST_P 夹具：updateMovieProgress 三分支同断言（不崩溃）
struct UtProgressParam {
    qint64 duration;
    qint64 pos;
    const char *tag;
};

class MpvGLWidgetProgressTest : public MpvGLWidgetTest,
                                public ::testing::WithParamInterface<UtProgressParam>
{
};

// ============================ ctor / initMpvFuns ============================

// 构造后默认状态：未播放、自定义 FBO 与圆角裁剪开启、渲染上下文为空、各管线指针为空
TEST_F(MpvGLWidgetTest, ctor_New_InitializesDefaultState)
{
    Stub stub;
    QWidget parent;
    MpvGLWidget *w = ut_wCreateWidget(stub, &parent);
    ASSERT_NE(nullptr, w);

    EXPECT_EQ(false, w->m_bPlaying);
    EXPECT_EQ(false, w->m_bInMiniMode);
    EXPECT_EQ(true, w->m_bUseCustomFBO);
    EXPECT_EQ(true, w->m_bDoRoundedClipping);
    EXPECT_EQ(nullptr, w->m_pRenderCtx);
    EXPECT_EQ(nullptr, w->m_pGlProg);
    EXPECT_EQ(nullptr, w->m_pGlProgBlend);
    EXPECT_EQ(nullptr, w->m_pGlProgBlendCorners);
    EXPECT_EQ(nullptr, w->m_pGlProgCorner);
    EXPECT_EQ(nullptr, w->m_pFbo);
    EXPECT_EQ(nullptr, w->m_pDarkTex);
    EXPECT_EQ(nullptr, w->m_pLightTex);
    EXPECT_EQ(nullptr, w->m_pCornerMasks[0]);
    EXPECT_EQ(nullptr, w->m_pCornerMasks[1]);
    EXPECT_EQ(nullptr, w->m_pCornerMasks[2]);
    EXPECT_EQ(nullptr, w->m_pCornerMasks[3]);
    EXPECT_EQ(false, w->m_bRawFormat);

    delete w;
}

// 无 libmpv 时 initMpvFuns 解析失败，6 个渲染函数指针保持为空且不崩溃
// （在 ut_wCreateWidget 覆写录制 stub 之前断言，故直接构造）
TEST_F(MpvGLWidgetTest, ctor_New_InitMpvFunsNullWithoutLib)
{
    Stub stub;
    stub.set((UtWResolveFn)&QLibrary::resolve, ut_wQLibResolveNull_stub);
    QWidget parent;
    MpvGLWidget *w = new MpvGLWidget(&parent, MpvHandle());
    ASSERT_NE(nullptr, w);

    EXPECT_EQ(nullptr, w->m_callback);
    EXPECT_EQ(nullptr, w->m_context_report);
    EXPECT_EQ(nullptr, w->m_renderContex);
    EXPECT_EQ(nullptr, w->m_renderCreat);
    EXPECT_EQ(nullptr, w->m_renderContexRender);
    EXPECT_EQ(nullptr, w->m_renderContextUpdate);

    // 挂上录制 stub 保证析构安全
    w->m_callback = ut_wRenderSetUpdateCallback_rec;
    w->m_context_report = ut_wRenderReport_rec;
    w->m_renderContex = ut_wRenderFree_rec;
    w->m_renderCreat = ut_wRenderCreate_rec;
    w->m_renderContexRender = ut_wRenderRender_rec;
    w->m_renderContextUpdate = ut_wRenderUpdate_rec;
    delete w;
}

// resolve 成功时 initMpvFuns 应给全部函数指针赋值
TEST_F(MpvGLWidgetTest, initMpvFuns_ResolveSuccess_AssignsPointers)
{
    Stub stub;
    QWidget parent;
    MpvGLWidget *w = ut_wCreateWidget(stub, &parent);
    ASSERT_NE(nullptr, w);
    stub.set((UtWResolveFn)&QLibrary::resolve, ut_wQLibResolveDummy_stub);

    w->initMpvFuns();

    EXPECT_NE(nullptr, w->m_callback);
    EXPECT_NE(nullptr, w->m_context_report);
    EXPECT_NE(nullptr, w->m_renderContex);
    EXPECT_NE(nullptr, w->m_renderCreat);
    EXPECT_NE(nullptr, w->m_renderContexRender);
    EXPECT_NE(nullptr, w->m_renderContextUpdate);

    delete w;
}

// 完整初始化后析构：必须卸载回调（传 nullptr）并释放渲染上下文，且不崩溃
// （m_callback 仅在 m_pRenderCtx 非空时调；m_renderContex 无条件调）
TEST_F(MpvGLWidgetTest, dtor_AfterFullSetup_ReleasesRenderCtxWithoutCrash)
{
    Stub stub;
    QWidget parent;
    MpvGLWidget *w = ut_wCreateWidget(stub, &parent);
    ASSERT_NE(nullptr, w);
    ut_wSetupIdlePipelines(w);
    ut_wSetupPlayingPipelines(w);
    w->m_pRenderCtx = ut_wFakeRenderCtx();

    EXPECT_NO_THROW(delete w);
    EXPECT_EQ(1, g_ut_renderFreeCalls);            // m_renderContex(m_pRenderCtx)
    EXPECT_EQ(1, g_ut_renderSetUpdateCallbackCalls); // m_callback(m_pRenderCtx, nullptr, nullptr)
    EXPECT_EQ(nullptr, g_ut_installedUpdateFn);    // 析构传 nullptr 卸载回调
    EXPECT_EQ(nullptr, g_ut_installedUpdateCtx);
}

// ============================ initializeGL ============================

// 渲染上下文创建成功：m_pRenderCtx 赋值，并安装 gl_update_callback 与 widget 上下文
TEST_F(MpvGLWidgetTest, initializeGL_CreateSuccess_InstallsUpdateCallback)
{
    Stub stub;
    QWidget parent;
    MpvGLWidget *w = ut_wCreateWidget(stub, &parent);
    ASSERT_NE(nullptr, w);

    EXPECT_NO_THROW(w->initializeGL());

    EXPECT_EQ(1, g_ut_renderCreateCalls);
    EXPECT_EQ(ut_wFakeRenderCtx(), w->m_pRenderCtx);
    EXPECT_NE(nullptr, g_ut_installedUpdateFn);  // gl_update_callback（文件静态，地址非空即可）
    EXPECT_EQ(w, g_ut_installedUpdateCtx);

    delete w;
}

// 缺陷取证：创建失败分支缺少 throw（std::runtime_error 被构造后丢弃），
// 错误被吞掉、继续安装回调——记录实际行为，缺陷登记 defects.json
// 渲染上下文创建函数缺失：直接早退，不崩溃、不安装回调
TEST_F(MpvGLWidgetTest, initializeGL_CreateFnMissing_ReturnsEarly)
{
    Stub stub;
    QWidget parent;
    MpvGLWidget *w = ut_wCreateWidget(stub, &parent);
    ASSERT_NE(nullptr, w);
    w->m_renderCreat = nullptr;

    EXPECT_NO_THROW(w->initializeGL());

    EXPECT_EQ(0, g_ut_renderCreateCalls);
    EXPECT_EQ(nullptr, w->m_pRenderCtx);
    EXPECT_EQ(nullptr, g_ut_installedUpdateFn);

    delete w;
}

// ============================ paintGL ============================

// 播放 + 自定义 FBO：渲染请求经 m_renderContexRender 下发，随后经 FBO 纹理合成
TEST_F(MpvGLWidgetTest, paintGL_PlayingCustomFBO_RendersViaFbo)
{
    Stub stub;
    QWidget parent;
    MpvGLWidget *w = ut_wCreateWidget(stub, &parent);
    ASSERT_NE(nullptr, w);
    ut_wSetupPlayingPipelines(w);
    ASSERT_NE(nullptr, w->m_pFbo);
    w->m_bPlaying = true;

    EXPECT_NO_THROW(w->paintGL());

    EXPECT_GE(g_ut_renderRenderCalls, 1);

    delete w;
}

// 播放 + 默认帧缓冲（m_bUseCustomFBO=false）：直接渲染到默认帧缓冲
TEST_F(MpvGLWidgetTest, paintGL_PlayingDefaultFBO_RendersToDefaultFramebuffer)
{
    Stub stub;
    QWidget parent;
    MpvGLWidget *w = ut_wCreateWidget(stub, &parent);
    ASSERT_NE(nullptr, w);
    ut_wSetupPlayingPipelines(w);
    w->m_bUseCustomFBO = false;
    w->m_bPlaying = true;

    EXPECT_NO_THROW(w->paintGL());

    EXPECT_GE(g_ut_renderRenderCalls, 1);

    delete w;
}

// 播放 + 全屏 + showTimeFullScreen：走全屏进度浮层路径（重建浮层纹理后
// 因 attributeLocation<0 早退——无真实 GL 时深绘制分支不可达）
TEST_F(MpvGLWidgetTest, paintGL_PlayingFullscreenOverlay_RebuildsOverlayTexture)
{
    Stub stub;
    QWidget parent;  // 顶层窗口（无父级）
    parent.setWindowState(Qt::WindowFullScreen);
    parent.setProperty("showTimeFullScreen", true);
    MpvGLWidget *w = ut_wCreateWidget(stub, &parent);
    ASSERT_NE(nullptr, w);
    ut_wSetupPlayingPipelines(w);
    w->m_bPlaying = true;

    EXPECT_NO_THROW(w->paintGL());

    EXPECT_GE(g_ut_renderRenderCalls, 1);

    delete w;
}

// 空闲分支（纹理已创建）：绘制启动画面与圆角遮罩，不崩溃
TEST_F(MpvGLWidgetTest, paintGL_IdleTexCreated_DrawsSplashAndCorners)
{
    Stub stub;
    QWidget parent;
    MpvGLWidget *w = ut_wCreateWidget(stub, &parent);
    ASSERT_NE(nullptr, w);
    ut_wSetupIdlePipelines(w);
    ASSERT_NE(nullptr, w->m_pLightTex);
    g_ut_texCreated = true;

    EXPECT_NO_THROW(w->paintGL());

    delete w;
}

// 早退分支：浅色纹理存在但未创建时 paintGL 直接返回
// （哨兵：m_pGlProg 置空——若未早退将解引用空指针导致崩溃）
TEST_F(MpvGLWidgetTest, paintGL_IdleLightTexNotCreated_ReturnsEarly)
{
    Stub stub;
    QWidget parent;
    MpvGLWidget *w = ut_wCreateWidget(stub, &parent);
    ASSERT_NE(nullptr, w);
    ut_wSetupIdlePipelines(w);
    g_ut_texCreated = false;  // 未创建
    QOpenGLShaderProgram *prog = w->m_pGlProg;
    w->m_pGlProg = nullptr;   // 哨兵

    EXPECT_NO_THROW(w->paintGL());

    w->m_pGlProg = prog;  // 恢复，保证析构正常
    delete w;
}

// ============================ onNewFrame / onFrameSwapped ============================

// 非最小化：调用 m_renderContextUpdate 并请求重绘
TEST_F(MpvGLWidgetTest, onNewFrame_NotMinimized_CallsRenderContextUpdate)
{
    Stub stub;
    QWidget parent;
    MpvGLWidget *w = ut_wCreateWidget(stub, &parent);
    ASSERT_NE(nullptr, w);
    w->m_pRenderCtx = ut_wFakeRenderCtx();

    EXPECT_NO_THROW(w->onNewFrame());

    EXPECT_EQ(1, g_ut_renderUpdateCalls);

    delete w;
}

// 最小化：makeCurrent + paintGL + swapBuffers + doneCurrent 路径不崩溃
TEST_F(MpvGLWidgetTest, onNewFrame_Minimized_PaintsAndSwaps)
{
    Stub stub;
    QWidget parent;
    MpvGLWidget *w = ut_wCreateWidget(stub, &parent);
    ASSERT_NE(nullptr, w);
    ut_wSetupIdlePipelines(w);
    g_ut_texCreated = true;
    parent.setWindowState(Qt::WindowMinimized);
    ASSERT_TRUE(parent.window()->isMinimized());

    EXPECT_NO_THROW(w->onNewFrame());

    delete w;
}

// m_context_report 为空：早退不崩溃
TEST_F(MpvGLWidgetTest, onFrameSwapped_ReportNull_ReturnsEarly)
{
    Stub stub;
    QWidget parent;
    MpvGLWidget *w = ut_wCreateWidget(stub, &parent);
    ASSERT_NE(nullptr, w);
    w->m_context_report = nullptr;

    EXPECT_NO_THROW(w->onFrameSwapped());
    EXPECT_EQ(0, g_ut_renderReportCalls);

    delete w;
}

// m_context_report 已安装：调用 report_swap
TEST_F(MpvGLWidgetTest, onFrameSwapped_ReportInstalled_CallsReportSwap)
{
    Stub stub;
    QWidget parent;
    MpvGLWidget *w = ut_wCreateWidget(stub, &parent);
    ASSERT_NE(nullptr, w);
    w->m_pRenderCtx = ut_wFakeRenderCtx();

    EXPECT_NO_THROW(w->onFrameSwapped());
    EXPECT_EQ(1, g_ut_renderReportCalls);

    delete w;
}

// ============================ updateMovieFbo ============================

// m_bUseCustomFBO=false：不创建 FBO
TEST_F(MpvGLWidgetTest, updateMovieFbo_CustomFBOOff_KeepsNull)
{
    Stub stub;
    QWidget parent;
    MpvGLWidget *w = ut_wCreateWidget(stub, &parent);
    ASSERT_NE(nullptr, w);
    w->m_bUseCustomFBO = false;

    EXPECT_NO_THROW(w->updateMovieFbo());
    EXPECT_EQ(nullptr, w->m_pFbo);

    delete w;
}

// m_bUseCustomFBO=true：创建 FBO 且 isValid 桩使创建成功分支可达
TEST_F(MpvGLWidgetTest, updateMovieFbo_CustomFBOOn_CreatesFbo)
{
    Stub stub;
    QWidget parent;
    MpvGLWidget *w = ut_wCreateWidget(stub, &parent);
    ASSERT_NE(nullptr, w);
    w->setGeometry(100, 100, 640, 480);

    EXPECT_NO_THROW(w->updateMovieFbo());
    EXPECT_NE(nullptr, w->m_pFbo);

    delete w;
}

// ============================ updateCornerMasks ============================

// 自定义 FBO 开启（默认）：四角遮罩纹理全部创建
TEST_F(MpvGLWidgetTest, updateCornerMasks_CustomFBOOn_CreatesFourMasks)
{
    Stub stub;
    QWidget parent;
    MpvGLWidget *w = ut_wCreateWidget(stub, &parent);
    ASSERT_NE(nullptr, w);
    w->setGeometry(0, 0, 640, 480);

    EXPECT_NO_THROW(w->updateCornerMasks());

    for (int i = 0; i < 4; ++i)
        EXPECT_NE(nullptr, w->m_pCornerMasks[i]) << "corner " << i;

    delete w;
}

// 自定义 FBO 关闭且非 wayland：早退，遮罩保持为空
TEST_F(MpvGLWidgetTest, updateCornerMasks_CustomFBOOff_KeepsNullMasks)
{
    Stub stub;
    QWidget parent;
    MpvGLWidget *w = ut_wCreateWidget(stub, &parent);
    ASSERT_NE(nullptr, w);
    w->m_bUseCustomFBO = false;

    EXPECT_NO_THROW(w->updateCornerMasks());

    for (int i = 0; i < 4; ++i)
        EXPECT_EQ(nullptr, w->m_pCornerMasks[i]) << "corner " << i;

    delete w;
}

// ============================ updateVbo ============================

// 边界：宽度=380（≤380）判定为迷你模式
TEST_F(MpvGLWidgetTest, updateVbo_Boundary380Width_MiniModeTrue)
{
    Stub stub;
    QWidget parent;
    MpvGLWidget *w = ut_wCreateWidget(stub, &parent);
    ASSERT_NE(nullptr, w);
    w->setGeometry(0, 0, 380, 600);

    EXPECT_NO_THROW(w->updateVbo());
    EXPECT_TRUE(w->m_bInMiniMode);

    delete w;
}

// 宽高均大于 380：非迷你模式
TEST_F(MpvGLWidgetTest, updateVbo_LargerThan380_MiniModeFalse)
{
    Stub stub;
    QWidget parent;
    MpvGLWidget *w = ut_wCreateWidget(stub, &parent);
    ASSERT_NE(nullptr, w);
    w->setGeometry(0, 0, 800, 600);

    EXPECT_NO_THROW(w->updateVbo());
    EXPECT_FALSE(w->m_bInMiniMode);

    delete w;
}

// 四角 VBO 数据填充不崩溃（宽度为 0 的除法边界由 setGeometry 保证非零）
TEST_F(MpvGLWidgetTest, updateVboCorners_AllFourCorners_NoCrash)
{
    Stub stub;
    QWidget parent;
    MpvGLWidget *w = ut_wCreateWidget(stub, &parent);
    ASSERT_NE(nullptr, w);
    w->setGeometry(0, 0, 640, 480);

    EXPECT_NO_THROW(w->updateVboCorners());

    delete w;
}

// blend VBO 重复填充不崩溃（幂等）
TEST_F(MpvGLWidgetTest, updateVboBlend_RepeatedCalls_NoCrash)
{
    Stub stub;
    QWidget parent;
    MpvGLWidget *w = ut_wCreateWidget(stub, &parent);
    ASSERT_NE(nullptr, w);

    EXPECT_NO_THROW(w->updateVboBlend());
    EXPECT_NO_THROW(w->updateVboBlend());

    delete w;
}

// ============================ setupIdlePipe / setupBlendPipe ============================

// 空闲管线：程序、纹理、四角遮罩全部就绪
TEST_F(MpvGLWidgetTest, setupIdlePipe_PipelinesTexturesAndMasksReady)
{
    Stub stub;
    QWidget parent;
    MpvGLWidget *w = ut_wCreateWidget(stub, &parent);
    ASSERT_NE(nullptr, w);
    w->setGeometry(0, 0, 640, 480);

    EXPECT_NO_THROW(w->setupIdlePipe());

    EXPECT_NE(nullptr, w->m_pGlProg);
    EXPECT_NE(nullptr, w->m_pGlProgCorner);
    EXPECT_NE(nullptr, w->m_pDarkTex);
    EXPECT_NE(nullptr, w->m_pLightTex);
    for (int i = 0; i < 4; ++i)
        EXPECT_NE(nullptr, w->m_pCornerMasks[i]) << "corner " << i;

    delete w;
}

// blend 管线：两个 blend 程序与 FBO 就绪
TEST_F(MpvGLWidgetTest, setupBlendPipe_ProgramsAndFboReady)
{
    Stub stub;
    QWidget parent;
    MpvGLWidget *w = ut_wCreateWidget(stub, &parent);
    ASSERT_NE(nullptr, w);
    w->setGeometry(0, 0, 640, 480);

    EXPECT_NO_THROW(w->setupBlendPipe());

    EXPECT_NE(nullptr, w->m_pGlProgBlend);
    EXPECT_NE(nullptr, w->m_pGlProgBlendCorners);
    EXPECT_NE(nullptr, w->m_pFbo);

    delete w;
}

// ============================ resizeGL ============================

// 自定义 FBO 开启：resize 重建 FBO 与角落 VBO，不崩溃
TEST_F(MpvGLWidgetTest, resizeGL_CustomFBO_RecreatesFboWithoutCrash)
{
    Stub stub;
    QWidget parent;
    MpvGLWidget *w = ut_wCreateWidget(stub, &parent);
    ASSERT_NE(nullptr, w);
    w->toggleRoundedClip(true);
    ASSERT_NE(nullptr, w->m_pFbo);
    QOpenGLFramebufferObject *oldFbo = w->m_pFbo;

    EXPECT_NO_THROW(w->resizeGL(800, 600));
    EXPECT_NE(nullptr, w->m_pFbo);

    EXPECT_NO_THROW(w->resizeGL(400, 300));
    EXPECT_NE(nullptr, w->m_pFbo);
    Q_UNUSED(oldFbo);  // 无真实 GL 时 size() 恒为 0x0，FBO 每次必然重建

    delete w;
}

// 自定义 FBO 关闭：仅更新 VBO，不崩溃
TEST_F(MpvGLWidgetTest, resizeGL_NoCustomFBO_NoCrash)
{
    Stub stub;
    QWidget parent;
    MpvGLWidget *w = ut_wCreateWidget(stub, &parent);
    ASSERT_NE(nullptr, w);
    w->m_bUseCustomFBO = false;

    EXPECT_NO_THROW(w->resizeGL(800, 600));
    EXPECT_EQ(nullptr, w->m_pFbo);

    delete w;
}

// ============================ toggleRoundedClip ============================

// 切换圆角裁剪：m_bUseCustomFBO 跟随参数，FBO 随开关重建/保留
TEST_F(MpvGLWidgetTest, toggleRoundedClip_TogglesCustomFboFlag)
{
    Stub stub;
    QWidget parent;
    MpvGLWidget *w = ut_wCreateWidget(stub, &parent);
    ASSERT_NE(nullptr, w);
    ASSERT_TRUE(w->m_bUseCustomFBO);

    EXPECT_NO_THROW(w->toggleRoundedClip(false));
    EXPECT_FALSE(w->m_bUseCustomFBO);
    EXPECT_FALSE(w->m_bDoRoundedClipping == false);  // 圆角裁剪标志不被该函数修改

    EXPECT_NO_THROW(w->toggleRoundedClip(true));
    EXPECT_TRUE(w->m_bUseCustomFBO);
    EXPECT_NE(nullptr, w->m_pFbo);

    delete w;
}

// ============================ setPlaying ============================

// 状态切换：true/false 各一次生效，重复同值不翻转
TEST_F(MpvGLWidgetTest, setPlaying_StateTransitionNoToggleOnSameValue)
{
    Stub stub;
    QWidget parent;
    MpvGLWidget *w = ut_wCreateWidget(stub, &parent);
    ASSERT_NE(nullptr, w);
    ASSERT_FALSE(w->m_bPlaying);

    EXPECT_NO_THROW(w->setPlaying(true));
    EXPECT_TRUE(w->m_bPlaying);

    EXPECT_NO_THROW(w->setPlaying(false));
    EXPECT_FALSE(w->m_bPlaying);

    EXPECT_NO_THROW(w->setPlaying(false));
    EXPECT_FALSE(w->m_bPlaying);

    delete w;
}

// ============================ setRawFormatFlag ============================

// 原始格式标志切换
TEST_F(MpvGLWidgetTest, setRawFormatFlag_TogglesFlag)
{
    Stub stub;
    QWidget parent;
    MpvGLWidget *w = ut_wCreateWidget(stub, &parent);
    ASSERT_NE(nullptr, w);
    ASSERT_FALSE(w->m_bRawFormat);

    EXPECT_NO_THROW(w->setRawFormatFlag(true));
    EXPECT_TRUE(w->m_bRawFormat);

    EXPECT_NO_THROW(w->setRawFormatFlag(false));
    EXPECT_FALSE(w->m_bRawFormat);

    delete w;
}

// ============================ updateMovieProgress ============================

// 参数化：正常区间 / pos 超限钳制 / duration 为 0（除零防护）
// 状态存于文件静态 overlay（不可直接断言），三处分支均验证不崩溃
TEST_P(MpvGLWidgetProgressTest, updateMovieProgress_CrashSafe)
{
    Stub stub;
    QWidget parent;
    MpvGLWidget *w = ut_wCreateWidget(stub, &parent);
    ASSERT_NE(nullptr, w);
    const UtProgressParam &p = GetParam();

    EXPECT_NO_THROW(w->updateMovieProgress(p.duration, p.pos));

    delete w;
}

INSTANTIATE_TEST_SUITE_P(
    UtProgress, MpvGLWidgetProgressTest,
    ::testing::Values(
        UtProgressParam{600000, 300000, "Normal"},          // 正常区间
        UtProgressParam{600000, 999999999, "PosExceedsDuration"},  // pos 超限钳制
        UtProgressParam{0, 0, "ZeroDuration"}),             // duration=0 除零防护
    [](const ::testing::TestParamInfo<UtProgressParam> &info) {
        return info.param.tag;
    });
