#include "dmr_lineedit.h"

namespace dmr {

LineEdit::LineEdit(QWidget* parent)
    :QLineEdit(parent)
{
    setFixedHeight(20);
    setStyleSheet(R"(
	QLineEdit {
        font-size: 11px;
		border-radius: 3px;
		background-color: #ffffff;
		border: 1px solid rgba(0, 0, 0, 0.08);
        color: #303030;
	}
    )");

    QIcon icon;
    icon.addFile(":/resources/icons/input_clear_normal.svg", QSize(), QIcon::Normal);
    icon.addFile(":/resources/icons/input_clear_press.svg", QSize(), QIcon::Selected);
    icon.addFile(":/resources/icons/input_clear_hover.svg", QSize(), QIcon::Active);
    _clearAct = new QAction(icon, "", this);

    connect(_clearAct, &QAction::triggered, this, &QLineEdit::clear);
    connect(this, &QLineEdit::textChanged, [=](const QString& s) {
        if (s.isEmpty()) {
            removeAction(_clearAct);
        } else {
            addAction(_clearAct, QLineEdit::TrailingPosition);
        }
    });

}

void LineEdit::showEvent(QShowEvent* se)
{
}

void LineEdit::resizeEvent(QResizeEvent* re)
{
}

}
