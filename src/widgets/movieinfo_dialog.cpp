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
#include <QDebug>

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
    setFixedSize(300, 700);
    setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);
    setWindowOpacity(0.9);
    setAttribute(Qt::WA_TranslucentBackground, true);

    auto layout = new QVBoxLayout(this);
    layout->setSpacing(0);
    layout->setContentsMargins(0, 0, 0, 10);
    setLayout(layout);

    DImageButton *closeBt = new DImageButton(this);
    closeBt->setFixedSize(50, 50);
    connect(closeBt, &DImageButton::clicked, this, &MovieInfoDialog::close);
    layout->addWidget(closeBt, 0, Qt::AlignTop | Qt::AlignRight);

    const auto &mi = pif.mi;

    auto *ml = new QVBoxLayout;
    ml->setContentsMargins(10, 0, 10, 10);
    ml->setSpacing(0);
    layout->addLayout(ml);

    auto *pm = new PosterFrame(this);
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
    ml->addSpacing(10);

    m_fileNameLbl = new DLabel(this);
    qDebug() << "fileNameLbl w,h: " << m_fileNameLbl->width() << "," << m_fileNameLbl->height();
    DFontSizeManager::instance()->bind(m_fileNameLbl, DFontSizeManager::T8);
    m_fileNameLbl->setForegroundRole(DPalette::BrightText);
    m_fileNameLbl->setText(m_fileNameLbl->fontMetrics().elidedText(QFileInfo(mi.filePath).fileName(), Qt::ElideMiddle, 260));
    ml->addWidget(m_fileNameLbl);
    ml->setAlignment(m_fileNameLbl, Qt::AlignHCenter);
    ml->addSpacing(44);

    InfoBottom *infoRect = new InfoBottom;
//    DWidget *infoRect = new DWidget;
//    DPalette pal_infoRect = DApplicationHelper::instance()->palette(infoRect);
//    pal_infoRect.setBrush(DPalette::Background, pal_infoRect.color(DPalette::ItemBackground));
//    infoRect->setPalette(pal_infoRect);
    infoRect->setFixedSize(280, 136);
    ml->addWidget(infoRect);
    ml->setAlignment(infoRect, Qt::AlignHCenter);
//    ml->addSpacing(10);

    auto *form = new QFormLayout(infoRect);
    form->setContentsMargins(10, 5, 0, 30);
    form->setVerticalSpacing(6);
    form->setHorizontalSpacing(10);
    form->setLabelAlignment(Qt::AlignLeft);
    form->setFormAlignment(Qt::AlignCenter);

    QList<DLabel *> tipLst;
    tipLst.clear();
#define ADD_ROW(title, field)  do { \
    auto f = new DLabel(title, this); \
    f->setFixedHeight(45); \
    f->setAlignment(Qt::AlignLeft | Qt::AlignTop); \
    DFontSizeManager::instance()->bind(f, DFontSizeManager::T8); \
    f->setForegroundRole(DPalette::WindowText); \
    auto t = new DLabel(field, this); \
    t->setFixedHeight(60); \
    t->setAlignment(Qt::AlignLeft | Qt::AlignTop); \
    t->setWordWrap(true); \
    DFontSizeManager::instance()->bind(t, DFontSizeManager::T8); \
    t->setForegroundRole(DPalette::WindowText); \
    form->addRow(f, t); \
    tipLst.append(t); \
} while (0)

    auto title = new DLabel(tr("Film info"), this);
    DFontSizeManager::instance()->bind(title, DFontSizeManager::T6, QFont::Weight::Medium);
    title->setForegroundRole(DPalette::WindowText);
    form->addRow(title);

    //ADD_ROW(tr("Resolution"), mi.resolution);
    ADD_ROW(tr("Type"), mi.fileType);
    ADD_ROW(tr("Size"), mi.sizeStr());
    ADD_ROW(tr("Duration"), mi.durationStr());

    DLabel *tmp = new DLabel;
    DFontSizeManager::instance()->bind(tmp, DFontSizeManager::T8);
    tmp->setText(mi.filePath);
    auto fm = tmp->fontMetrics();
    auto w = fm.width(mi.filePath);
//    auto fp = ElideText(mi.filePath, {LINE_MAX_WIDTH, LINE_HEIGHT}, QTextOption::WrapAnywhere,
//                               tmp->font(), Qt::ElideRight, fm.height(), LINE_MAX_WIDTH);
    ADD_ROW(tr("Path"), mi.filePath);

    //添加视频信息
    InfoBottom *codecRect = new InfoBottom;
    codecRect->setFixedSize(280, 140);
    ml->addSpacing(10);
    ml->addWidget(codecRect);
    ml->setAlignment(codecRect, Qt::AlignHCenter);

    auto *codecForm = new QFormLayout(codecRect);
    codecForm->setContentsMargins(10, 5, 0, 15);
    codecForm->setVerticalSpacing(6);
    codecForm->setHorizontalSpacing(10);
    codecForm->setLabelAlignment(Qt::AlignLeft);
    codecForm->setFormAlignment(Qt::AlignCenter);

    auto codecTitle = new DLabel(tr("Codec info"), this);
    DFontSizeManager::instance()->bind(codecTitle, DFontSizeManager::T6, QFont::Weight::Medium);
    codecTitle->setForegroundRole(DPalette::WindowText);
    codecForm->addRow(codecTitle);

    addRow(tr("Video CodecID"), mi.videoCodec(), codecForm, tipLst);
    addRow(tr("Video CodeRate"), QString(tr("%1 kbps")).arg(mi.vCodeRate), codecForm, tipLst);
    addRow(tr("FPS"), QString(tr("%1 fps")).arg(mi.fps), codecForm, tipLst);
    addRow(tr("Proportion"), QString(tr("%1")).arg(mi.proportion), codecForm, tipLst);
    addRow(tr("Resolution"), mi.resolution, codecForm, tipLst);

    //添加音频信息
    InfoBottom *audioRect = new InfoBottom;
    audioRect->setFixedSize(280, 140);
    ml->addSpacing(10);
    ml->addWidget(audioRect);
    ml->setAlignment(audioRect, Qt::AlignHCenter);
//    ml->addSpacing(10);

    auto *audioForm = new QFormLayout(audioRect);
    audioForm->setContentsMargins(10, 5, 0, 15);
    audioForm->setVerticalSpacing(6);
    audioForm->setHorizontalSpacing(10);
    audioForm->setLabelAlignment(Qt::AlignLeft);
    audioForm->setFormAlignment(Qt::AlignCenter);

    auto audioTitle = new DLabel(tr("Audio info"), this);
    DFontSizeManager::instance()->bind(audioTitle, DFontSizeManager::T6, QFont::Weight::Medium);
    audioTitle->setForegroundRole(DPalette::WindowText);
    audioForm->addRow(audioTitle);

    addRow(tr("Audio CodecID"), mi.audioCodec(), audioForm, tipLst);
    addRow(tr("Audio CodeRate"), QString(tr("%1 kbps")).arg(mi.aCodeRate), audioForm, tipLst);
    addRow(tr("Audio digit"), QString(tr("%1 bits").arg(mi.aDigit)), audioForm, tipLst);
    addRow(tr("Channels"), QString(tr("%1 channels")).arg(mi.channels), audioForm, tipLst);
    addRow(tr("Sampling"), QString(tr("%1hz")).arg(mi.sampling), audioForm, tipLst);

    auto th = new ToolTipEvent(this);
    if (tipLst.size() > 1) {
        auto filePathLbl = tipLst.last();
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

#undef ADD_ROW

    connect(qApp, &QGuiApplication::fontChanged, this, &MovieInfoDialog::OnFontChanged);
    connect(DApplicationHelper::instance(), &DApplicationHelper::themeTypeChanged, this, [ = ] {
        m_fileNameLbl->setForegroundRole(DPalette::BrightText);
        title->setForegroundRole(DPalette::Text);
    });

    if (DGuiApplicationHelper::LightType == DGuiApplicationHelper::instance()->themeType()) {
        closeBt->setNormalPic(INFO_CLOSE_LIGHT);
    } else if (DGuiApplicationHelper::DarkType == DGuiApplicationHelper::instance()->themeType()) {
        closeBt->setNormalPic(INFO_CLOSE_DARK);
    } else {
        closeBt->setNormalPic(INFO_CLOSE_LIGHT);
    }

//#if DTK_VERSION > DTK_VERSION_CHECK(2, 0, 6, 0)
//    DThemeManager::instance()->setTheme(this, "light");
//    DThemeManager::instance()->setTheme(closeBt, "light");
//#else
////    DThemeManager::instance()->registerWidget(this);
////    closeBt->setStyleSheet(DThemeManager::instance()->getQssForWidget("DWindowCloseButton", "light"));
    //#endif
}

void MovieInfoDialog::OnFontChanged(const QFont &font)
{
    QFontMetrics fm(font);

    qDebug() << "fileNameLbl w,h: " << m_fileNameLbl->width() << "," << m_fileNameLbl->height();
    QString strFileName = m_fileNameLbl->fontMetrics().elidedText(QFileInfo(m_strFilePath).fileName(), Qt::ElideMiddle, m_fileNameLbl->width());
    m_fileNameLbl->setText(strFileName);

    qDebug() << "filePathLbl w,h: " << m_filePathLbl->width() << "," << m_filePathLbl->height();
    auto w = fm.width(m_strFilePath);
    qDebug() << "font width: " << w;
    auto fp = ElideText(m_strFilePath, {m_filePathLbl->width(), fm.height()}, QTextOption::WrapAnywhere,
                        m_filePathLbl->font(), Qt::ElideRight, fm.height(), m_filePathLbl->width());
    m_filePathLbl->setText(fp);

}

void MovieInfoDialog::addRow(QString title, QString field, QFormLayout *form, QList<DLabel *> tipLst)
{
    auto f = new DLabel(title, this);
    f->setFixedHeight(45);
    f->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    DFontSizeManager::instance()->bind(f, DFontSizeManager::T8);
    f->setForegroundRole(DPalette::WindowText);
    auto t = new DLabel(field, this);
    t->setFixedHeight(60);
    t->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    t->setWordWrap(true);
    DFontSizeManager::instance()->bind(t, DFontSizeManager::T8);
    t->setForegroundRole(DPalette::WindowText);
    form->addRow(f, t);
    tipLst.append(t);
}

}
