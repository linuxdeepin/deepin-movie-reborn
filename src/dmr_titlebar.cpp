#include "dmr_titlebar.h"

#include <QDebug>
#include <QMenu>
#include <QHBoxLayout>
#include <QApplication>
#include <QMouseEvent>

#include <dwindowclosebutton.h>
#include <dwindowmaxbutton.h>
#include <dwindowminbutton.h>
#include <dwindowrestorebutton.h>
#include <dwindowoptionbutton.h>
#include <dlabel.h>
#include <dplatformwindowhandle.h>
#include "dobject_p.h"
#ifdef Q_OS_LINUX
#include "xutil.h"
#endif

namespace dmr {

const int DefaultTitlebarHeight = 40;
const int DefaultIconHeight = 20;
const int DefaultIconWidth = 20;

class DMRTitlebarPrivate : public DObjectPrivate
{
protected:
    DMRTitlebarPrivate(DMRTitlebar *qq);

private:
    void init();

    QHBoxLayout         *mainLayout;
    DLabel              *iconLabel;
    DLabel              *titleLabel;
    DWindowMinButton    *minButton;
    DWindowMaxButton    *maxButton;
    DWindowCloseButton  *closeButton;
    DWindowOptionButton *optionButton;

    QWidget             *customWidget = Q_NULLPTR;
    QWidget             *coustomAtea;
    QWidget             *buttonArea;
    QWidget             *titleArea;
    QWidget             *titlePadding;
    QLabel              *separator;

#ifndef QT_NO_MENU
    QMenu               *menu = Q_NULLPTR;
#endif

    QWidget             *parentWindow = Q_NULLPTR;

    bool                mousePressed = false;

    Q_DECLARE_PUBLIC(DMRTitlebar)
};

DMRTitlebarPrivate::DMRTitlebarPrivate(DMRTitlebar *qq): DObjectPrivate(qq)
{
}

void DMRTitlebarPrivate::init()
{
    D_Q(DMRTitlebar);

    mainLayout      = new QHBoxLayout;
    iconLabel       = new DLabel;
    titleLabel      = new DLabel;
    minButton       = new DWindowMinButton;
    maxButton       = new DWindowMaxButton;
    closeButton     = new DWindowCloseButton;
    optionButton    = new DWindowOptionButton;
    coustomAtea     = new QWidget;
    buttonArea      = new QWidget;
    titleArea       = new QWidget;
    titlePadding    = new QWidget;
    separator       = new QLabel(q);

    optionButton->setObjectName("DMRTitlebarDWindowOptionButton");
    minButton->setObjectName("DMRTitlebarDWindowMinButton");
    maxButton->setObjectName("DMRTitlebarDWindowMaxButton");
    closeButton->setObjectName("DMRTitlebarDWindowCloseButton");

    mainLayout->setContentsMargins(6, 0, 6, 0);
    mainLayout->setSpacing(0);

    iconLabel->setFixedSize(DefaultIconWidth, DefaultIconHeight);
    titleLabel->setText(qApp->applicationName());
    // TODO: use QSS
    titleLabel->setStyleSheet("font-size: 14px;");
    titleLabel->setContentsMargins(0, 0, DefaultIconWidth + 10, 0);
//    q->setStyleSheet("background-color: green;");

    separator->setFixedHeight(1);
    separator->setStyleSheet("background: rgba(0, 0, 0, 20);");
    separator->hide();

    QHBoxLayout *buttonAreaLayout = new QHBoxLayout;
    buttonAreaLayout->setContentsMargins(0, 1, 0, 0);
    buttonAreaLayout->setMargin(0);
    buttonAreaLayout->setSpacing(0);
    buttonAreaLayout->addWidget(optionButton);
    buttonAreaLayout->addWidget(minButton);
    buttonAreaLayout->addWidget(maxButton);
    buttonAreaLayout->addWidget(closeButton);
    buttonArea->setLayout(buttonAreaLayout);

    QHBoxLayout *titleAreaLayout = new QHBoxLayout;
    titleAreaLayout->setMargin(0);
    titleAreaLayout->setSpacing(0);
    titlePadding->setFixedSize(buttonArea->size());
    titleAreaLayout->addWidget(titlePadding);
    titleAreaLayout->addStretch();
    titleAreaLayout->addWidget(iconLabel);
    titleAreaLayout->setAlignment(iconLabel, Qt::AlignCenter);
    titleAreaLayout->addSpacing(10);
    titleAreaLayout->addWidget(titleLabel);
    titleAreaLayout->setAlignment(titleLabel, Qt::AlignCenter);

    titleAreaLayout->addStretch();
    titleArea->setLayout(titleAreaLayout);

    QHBoxLayout *coustomAteaLayout = new QHBoxLayout;
    coustomAteaLayout->setMargin(0);
    coustomAteaLayout->setSpacing(0);
    coustomAteaLayout->addWidget(titleArea);
    coustomAtea->setLayout(coustomAteaLayout);

    mainLayout->addWidget(coustomAtea);
    mainLayout->addWidget(buttonArea);
    mainLayout->setAlignment(buttonArea, Qt::AlignRight |  Qt::AlignVCenter);

    q->setLayout(mainLayout);
    q->setFixedHeight(DefaultTitlebarHeight);
    q->setMinimumHeight(DefaultTitlebarHeight);
    coustomAtea->setFixedHeight(q->height());
    buttonArea->setFixedHeight(q->height());

    q->connect(optionButton, &DWindowOptionButton::clicked, q, &DMRTitlebar::optionClicked);
}

DMRTitlebar::DMRTitlebar(QWidget *parent) :
    QWidget(parent),
    DObject(*new DMRTitlebarPrivate(this))
{
    D_D(DMRTitlebar);
    d->init();
    d->buttonArea->adjustSize();
    d->buttonArea->resize(d->buttonArea->size());
    d->titlePadding->setFixedSize(d->buttonArea->size());
}

#ifndef QT_NO_MENU
QMenu *DMRTitlebar::menu() const
{
    D_DC(DMRTitlebar);

    return d->menu;
}

void DMRTitlebar::setMenu(QMenu *menu)
{
    D_D(DMRTitlebar);

    d->menu = menu;
    if (d->menu) {
        disconnect(this, &DMRTitlebar::optionClicked, 0, 0);
        connect(this, &DMRTitlebar::optionClicked, this, &DMRTitlebar::showMenu);
    }
}
#endif

QWidget *DMRTitlebar::customWidget() const
{
    D_DC(DMRTitlebar);

    return d->customWidget;
}

///
/// \brief setWindowFlags
/// \param type
/// accpet  WindowTitleHint, WindowSystemMenuHint, WindowMinimizeButtonHint, WindowMaximizeButtonHint
/// and WindowMinMaxButtonsHint.
void DMRTitlebar::setWindowFlags(Qt::WindowFlags type)
{
    D_D(DMRTitlebar);
    if (d->titleLabel) {
        d->titleLabel->setVisible(type & Qt::WindowTitleHint);
    }

    if (d->iconLabel) {
        d->iconLabel->setVisible(type & Qt::WindowTitleHint);
    }

    d->minButton->setVisible(type & Qt::WindowMinimizeButtonHint);
    d->maxButton->setVisible(type & Qt::WindowMaximizeButtonHint);
    d->closeButton->setVisible(type & Qt::WindowCloseButtonHint);
    d->optionButton->setVisible(type & Qt::WindowSystemMenuHint);
    d->buttonArea->adjustSize();
    d->buttonArea->resize(d->buttonArea->size());

    if (d->titlePadding) {
        d->titlePadding->setFixedSize(d->buttonArea->size());
    }
}

#ifndef QT_NO_MENU
void DMRTitlebar::showMenu()
{
    D_D(DMRTitlebar);

    if (d->menu) {
        d->menu->exec(d->optionButton->mapToGlobal(d->optionButton->rect().bottomLeft()));
    } else {
        d->menu->exec(d->optionButton->mapToGlobal(d->optionButton->rect().bottomLeft()));
    }
}
#endif

void DMRTitlebar::showEvent(QShowEvent *event)
{
    D_D(DMRTitlebar);
    d->separator->setFixedWidth(width());
    d->separator->move(0, height() - d->separator->height());
    QWidget::showEvent(event);
}

void DMRTitlebar::mousePressEvent(QMouseEvent *event)
{
    D_D(DMRTitlebar);
    d->mousePressed = (event->buttons() == Qt::LeftButton);

#ifdef Q_OS_WIN
    emit mousePosPressed(event->buttons(), event->globalPos());
#endif
    emit mousePressed(event->buttons());
}

void DMRTitlebar::mouseReleaseEvent(QMouseEvent *event)
{
    D_D(DMRTitlebar);
    if (event->buttons() == Qt::LeftButton) {
        d->mousePressed = false;
    }
}

bool DMRTitlebar::eventFilter(QObject *obj, QEvent *event)
{
    D_D(DMRTitlebar);

    if (obj == d->parentWindow) {
        if (event->type() == QEvent::WindowStateChange) {
            d->maxButton->setMaximized(d->parentWindow->windowState() == Qt::WindowMaximized);
        }
    }

    return QWidget::eventFilter(obj, event);
}

void DMRTitlebar::setCustomWidget(QWidget *w, bool fixCenterPos)
{
    setCustomWidget(w, Qt::AlignCenter, fixCenterPos);
}


void DMRTitlebar::setCustomWidget(QWidget *w, Qt::AlignmentFlag wflag, bool fixCenterPos)
{
    D_D(DMRTitlebar);
    if (!w || w == d->titleArea) {
        return;
    }

    QSize old = d->buttonArea->size();

    QHBoxLayout *l = new QHBoxLayout;
    l->setSpacing(0);
    l->setMargin(0);

    if (fixCenterPos) {
        d->titlePadding = new QWidget;
        d->titlePadding->setFixedSize(old);
        l->addWidget(d->titlePadding);
    }

    l->addWidget(w, 0, wflag);
    qDeleteAll(d->coustomAtea->children());
    d->titleLabel = Q_NULLPTR;
    d->titleArea = Q_NULLPTR;
    d->iconLabel = Q_NULLPTR;
    d->titlePadding = Q_NULLPTR;
    d->coustomAtea->setLayout(l);
    d->buttonArea->resize(old);
    d->customWidget = w;

    w->resize(d->coustomAtea->size());
}

void DMRTitlebar::setFixedHeight(int h)
{
    D_D(DMRTitlebar);
    QWidget::setFixedHeight(h);
    d->coustomAtea->setFixedHeight(h);
    d->buttonArea->setFixedHeight(h);
}

void DMRTitlebar::setSeparatorVisible(bool visible)
{
    D_D(DMRTitlebar);
    if (visible) {
        d->separator->show();
        d->separator->raise();
    } else {
        d->separator->hide();
    }
}

void DMRTitlebar::setTitle(const QString &title)
{
    D_D(DMRTitlebar);
    if (d->titleLabel) {
        d->titleLabel->setText(title);
    }
}

void DMRTitlebar::setIcon(const QPixmap &icon)
{
    D_D(DMRTitlebar);
    if (d->titleLabel) {
        d->titleLabel->setContentsMargins(0, 0, 0, 0);
        d->iconLabel->setPixmap(icon.scaled(DefaultIconWidth, DefaultIconHeight, Qt::KeepAspectRatio));
    }
}

int DMRTitlebar::buttonAreaWidth() const
{
    D_DC(DMRTitlebar);
    return d->buttonArea->width();
}

bool DMRTitlebar::separatorVisible() const
{
    D_DC(DMRTitlebar);
    return d->separator->isVisible();
}

void DMRTitlebar::setVisible(bool visible)
{
    D_D(DMRTitlebar);

    if (visible == isVisible()) {
        return;
    }

    QWidget::setVisible(visible);

    if (visible) {
        d->parentWindow = parentWidget();

        if (!d->parentWindow) {
            return;
        }

        d->parentWindow = d->parentWindow->window();
        d->parentWindow->installEventFilter(this);

        connect(d->maxButton, SIGNAL(clicked()), this, SIGNAL(maxButtonClicked()));
        connect(this, SIGNAL(doubleClicked()), this, SIGNAL(maxButtonClicked()));
        connect(d->minButton, SIGNAL(clicked()), this, SIGNAL(minButtonClicked()));
        connect(d->closeButton, &DWindowCloseButton::clicked, this, &DMRTitlebar::closeButtonClicked);
    } else {
        if (!d->parentWindow) {
            return;
        }

        d->parentWindow->removeEventFilter(this);

        disconnect(d->maxButton, SIGNAL(clicked()), 0, 0);
        disconnect(this, SIGNAL(doubleClicked()), 0, 0);
        disconnect(d->minButton, SIGNAL(clicked()), 0, 0);
        disconnect(d->closeButton, &DWindowCloseButton::clicked, 0, 0);
    }
}

void DMRTitlebar::resize(int w, int h)
{
    D_DC(DMRTitlebar);
    if (d->customWidget) {
        d->customWidget->resize(w - d->buttonArea->width(), h);
    }
}

void DMRTitlebar::resize(const QSize &sz)
{
    DMRTitlebar::resize(sz.width(), sz.height());
}

void DMRTitlebar::mouseMoveEvent(QMouseEvent *event)
{
    D_DC(DMRTitlebar);

    Qt::MouseButton button = event->buttons() & Qt::LeftButton ? Qt::LeftButton : Qt::NoButton;
    if (event->buttons() == Qt::LeftButton /*&& d->mousePressed*/) {
        emit mouseMoving(button);
    }

#ifdef Q_OS_WIN
    if (d->mousePressed) {
        emit mousePosMoving(button, event->globalPos());
    }
#endif
    QWidget::mouseMoveEvent(event);
}

void DMRTitlebar::mouseDoubleClickEvent(QMouseEvent *event)
{
    D_D(DMRTitlebar);

    if (event->buttons() == Qt::LeftButton) {
        d->mousePressed = false;
        emit doubleClicked();
    }
}

}
