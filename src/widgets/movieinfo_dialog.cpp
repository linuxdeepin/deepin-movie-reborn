#include "movieinfo_dialog.h"
#include "mpv_proxy.h"
#include "playlist_model.h"

DWIDGET_USE_NAMESPACE

namespace dmr {
MovieInfoDialog::MovieInfoDialog(const struct PlayItemInfo& pif)
    :DDialog(nullptr)
{
    setFixedWidth(320);

    const auto& mi = pif.mi;

    auto *ml = new QVBoxLayout;
    ml->setContentsMargins(0, 50, 0, 0);
    ml->setSpacing(4);

    auto *pm = new QLabel(this);
    if (pif.thumbnail.isNull())
        pm->setPixmap(QPixmap(":/resources/icons/logo-big.svg"));
    else {
        auto img = pif.thumbnail.scaledToHeight(120, Qt::SmoothTransformation);
        pm->setPixmap(img.copy((img.width()-100)/2, 0, 100, 120));
    }
    pm->ensurePolished();
    ml->addWidget(pm);
    ml->setAlignment(pm, Qt::AlignHCenter);

    auto *nm = new QLabel(this);
    nm->setText(nm->fontMetrics().elidedText(QFileInfo(mi.filePath).fileName(), Qt::ElideMiddle, 280));
    ml->addWidget(nm);
    ml->setAlignment(nm, Qt::AlignHCenter);

    auto *sp = new QFrame(this);
    sp->setFixedHeight(1);
    sp->setStyleSheet("background-color: rgba(0, 0, 0, 0.5)");
    ml->addWidget(sp);

    auto *form = new QFormLayout();
    ml->addLayout(form);
    
    form->setVerticalSpacing(16);
    form->setHorizontalSpacing(11);
    form->setLabelAlignment(Qt::AlignRight);
    form->setFormAlignment(Qt::AlignCenter);

#define ADD_ROW(title, field)  do { \
    auto d = new QLabel(field, this); \
    d->setWordWrap(true); \
    form->addRow(new QLabel(tr(title), this), d); \
} while (0)

    ADD_ROW("File Type", mi.fileType);
    ADD_ROW("Resolution", mi.resolution);
    ADD_ROW("File Size", mi.sizeStr());
    ADD_ROW("Duration", mi.durationStr());

    auto fp = nm->fontMetrics().elidedText(mi.filePath, Qt::ElideMiddle, 320);
    ADD_ROW("Path", fp);
    ADD_ROW("Creation Time", mi.creation);

#undef ADD_ROW

    QWidget  *mainContent = new QWidget;
    mainContent->setLayout(ml);
    addContent(mainContent, Qt::AlignCenter);
}
}
