#pragma once

#include <QObject>
#include <QHash>
#include <QUuid>
#include "core/types.h"

namespace xrk {

class SessionManager : public QObject {
    Q_OBJECT
public:
    explicit SessionManager(QObject* parent = nullptr);
    ~SessionManager();

    QString createSession(const QString& deviceId);
    void closeSession(const QString& sessionId);
    void closeAllSessions();
    
    SessionInfo getSession(const QString& sessionId) const;
    QList<SessionInfo> getActiveSessions() const;
    bool hasActiveSession(const QString& deviceId) const;
    bool isSessionValid(const QString& sessionId) const;

signals:
    void sessionCreated(const QString& sessionId, const QString& deviceId);
    void sessionClosed(const QString& sessionId);
    void sessionExpired(const QString& sessionId);

private slots:
    void onCleanupExpiredSessions();

private:
    QString generateSessionId();
    
    QHash<QString, SessionInfo> m_sessions;
    QTimer* m_cleanupTimer = nullptr;
    static constexpr int SESSION_EXPIRY_MS = 3600000;
};

} // namespace xrk
