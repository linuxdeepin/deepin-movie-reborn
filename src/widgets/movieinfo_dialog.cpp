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
#include "movieinfo_dialog.h"
#include "mpv_proxy.h"
#include "playlist_model.h"
#include "utils.h"
#include "tip.h"

#include <QScrollArea>
#include <QDebug>

#include <denhancedwidget.h>

DWIDGET_USE_NAMESPACE

//#define MV_BASE_INFO tr("Film info")
//#define MV_FILE_TYPE tr("File type")
//#define MV_RESOLUTION tr("Resolution")
//#define MV_FILE_SIZE tr("File size")
//#define MV_DURATION tr("Duration")
//#define MV_FILE_PATH tr("File path")

namespace dmr {
static QString ElideText(const QString &text, const QSize &size,
                         QTextOption::WrapMode wordWrap, const QFont &font,
                         Qt::TextElideMode mode, int lineHeight, int lastLineWidth)
{
    int height = 0;

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
        while (line.isValid()) {
            height += lineHeight;
            line.setLineWidth(size.width());

            if (textLayout.lineCount() == 2) {
                QStringList strLst;
                if (!text.isEmpty()) {
                    strLst = text.split(tmp_str);
                }

                if (fontMetrics.boundingRect(strLst.last()).width() > line.width() - 1) {
                    str += fontMetrics.elidedText(strLst.last(), mode, lastLineWidth);
                    break;
                }
            }

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

class ToolTipEvent: public QObject
{
public:
    ToolTipEvent(QObject *parent): QObject(parent) {}

protected:
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

//            QPoint pos = btn->pos();
//            pos.rx() = tip->parentWidget()->rect().width()/2 - tip->width()/2;
//            pos.ry() = tip->parentWidget()->rect().bottom() - tip->height() - btn->height() - 15;
            auto pos = he->globalPos() + QPoint{0, 0};
            auto dw = qApp->desktop()->availableGeometry(btn).width();
            if (pos.x() + tip->width() > dw) {
                pos.rx() = dw - tip->width();
            }
            pos.ry() = pos.y() - tip->height();
            tip->move(pos);
            return true;
        }

        case QEvent::Leave: {
            auto parent = obj->property("HintWidget").value<Tip *>();
            parent->hide();
            event->ignore();

        }
        default:
            break;
        }

        return QObject::eventFilter(obj, event);
    }
};

MovieInfoDialog::MovieInfoDialog(const struct PlayItemInfo &pif)
    : DAbstractDialog(nullptr)
{
    setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    m_titleList.clear();

    auto layout = new QVBoxLayout(this);
    layout->setSpacing(0);
    layout->setContentsMargins(0, 0, 0, 0);
    setLayout(layout);

    DImageButton *closeBt = new DImageButton(this);
    closeBt->setFixedSize(50, 50);
    connect(closeBt, &DImageButton::clicked, this, &MovieInfoDialog::close);
    layout->addWidget(closeBt, 0, Qt::AlignTop | Qt::AlignRight);

    const auto &mi = pif.mi;

    auto *ml = new QVBoxLayout;
    ml->setContentsMargins(10, 0, 0, 0);
    ml->setSpacing(0);
    layout->addLayout(ml);

    auto *pm = new PosterFrame(this);
    pm->setWindowOpacity(1);
    pm->setFixedHeight(128);

    auto dpr = qApp->devicePixelRatio();
    QPixmap cover;
    if (pif.thumbnail.isNull()) {
        cover = (utils::LoadHiDPIPixmap(LOGO_BIG));
    } else {
        QSize sz(220, 128);
        sz *= dpr;
        auto img = pif.thumbnail.scaledToWidth(sz.width(), Qt::SmoothTransformation);
        cover = img.copy(0, (img.height() - sz.height()) / 2, sz.width(), sz.height());
        cover.setDevicePixelRatio(dpr);
    }
    cover = utils::MakeRoundedPixmap(cover, 8, 8);
    pm->setPixmap(cover);
    pm->ensurePolished();
    ml->addWidget(pm);
    ml->setAlignment(pm, Qt::AlignHCenter);
    ml->addSpacing(9);

    m_fileNameLbl = new DLabel(this);
    qDebug() << "fileNameLbl w,h: " << m_fileNameLbl->width() << "," << m_fileNameLbl->height();
    DFontSizeManager::instance()->bind(m_fileNameLbl, DFontSizeManager::T8);
    m_fileNameLbl->setForegroundRole(DPalette::BrightText);
    m_fileNameLbl->setText(m_fileNameLbl->fontMetrics().elidedText(QFileInfo(mi.filePath).fileName(), Qt::ElideMiddle, 260));
    ml->addWidget(m_fileNameLbl);
    ml->setAlignment(m_fileNameLbl, Qt::AlignHCenter);
    ml->addSpacing(50);

    QList<DLabel *> tipLst;
    tipLst.clear();

    m_scrollArea = new QScrollArea;
    QPalette palette = m_scrollArea->viewport()->palette();
    palette.setBrush(QPalette::Background, Qt::NoBrush);
    m_scrollArea->viewport()->setPalette(palette);
    m_scrollArea->setFrameShape(QFrame::Shape::NoFrame);
    ml->addWidget(m_scrollArea);
    m_scrollArea->setWidgetResizable(true);

    QWidget *scrollContentWidget = new QWidget(m_scrollArea);
    QVBoxLayout *scrollWidgetLayout = new QVBoxLayout;
    scrollWidgetLayout->setContentsMargins(0, 0, 10, 10);
    scrollWidgetLayout->setSpacing(10);
    scrollContentWidget->setLayout(scrollWidgetLayout);
    m_scrollArea->setWidget(scrollContentWidget);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarPolicy::ScrollBarAlwaysOff);

    ArrowLine *film = new ArrowLine;
    film->setTitle(tr("Film info"));
    InfoBottom *infoRect = new InfoBottom;
    scrollWidgetLayout->addWidget(film);
    scrollWidgetLayout->setAlignment(film, Qt::AlignHCenter);
    film->setContent(infoRect);
    film->setFixedWidth(280);
    infoRect->setFixedSize(280, 132);
    film->setExpand(true);
    m_expandGroup.append(film);
    auto *form = new QFormLayout(infoRect);
    form->setContentsMargins(10, 5, 20, 30);
    form->setVerticalSpacing(6);
    form->setHorizontalSpacing(10);
    form->setLabelAlignment(Qt::AlignLeft);
    form->setFormAlignment(Qt::AlignCenter);
    DEnhancedWidget *hanceedWidget = new DEnhancedWidget(film);
    connect(hanceedWidget, &DEnhancedWidget::heightChanged, this, &MovieInfoDialog::changedHeight);

    addRow(tr("Type"), mi.fileType, form, tipLst);
    addRow(tr("Size"), mi.sizeStr(), form, tipLst);
    addRow(tr("Duration"), mi.durationStr(), form, tipLst);
    DLabel *tmp = new DLabel;
    DFontSizeManager::instance()->bind(tmp, DFontSizeManager::T8);
    tmp->setText(mi.filePath);
    auto fm = tmp->fontMetrics();
    auto w = fm.width(mi.filePath);
    addRow(tr("Path"), mi.filePath, form, tipLst);

    //添加视频信息
    ArrowLine *video = new ArrowLine;
    video->setTitle(tr("Codec info"));
    InfoBottom *videoRect = new InfoBottom;
    scrollWidgetLayout->addWidget(video);
    scrollWidgetLayout->setAlignment(video, Qt::AlignHCenter);
    video->setContent(videoRect);
    video->setFixedSize(280, 136);
    videoRect->setFixedSize(280, 136);
    video->setExpand(true);
    m_expandGroup.append(video);
    auto *videoForm = new QFormLayout(videoRect);
    videoForm->setContentsMargins(10, 5, 20, 19);
    videoForm->setVerticalSpacing(6);
    videoForm->setHorizontalSpacing(10);
    videoForm->setLabelAlignment(Qt::AlignLeft);
    videoForm->setFormAlignment(Qt::AlignCenter);
    DEnhancedWidget *videoWidget = new DEnhancedWidget(video);
    connect(videoWidget, &DEnhancedWidget::heightChanged, this, &MovieInfoDialog::changedHeight);

    addRow(tr("Video CodecID"), mi.videoCodec(), videoForm, tipLst);
    addRow(tr("Video CodeRate"), QString(tr("%1 kbps")).arg(mi.vCodeRate), videoForm, tipLst);
    addRow(tr("FPS"), QString(tr("%1 fps")).arg(mi.fps), videoForm, tipLst);
    addRow(tr("Proportion"), QString(tr("%1")).arg(mi.proportion), videoForm, tipLst);
    addRow(tr("Resolution"), mi.resolution, videoForm, tipLst);

    //添加音频信息
    ArrowLine *audio = new ArrowLine;
    audio->setTitle(tr("Audio info"));
    InfoBottom *audioRect = new InfoBottom;
    scrollWidgetLayout->addWidget(audio);
    scrollWidgetLayout->setAlignment(audio, Qt::AlignHCenter);
    audio->setContent(audioRect);
    audio->setFixedWidth(280);
    audioRect->setFixedSize(280, 136);
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

    addRow(tr("Audio CodecID"), mi.audioCodec(), audioForm, tipLst);
    addRow(tr("Audio CodeRate"), QString(tr("%1 kbps")).arg(mi.aCodeRate), audioForm, tipLst);
    addRow(tr("Audio digit"), QString(tr("%1 bits").arg(mi.aDigit)), audioForm, tipLst);
    addRow(tr("Channels"), QString(tr("%1 channels")).arg(mi.channels), audioForm, tipLst);
    addRow(tr("Sampling"), QString(tr("%1hz")).arg(mi.sampling), audioForm, tipLst);

    setFixedSize(300, 642);

    /*InfoBottom *infoRect = new InfoBottom;
    infoRect->setFixedSize(280, 135);
    ml->addWidget(infoRect);
    ml->setAlignment(infoRect, Qt::AlignHCenter);

    auto *form = new QFormLayout(infoRect);
    form->setContentsMargins(10, 10, 20, 25);
    form->setVerticalSpacing(6);
    form->setHorizontalSpacing(10);
    form->setLabelAlignment(Qt::AlignLeft);
    form->setFormAlignment(Qt::AlignCenter);

    QList<DLabel *> tipLst;
    tipLst.clear();

    auto title = new DLabel(tr("Film info"), this);
    QFont font = title->font();
    font.setPixelSize(14);
    font.setWeight(QFont::Weight::Medium);
    font.setFamily("SourceHanSansSC");
    title->setFont(font);
    form->addRow(title);

    addRow(tr("Type"), mi.fileType, form, tipLst);
    addRow(tr("Size"), mi.sizeStr(), form, tipLst);
    addRow(tr("Duration"), mi.durationStr(), form, tipLst);

    DLabel *tmp = new DLabel;
    DFontSizeManager::instance()->bind(tmp, DFontSizeManager::T8);
    tmp->setText(mi.filePath);
    auto fm = tmp->fontMetrics();
    auto w = fm.width(mi.filePath);
    addRow(tr("Path"), mi.filePath, form, tipLst);

    //添加视频信息
    InfoBottom *codecRect = new InfoBottom;
    codecRect->setFixedSize(280, 145);
    ml->addSpacing(10);
    ml->addWidget(codecRect);
    ml->setAlignment(codecRect, Qt::AlignHCenter);

    auto *codecForm = new QFormLayout(codecRect);
    codecForm->setContentsMargins(10, 10, 20, 10);
    codecForm->setVerticalSpacing(6);
    codecForm->setHorizontalSpacing(10);
    codecForm->setLabelAlignment(Qt::AlignLeft);
    codecForm->setFormAlignment(Qt::AlignCenter);

    auto codecTitle = new DLabel(tr("Codec info"), this);
    codecTitle->setFont(font);
    codecForm->addRow(codecTitle);

    addRow(tr("Video CodecID"), mi.videoCodec(), codecForm, tipLst);
    addRow(tr("Video CodeRate"), QString(tr("%1 kbps")).arg(mi.vCodeRate), codecForm, tipLst);
    addRow(tr("FPS"), QString(tr("%1 fps")).arg(mi.fps), codecForm, tipLst);
    addRow(tr("Proportion"), QString(tr("%1")).arg(mi.proportion), codecForm, tipLst);
    addRow(tr("Resolution"), mi.resolution, codecForm, tipLst);

    //添加音频信息
    InfoBottom *audioRect = new InfoBottom;
    audioRect->setFixedSize(280, 145);
    ml->addSpacing(10);
    ml->addWidget(audioRect);
    ml->setAlignment(audioRect, Qt::AlignHCenter);
    //    ml->addSpacing(10);

    auto *audioForm = new QFormLayout(audioRect);
    audioForm->setContentsMargins(10, 10, 20, 10);
    audioForm->setVerticalSpacing(6);
    audioForm->setHorizontalSpacing(10);
    audioForm->setLabelAlignment(Qt::AlignLeft);
    audioForm->setFormAlignment(Qt::AlignCenter);

    auto audioTitle = new DLabel(tr("Audio info"), this);
    audioTitle->setFont(font);
    title->setFont(font);
    audioForm->addRow(audioTitle);

    addRow(tr("Audio CodecID"), mi.audioCodec(), audioForm, tipLst);
    addRow(tr("Audio CodeRate"), QString(tr("%1 kbps")).arg(mi.aCodeRate), audioForm, tipLst);
    addRow(tr("Audio digit"), QString(tr("%1 bits").arg(mi.aDigit)), audioForm, tipLst);
    addRow(tr("Channels"), QString(tr("%1 channels")).arg(mi.channels), audioForm, tipLst);
    addRow(tr("Sampling"), QString(tr("%1hz")).arg(mi.sampling), audioForm, tipLst);*/

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
        auto filePathLbl = tipLst.at(3);
        qDebug() << "filePathLbl w,h: " << filePathLbl->width() << "," << filePathLbl->height();
        filePathLbl->setMinimumWidth(190);
        qDebug() << "filePathLbl w,h: " << filePathLbl->width() << "," << filePathLbl->height();
        auto fp = ElideText(tmp->text(), {filePathLbl->width(), fm.height()}, QTextOption::WrapAnywhere,
                            filePathLbl->font(), Qt::ElideRight, fm.height(), filePathLbl->width());
        filePathLbl->setText(fp);
        m_filePathLbl = filePathLbl;
        m_strFilePath = tmp->text();
        filePathLbl->setFixedHeight(LINE_HEIGHT * 2);
        filePathLbl->setToolTip(tmp->text());
        auto t = new Tip(QPixmap(), tmp->text(), nullptr);
        t->resetSize(QApplication::desktop()->availableGeometry().width());
        t->setProperty("for", QVariant::fromValue<QWidget *>(filePathLbl));
        filePathLbl->setProperty("HintWidget", QVariant::fromValue<QWidget *>(t));
        filePathLbl->installEventFilter(th);
    }

    delete tmp;
    tmp = nullptr;

    connect(qApp, &QGuiApplication::fontChanged, this, &MovieInfoDialog::OnFontChanged);
    connect(DApplicationHelper::instance(), &DApplicationHelper::themeTypeChanged, this, [ = ] {
        m_fileNameLbl->setForegroundRole(DPalette::BrightText);
        //title->setForegroundRole(DPalette::Text);
    });

    if (DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType()) {
        closeBt->setNormalPic(INFO_CLOSE_LIGHT);
    } else if (DGuiApplicationHelper::DarkType == DGuiApplicationHelper::instance()->themeType()) {
        closeBt->setNormalPic(INFO_CLOSE_DARK);
    } else {
        closeBt->setNormalPic(INFO_CLOSE_LIGHT);
    }
    m_expandGroup.at(0)->setExpand(true);
    m_expandGroup.at(1)->setExpand(true);
    m_expandGroup.at(2)->setExpand(true);
}

void MovieInfoDialog::paintEvent(QPaintEvent *ev)
{
    QPainter painter(this);
    painter.fillRect(this->rect(), QColor(0, 0, 0, 255 * 0.8));
    QDialog::paintEvent(ev);
}

void MovieInfoDialog::OnFontChanged(const QFont &font)
{
    QFontMetrics fm(font);

    qDebug() << "fileNameLbl w,h: " << m_fileNameLbl->width() << "," << m_fileNameLbl->height();
    QString strFileName = m_fileNameLbl->fontMetrics().elidedText(QFileInfo(m_strFilePath).fileName(), Qt::ElideMiddle, m_fileNameLbl->width());
    m_fileNameLbl->setText(strFileName);

    if (m_filePathLbl) {
        qDebug() << "filePathLbl w,h: " << m_filePathLbl->width() << "," << m_filePathLbl->height();
        auto w = fm.width(m_strFilePath);
        qDebug() << "font width: " << w;
        auto fp = ElideText(m_strFilePath, {m_filePathLbl->width(), fm.height()}, QTextOption::WrapAnywhere,
                            m_filePathLbl->font(), Qt::ElideRight, fm.height(), m_filePathLbl->width());
        m_filePathLbl->setText(fp);
    }
}

void MovieInfoDialog::changedHeight(const int height)
{
    if (lastHeight == -1) {
        lastHeight = height;
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
//        foreach (DDrawer *drawer, m_expandGroup) {
//            h = h + drawer->height() + 10;
//        }
        h += 260;
        if (h > 642) {
            this->setFixedHeight(642);
        } else {
            setFixedHeight(h);
            //QTimer::singleShot(50, this, [ = ] {this->setFixedHeight(h);});
        }
        lastHeight = -1;
    }
}

void MovieInfoDialog::addRow(QString title, QString field, QFormLayout *form, QList<DLabel *> &tipLst)
{
    auto f = new DLabel(title, this);
    f->setFixedSize(60, 20);
    f->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    //DFontSizeManager::instance()->bind(f, DFontSizeManager::T8, 0);
    //f->setForegroundRole(DPalette::WindowText);
    QFont font = f->font();
    font.setPixelSize(12);
    font.setWeight(QFont::Weight::Normal);
    font.setFamily("SourceHanSansSC");
    f->setFont(font);
    auto t = new DLabel(field, this);
    t->setFixedHeight(60);
    t->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    t->setWordWrap(true);
    t->setFont(font);
    form->addRow(f, t);
    tipLst.append(t);
    m_titleList.append(f);
}

}
