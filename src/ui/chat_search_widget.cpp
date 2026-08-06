#include "chat_search_widget.h"
#include <QKeyEvent>

namespace xrk {

ChatSearchWidget::ChatSearchWidget(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
    setFixedHeight(44);
    setMinimumWidth(360);
    move(10, 10);
    hide();

    m_opacityEffect = new QGraphicsOpacityEffect(this);
    m_opacityEffect->setOpacity(0.0f);
    setGraphicsEffect(m_opacityEffect);

    m_showAnim = new QPropertyAnimation(m_opacityEffect, "opacity", this);
    m_showAnim->setDuration(200);
    m_showAnim->setStartValue(0.0);
    m_showAnim->setEndValue(1.0);

    m_hideAnim = new QPropertyAnimation(m_opacityEffect, "opacity", this);
    m_hideAnim->setDuration(150);
    m_hideAnim->setStartValue(1.0);
    m_hideAnim->setEndValue(0.0);
    connect(m_hideAnim, &QPropertyAnimation::finished, this, [this]() { hide(); });
}

ChatSearchWidget::~ChatSearchWidget() {}

void ChatSearchWidget::setupUI() {
    setStyleSheet(
        "ChatSearchWidget {"
        "  background-color: #2E2E2E;"
        "  border: 1px solid #555;"
        "  border-radius: 8px;"
        "}"
    );

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(8, 4, 8, 4);
    layout->setSpacing(4);

    m_input = new QLineEdit(this);
    m_input->setPlaceholderText(tr("搜索聊天记录..."));
    m_input->setFixedWidth(180);
    m_input->setStyleSheet(
        "QLineEdit { background-color: #3A3A3A; color: #E0E0E0; border: 1px solid #555; "
        "border-radius: 4px; padding: 4px 8px; font-size: 13px; }"
        "QLineEdit:focus { border-color: #07C160; }");
    connect(m_input, &QLineEdit::textChanged, this, &ChatSearchWidget::searchChanged);
    connect(m_input, &QLineEdit::returnPressed, this, [this]() {
        emit searchNext(m_input->text());
    });

    // Intercept Up/Down keys
    m_input->installEventFilter(this);
    layout->addWidget(m_input);

    m_prevBtn = new QPushButton(tr("▲"), this);
    m_prevBtn->setFixedSize(28, 28);
    m_prevBtn->setStyleSheet(
        "QPushButton { background: transparent; color: #AAA; border: none; font-size: 12px; }"
        "QPushButton:hover { color: #fff; background: #4A4A4A; border-radius: 4px; }");
    m_prevBtn->setToolTip(tr("上一个 (Shift+Enter)"));
    connect(m_prevBtn, &QPushButton::clicked, this, [this]() {
        emit searchPrev(m_input->text());
    });
    layout->addWidget(m_prevBtn);

    m_nextBtn = new QPushButton(tr("▼"), this);
    m_nextBtn->setFixedSize(28, 28);
    m_nextBtn->setStyleSheet(m_prevBtn->styleSheet());
    m_nextBtn->setToolTip(tr("下一个 (Enter)"));
    connect(m_nextBtn, &QPushButton::clicked, this, [this]() {
        emit searchNext(m_input->text());
    });
    layout->addWidget(m_nextBtn);

    m_countLabel = new QLabel(tr("0 结果"), this);
    m_countLabel->setStyleSheet("color: #888; font-size: 12px; background: transparent; border: none;");
    m_countLabel->setMinimumWidth(50);
    layout->addWidget(m_countLabel);

    layout->addStretch();

    m_closeBtn = new QPushButton(tr("✕"), this);
    m_closeBtn->setFixedSize(28, 28);
    m_closeBtn->setStyleSheet(
        "QPushButton { background: transparent; color: #AAA; border: none; font-size: 14px; font-weight: bold; }"
        "QPushButton:hover { color: #fff; background: #E53935; border-radius: 4px; }");
    m_closeBtn->setToolTip(tr("关闭 (Esc)"));
    connect(m_closeBtn, &QPushButton::clicked, this, &ChatSearchWidget::deactivate);
    layout->addWidget(m_closeBtn);
}

void ChatSearchWidget::setAnimOpacity(float v) {
    m_opacityEffect->setOpacity(v);
}

void ChatSearchWidget::activate() {
    if (m_active) {
        m_input->selectAll();
        m_input->setFocus();
        return;
    }
    m_active = true;
    show();
    animateShow();
    m_input->clear();
    m_input->setFocus();
}

void ChatSearchWidget::deactivate() {
    if (!m_active) return;
    m_active = false;
    animateHide();
    emit searchClose();
}

void ChatSearchWidget::animateShow() {
    m_hideAnim->stop();
    m_showAnim->start();
}

void ChatSearchWidget::animateHide() {
    m_showAnim->stop();
    m_hideAnim->start();
}

bool ChatSearchWidget::eventFilter(QObject* obj, QEvent* event) {
    if (obj == m_input && event->type() == QEvent::KeyPress) {
        QKeyEvent* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Escape) {
            deactivate();
            return true;
        }
        if (ke->key() == Qt::Key_Up) {
            emit searchPrev(m_input->text());
            return true;
        }
        if (ke->key() == Qt::Key_Down) {
            emit searchNext(m_input->text());
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

} // namespace xrk
