#pragma once

#include <QWidget>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QPushButton>
#include <memory>

namespace xrk {

class TerminalWidget : public QWidget {
    Q_OBJECT
public:
    explicit TerminalWidget(QWidget* parent = nullptr);
    ~TerminalWidget();

    void appendOutput(const QString& text);
    void clear();
    void setConnected(bool connected);
    bool isConnected() const;

signals:
    void inputCommand(const QString& command);

private slots:
    void onSendClicked();

private:
    void setupUI();

    QPlainTextEdit* m_outputView = nullptr;
    QLineEdit* m_inputLine = nullptr;
    QPushButton* m_sendBtn = nullptr;
    bool m_connected = false;
};

} // namespace xrk
