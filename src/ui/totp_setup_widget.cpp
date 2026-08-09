#include "totp_setup_widget.h"
#include "app/totp_manager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QClipboard>
#include <QApplication>
#include <QRandomGenerator>
#include <QPainter>
#include <QCryptographicHash>

namespace xrk {

TotpSetupWidget::TotpSetupWidget(TotpManager* totpManager, const QString& accountId, QWidget* parent)
    : QDialog(parent)
    , m_totpManager(totpManager)
    , m_accountId(accountId)
{
    setWindowTitle(tr("设置双因素认证"));
    setFixedSize(360, 480);
    setupUI();

    if (m_totpManager && m_totpManager->is2FAEnabled(accountId)) {
        m_secret = m_totpManager->getSecret(accountId);
        m_secretLabel->setText(tr("密钥: %1").arg(m_secret));
        m_qrLabel->setPixmap(QPixmap::fromImage(generateQrCodeImage(
            m_totpManager->provisioningUri(m_secret, accountId))).scaled(200, 200));
        m_enableBtn->hide();
        m_disableBtn->show();
        QStringList codes = m_totpManager->getBackupCodes(accountId);
        m_backupCodesLabel->setText(tr("恢复码:\n%1").arg(codes.join("\n")));
        m_backupCodesLabel->show();
    } else {
        m_secret = m_totpManager ? m_totpManager->generateSecret() : QString();
        m_secretLabel->setText(tr("密钥: %1").arg(m_secret));
        m_qrLabel->setPixmap(QPixmap::fromImage(generateQrCodeImage(
            m_totpManager->provisioningUri(m_secret, accountId))).scaled(200, 200));
        m_enableBtn->show();
        m_disableBtn->hide();
        m_backupCodesLabel->hide();
    }
}

TotpSetupWidget::~TotpSetupWidget() {}

void TotpSetupWidget::setupUI() {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(10);

    auto* titleLabel = new QLabel(tr("扫描二维码或手动输入密钥"), this);
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLabel->setStyleSheet("font-size: 14px; font-weight: bold; color: #ddd;");
    mainLayout->addWidget(titleLabel);

    m_qrLabel = new QLabel(this);
    m_qrLabel->setFixedSize(200, 200);
    m_qrLabel->setAlignment(Qt::AlignCenter);
    m_qrLabel->setStyleSheet("background: white; border-radius: 8px;");
    mainLayout->addWidget(m_qrLabel, 0, Qt::AlignCenter);

    m_secretLabel = new QLabel(this);
    m_secretLabel->setAlignment(Qt::AlignCenter);
    m_secretLabel->setStyleSheet("color: #aaa; font-size: 11px; word-wrap: break-all;");
    m_secretLabel->setWordWrap(true);
    mainLayout->addWidget(m_secretLabel);

    auto* copyBtn = new QPushButton(tr("复制密钥"), this);
    connect(copyBtn, &QPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(m_secret);
    });
    mainLayout->addWidget(copyBtn, 0, Qt::AlignCenter);

    mainLayout->addWidget(new QLabel(tr("输入验证码确认:"), this));

    auto* verifyLayout = new QHBoxLayout();
    m_verifyInput = new QLineEdit(this);
    m_verifyInput->setPlaceholderText("6位数字");
    m_verifyInput->setMaxLength(6);
    m_verifyInput->setStyleSheet("background: #3a3a3a; color: #ddd; border: 1px solid #555; border-radius: 4px; padding: 6px; font-size: 16px;");
    verifyLayout->addWidget(m_verifyInput);
    m_verifyBtn = new QPushButton(tr("验证"), this);
    m_verifyBtn->setStyleSheet("background: #4a9eff; color: white; border: none; border-radius: 4px; padding: 6px 16px;");
    connect(m_verifyBtn, &QPushButton::clicked, this, &TotpSetupWidget::onVerifyClicked);
    verifyLayout->addWidget(m_verifyBtn);
    mainLayout->addLayout(verifyLayout);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(m_statusLabel);

    m_enableBtn = new QPushButton(tr("启用 2FA"), this);
    m_enableBtn->setStyleSheet("background: #27ae60; color: white; border: none; border-radius: 6px; padding: 8px; font-size: 14px;");
    connect(m_enableBtn, &QPushButton::clicked, this, &TotpSetupWidget::onEnableClicked);
    mainLayout->addWidget(m_enableBtn);

    m_disableBtn = new QPushButton(tr("禁用 2FA"), this);
    m_disableBtn->setStyleSheet("background: #e74c3c; color: white; border: none; border-radius: 6px; padding: 8px; font-size: 14px;");
    connect(m_disableBtn, &QPushButton::clicked, this, &TotpSetupWidget::onDisableClicked);
    mainLayout->addWidget(m_disableBtn);

    m_backupCodesLabel = new QLabel(this);
    m_backupCodesLabel->setAlignment(Qt::AlignCenter);
    m_backupCodesLabel->setWordWrap(true);
    m_backupCodesLabel->setStyleSheet("color: #f39c12; font-size: 11px; background: #2a2a2a; border-radius: 4px; padding: 8px;");
    mainLayout->addWidget(m_backupCodesLabel);

    mainLayout->addStretch();
}

QImage TotpSetupWidget::generateQrCodeImage(const QString& data, int size) {
    // Simple QR code: use a deterministic pattern based on the data hash
    // In production, use a real QR library like libqrencode
    QImage img(size, size, QImage::Format_RGB32);
    img.fill(Qt::white);
    QPainter painter(&img);
    painter.setPen(Qt::black);

    // Generate a simple matrix pattern from the data hash
    QByteArray hash = QCryptographicHash::hash(data.toUtf8(), QCryptographicHash::Md5);
    int moduleSize = size / 25;

    // Draw finder patterns (top-left, top-right, bottom-left)
    auto drawFinder = [&](int x, int y) {
        for (int i = 0; i < 7; i++) {
            for (int j = 0; j < 7; j++) {
                bool black = (i == 0 || i == 6 || j == 0 || j == 6 ||
                             (i >= 2 && i <= 4 && j >= 2 && j <= 4));
                if (black) {
                    painter.fillRect(x + i * moduleSize, y + j * moduleSize,
                                    moduleSize, moduleSize, Qt::black);
                }
            }
        }
    };
    drawFinder(moduleSize, moduleSize);
    drawFinder(size - 8 * moduleSize, moduleSize);
    drawFinder(moduleSize, size - 8 * moduleSize);

    // Fill data area with hash-derived pattern
    for (int i = 9; i < 25; i++) {
        for (int j = 9; j < 25; j++) {
            int byteIdx = (i * 25 + j) % hash.size();
            bool black = (hash[byteIdx] >> ((i + j) % 8)) & 1;
            if (black) {
                painter.fillRect(i * moduleSize, j * moduleSize,
                                moduleSize, moduleSize, Qt::black);
            }
        }
    }

    painter.end();
    return img;
}

void TotpSetupWidget::onVerifyClicked() {
    if (!m_totpManager) return;
    QString code = m_verifyInput->text().trimmed();
    if (code.length() != 6) {
        m_statusLabel->setText(tr("请输入6位数字"));
        m_statusLabel->setStyleSheet("color: #e74c3c;");
        return;
    }
    if (m_totpManager->verifyCode(m_secret, code)) {
        m_statusLabel->setText(tr("验证成功!"));
        m_statusLabel->setStyleSheet("color: #27ae60;");
    } else {
        m_statusLabel->setText(tr("验证码错误，请重试"));
        m_statusLabel->setStyleSheet("color: #e74c3c;");
    }
}

void TotpSetupWidget::onEnableClicked() {
    if (!m_totpManager) return;
    QString code = m_verifyInput->text().trimmed();
    if (!m_totpManager->verifyCode(m_secret, code)) {
        m_statusLabel->setText(tr("请先验证验证码"));
        m_statusLabel->setStyleSheet("color: #e74c3c;");
        return;
    }
    m_totpManager->enable2FA(m_accountId, m_secret);
    QStringList codes = m_totpManager->generateBackupCodes();
    m_backupCodesLabel->setText(tr("恢复码 (请妥善保存):\n%1").arg(codes.join("  ")));
    m_backupCodesLabel->show();
    m_enableBtn->hide();
    m_disableBtn->show();
    m_statusLabel->setText(tr("双因素认证已启用"));
    m_statusLabel->setStyleSheet("color: #27ae60;");
    emit twoFAEnabled(m_accountId);
}

void TotpSetupWidget::onDisableClicked() {
    if (!m_totpManager) return;
    m_totpManager->disable2FA(m_accountId);
    m_disableBtn->hide();
    m_enableBtn->show();
    m_backupCodesLabel->hide();
    m_statusLabel->setText(tr("双因素认证已禁用"));
    m_statusLabel->setStyleSheet("color: #f39c12;");
}

void TotpSetupWidget::applyDarkTheme() {
    setStyleSheet(R"(
        TotpSetupWidget { background: #2b2b2b; color: #ddd; }
        QLabel { color: #ddd; background: transparent; }
        QPushButton { background: #3a3a3a; color: #ddd; border: 1px solid #555; border-radius: 4px; }
        QPushButton:hover { background: #4a4a4a; }
    )");
}

} // namespace xrk
