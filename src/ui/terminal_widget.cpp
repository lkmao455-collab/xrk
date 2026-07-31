#include "terminal_widget.h"
#include <QFont>
#include <QScrollBar>

namespace xrk {

TerminalWidget::TerminalWidget(QWidget* parent) : QWidget(parent) {
    setupUI();
}

TerminalWidget::~TerminalWidget() {
}

void TerminalWidget::setupUI() {
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    m_outputView = new QPlainTextEdit(this);
    m_outputView->setReadOnly(true);
    m_outputView->setFont(QFont("Cascadia Code", 10));
    m_outputView->setObjectName("terminal-output");
    m_outputView->setMaximumBlockCount(10000);
    m_outputView->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    m_outputView->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    QHBoxLayout* inputLayout = new QHBoxLayout();
    m_inputLine = new QLineEdit(this);
    m_inputLine->setFont(QFont("Cascadia Code", 10));
    m_inputLine->setObjectName("terminal-input");
    m_inputLine->setPlaceholderText("Enter command...");
    m_inputLine->setEnabled(false);

    m_sendBtn = new QPushButton("Send", this);
    m_sendBtn->setEnabled(false);
    m_sendBtn->setObjectName("send-button");

    inputLayout->addWidget(m_inputLine);
    inputLayout->addWidget(m_sendBtn);

    layout->addWidget(m_outputView);
    layout->addLayout(inputLayout);

    setLayout(layout);

    connect(m_sendBtn, &QPushButton::clicked, this, &TerminalWidget::onSendClicked);
    connect(m_inputLine, &QLineEdit::returnPressed, this, &TerminalWidget::onSendClicked);
}

void TerminalWidget::appendOutput(const QString& text) {
    m_outputView->moveCursor(QTextCursor::End);
    m_outputView->insertPlainText(text);

    QScrollBar* sb = m_outputView->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void TerminalWidget::clear() {
    m_outputView->clear();
}

void TerminalWidget::setConnected(bool connected) {
    m_connected = connected;
    m_inputLine->setEnabled(connected);
    m_sendBtn->setEnabled(connected);

    if (connected) {
        m_inputLine->setFocus();
        m_inputLine->setPlaceholderText("Type command and press Enter...");
    } else {
        m_inputLine->setPlaceholderText("Terminal not connected");
    }
}

bool TerminalWidget::isConnected() const {
    return m_connected;
}

void TerminalWidget::onSendClicked() {
    QString cmd = m_inputLine->text();
    if (cmd.isEmpty()) return;

    appendOutput("> " + cmd + "\n");
    emit inputCommand(cmd);
    m_inputLine->clear();
}

} // namespace xrk
