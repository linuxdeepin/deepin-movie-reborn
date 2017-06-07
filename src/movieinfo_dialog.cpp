#include "movieinfo_dialog.h"
#include "mpv_proxy.h"

DWIDGET_USE_NAMESPACE

namespace dmr {
MovieInfoDialog::MovieInfoDialog(const struct MovieInfo& mi)
    :DDialog(nullptr)
{
    auto *ml = new QVBoxLayout;
    ml->setContentsMargins(0, 0, 0, 0);

    auto *pm = new QLabel(this);
    pm->setPixmap(QPixmap(":/resources/icons/logo-big.svg"));
    ml->setAlignment(pm, Qt::AlignCenter);
    ml->addWidget(pm);

    auto *nm = new QLabel(QFileInfo(mi.filePath).fileName(), this);
    ml->setAlignment(nm, Qt::AlignCenter);
    ml->addWidget(nm);

    auto *sp = new QFrame(this);
    sp->setFixedHeight(1);
    sp->setStyleSheet("background-color: rgba(0, 0, 0, 0.5)");
    ml->addWidget(sp);

    auto *form = new QFormLayout(this);
    ml->addLayout(form);
    
    form->setSpacing(3);
    form->setLabelAlignment(Qt::AlignRight);
    form->setFormAlignment(Qt::AlignCenter);

#define ADD_ROW(title, field)  form->addRow(new QLabel(tr(title), this), new QLabel(field, this))

    ADD_ROW("File Type", mi.fileType);
    ADD_ROW("Resolution", mi.resolution);
    ADD_ROW("File Size", mi.fileSize);
    ADD_ROW("Duration", mi.duration);
    ADD_ROW("Path", mi.filePath);
    ADD_ROW("Creation Time", mi.creation);

#undef ADD_ROW

    QWidget  *mainContent = new QWidget;
    mainContent->setLayout(ml);
    addContent(mainContent, Qt::AlignCenter);
}
}
