#include "security_manager.h"
#include <QUuid>
#include <QRandomGenerator>
#include <QDateTime>
#include <QCryptographicHash>

namespace xrk {

SecurityManager::SecurityManager(QObject* parent) : QObject(parent) {
}

SecurityManager::~SecurityManager() {
}

QString SecurityManager::generateDeviceId() {
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString SecurityManager::generateSessionToken() {
    QString token = generateRandomString(32);
    // Register the freshly issued token so validateToken() can find it.
    // The value stores the issue timestamp (ISO 8601); it can later be used
    // for expiry checks, and invalidateToken() clears it.
    m_activeTokens.insert(token, QDateTime::currentDateTime().toString(Qt::ISODate));
    return token;
}

bool SecurityManager::validateToken(const QString& token) const {
    return m_activeTokens.contains(token);
}

void SecurityManager::invalidateToken(const QString& token) {
    m_activeTokens.remove(token);
}

QByteArray SecurityManager::encrypt(const QByteArray& data, const QByteArray& key) {
    if (!m_encryptionEnabled || key.isEmpty()) {
        return data;
    }
    
    QByteArray result = data;
    for (int i = 0; i < result.size(); ++i) {
        result[i] ^= key[i % key.size()];
    }
    return result;
}

QByteArray SecurityManager::decrypt(const QByteArray& data, const QByteArray& key) {
    return encrypt(data, key);
}

QByteArray SecurityManager::hashPassword(const QString& password) {
    QByteArray data = password.toUtf8();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(data);
    return hash.result();
}

bool SecurityManager::verifyPassword(const QString& password, const QByteArray& hash) {
    return hashPassword(password) == hash;
}

void SecurityManager::setEncryptionEnabled(bool enabled) {
    m_encryptionEnabled = enabled;
}

bool SecurityManager::isEncryptionEnabled() const {
    return m_encryptionEnabled;
}

QString SecurityManager::generateRandomString(int length) {
    const QString chars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    QString result;
    result.reserve(length);
    
    auto* rng = QRandomGenerator::global();
    for (int i = 0; i < length; ++i) {
        result.append(chars[rng->bounded(chars.size())]);
    }
    
    return result;
}

} // namespace xrk
