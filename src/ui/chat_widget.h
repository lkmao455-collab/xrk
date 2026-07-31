#pragma once

#include <QWidget>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace xrk {

class ChatWidget : public QWidget {
    Q_OBJECT
public:
    explicit ChatWidget(QWidget* parent = nullptr);
    ~ChatWidget();

    void appendMessage(const QString& sender, const QString& message);
    void clear();
    void setConnected(bool connected);

signals:
    void sendMessage(const QString& message);

private slots:
    void onSendClicked();

private:
    void setupUI();

    QTextEdit* m_messageView = nullptr;
    QLineEdit* m_inputLine = nullptr;
    QPushButton* m_sendBtn = nullptr;
    bool m_connected = false;
};

} // namespace xrk
