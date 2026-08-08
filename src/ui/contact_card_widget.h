#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QLineEdit>
#include <QTextEdit>
#include "core/types.h"

namespace xrk {

class ContactCardWidget : public QWidget {
    Q_OBJECT
public:
    explicit ContactCardWidget(QWidget* parent = nullptr);
    ~ContactCardWidget();

    void setContact(const ContactInfo& contact);
    ContactInfo contact() const;
    void setEditable(bool editable);
    void setAvatar(const QPixmap& avatar);

signals:
    void saveRequested(const ContactInfo& contact);
    void deleteRequested(const QString& contactId);
    void sendMessageRequested(const QString& contactId);
    void startCallRequested(const QString& contactId, bool video);
    void closed();

private slots:
    void onSaveClicked();
    void onDeleteClicked();
    void onSendMessageClicked();
    void onVoiceCallClicked();
    void onVideoCallClicked();

private:
    void setupUI();
    void updateDisplay();

    ContactInfo m_contact;
    bool m_editable = false;

    QLabel* m_avatarLabel = nullptr;
    QLabel* m_nameLabel = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QLabel* m_ipLabel = nullptr;
    QLabel* m_deviceLabel = nullptr;
    QLabel* m_statusLabel = nullptr;
    QLabel* m_noteLabel = nullptr;
    QTextEdit* m_noteEdit = nullptr;
    QLabel* m_lastSeenLabel = nullptr;

    QPushButton* m_saveBtn = nullptr;
    QPushButton* m_deleteBtn = nullptr;
    QPushButton* m_messageBtn = nullptr;
    QPushButton* m_voiceCallBtn = nullptr;
    QPushButton* m_videoCallBtn = nullptr;
    QPushButton* m_closeBtn = nullptr;
};

} // namespace xrk
