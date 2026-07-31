#include "chat_widget.h"
#include <QFont>
#include <QScrollBar>
#include <QDateTime>

namespace xrk {

ChatWidget::ChatWidget(QWidget* parent) : QWidget(parent) {
    setupUI();
}

ChatWidget::~ChatWidget() {
}

void ChatWidget::setupUI() {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    m_messageView = new QTextEdit(this);
    m_messageView->setReadOnly(true);
    m_messageView->setFont(QFont("Segoe UI", 10));
    m_messageView->setObjectName("chat-view");

    QHBoxLayout* inputLayout = new QHBoxLayout();
    m_inputLine = new QLineEdit(this);
    m_inputLine->setFont(QFont("Segoe UI", 10));
    m_inputLine->setObjectName("chat-input");
    m_inputLine->setPlaceholderText("Type a message...");
    m_inputLine->setEnabled(false);

    m_sendBtn = new QPushButton("Send", this);
    m_sendBtn->setEnabled(false);
    m_sendBtn->setObjectName("send-button");

    inputLayout->addWidget(m_inputLine);
    inputLayout->addWidget(m_sendBtn);

    layout->addWidget(m_messageView);
    layout->addLayout(inputLayout);

    connect(m_sendBtn, &QPushButton::clicked, this, &ChatWidget::onSendClicked);
    connect(m_inputLine, &QLineEdit::returnPressed, this, &ChatWidget::onSendClicked);
}

void ChatWidget::appendMessage(const QString& sender, const QString& message) {
    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    m_messageView->append(QString("<span style='color:#888;'>[%1]</span> "
                                  "<span style='color:#4ec9b0;'>&lt;%2&gt;</span> "
                                  "<span style='color:#d4d4d4;'>%3</span>")
                              .arg(timestamp, sender, message.toHtmlEscaped()));
    
    QScrollBar* sb = m_messageView->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void ChatWidget::clear() {
    m_messageView->clear();
}

void ChatWidget::setConnected(bool connected) {
    m_connected = connected;
    m_inputLine->setEnabled(connected);
    m_sendBtn->setEnabled(connected);
    m_inputLine->setPlaceholderText(connected ? "Type a message..." : "Not connected");
    if (connected) m_inputLine->setFocus();
}

void ChatWidget::onSendClicked() {
    QString msg = m_inputLine->text().trimmed();
    if (msg.isEmpty()) return;
    
    appendMessage("Me", msg);
    emit sendMessage(msg);
    m_inputLine->clear();
}

} // namespace xrk
