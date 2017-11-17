#include "movieinfo_dialog.h"
#include "mpv_proxy.h"
#include "playlist_model.h"
#include "utils.h"

#include <dwindowclosebutton.h>
#include <DThemeManager>

DWIDGET_USE_NAMESPACE

namespace dmr {
MovieInfoDialog::MovieInfoDialog(const struct PlayItemInfo& pif)
    :DAbstractDialog(nullptr)
{
    setFixedWidth(320);
    setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);

    DThemeManager::instance()->registerWidget(this);

    auto layout = new QVBoxLayout(this);
    layout->setSpacing(0);
    layout->setContentsMargins(0, 0, 0, 10);
    setLayout(layout);

    auto closeBt = new DWindowCloseButton;
    closeBt->setFixedSize(27, 23);
    layout->addWidget(closeBt, 0, Qt::AlignTop | Qt::AlignRight);
    layout->addSpacing(26);
    connect(closeBt, &DWindowCloseButton::clicked, this, &DAbstractDialog::hide);
    closeBt->setStyleSheet(DThemeManager::instance()->getQssForWidget("DWindowCloseButton", "light"));

    const auto& mi = pif.mi;

    auto *ml = new QVBoxLayout;
    ml->setContentsMargins(10, 0, 10, 0);
    ml->setSpacing(0);
    layout->addLayout(ml);

    auto *pm = new PosterFrame(this);
    pm->setFixedSize(176, 118);

    auto dpr = qApp->devicePixelRatio();
    QPixmap cover;
    if (pif.thumbnail.isNull()) {
        cover = (utils::LoadHiDPIPixmap(":/resources/icons/logo-big.svg"));
    } else {
        QSize sz(176, 118);
        sz *= dpr;
        auto img = pif.thumbnail.scaledToWidth(sz.width(), Qt::SmoothTransformation);
        cover = img.copy(0, (img.height()-sz.height())/2, sz.width(), sz.height());
        cover.setDevicePixelRatio(dpr);
    }
    cover = utils::MakeRoundedPixmap(cover, 4, 4);
    pm->setPixmap(cover);
    pm->ensurePolished();
    ml->addWidget(pm);
    ml->setAlignment(pm, Qt::AlignHCenter);
    ml->addSpacing(19);

    auto *nm = new QLabel(this);
    nm->setObjectName("MovieInfoTitle");
    nm->setText(nm->fontMetrics().elidedText(QFileInfo(mi.filePath).fileName(), Qt::ElideMiddle, 280));
    ml->addWidget(nm);
    ml->setAlignment(nm, Qt::AlignHCenter);
    ml->addSpacing(19);

    auto *sp = new QFrame(this);
    sp->setObjectName("MovieInfoSplit");
    sp->setFixedHeight(1);
    ml->addWidget(sp);
    ml->addSpacing(10);

    auto *form = new QFormLayout();
    form->setContentsMargins(25, 0, 25, 0);
    ml->addLayout(form);
    ml->setAlignment(ml, Qt::AlignHCenter);
    
    form->setVerticalSpacing(10);
    form->setHorizontalSpacing(10);
    form->setLabelAlignment(Qt::AlignRight);
    form->setFormAlignment(Qt::AlignCenter);

#define ADD_ROW(title, field)  do { \
    auto f = new QLabel(field, this); \
    f->setObjectName("MovieInfoValue"); \
    f->setWordWrap(true); \
    auto t = new QLabel((title), this); \
    t->setObjectName("MovieInfoKey"); \
    form->addRow(t, f); \
} while (0)

    ADD_ROW(tr("File Type:"), mi.fileType);
    ADD_ROW(tr("Resolution:"), mi.resolution);
    ADD_ROW(tr("File Size:"), mi.sizeStr());
    ADD_ROW(tr("Duration:"), mi.durationStr());

    auto fp = nm->fontMetrics().elidedText(mi.filePath, Qt::ElideMiddle, 320);
    ADD_ROW(tr("File Path:"), fp);

#undef ADD_ROW

    ml->addSpacing(16);
}
}
