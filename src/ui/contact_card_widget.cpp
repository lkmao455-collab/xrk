#include "contact_card_widget.h"
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFrame>
#include <QFont>
#include <QDateTime>

namespace xrk {

ContactCardWidget::ContactCardWidget(QWidget* parent) : QWidget(parent) {
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    setFixedSize(340, 400);
    setupUI();
}

ContactCardWidget::~ContactCardWidget() {}

void ContactCardWidget::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(16, 16, 16, 16);
    mainLayout->setSpacing(12);

    QWidget* topSection = new QWidget(this);
    QHBoxLayout* topLayout = new QHBoxLayout(topSection);
    topLayout->setContentsMargins(0, 0, 0, 0);

    m_avatarLabel = new QLabel(topSection);
    m_avatarLabel->setFixedSize(64, 64);
    m_avatarLabel->setStyleSheet(
        "background-color: #3a3a5c; border-radius: 32px; color: white; font-size: 24px;");
    m_avatarLabel->setAlignment(Qt::AlignCenter);
    topLayout->addWidget(m_avatarLabel);

    QVBoxLayout* nameLayout = new QVBoxLayout();
    m_nameLabel = new QLabel(topSection);
    m_nameLabel->setStyleSheet("font-size: 16px; font-weight: bold; color: #e0e0e0;");
    m_nameEdit = new QLineEdit(topSection);
    m_nameEdit->setStyleSheet("font-size: 16px; font-weight: bold; color: #e0e0e0;");
    m_nameEdit->hide();
    nameLayout->addWidget(m_nameLabel);
    nameLayout->addWidget(m_nameEdit);

    m_statusLabel = new QLabel(topSection);
    m_statusLabel->setStyleSheet("font-size: 11px; color: #4ec9b0;");
    nameLayout->addWidget(m_statusLabel);
    nameLayout->addStretch();

    topLayout->addLayout(nameLayout);
    mainLayout->addWidget(topSection);

    QFrame* sep1 = new QFrame(this);
    sep1->setFrameShape(QFrame::HLine);
    sep1->setStyleSheet("color: #3a3a5c;");
    mainLayout->addWidget(sep1);

    QGridLayout* infoGrid = new QGridLayout();
    infoGrid->setSpacing(8);

    auto addInfoRow = [&](int row, const QString& label, QLabel*& valueLabel, QLineEdit*& valueEdit) {
        QLabel* lbl = new QLabel(label, this);
        lbl->setStyleSheet("color: #888; font-size: 12px;");
        valueLabel = new QLabel(this);
        valueLabel->setStyleSheet("color: #d4d4d4; font-size: 12px;");
        valueEdit = new QLineEdit(this);
        valueEdit->setStyleSheet("color: #d4d4d4; font-size: 12px; background: #2a2a3e;");
        valueEdit->hide();
        infoGrid->addWidget(lbl, row, 0);
        infoGrid->addWidget(valueLabel, row, 1);
        infoGrid->addWidget(valueEdit, row, 1);
    };

    addInfoRow(0, tr("IP Address:"), m_ipLabel, m_nameEdit);
    addInfoRow(1, tr("Device:"), m_deviceLabel, m_nameEdit);

    QLabel* noteLbl = new QLabel(tr("Note:"), this);
    noteLbl->setStyleSheet("color: #888; font-size: 12px;");
    m_noteLabel = new QLabel(this);
    m_noteLabel->setStyleSheet("color: #d4d4d4; font-size: 12px;");
    m_noteEdit = new QTextEdit(this);
    m_noteEdit->setMaximumHeight(60);
    m_noteEdit->setStyleSheet("color: #d4d4d4; font-size: 12px; background: #2a2a3e;");
    m_noteEdit->hide();
    infoGrid->addWidget(noteLbl, 2, 0);
    infoGrid->addWidget(m_noteLabel, 2, 1);
    infoGrid->addWidget(m_noteEdit, 2, 1);

    m_lastSeenLabel = new QLabel(this);
    m_lastSeenLabel->setStyleSheet("color: #888; font-size: 11px;");
    infoGrid->addWidget(m_lastSeenLabel, 3, 0, 1, 2);

    mainLayout->addLayout(infoGrid);

    QFrame* sep2 = new QFrame(this);
    sep2->setFrameShape(QFrame::HLine);
    sep2->setStyleSheet("color: #3a3a5c;");
    mainLayout->addWidget(sep2);

    QHBoxLayout* actionLayout = new QHBoxLayout();
    actionLayout->setSpacing(8);

    m_messageBtn = new QPushButton(tr("Message"), this);
    m_voiceCallBtn = new QPushButton(tr("Voice"), this);
    m_videoCallBtn = new QPushButton(tr("Video"), this);
    m_saveBtn = new QPushButton(tr("Save"), this);
    m_deleteBtn = new QPushButton(tr("Delete"), this);
    m_closeBtn = new QPushButton(tr("Close"), this);

    QString btnStyle = "QPushButton { padding: 6px 12px; border-radius: 4px; color: white; font-size: 12px; }";
    m_messageBtn->setStyleSheet(btnStyle + "QPushButton { background: #0e639c; }"
                                "QPushButton:hover { background: #1177bb; }");
    m_voiceCallBtn->setStyleSheet(btnStyle + "QPushButton { background: #388a34; }"
                                  "QPushButton:hover { background: #45a741; }");
    m_videoCallBtn->setStyleSheet(btnStyle + "QPushButton { background: #6a3d9a; }"
                                  "QPushButton:hover { background: #7e57c2; }");
    m_saveBtn->setStyleSheet(btnStyle + "QPushButton { background: #0e639c; }"
                             "QPushButton:hover { background: #1177bb; }");
    m_deleteBtn->setStyleSheet(btnStyle + "QPushButton { background: #c53030; }"
                               "QPushButton:hover { background: #e04040; }");
    m_closeBtn->setStyleSheet(btnStyle + "QPushButton { background: #555; }"
                              "QPushButton:hover { background: #666; }");

    m_saveBtn->hide();
    m_deleteBtn->hide();

    actionLayout->addWidget(m_messageBtn);
    actionLayout->addWidget(m_voiceCallBtn);
    actionLayout->addWidget(m_videoCallBtn);
    actionLayout->addStretch();
    actionLayout->addWidget(m_saveBtn);
    actionLayout->addWidget(m_deleteBtn);
    actionLayout->addWidget(m_closeBtn);
    mainLayout->addLayout(actionLayout);

    mainLayout->addStretch();

    connect(m_saveBtn, &QPushButton::clicked, this, &ContactCardWidget::onSaveClicked);
    connect(m_deleteBtn, &QPushButton::clicked, this, &ContactCardWidget::onDeleteClicked);
    connect(m_messageBtn, &QPushButton::clicked, this, &ContactCardWidget::onSendMessageClicked);
    connect(m_voiceCallBtn, &QPushButton::clicked, this, &ContactCardWidget::onVoiceCallClicked);
    connect(m_videoCallBtn, &QPushButton::clicked, this, &ContactCardWidget::onVideoCallClicked);
    connect(m_closeBtn, &QPushButton::clicked, this, [this]() { emit closed(); hide(); });
}

void ContactCardWidget::setContact(const ContactInfo& contact) {
    m_contact = contact;
    updateDisplay();
}

ContactInfo ContactCardWidget::contact() const {
    return m_contact;
}

void ContactCardWidget::setEditable(bool editable) {
    m_editable = editable;
    m_nameEdit->setVisible(editable);
    m_nameLabel->setVisible(!editable);
    m_noteEdit->setVisible(editable);
    m_noteLabel->setVisible(!editable);
    m_saveBtn->setVisible(editable);
    m_deleteBtn->setVisible(editable);
}

void ContactCardWidget::setAvatar(const QPixmap& avatar) {
    m_avatarLabel->setPixmap(avatar.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void ContactCardWidget::updateDisplay() {
    QString initial = m_contact.displayName.isEmpty() ? "?" : m_contact.displayName.left(1).toUpper();
    m_avatarLabel->setText(initial);
    m_nameLabel->setText(m_contact.displayName);
    m_nameEdit->setText(m_contact.displayName);
    m_ipLabel->setText(m_contact.ipAddress);
    m_deviceLabel->setText(m_contact.deviceName);
    m_noteLabel->setText(m_contact.note);
    m_noteEdit->setText(m_contact.note);

    if (m_contact.online) {
        m_statusLabel->setText(tr("Online"));
        m_statusLabel->setStyleSheet("color: #4ec9b0; font-size: 11px;");
    } else {
        m_statusLabel->setText(tr("Offline"));
        m_statusLabel->setStyleSheet("color: #888; font-size: 11px;");
    }

    if (m_contact.lastSeen > 0) {
        QDateTime dt = QDateTime::fromMSecsSinceEpoch(m_contact.lastSeen);
        m_lastSeenLabel->setText(tr("Last seen: %1").arg(dt.toString("yyyy-MM-dd HH:mm")));
    } else {
        m_lastSeenLabel->setText("");
    }
}

void ContactCardWidget::onSaveClicked() {
    m_contact.displayName = m_nameEdit->text();
    m_contact.note = m_noteEdit->toPlainText();
    emit saveRequested(m_contact);
}

void ContactCardWidget::onDeleteClicked() {
    emit deleteRequested(m_contact.contactId);
}

void ContactCardWidget::onSendMessageClicked() {
    emit sendMessageRequested(m_contact.contactId);
}

void ContactCardWidget::onVoiceCallClicked() {
    emit startCallRequested(m_contact.contactId, false);
}

void ContactCardWidget::onVideoCallClicked() {
    emit startCallRequested(m_contact.contactId, true);
}

} // namespace xrk
