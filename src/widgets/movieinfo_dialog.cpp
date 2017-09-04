#include "movieinfo_dialog.h"
#include "mpv_proxy.h"
#include "playlist_model.h"

#include <dwindowclosebutton.h>
#include <DThemeManager>

DWIDGET_USE_NAMESPACE

namespace dmr {
class PosterFrame: public QLabel { 
public:
    PosterFrame(QWidget* parent) :QLabel(parent) {
        setStyleSheet(
                "dmr--PosterFrame {"
                "border-radius: 6px;"
                "border: 1px solid rgba(255, 255, 255, 0.1); }");
        auto e = new QGraphicsDropShadowEffect(this);
        //box-shadow: 0 2px 4px 0 rgba(0, 0, 0, 0.2);
        e->setColor(qRgba(0, 0, 0, 20));
        e->setOffset(2, 2);
        e->setBlurRadius(4);
        setGraphicsEffect(e);
    }
};
MovieInfoDialog::MovieInfoDialog(const struct PlayItemInfo& pif)
    :DAbstractDialog(nullptr)
{
    setFixedWidth(320);
    setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);

    DThemeManager::instance()->registerWidget(this);

    auto layout = new QVBoxLayout(this);
    layout->setSpacing(0);
    layout->setMargin(5);
    setLayout(layout);

    auto closeBt = new DWindowCloseButton;
    closeBt->setFixedSize(27, 23);
    layout->addWidget(closeBt, 0, Qt::AlignTop | Qt::AlignRight);
    layout->addSpacing(26);
    connect(closeBt, &DWindowCloseButton::clicked, this, &DAbstractDialog::hide);

    const auto& mi = pif.mi;

    auto *ml = new QVBoxLayout;
    ml->setContentsMargins(0, 0, 0, 0);
    ml->setSpacing(0);
    layout->addLayout(ml);

    auto *pm = new PosterFrame(this);
    if (pif.thumbnail.isNull())
        pm->setPixmap(QPixmap(":/resources/icons/logo-big.svg"));
    else {
        auto img = pif.thumbnail.scaledToWidth(176, Qt::SmoothTransformation);
        pm->setPixmap(img.copy((img.width()-100)/2, 0, 176, 118));
    }
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
    form->setContentsMargins(30, 0, 30, 0);
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
    ADD_ROW(tr("Path:"), fp);

#undef ADD_ROW

    ml->addSpacing(16);
}
}
