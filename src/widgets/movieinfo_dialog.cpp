/*
 * Copyright (C) 2020 ~ 2021, Deepin Technology Co., Ltd. <support@deepin.org>
 *
 * Author:     xiangxiaojun <xiangxiaoju@uniontech.com>
 *
 * Maintainer: liuzheng <liuzheng@uniontech.com>
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
/**
 *@file 这个文件是影片信息窗口相关的类
 */
#include "movieinfo_dialog.h"
#include "mpv_proxy.h"
#include "playlist_model.h"
#include "utils.h"
#include "tip.h"

#include <QScrollArea>
#include <QDebug>

#include <DPushButton>

#include <denhancedwidget.h>

DWIDGET_USE_NAMESPACE

namespace dmr {
/**
 * @brief ElideText 根据字体大小调整字体缩减
 * @param text 全部的文本
 * @param size 控件的大小
 * @param wordWrap 文本包装的类型
 * @param font 字体
 * @param mode 指定省略号的位置
 * @param lineHeight 行高
 * @param lastLineWidth 保存的宽度
 * @return 返回省略后的文本
 */
static QString ElideText(const QString &text, const QSize &size,
                         QTextOption::WrapMode wordWrap, const QFont &font,
                         Qt::TextElideMode mode, int lineHeight, int lastLineWidth)
{
    QTextLayout textLayout(text);
    QString str;
    QFontMetrics fontMetrics(font);

    textLayout.setFont(font);
    const_cast<QTextOption *>(&textLayout.textOption())->setWrapMode(wordWrap);

    textLayout.beginLayout();
    QTextLine line = textLayout.createLine();
    line.setLineWidth(size.width());

    QString tmp_str = nullptr;
    if (fontMetrics.boundingRect(text).width() <= line.width()) {
        tmp_str = text.mid(line.textStart(), line.textLength());
        str = tmp_str;
    } else {
        int height = 0;
        while (line.isValid()) {
            line.setLineWidth(size.width());
            tmp_str = text.mid(line.textStart(), line.textLength());

            if (tmp_str.indexOf('\n'))
                height += lineHeight;

            str += tmp_str;
            line = textLayout.createLine();

            if (line.isValid())
                str.append("\n");
        }
    }

    textLayout.endLayout();

    return str;
}

/**
 * @brief The ToolTipEvent class
 * 鼠标悬停时的tooltip类
 */
class ToolTipEvent: public QObject
{
public:
    /**
     * @brief ToolTipEvent 构造函数
     * @param parent 父窗口
     */
    explicit ToolTipEvent(QObject *parent): QObject(parent) {}

protected:
    /**
     * @brief eventFilter 事件过滤器
     * @param obj 过滤的对象
     * @param event 事件
     * @return 是否继续执行
     */
    bool eventFilter(QObject *obj, QEvent *event)
    {
        switch (event->type()) {
//        case QEvent::Enter:
        case QEvent::ToolTip: {
            QHelpEvent *he = static_cast<QHelpEvent *>(event);
            auto tip = obj->property("HintWidget").value<Tip *>();
            auto btn = tip->property("for").value<QWidget *>();
            tip->setText(btn->toolTip());
            tip->show();
            tip->raise();
            tip->adjustSize();
            QPoint pos = he->globalPos() + QPoint{0, 0};
            int nDesktopWidth = qApp->desktop()->availableGeometry(btn).width();
            if (pos.x() + tip->width() > nDesktopWidth) {
                pos.rx() = nDesktopWidth - tip->width();
            }
            pos.ry() = pos.y() - tip->height();
            tip->move(pos);
            return true;
        }

        case QEvent::Leave: {
            auto parent = obj->property("HintWidget").value<Tip *>();
            parent->hide();
            event->ignore();
            break;
        }
        default:
            break;
        }

        return QObject::eventFilter(obj, event);
    }
};
/**
 * @brief The CloseButton class
 * 影片信息窗口关闭按钮
 */
class CloseButton : public DPushButton
{
public:
    /**
     * @brief CloseButton 构造函数
     * @param parent 父窗口
     */
    explicit CloseButton(QWidget *parent) {}
protected:
    /**
     * @brief paintEvent 重载绘制事件函数
     * @param pPaintEvent 绘制事件
     */
    void paintEvent(QPaintEvent *pPaintEvent) override
    {
        QPainter painter(this);
        QRect rect = this->rect();
        if (DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType()) {
            painter.drawPixmap(rect, QPixmap(INFO_CLOSE_LIGHT));
        } else if (DGuiApplicationHelper::DarkType == DGuiApplicationHelper::instance()->themeType()) {
            painter.drawPixmap(rect, QPixmap(INFO_CLOSE_DARK));
        } else {
            painter.drawPixmap(rect, QPixmap(INFO_CLOSE_LIGHT));
        }
    }
};
/**
 * @brief MovieInfoDialog 构造函数
 */
MovieInfoDialog::MovieInfoDialog(const struct PlayItemInfo &pif ,QWidget *parent)
    : DAbstractDialog(parent)
{
    initMember();
   if(utils::check_wayland_env()){
       setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);
   }else{
       setWindowFlags(windowFlags() /*| Qt::WindowStaysOnTopHint*/);  //和其他应用保持统一取消置顶
   }
    this->setObjectName(MOVIE_INFO_DIALOG);
    this->setAccessibleName(MOVIE_INFO_DIALOG);
    m_titleList.clear();

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setSpacing(0);
    layout->setContentsMargins(0, 0, 0, 0);
    setLayout(layout);

    CloseButton *closeBt = new CloseButton(this);
    closeBt->setObjectName(MOVIEINFO_CLOSE_BUTTON);
    closeBt->setAccessibleName(MOVIEINFO_CLOSE_BUTTON);
    closeBt->setFixedSize(50, 50);
    connect(closeBt, &CloseButton::clicked, this, &MovieInfoDialog::close);
    layout->addWidget(closeBt, 0, Qt::AlignTop | Qt::AlignRight);

    const MovieInfo &strMovieInfo = pif.mi;

    QVBoxLayout *pMainLayout = new QVBoxLayout;
    pMainLayout->setContentsMargins(10, 0, 0, 0);
    pMainLayout->setSpacing(0);
    layout->addLayout(pMainLayout);

    PosterFrame *pPosterFrame = new PosterFrame(this);
    pPosterFrame->setWindowOpacity(1);
    pPosterFrame->setFixedHeight(128);

    qreal pixelRatio = qApp->devicePixelRatio();
    QPixmap cover;
    if (pif.thumbnail.isNull()) {
        cover = (utils::LoadHiDPIPixmap(LOGO_BIG));
    } else {
        QSize sz(220, 128);
        sz *= pixelRatio;
        auto img = pif.thumbnail.scaledToWidth(sz.width(), Qt::SmoothTransformation);
        cover = img.copy(0, (img.height() - sz.height()) / 2, sz.width(), sz.height());
        cover.setDevicePixelRatio(pixelRatio);
    }
    cover = utils::MakeRoundedPixmap(cover, 8, 8);
    pPosterFrame->setPixmap(cover);
    pPosterFrame->ensurePolished();
    pMainLayout->addWidget(pPosterFrame);
    pMainLayout->setAlignment(pPosterFrame, Qt::AlignHCenter);
    pMainLayout->addSpacing(9);

    m_pFileNameLbl->setMinimumWidth(260);
    DFontSizeManager::instance()->bind(m_pFileNameLbl, DFontSizeManager::T8);
    m_pFileNameLbl->setForegroundRole(DPalette::BrightText);
    m_pFileNameLbl->setText(m_pFileNameLbl->fontMetrics().elidedText(QFileInfo(strMovieInfo.filePath).fileName(), Qt::ElideMiddle, 260));
    m_pFileNameLbl->setAlignment(Qt::AlignCenter);
    pMainLayout->addWidget(m_pFileNameLbl);
    pMainLayout->setAlignment(m_pFileNameLbl, Qt::AlignHCenter);
    pMainLayout->addSpacing(50);

    QList<DLabel *> tipLst;
    tipLst.clear();

    m_pScrollArea->setObjectName(MOVIE_INFO_SCROLL_AREA);
    m_pScrollArea->setAccessibleName(MOVIE_INFO_SCROLL_AREA);
    m_pScrollArea->viewport()->setObjectName(SCROLL_AREA_VIEWPORT);
    QPalette palette = m_pScrollArea->viewport()->palette();
    palette.setBrush(QPalette::Background, Qt::NoBrush);
    m_pScrollArea->viewport()->setPalette(palette);
    m_pScrollArea->setFrameShape(QFrame::Shape::NoFrame);
    pMainLayout->addWidget(m_pScrollArea);
    m_pScrollArea->setWidgetResizable(true);

    QWidget *scrollContentWidget = new QWidget(m_pScrollArea);
    scrollContentWidget->setObjectName(MOVIE_INFO_SCROLL_CONTENT);
    QVBoxLayout *scrollWidgetLayout = new QVBoxLayout;
    scrollWidgetLayout->setContentsMargins(0, 0, 10, 10);
    scrollWidgetLayout->setSpacing(10);
    scrollContentWidget->setLayout(scrollWidgetLayout);
    m_pScrollArea->setWidget(scrollContentWidget);
    m_pScrollArea->setWidgetResizable(true);
    m_pScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarPolicy::ScrollBarAlwaysOff);

    //添加基本信息
    ArrowLine *film = new ArrowLine;
    film->setObjectName(FILM_INFO_WIDGET);
    film->setTitle(tr("Film info"));
    InfoBottom *infoRect = new InfoBottom;
    scrollWidgetLayout->addWidget(film);
    scrollWidgetLayout->setAlignment(film, Qt::AlignHCenter);
    film->setContent(infoRect);
    film->setFixedWidth(280);
    infoRect->setFixedWidth(280);
    infoRect->setMinimumHeight(132);
    //infoRect->setFixedSize(280, 132);
    film->setExpand(true);
    m_expandGroup.append(film);
    QFormLayout *pFormLayout = new QFormLayout(infoRect);
    pFormLayout->setContentsMargins(10, 5, 20, 16);
    pFormLayout->setVerticalSpacing(6);
    pFormLayout->setHorizontalSpacing(10);
    pFormLayout->setLabelAlignment(Qt::AlignLeft);
    pFormLayout->setFormAlignment(Qt::AlignCenter);
    DEnhancedWidget *hanceedWidget = new DEnhancedWidget(film);
    connect(hanceedWidget, &DEnhancedWidget::heightChanged, this, &MovieInfoDialog::changedHeight);

    addRow(tr("Type"), strMovieInfo.fileType, pFormLayout, tipLst);
    addRow(tr("Size"), strMovieInfo.sizeStr(), pFormLayout, tipLst);
    addRow(tr("Duration"), strMovieInfo.durationStr(), pFormLayout, tipLst);
    DLabel *tmp = new DLabel;
    DFontSizeManager::instance()->bind(tmp, DFontSizeManager::T8);
    tmp->setText(strMovieInfo.filePath);
    QFontMetrics fontMetrics = tmp->fontMetrics();
    auto w = fontMetrics.width(strMovieInfo.filePath);
    addRow(tr("Path"), strMovieInfo.filePath, pFormLayout, tipLst);

    //添加视频信息
    ArrowLine *video = new ArrowLine;
    video->setObjectName(CODEC_INFO_WIDGET);
    video->setTitle(tr("Codec info"));
    InfoBottom *videoRect = new InfoBottom;
    scrollWidgetLayout->addWidget(video);
    scrollWidgetLayout->setAlignment(video, Qt::AlignHCenter);
    video->setContent(videoRect);
    video->setFixedSize(280, 136);
    videoRect->setFixedWidth(280);
    videoRect->setMinimumHeight(136);
    //videoRect->setFixedSize(280, 136);
    video->setExpand(true);
    m_expandGroup.append(video);
    QFormLayout *pVideoFormLayout = new QFormLayout(videoRect);
    pVideoFormLayout->setContentsMargins(10, 5, 20, 19);
    pVideoFormLayout->setVerticalSpacing(6);
    pVideoFormLayout->setHorizontalSpacing(10);
    pVideoFormLayout->setLabelAlignment(Qt::AlignLeft);
    pVideoFormLayout->setFormAlignment(Qt::AlignCenter);
    DEnhancedWidget *videoWidget = new DEnhancedWidget(video);
    connect(videoWidget, &DEnhancedWidget::heightChanged, this, &MovieInfoDialog::changedHeight);

    addRow(tr("Video CodecID"), strMovieInfo.videoCodec(), pVideoFormLayout, tipLst);
    addRow(tr("Video CodeRate"), QString(tr("%1 kbps")).arg(strMovieInfo.vCodeRate), pVideoFormLayout, tipLst);
    addRow(tr("FPS"), QString(tr("%1 fps")).arg(strMovieInfo.fps), pVideoFormLayout, tipLst);
    addRow(tr("Proportion"), QString(tr("%1")).arg(static_cast<double>(strMovieInfo.proportion)), pVideoFormLayout, tipLst);
    addRow(tr("Resolution"), strMovieInfo.resolution, pVideoFormLayout, tipLst);

    //添加音频信息
    ArrowLine *audio = new ArrowLine;
    audio->setObjectName(AUDIO_INFO_WIDGET);
    audio->setTitle(tr("Audio info"));
    InfoBottom *audioRect = new InfoBottom;
    scrollWidgetLayout->addWidget(audio);
    scrollWidgetLayout->setAlignment(audio, Qt::AlignHCenter);
    audio->setContent(audioRect);
    audio->setFixedWidth(280);
    audioRect->setFixedWidth(280);
    audioRect->setMinimumHeight(136);
    //audioRect->setFixedSize(280, 136);
    audio->setExpand(true);
    m_expandGroup.append(audio);
    auto *audioForm = new QFormLayout(audioRect);
    audioForm->setContentsMargins(10, 5, 20, 16);
    audioForm->setVerticalSpacing(6);
    audioForm->setHorizontalSpacing(10);
    audioForm->setLabelAlignment(Qt::AlignLeft);
    audioForm->setFormAlignment(Qt::AlignCenter);
    DEnhancedWidget *audioWidget = new DEnhancedWidget(audio);
    connect(audioWidget, &DEnhancedWidget::heightChanged, this, &MovieInfoDialog::changedHeight);

    addRow(tr("Audio CodecID"), strMovieInfo.audioCodec(), audioForm, tipLst);
    addRow(tr("Audio CodeRate"), QString(tr("%1 kbps")).arg(strMovieInfo.aCodeRate), audioForm, tipLst);
    addRow(tr("Audio digit"), QString(tr("%1 bits").arg(strMovieInfo.aDigit)), audioForm, tipLst);
    addRow(tr("Channels"), QString(tr("%1 channels")).arg(strMovieInfo.channels), audioForm, tipLst);
    addRow(tr("Sampling"), QString(tr("%1hz")).arg(strMovieInfo.sampling), audioForm, tipLst);

    setFixedSize(300, 642);

    if (!m_titleList.isEmpty()) {
        auto f = m_titleList[10]->fontMetrics();
        auto widget = f.boundingRect(m_titleList[10]->text()).width();
        if (widget > 60) {
            foreach (QLabel *l, m_titleList) {
                l->setFixedWidth(widget + 3);
            }
        }
    }

    auto th = new ToolTipEvent(this);
    if (tipLst.size() > 1) {
        DLabel *pFilePathLbl = tipLst.at(3);
        pFilePathLbl->setObjectName("filePathLabel");
        auto codeLabel = m_titleList.at(5);
        QFontMetrics fm_cl = codeLabel->fontMetrics();
        pFilePathLbl->setMinimumWidth(qMin(160, 250 - fm_cl.boundingRect(codeLabel->text()).width()));
        auto fp = ElideText(tmp->text(), {pFilePathLbl->width(), fm_cl.height()}, QTextOption::WrapAnywhere,
                            pFilePathLbl->font(), Qt::ElideRight, fm_cl.height(), pFilePathLbl->width());
        pFilePathLbl->setText(fp);
        m_pFilePathLbl = pFilePathLbl;
        m_sFilePath = tmp->text();
        pFilePathLbl->setToolTip(tmp->text());
        auto t = new Tip(QPixmap(), tmp->text(), nullptr);
        t->resetSize(QApplication::desktop()->availableGeometry().width());
        t->setProperty("for", QVariant::fromValue<QWidget *>(pFilePathLbl));
        pFilePathLbl->setProperty("HintWidget", QVariant::fromValue<QWidget *>(t));
        pFilePathLbl->installEventFilter(th);
//        filePathLbl->hide();
    }

    delete tmp;
    tmp = nullptr;

    connect(qApp, &QGuiApplication::fontChanged, this, &MovieInfoDialog::onFontChanged);
    connect(DApplicationHelper::instance(), &DApplicationHelper::themeTypeChanged, this, &MovieInfoDialog::slotThemeTypeChanged);

    m_expandGroup.at(0)->setExpand(true);
    m_expandGroup.at(1)->setExpand(true);
    m_expandGroup.at(2)->setExpand(true);
}
/**
 * @brief paintEvent 重载绘制事件函数
 * @param pPaintEvent 绘制事件
 */
void MovieInfoDialog::paintEvent(QPaintEvent *ev)
{
    QPainter painter(this);
    painter.fillRect(this->rect(), QColor(0, 0, 0, static_cast<int>(255 * 0.8)));
    QDialog::paintEvent(ev);
}
/**
 * @brief onFontChanged 字体变化槽函数（跟随系统变化）
 * @param font 变化后的字体
 */
void MovieInfoDialog::onFontChanged(const QFont &font)
{
    QFontMetrics fm(font);
    QString strFileName = m_pFileNameLbl->fontMetrics().elidedText(QFileInfo(m_sFilePath).fileName(), Qt::ElideMiddle, m_pFileNameLbl->width());
    m_pFileNameLbl->setText(strFileName);

    if (m_pFilePathLbl) {
        QString sFilePath = ElideText(m_sFilePath, {m_pFilePathLbl->width(), fm.height()}, QTextOption::WrapAnywhere,
                            m_pFilePathLbl->font(), Qt::ElideRight, fm.height(), m_pFilePathLbl->width());
        m_pFilePathLbl->setText(sFilePath);
    }
}
/**
 * @brief changedHeight 高度变化槽函数
 */
void MovieInfoDialog::changedHeight(const int height)
{
    if (m_nLastHeight == -1) {
        m_nLastHeight = height;
    } else {
        //xpf修改此过程
        int h = 10;
        if (m_expandGroup.at(0)->expand()) {
            h = h + 164;
        } else {
            h = h + 32;
        }
        if (m_expandGroup.at(1)->expand()) {
            h = h + 168 + 10;
        } else {
            h = h + 32 + 10;
        }
        if (m_expandGroup.at(2)->expand()) {
            h = h + 168 + 10;
        } else {
            h = h + 32 + 10;
        }
        h += 260;
        if (h > 642) {
            this->setFixedHeight(642);
        } else {
            setFixedHeight(h);
        }
        m_nLastHeight = -1;
    }
}
/**
 * @brief slotThemeTypeChanged
 * 主题变化槽函数
 */
void MovieInfoDialog::slotThemeTypeChanged()
{
    m_pFileNameLbl->setForegroundRole(DPalette::BrightText);
}
/**
 * @brief addRow 增加信息条目函数
 * @param title 信息名称
 * @param field 信息内容
 * @param form 布局管理
 * @retval tipList 传回当前添加信息的label控件
 */
void MovieInfoDialog::addRow(QString sTitle, QString sField, QFormLayout *pForm, QList<DLabel *> &tipList)
{
    auto f = new DLabel(sTitle, this);
    f->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    f->setMinimumSize(60, 20);
    QFont font = f->font();
    font.setPixelSize(12);
    font.setWeight(QFont::Weight::Normal);
    font.setFamily("SourceHanSansSC");
    auto t = new DLabel(sField, this);
    t->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    t->setMinimumHeight(20);
    t->setWordWrap(true);
    pForm->addRow(f, t);
    tipList.append(t);
    m_titleList.append(f);
}
/**
 * @brief initMember 初始化成员变量
 */
void MovieInfoDialog::initMember()
{
    m_pFileNameLbl = new DLabel(this);
    m_pFilePathLbl = nullptr;
    m_sFilePath = QString();
    m_pScrollArea = new QScrollArea;
    m_nLastHeight = -1;
}

}
