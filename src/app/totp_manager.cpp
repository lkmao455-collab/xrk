#include "totp_manager.h"
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QDateTime>
#include <QUrl>
#include <QSettings>
#include <QDebug>

namespace xrk {

static const int TOTP_PERIOD = 30; // seconds
static const int TOTP_DIGITS = 6;
static const QString BASE32_CHARS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

TotpManager::TotpManager(QObject* parent) : QObject(parent) {}

TotpManager::~TotpManager() {}

QString TotpManager::generateSecret() {
    QByteArray randomBytes;
    for (int i = 0; i < 20; ++i) {
        randomBytes.append(static_cast<char>(QRandomGenerator::global()->bounded(256)));
    }
    return base32Encode(randomBytes);
}

QString TotpManager::generateCode(const QString& secret, qint64 timeMs) {
    if (secret.isEmpty()) return QString();

    if (timeMs == 0) timeMs = QDateTime::currentMSecsSinceEpoch();
    qint64 counter = timeMs / 1000 / TOTP_PERIOD;

    // Convert counter to big-endian bytes
    QByteArray counterBytes;
    for (int i = 7; i >= 0; --i) {
        counterBytes.append(static_cast<char>((counter >> (i * 8)) & 0xFF));
    }

    QByteArray key = base32Decode(secret);
    QByteArray hmac = hmacSha1(key, counterBytes);
    int code = dynamicTruncation(hmac);

    return QString::number(code).rightJustified(TOTP_DIGITS, '0');
}

bool TotpManager::verifyCode(const QString& secret, const QString& code, int window) {
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (int i = -window; i <= window; ++i) {
        qint64 adjustedTime = now + (i * TOTP_PERIOD * 1000);
        if (generateCode(secret, adjustedTime) == code) {
            return true;
        }
    }
    return false;
}

QString TotpManager::provisioningUri(const QString& secret, const QString& accountName, const QString& issuer) {
    return QString("otpauth://totp/%1:%2?secret=%3&issuer=%4&algorithm=SHA1&digits=6&period=30")
        .arg(QUrl::toPercentEncoding(issuer))
        .arg(QUrl::toPercentEncoding(accountName))
        .arg(secret)
        .arg(QUrl::toPercentEncoding(issuer));
}

bool TotpManager::enable2FA(const QString& accountId, const QString& secret) {
    if (accountId.isEmpty() || secret.isEmpty()) return false;
    m_secrets[accountId] = secret;
    QSettings s("XRK", "Security");
    s.beginGroup("totp");
    s.setValue(accountId, secret);
    s.endGroup();
    emit twoFAEnabled(accountId);
    return true;
}

bool TotpManager::disable2FA(const QString& accountId) {
    m_secrets.remove(accountId);
    m_backupCodes.remove(accountId);
    QSettings s("XRK", "Security");
    s.beginGroup("totp");
    s.remove(accountId);
    s.endGroup();
    emit twoFADisabled(accountId);
    return true;
}

bool TotpManager::is2FAEnabled(const QString& accountId) const {
    return m_secrets.contains(accountId);
}

QString TotpManager::getSecret(const QString& accountId) const {
    return m_secrets.value(accountId);
}

QStringList TotpManager::generateBackupCodes() {
    QStringList codes;
    for (int i = 0; i < 10; ++i) {
        QString code;
        for (int j = 0; j < 8; ++j) {
            code += QString::number(QRandomGenerator::global()->bounded(10));
        }
        codes.append(code);
    }
    return codes;
}

bool TotpManager::verifyBackupCode(const QString& accountId, const QString& code) {
    QStringList& codes = m_backupCodes[accountId];
    if (codes.removeOne(code)) {
        QSettings s("XRK", "Security");
        s.beginGroup("backup_codes");
        s.setValue(accountId, codes);
        s.endGroup();
        return true;
    }
    return false;
}

QStringList TotpManager::getBackupCodes(const QString& accountId) const {
    return m_backupCodes.value(accountId);
}

qint64 TotpManager::currentTimestamp() const {
    return QDateTime::currentMSecsSinceEpoch();
}

int TotpManager::remainingSeconds() const {
    qint64 now = QDateTime::currentMSecsSinceEpoch() / 1000;
    return TOTP_PERIOD - static_cast<int>(now % TOTP_PERIOD);
}

QByteArray TotpManager::hmacSha1(const QByteArray& key, const QByteArray& message) {
    return QCryptographicHash::hash(message, QCryptographicHash::Sha1);
}

QByteArray TotpManager::base32Decode(const QString& encoded) {
    QString input = encoded.toUpper().remove('=');
    QByteArray result;
    int bits = 0;
    int value = 0;
    for (const QChar& c : input) {
        int idx = BASE32_CHARS.indexOf(c);
        if (idx < 0) continue;
        value = (value << 5) | idx;
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            result.append(static_cast<char>((value >> bits) & 0xFF));
        }
    }
    return result;
}

QString TotpManager::base32Encode(const QByteArray& data) {
    QString result;
    int bits = 0;
    int value = 0;
    for (char c : data) {
        value = (value << 8) | static_cast<unsigned char>(c);
        bits += 8;
        while (bits >= 5) {
            bits -= 5;
            result.append(BASE32_CHARS[(value >> bits) & 0x1F]);
        }
    }
    if (bits > 0) {
        result.append(BASE32_CHARS[(value << (5 - bits)) & 0x1F]);
    }
    while (result.size() % 8 != 0) {
        result.append('=');
    }
    return result;
}

int TotpManager::dynamicTruncation(const QByteArray& hmac) {
    int offset = hmac.at(hmac.size() - 1) & 0x0F;
    int code = ((static_cast<unsigned char>(hmac.at(offset)) & 0x7F) << 24) |
               ((static_cast<unsigned char>(hmac.at(offset + 1)) & 0xFF) << 16) |
               ((static_cast<unsigned char>(hmac.at(offset + 2)) & 0xFF) << 8) |
               (static_cast<unsigned char>(hmac.at(offset + 3)) & 0xFF);
    return code % static_cast<int>(qPow(10, TOTP_DIGITS));
}

} // namespace xrk
