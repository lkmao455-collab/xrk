#include "key_verification_dialog.h"
#include "app/ipmsg_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QMessageBox>
#include <QFrame>
#include <QGridLayout>
#include <QApplication>
#include <QClipboard>

namespace xrk {

KeyVerificationDialog::KeyVerificationDialog(IPMsgManager* manager, const QString& deviceId,
                                             const QString& peerName, QWidget* parent)
    : QDialog(parent), m_manager(manager), m_deviceId(deviceId), m_peerName(peerName) {
    setWindowTitle(tr("密钥验证 - %1").arg(peerName));
    setMinimumSize(480, 420);
    setModal(true);
    setupUI();
    updateStatusDisplay();
}

KeyVerificationDialog::~KeyVerificationDialog() {
}

void KeyVerificationDialog::showVerification(IPMsgManager* manager, const QString& deviceId,
                                             const QString& peerName, QWidget* parent) {
    if (!manager || deviceId.isEmpty()) return;
    if (!manager->hasEstablishedSession(deviceId)) {
        QMessageBox::information(parent, tr("加密未就绪"),
            tr("与 %1 的加密会话尚未建立，请先发送一条消息以触发密钥交换。").arg(peerName));
        return;
    }
    auto* dialog = new KeyVerificationDialog(manager, deviceId, peerName, parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

void KeyVerificationDialog::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(24, 20, 24, 20);
    mainLayout->setSpacing(16);

    // Header
    auto* headerLabel = new QLabel(tr("安全码验证"));
    headerLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #E0E0E0;");
    headerLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(headerLabel);

    // Explanation
    auto* explainLabel = new QLabel(
        tr("如果你能当面或通过安全渠道与对方确认以下安全码完全一致，\n"
           "即可确保通信未被中间人窃听或篡改。"));
    explainLabel->setStyleSheet("color: #aaa; font-size: 12px; line-height: 1.4;");
    explainLabel->setAlignment(Qt::AlignCenter);
    explainLabel->setWordWrap(true);
    mainLayout->addWidget(explainLabel);

    // Fingerprint visual (color blocks)
    m_fingerprintVisual = new QWidget();
    auto* visualLayout = new QGridLayout(m_fingerprintVisual);
    visualLayout->setSpacing(6);
    visualLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->addWidget(m_fingerprintVisual);

    // Fingerprint text
    m_fingerprintLabel = new QLabel();
    m_fingerprintLabel->setStyleSheet(
        "background-color: #1e1e1e; color: #4a9eff; font-family: 'Consolas', monospace; "
        "font-size: 15px; padding: 12px; border-radius: 6px; border: 1px solid #333;");
    m_fingerprintLabel->setAlignment(Qt::AlignCenter);
    m_fingerprintLabel->setWordWrap(true);
    mainLayout->addWidget(m_fingerprintLabel);

    // Copy button
    auto* copyBtn = new QPushButton(tr("复制安全码"));
    copyBtn->setStyleSheet(
        "QPushButton { background: #3a3a3a; color: #ddd; border: 1px solid #555; "
        "border-radius: 4px; padding: 6px 12px; font-size: 12px; }"
        "QPushButton:hover { background: #4a4a4a; }");
    connect(copyBtn, &QPushButton::clicked, this, [this]() {
        if (m_manager) {
            QString fp = m_manager->getFingerprintDisplay(m_deviceId);
            QApplication::clipboard()->setText(fp);
        }
    });
    auto* copyLayout = new QHBoxLayout();
    copyLayout->addStretch(1);
    copyLayout->addWidget(copyBtn);
    copyLayout->addStretch(1);
    mainLayout->addLayout(copyLayout);

    // Status label
    m_statusLabel = new QLabel();
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setStyleSheet("font-size: 14px; font-weight: bold; padding: 8px;");
    mainLayout->addWidget(m_statusLabel);

    // Compare button (for side-by-side comparison)
    m_compareButton = new QPushButton(tr("与对方比对"));
    m_compareButton->setStyleSheet(
        "QPushButton { background: #2196F3; color: white; border: none; "
        "border-radius: 6px; padding: 10px 20px; font-size: 13px; font-weight: bold; }"
        "QPushButton:hover { background: #1E88E5; }");
    connect(m_compareButton, &QPushButton::clicked, this, &KeyVerificationDialog::onCompareClicked);
    mainLayout->addWidget(m_compareButton);

    // Verify / Unverify button
    m_verifyButton = new QPushButton();
    m_verifyButton->setStyleSheet(
        "QPushButton { border: none; border-radius: 6px; padding: 10px 20px; "
        "font-size: 13px; font-weight: bold; }");
    connect(m_verifyButton, &QPushButton::clicked, this, &KeyVerificationDialog::onVerifyClicked);
    mainLayout->addWidget(m_verifyButton);

    mainLayout->addStretch(1);
}

void KeyVerificationDialog::updateStatusDisplay() {
    if (!m_manager) return;

    QString fingerprint = m_manager->getFingerprintDisplay(m_deviceId);
    m_fingerprintLabel->setText(fingerprint);

    // Build color blocks
    QGridLayout* grid = qobject_cast<QGridLayout*>(m_fingerprintVisual->layout());
    if (!grid) return;
    // Clear existing
    while (grid->count() > 0) {
        QLayoutItem* item = grid->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    if (!m_manager) return;
    QByteArray fp = m_manager->getLocalFingerprint(m_deviceId);
    if (!fp.isEmpty()) {
        QString hex = fp.toHex().toUpper();
        QStringList groups;
        for (int i = 0; i < hex.size() && i < 64; i += 4) {
            groups.append(hex.mid(i, 4));
        }
        for (int i = 0; i < groups.size(); ++i) {
            QColor color = generateColorFromFingerprint(QByteArray::fromHex(groups[i].toLatin1()));
            QWidget* block = createFingerprintBlock(groups[i], color);
            grid->addWidget(block, i / 4, i % 4);
        }
    }

    m_isVerified = m_manager->isSessionVerified(m_deviceId);
    if (m_isVerified) {
        m_statusLabel->setText(tr("已验证 - 通信安全"));
        m_statusLabel->setStyleSheet("color: #4caf50; font-size: 14px; font-weight: bold; padding: 8px;");
        m_verifyButton->setText(tr("取消验证"));
        m_verifyButton->setStyleSheet(
            "QPushButton { background: #f44336; color: white; border: none; "
            "border-radius: 6px; padding: 10px 20px; font-size: 13px; font-weight: bold; }"
            "QPushButton:hover { background: #d32f2f; }");
    } else {
        m_statusLabel->setText(tr("未验证 - 请与对方比对安全码"));
        m_statusLabel->setStyleSheet("color: #ff9800; font-size: 14px; font-weight: bold; padding: 8px;");
        m_verifyButton->setText(tr("我已确认一致"));
        m_verifyButton->setStyleSheet(
            "QPushButton { background: #4caf50; color: white; border: none; "
            "border-radius: 6px; padding: 10px 20px; font-size: 13px; font-weight: bold; }"
            "QPushButton:hover { background: #388E3C; }");
    }
}

void KeyVerificationDialog::onVerifyClicked() {
    if (!m_manager) return;
    if (m_isVerified) {
        m_manager->unverifySession(m_deviceId);
        emit unverified(m_deviceId);
    } else {
        QMessageBox::StandardButton reply = QMessageBox::question(this,
            tr("确认验证"),
            tr("你是否已通过安全渠道（如当面、电话）与 %1 确认安全码完全一致？").arg(m_peerName),
            QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            m_manager->verifySession(m_deviceId);
            emit verified(m_deviceId);
        }
    }
    updateStatusDisplay();
}

void KeyVerificationDialog::onCompareClicked() {
    QString fp = m_manager ? m_manager->getFingerprintDisplay(m_deviceId) : QString();
    QMessageBox::information(this, tr("比对安全码"),
        tr("请让对方打开相同的验证界面，逐组比对以下安全码：\n\n"
           "%1\n\n"
           "如果 8 组数字完全一致，说明通信未被窃听。").arg(fp));
}

QColor KeyVerificationDialog::generateColorFromFingerprint(const QByteArray& data) const {
    if (data.size() < 3) return QColor(100, 100, 100);
    // Use first 3 bytes as HSL hue (0-360), fixed S and L
    int hue = (static_cast<quint8>(data[0]) << 8 | static_cast<quint8>(data[1])) % 360;
    int sat = 60 + (static_cast<quint8>(data[2]) % 30);  // 60-90%
    return QColor::fromHsl(hue, sat, 55);
}

QWidget* KeyVerificationDialog::createFingerprintBlock(const QString& group, const QColor& color) {
    auto* block = new QWidget();
    block->setFixedSize(100, 36);
    block->setStyleSheet(QString(
        "background-color: %1; border-radius: 4px;").arg(color.name()));

    auto* layout = new QVBoxLayout(block);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* label = new QLabel(group);
    label->setAlignment(Qt::AlignCenter);
    label->setStyleSheet("color: white; font-family: 'Consolas', monospace; font-size: 12px; font-weight: bold;");
    layout->addWidget(label);

    return block;
}

} // namespace xrk
