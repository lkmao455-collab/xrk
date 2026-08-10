#include "database_manager.h"
#include "ipmsg_manager.h"
#include "core/logger.h"
#include "core/permission_model.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QDebug>
#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QSettings>

namespace xrk {

namespace {

// Settings key holding the host-wide capability toggle bitmask.
constexpr const char* kCapabilityTogglesKey = "perm/capability_toggles";

QString makePasswordSalt() {
    QByteArray raw(16, Qt::Uninitialized);
    for (int i = 0; i < raw.size(); ++i) {
        raw[i] = static_cast<char>(QRandomGenerator::system()->bounded(256));
    }
    return QString::fromLatin1(raw.toHex());
}

QString hashPassword(const QString& password, const QString& salt) {
    return QString::fromLatin1(
        QCryptographicHash::hash((salt + password).toUtf8(),
                                 QCryptographicHash::Sha256).toHex());
}

// True when removing/demoting/disabling `username` would leave zero enabled
// Admin accounts, i.e. nobody able to manage users ever again. Mirrors the
// self-lock protection PermissionModel applies to the UserManage capability.
bool wouldOrphanAdmins(QSqlDatabase& db, const QString& username) {
    QSqlQuery current(db);
    current.prepare("SELECT level, enabled FROM users WHERE username = ?");
    current.addBindValue(username);
    if (!current.exec() || !current.next()) return false;  // unknown user: nothing to protect
    const bool targetIsActiveAdmin =
        current.value(0).toInt() == static_cast<int>(PermLevel::Admin) && current.value(1).toBool();
    if (!targetIsActiveAdmin) return false;

    QSqlQuery others(db);
    others.prepare("SELECT COUNT(*) FROM users WHERE enabled = 1 AND level = ? AND username <> ?");
    others.addBindValue(static_cast<int>(PermLevel::Admin));
    others.addBindValue(username);
    return others.exec() && others.next() && others.value(0).toInt() == 0;
}

} // namespace

DatabaseManager::DatabaseManager(QObject* parent)
    : QObject(parent) {
}

DatabaseManager::~DatabaseManager() {
    shutdown();
}

bool DatabaseManager::initialize(const QString& dbPath) {
    QMutexLocker locker(&m_mutex);

    // Resolve the target path first so we can detect a path change while already
    // initialized. This guarantees a reset-to-a-new-database actually re-binds the
    // singleton instead of silently keeping the previous connection.
    QString targetPath;
    if (dbPath.isEmpty()) {
        QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        QDir().mkpath(dataDir);
        targetPath = dataDir + "/ipmsg.db";
    } else {
        targetPath = dbPath;
    }

    if (m_initialized) {
        if (m_db.isOpen() && m_db.databaseName() == targetPath) {
            return true;  // Already bound to the exact same database — nothing to do.
        }
        // Re-initializing against a different path: fully release the old connection
        // so the new addDatabase() creates a fresh handle rather than reusing a stale one.
        if (m_db.isOpen()) {
            m_db.close();
        }
        m_db = QSqlDatabase();
        QSqlDatabase::removeDatabase("ipmsg_connection");
        m_initialized = false;
    }

    m_dbPath = targetPath;

    m_db = QSqlDatabase::addDatabase("QSQLITE", "ipmsg_connection");
    m_db.setDatabaseName(m_dbPath);

    if (!m_db.open()) {
        QString err = m_db.lastError().text();
        QString msg = tr("数据库打开失败: %1").arg(err);
        emit databaseError(msg);
        LOG_ERROR("DatabaseManager: " + msg);
        return false;
    }

    // Enable WAL mode for better concurrency
    QSqlQuery query(m_db);
    query.exec("PRAGMA journal_mode=WAL;");
    query.exec("PRAGMA synchronous=NORMAL;");
    query.exec("PRAGMA cache_size=10000;");
    query.exec("PRAGMA temp_store=MEMORY;");

    if (!createTables()) {
        return false;
    }

    runMigrations();

    m_initialized = true;
    qDebug() << "Database initialized at:" << m_dbPath;
    return true;
}

void DatabaseManager::shutdown() {
    QMutexLocker locker(&m_mutex);
    if (m_db.isOpen()) {
        m_db.close();
    }
    // Drop our own handle BEFORE removeDatabase(). QSqlDatabase::removeDatabase()
    // is a no-op (and emits "connection is still in use") while any QSqlDatabase
    // instance still references the connection — and the member m_db does. Without
    // releasing it here, the connection leaks across initialize()/shutdown() cycles,
    // so the next initialize()'s addDatabase() returns the stale handle pointing at
    // the previous database file instead of the newly requested one.
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase("ipmsg_connection");
    m_dbPath.clear();
    m_initialized = false;
}

bool DatabaseManager::createTables() {
    QSqlQuery query(m_db);

    // Messages table
    if (!query.exec(R"(
        CREATE TABLE IF NOT EXISTS messages (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            message_id TEXT UNIQUE NOT NULL,
            sender_id TEXT NOT NULL,
            sender_name TEXT NOT NULL,
            sender_ip TEXT,
            content TEXT,
            timestamp INTEGER NOT NULL,
            is_file INTEGER DEFAULT 0,
            file_path TEXT,
            file_size INTEGER DEFAULT 0,
            is_directory INTEGER DEFAULT 0,
            is_image INTEGER DEFAULT 0,
            image_data BLOB,
            image_file_name TEXT,
            reply_to TEXT,
            reply_content TEXT,
            recall_id TEXT,
            is_recalled INTEGER DEFAULT 0,
            target_id TEXT NOT NULL,
            is_group INTEGER DEFAULT 0,
            is_read INTEGER DEFAULT 0,
            read_by TEXT,
            created_at INTEGER DEFAULT (strftime('%s', 'now'))
        )
    )")) {
        emit databaseError(tr("创建messages表失败: %1").arg(query.lastError().text()));
        return false;
    }

    // Indexes for messages
    query.exec("CREATE INDEX IF NOT EXISTS idx_messages_target_time ON messages(target_id, timestamp DESC);");
    query.exec("CREATE INDEX IF NOT EXISTS idx_messages_recall_id ON messages(recall_id);");
    query.exec("CREATE INDEX IF NOT EXISTS idx_messages_unread ON messages(target_id, is_read);");

    // Devices/Contacts table
    if (!query.exec(R"(
        CREATE TABLE IF NOT EXISTS devices (
            device_id TEXT PRIMARY KEY,
            name TEXT NOT NULL,
            ip TEXT,
            port INTEGER DEFAULT 2425,
            last_seen INTEGER DEFAULT 0,
            is_friend INTEGER DEFAULT 0,
            friend_remark TEXT,
            dnd INTEGER DEFAULT 0,
            avatar_path TEXT,
            signature TEXT,
            created_at INTEGER DEFAULT (strftime('%s', 'now')),
            updated_at INTEGER DEFAULT (strftime('%s', 'now'))
        )
    )")) {
        emit databaseError(tr("创建devices表失败: %1").arg(query.lastError().text()));
        return false;
    }

    // Groups table
    if (!query.exec(R"(
        CREATE TABLE IF NOT EXISTS groups (
            group_id TEXT PRIMARY KEY,
            name TEXT NOT NULL,
            member_ids TEXT NOT NULL, -- JSON array
            member_names TEXT NOT NULL, -- JSON array
            owner_id TEXT,
            announcement TEXT,
            avatar_path TEXT,
            created_at INTEGER NOT NULL,
            updated_at INTEGER DEFAULT (strftime('%s', 'now'))
        )
    )")) {
        emit databaseError(tr("创建groups表失败: %1").arg(query.lastError().text()));
        return false;
    }

    // Settings table
    if (!query.exec(R"(
        CREATE TABLE IF NOT EXISTS settings (
            key TEXT PRIMARY KEY,
            value TEXT NOT NULL,
            updated_at INTEGER DEFAULT (strftime('%s', 'now'))
        )
    )")) {
        emit databaseError(tr("创建settings表失败: %1").arg(query.lastError().text()));
        return false;
    }

    // Recent chats table
    if (!query.exec(R"(
        CREATE TABLE IF NOT EXISTS recent_chats (
            target_id TEXT PRIMARY KEY,
            target_name TEXT NOT NULL,
            last_message TEXT,
            timestamp INTEGER NOT NULL,
            unread_count INTEGER DEFAULT 0,
            is_group INTEGER DEFAULT 0,
            updated_at INTEGER DEFAULT (strftime('%s', 'now'))
        )
    )")) {
        emit databaseError(tr("创建recent_chats表失败: %1").arg(query.lastError().text()));
        return false;
    }

    // File transfers table
    if (!query.exec(R"(
        CREATE TABLE IF NOT EXISTS file_transfers (
            file_id TEXT PRIMARY KEY,
            file_name TEXT NOT NULL,
            file_size INTEGER NOT NULL,
            md5 TEXT,
            local_path TEXT,
            remote_path TEXT,
            transferred_size INTEGER DEFAULT 0,
            status INTEGER DEFAULT 0, -- 0=pending, 1=transferring, 2=completed, 3=failed, 4=paused
            start_time INTEGER NOT NULL,
            end_time INTEGER DEFAULT 0,
            is_sender INTEGER NOT NULL,
            target_id TEXT NOT NULL,
            is_group INTEGER DEFAULT 0,
            checksum BLOB,
            resume_offset INTEGER DEFAULT 0,
            session_id TEXT,
            created_at INTEGER DEFAULT (strftime('%s', 'now'))
        )
    )")) {
        emit databaseError(tr("创建file_transfers表失败: %1").arg(query.lastError().text()));
        return false;
    }

    query.exec("CREATE INDEX IF NOT EXISTS idx_file_transfers_target ON file_transfers(target_id, start_time DESC);");
    query.exec("CREATE INDEX IF NOT EXISTS idx_file_transfers_status ON file_transfers(status, target_id);");

    // Offline messages table
    if (!query.exec(R"(
        CREATE TABLE IF NOT EXISTS offline_messages (
            id TEXT PRIMARY KEY,
            target_device_id TEXT NOT NULL,
            payload BLOB NOT NULL,
            retry_count INTEGER DEFAULT 0,
            max_retries INTEGER DEFAULT 10,
            created_at INTEGER NOT NULL,
            expires_at INTEGER NOT NULL,
            delivered_at INTEGER DEFAULT 0
        )
    )")) {
        emit databaseError(tr("创建offline_messages表失败: %1").arg(query.lastError().text()));
        return false;
    }

    query.exec("CREATE INDEX IF NOT EXISTS idx_offline_target ON offline_messages(target_device_id, created_at);");
    query.exec("CREATE INDEX IF NOT EXISTS idx_offline_expires ON offline_messages(expires_at);");

    // E2EE sessions table
    if (!query.exec(R"(
        CREATE TABLE IF NOT EXISTS e2ee_sessions (
            device_id TEXT PRIMARY KEY,
            public_key BLOB NOT NULL,
            shared_secret BLOB,
            session_id TEXT UNIQUE NOT NULL,
            established_at INTEGER NOT NULL,
            last_activity INTEGER DEFAULT 0,
            encryption_algorithm TEXT DEFAULT 'AES-256-GCM',
            nonce BLOB,
            is_active INTEGER DEFAULT 1,
            created_at INTEGER DEFAULT (strftime('%s', 'now')),
            updated_at INTEGER DEFAULT (strftime('%s', 'now'))
        )
    )")) {
        emit databaseError(tr("创建e2ee_sessions表失败: %1").arg(query.lastError().text()));
        return false;
    }

    query.exec("CREATE INDEX IF NOT EXISTS idx_e2ee_sessions_device ON e2ee_sessions(device_id);");
    query.exec("CREATE INDEX IF NOT EXISTS idx_e2ee_sessions_active ON e2ee_sessions(is_active);");

    // Voice messages table
    if (!query.exec(R"(
        CREATE TABLE IF NOT EXISTS voice_messages (
            message_id TEXT PRIMARY KEY,
            sender_id TEXT NOT NULL,
            sender_name TEXT NOT NULL,
            voice_data BLOB NOT NULL,
            voice_file_name TEXT,
            duration INTEGER DEFAULT 0,
            timestamp INTEGER NOT NULL,
            is_read INTEGER DEFAULT 0,
            target_id TEXT NOT NULL,
            is_group INTEGER DEFAULT 0,
            created_at INTEGER DEFAULT (strftime('%s', 'now'))
        )
    )")) {
        emit databaseError(tr("创建voice_messages表失败: %1").arg(query.lastError().text()));
        return false;
    }

    query.exec("CREATE INDEX IF NOT EXISTS idx_voice_messages_target ON voice_messages(target_id, timestamp DESC);");
    query.exec("CREATE INDEX IF NOT EXISTS idx_voice_messages_unread ON voice_messages(target_id, is_read);");

    // Video messages table
    if (!query.exec(R"(
        CREATE TABLE IF NOT EXISTS video_messages (
            message_id TEXT PRIMARY KEY,
            sender_id TEXT NOT NULL,
            sender_name TEXT NOT NULL,
            video_data BLOB NOT NULL,
            video_file_name TEXT,
            duration INTEGER DEFAULT 0,
            width REAL DEFAULT 0,
            height REAL DEFAULT 0,
            timestamp INTEGER NOT NULL,
            is_read INTEGER DEFAULT 0,
            target_id TEXT NOT NULL,
            is_group INTEGER DEFAULT 0,
            created_at INTEGER DEFAULT (strftime('%s', 'now'))
        )
    )")) {
        emit databaseError(tr("创建video_messages表失败: %1").arg(query.lastError().text()));
        return false;
    }

    query.exec("CREATE INDEX IF NOT EXISTS idx_video_messages_target ON video_messages(target_id, timestamp DESC);");
    query.exec("CREATE INDEX IF NOT EXISTS idx_video_messages_unread ON video_messages(target_id, is_read);");

    // Location messages table
    if (!query.exec(R"(
        CREATE TABLE IF NOT EXISTS location_messages (
            message_id TEXT PRIMARY KEY,
            sender_id TEXT NOT NULL,
            sender_name TEXT NOT NULL,
            latitude REAL NOT NULL,
            longitude REAL NOT NULL,
            location_name TEXT,
            timestamp INTEGER NOT NULL,
            is_read INTEGER DEFAULT 0,
            target_id TEXT NOT NULL,
            is_group INTEGER DEFAULT 0,
            created_at INTEGER DEFAULT (strftime('%s', 'now'))
        )
    )")) {
        emit databaseError(tr("创建location_messages表失败: %1").arg(query.lastError().text()));
        return false;
    }

    query.exec("CREATE INDEX IF NOT EXISTS idx_location_messages_target ON location_messages(target_id, timestamp DESC);");
    query.exec("CREATE INDEX IF NOT EXISTS idx_location_messages_unread ON location_messages(target_id, is_read);");

    // Card messages table
    if (!query.exec(R"(
        CREATE TABLE IF NOT EXISTS card_messages (
            message_id TEXT PRIMARY KEY,
            sender_id TEXT NOT NULL,
            sender_name TEXT NOT NULL,
            vcard_data TEXT NOT NULL,
            timestamp INTEGER NOT NULL,
            is_read INTEGER DEFAULT 0,
            target_id TEXT NOT NULL,
            is_group INTEGER DEFAULT 0,
            created_at INTEGER DEFAULT (strftime('%s', 'now'))
        )
    )")) {
        emit databaseError(tr("创建card_messages表失败: %1").arg(query.lastError().text()));
        return false;
    }

    query.exec("CREATE INDEX IF NOT EXISTS idx_card_messages_target ON card_messages(target_id, timestamp DESC);");
    query.exec("CREATE INDEX IF NOT EXISTS idx_card_messages_unread ON card_messages(target_id, is_read);");

    // Merge forward messages table
    if (!query.exec(R"(
        CREATE TABLE IF NOT EXISTS merge_forward_messages (
            message_id TEXT PRIMARY KEY,
            sender_id TEXT NOT NULL,
            sender_name TEXT NOT NULL,
            merged_data BLOB NOT NULL,
            timestamp INTEGER NOT NULL,
            is_read INTEGER DEFAULT 0,
            target_id TEXT NOT NULL,
            is_group INTEGER DEFAULT 0,
            created_at INTEGER DEFAULT (strftime('%s', 'now'))
        )
    )")) {
        emit databaseError(tr("创建merge_forward_messages表失败: %1").arg(query.lastError().text()));
        return false;
    }

    query.exec("CREATE INDEX IF NOT EXISTS idx_merge_forward_messages_target ON merge_forward_messages(target_id, timestamp DESC);");
    query.exec("CREATE INDEX IF NOT EXISTS idx_merge_forward_messages_unread ON merge_forward_messages(target_id, is_read);");

    // Group announcements table
    if (!query.exec(R"(
        CREATE TABLE IF NOT EXISTS group_announcements (
            id TEXT PRIMARY KEY,
            group_id TEXT NOT NULL,
            group_name TEXT NOT NULL,
            announcement TEXT NOT NULL,
            announcer_id TEXT NOT NULL,
            announcer_name TEXT NOT NULL,
            timestamp INTEGER NOT NULL
        )
    )")) {
        emit databaseError(tr("创建group_announcements表失败: %1").arg(query.lastError().text()));
        return false;
    }
    query.exec("CREATE INDEX IF NOT EXISTS idx_group_announcements_group ON group_announcements(group_id, timestamp DESC);");

    // Group mentions table
    if (!query.exec(R"(
        CREATE TABLE IF NOT EXISTS group_mentions (
            id TEXT PRIMARY KEY,
            group_id TEXT NOT NULL,
            group_name TEXT NOT NULL,
            message TEXT NOT NULL,
            mentioned_member_ids TEXT NOT NULL,
            mentioned_member_names TEXT NOT NULL,
            sender_id TEXT NOT NULL,
            sender_name TEXT NOT NULL,
            timestamp INTEGER NOT NULL
        )
    )")) {
        emit databaseError(tr("创建group_mentions表失败: %1").arg(query.lastError().text()));
        return false;
    }
    query.exec("CREATE INDEX IF NOT EXISTS idx_group_mentions_group ON group_mentions(group_id, timestamp DESC);");

    // Group votes table
    if (!query.exec(R"(
        CREATE TABLE IF NOT EXISTS group_votes (
            id TEXT PRIMARY KEY,
            group_id TEXT NOT NULL,
            group_name TEXT NOT NULL,
            vote_title TEXT NOT NULL,
            options TEXT NOT NULL,
            duration_seconds INTEGER NOT NULL,
            creator_id TEXT NOT NULL,
            creator_name TEXT NOT NULL,
            timestamp INTEGER NOT NULL
        )
    )")) {
        emit databaseError(tr("创建group_votes表失败: %1").arg(query.lastError().text()));
        return false;
    }
    query.exec("CREATE INDEX IF NOT EXISTS idx_group_votes_group ON group_votes(group_id, timestamp DESC);");

    // Group files table
    if (!query.exec(R"(
        CREATE TABLE IF NOT EXISTS group_files (
            id TEXT PRIMARY KEY,
            group_id TEXT NOT NULL,
            group_name TEXT NOT NULL,
            file_id TEXT NOT NULL,
            file_name TEXT NOT NULL,
            file_size INTEGER NOT NULL,
            md5 TEXT NOT NULL,
            uploader_id TEXT NOT NULL,
            uploader_name TEXT NOT NULL,
            timestamp INTEGER NOT NULL
        )
    )")) {
        emit databaseError(tr("创建group_files表失败: %1").arg(query.lastError().text()));
        return false;
    }
    query.exec("CREATE INDEX IF NOT EXISTS idx_group_files_group ON group_files(group_id, timestamp DESC);");

    // Group albums table
    if (!query.exec(R"(
        CREATE TABLE IF NOT EXISTS group_albums (
            id TEXT PRIMARY KEY,
            group_id TEXT NOT NULL,
            group_name TEXT NOT NULL,
            album_id TEXT NOT NULL,
            album_name TEXT NOT NULL,
            file_ids TEXT NOT NULL,
            file_names TEXT NOT NULL,
            creator_id TEXT NOT NULL,
            creator_name TEXT NOT NULL,
            timestamp INTEGER NOT NULL
        )
    )")) {
        emit databaseError(tr("创建group_albums表失败: %1").arg(query.lastError().text()));
        return false;
    }
    query.exec("CREATE INDEX IF NOT EXISTS idx_group_albums_group ON group_albums(group_id, timestamp DESC);");

    // Group todos table
    if (!query.exec(R"(
        CREATE TABLE IF NOT EXISTS group_todos (
            id TEXT PRIMARY KEY,
            group_id TEXT NOT NULL,
            group_name TEXT NOT NULL,
            todo_id TEXT NOT NULL,
            title TEXT NOT NULL,
            description TEXT,
            status INTEGER DEFAULT 0,
            priority INTEGER DEFAULT 0,
            assignee_id TEXT,
            assignee_name TEXT,
            creator_id TEXT NOT NULL,
            creator_name TEXT NOT NULL,
            due_date INTEGER DEFAULT 0,
            timestamp INTEGER NOT NULL
        )
    )")) {
        emit databaseError(tr("创建group_todos表失败: %1").arg(query.lastError().text()));
        return false;
    }
    query.exec("CREATE INDEX IF NOT EXISTS idx_group_todos_group ON group_todos(group_id, timestamp DESC);");
    query.exec("CREATE INDEX IF NOT EXISTS idx_group_todos_status ON group_todos(status);");

    // Sync request table
    if (!query.exec(R"(
        CREATE TABLE IF NOT EXISTS sync_requests (
            request_id TEXT PRIMARY KEY,
            account_hash TEXT NOT NULL,
            sync_key_hash TEXT NOT NULL,
            timestamp INTEGER NOT NULL
        )
    )")) {
        emit databaseError(tr("创建sync_requests表失败: %1").arg(query.lastError().text()));
        return false;
    }

    // Sync snapshot table
    if (!query.exec(R"(
        CREATE TABLE IF NOT EXISTS sync_snapshots (
            version INTEGER PRIMARY KEY,
            data BLOB NOT NULL,
            timestamp INTEGER NOT NULL
        )
    )")) {
        emit databaseError(tr("创建sync_snapshots表失败: %1").arg(query.lastError().text()));
        return false;
    }

    // Sync ack table
    if (!query.exec(R"(
        CREATE TABLE IF NOT EXISTS sync_acks (
            request_id TEXT PRIMARY KEY,
            success INTEGER NOT NULL,
            applied_count INTEGER NOT NULL,
            error_message TEXT,
            timestamp INTEGER NOT NULL
        )
    )")) {
        emit databaseError(tr("创建sync_acks表失败: %1").arg(query.lastError().text()));
        return false;
    }

    // Blocked users table
    if (!query.exec(R"(
        CREATE TABLE IF NOT EXISTS blocked_users (
            device_id TEXT PRIMARY KEY,
            reason TEXT,
            blocked_at INTEGER DEFAULT (strftime('%s', 'now'))
        )
    )")) {
        emit databaseError(tr("创建blocked_users表失败: %1").arg(query.lastError().text()));
        return false;
    }

    // Blacklisted IPs table
    if (!query.exec(R"(
        CREATE TABLE IF NOT EXISTS blacklisted_ips (
            ip TEXT PRIMARY KEY,
            reason TEXT,
            blocked_at INTEGER DEFAULT (strftime('%s', 'now'))
        )
    )")) {
        emit databaseError(tr("创建blacklisted_ips表失败: %1").arg(query.lastError().text()));
        return false;
    }

    // Users table (permission management)
    if (!query.exec(R"(
        CREATE TABLE IF NOT EXISTS users (
            username TEXT PRIMARY KEY,
            password_hash TEXT NOT NULL,
            salt TEXT NOT NULL,
            level INTEGER DEFAULT 1,
            enabled INTEGER DEFAULT 1,
            last_login INTEGER DEFAULT 0,
            created_at INTEGER DEFAULT (strftime('%s', 'now'))
        )
    )")) {
        emit databaseError(tr("创建users表失败: %1").arg(query.lastError().text()));
        return false;
    }

    // Per-device permission overrides. cap_mask = -1 means "no mask, use level".
    if (!query.exec(R"(
        CREATE TABLE IF NOT EXISTS device_permissions (
            device_id TEXT PRIMARY KEY,
            level INTEGER DEFAULT 1,
            cap_mask INTEGER DEFAULT -1,
            note TEXT,
            updated_at INTEGER DEFAULT (strftime('%s', 'now'))
        )
    )")) {
        emit databaseError(tr("创建device_permissions表失败: %1").arg(query.lastError().text()));
        return false;
    }

    return true;
}

void DatabaseManager::runMigrations() {
    // Migration version tracking
    QSqlQuery query(m_db);
    query.exec("CREATE TABLE IF NOT EXISTS schema_version (version INTEGER PRIMARY KEY);");
    
    int currentVersion = 0;
    if (query.exec("SELECT version FROM schema_version ORDER BY version DESC LIMIT 1") && query.next()) {
        currentVersion = query.value(0).toInt();
    }

    const int LATEST_VERSION = 5;
    if (currentVersion < 1) {
        // Version 1: Initial schema (already created above).
        // NOTE: migrations intentionally do NOT write a per-step version row. Writing
        // a distinct row per step (with `version` as the PRIMARY KEY) used to create
        // multiple schema_version rows (1..5), which made `SELECT version` ambiguous.
        // The single authoritative version row is written once at the end of this
        // function instead.
    }
    if (currentVersion < 2) {
        // Version 2: Add E2EE sessions table
        QSqlQuery e2eeQuery(m_db);
        e2eeQuery.exec(R"(
            CREATE TABLE IF NOT EXISTS e2ee_sessions (
                device_id TEXT PRIMARY KEY,
                public_key BLOB NOT NULL,
                shared_secret BLOB,
                session_id TEXT UNIQUE NOT NULL,
                established_at INTEGER NOT NULL,
                last_activity INTEGER DEFAULT 0,
                encryption_algorithm TEXT DEFAULT 'AES-256-GCM',
                nonce BLOB,
                is_active INTEGER DEFAULT 1,
                created_at INTEGER DEFAULT (strftime('%s', 'now')),
                updated_at INTEGER DEFAULT (strftime('%s', 'now'))
            )
        )");
        e2eeQuery.exec("CREATE INDEX IF NOT EXISTS idx_e2ee_sessions_device ON e2ee_sessions(device_id);");
        e2eeQuery.exec("CREATE INDEX IF NOT EXISTS idx_e2ee_sessions_active ON e2ee_sessions(is_active);");
    }
    if (currentVersion < 3) {
        // Version 3: Add resumable transfer support (checksum, resume_offset, session_id)
        QSqlQuery transferQuery(m_db);
        transferQuery.exec("ALTER TABLE file_transfers ADD COLUMN checksum BLOB");
        transferQuery.exec("ALTER TABLE file_transfers ADD COLUMN resume_offset INTEGER DEFAULT 0");
        transferQuery.exec("ALTER TABLE file_transfers ADD COLUMN session_id TEXT");
        transferQuery.exec("CREATE INDEX IF NOT EXISTS idx_file_transfers_status ON file_transfers(status, target_id)");
    }
    if (currentVersion < 4) {
        // Version 4: Add pinned column to messages, blocked_users, blacklisted_ips tables
        QSqlQuery migQuery(m_db);
        migQuery.exec("ALTER TABLE messages ADD COLUMN is_pinned INTEGER DEFAULT 0");
        migQuery.exec("CREATE TABLE IF NOT EXISTS blocked_users (device_id TEXT PRIMARY KEY, reason TEXT, blocked_at INTEGER DEFAULT (strftime('%s','now')))");
        migQuery.exec("CREATE TABLE IF NOT EXISTS blacklisted_ips (ip TEXT PRIMARY KEY, reason TEXT, blocked_at INTEGER DEFAULT (strftime('%s','now')))");
    }
    if (currentVersion < 5) {
        // Version 5: permission management - named users, per-device ACL overrides
        // and a host-wide capability toggle bitmask.
        QSqlQuery permQuery(m_db);
        permQuery.exec("CREATE TABLE IF NOT EXISTS users (username TEXT PRIMARY KEY, password_hash TEXT NOT NULL, salt TEXT NOT NULL, level INTEGER DEFAULT 1, enabled INTEGER DEFAULT 1, last_login INTEGER DEFAULT 0, created_at INTEGER DEFAULT (strftime('%s','now')))");
        permQuery.exec("CREATE TABLE IF NOT EXISTS device_permissions (device_id TEXT PRIMARY KEY, level INTEGER DEFAULT 1, cap_mask INTEGER DEFAULT -1, note TEXT, updated_at INTEGER DEFAULT (strftime('%s','now')))");
        permQuery.exec("CREATE INDEX IF NOT EXISTS idx_users_enabled ON users(enabled)");

        // Seed the built-in admin from the legacy single-password setting so an
        // upgraded host keeps accepting the credentials its operator already knows.
        // An empty legacy password stays empty here; the host keeps treating that
        // as "no auth required", exactly as it did before v5.
        QSqlQuery countQuery(m_db);
        if (countQuery.exec("SELECT COUNT(*) FROM users") && countQuery.next()
            && countQuery.value(0).toInt() == 0) {
            const QString legacyPassword =
                QSettings("XRK", "XRK").value("security/password", "").toString();
            const QString salt = makePasswordSalt();
            QSqlQuery seedQuery(m_db);
            seedQuery.prepare("INSERT INTO users (username, password_hash, salt, level, enabled) VALUES (?, ?, ?, ?, 1)");
            seedQuery.addBindValue(QStringLiteral("admin"));
            seedQuery.addBindValue(hashPassword(legacyPassword, salt));
            seedQuery.addBindValue(salt);
            seedQuery.addBindValue(static_cast<int>(PermLevel::Admin));
            seedQuery.exec();
        }

        // Default every capability to enabled so an upgraded host behaves as it
        // did before the toggles existed. INSERT OR IGNORE keeps a re-run safe.
        QSqlQuery toggleQuery(m_db);
        toggleQuery.prepare("INSERT OR IGNORE INTO settings (key, value, updated_at) VALUES (?, ?, strftime('%s','now'))");
        toggleQuery.addBindValue(QString::fromLatin1(kCapabilityTogglesKey));
        toggleQuery.addBindValue(QString::number(kAllCapabilities));
        toggleQuery.exec();
    }

    // Persist a single authoritative schema version row. We deliberately keep only
    // one row (writing the latest version) so a `SELECT version FROM schema_version`
    // is unambiguous — previously each step inserted a distinct primary key, leaving
    // multiple rows (1..5) and any single-row read returned the lowest one.
    query.exec("DELETE FROM schema_version;");
    query.exec(QString("INSERT INTO schema_version (version) VALUES (%1);").arg(LATEST_VERSION));
}

// --- Messages ---

bool DatabaseManager::saveMessage(const IPMsgMessage& msg, const QString& targetId, bool isGroup) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    QSqlQuery query(m_db);
    query.prepare(R"(
        INSERT OR REPLACE INTO messages (
            message_id, sender_id, sender_name, sender_ip, content, timestamp,
            is_file, file_path, file_size, is_directory, is_image, image_data, image_file_name,
            reply_to, reply_content, recall_id, is_recalled,
            target_id, is_group, is_read
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )");

    QString messageId = msg.recallId.isEmpty() 
        ? QString("%1_%2").arg(msg.senderId).arg(msg.timestamp)
        : msg.recallId;

    query.addBindValue(messageId);
    query.addBindValue(msg.senderId);
    query.addBindValue(msg.senderName);
    query.addBindValue(msg.senderIp);
    query.addBindValue(msg.content);
    query.addBindValue(msg.timestamp);
    query.addBindValue(msg.isFile ? 1 : 0);
    query.addBindValue(msg.filePath);
    query.addBindValue(msg.fileSize);
    query.addBindValue(msg.isDirectory ? 1 : 0);
    query.addBindValue(msg.isImage ? 1 : 0);
    query.addBindValue(msg.imageData);
    query.addBindValue(msg.imageFileName);
    query.addBindValue(msg.replyTo);
    query.addBindValue(msg.replyContent);
    query.addBindValue(msg.recallId);
    query.addBindValue(msg.isRecalled ? 1 : 0);
    query.addBindValue(targetId);
    query.addBindValue(isGroup ? 1 : 0);
    query.addBindValue(0); // is_read

    if (!query.exec()) {
        emit databaseError(tr("保存消息失败: %1").arg(query.lastError().text()));
        return false;
    }

    // Update recent chat
    updateRecentChat(targetId, msg.senderName, msg.content, msg.timestamp, isGroup);
    return true;
}

QList<IPMsgMessage> DatabaseManager::loadMessages(const QString& targetId, int limit, qint64 beforeTimestamp, bool isGroup) {
    QMutexLocker locker(&m_mutex);
    QList<IPMsgMessage> messages;

    if (!m_db.isOpen()) return messages;

    QSqlQuery query(m_db);
    QString sql = "SELECT * FROM messages WHERE target_id = ? AND is_group = ?";
    if (beforeTimestamp > 0) {
        sql += " AND timestamp < ?";
    }
    sql += " ORDER BY timestamp DESC LIMIT ?";

    query.prepare(sql);
    query.addBindValue(targetId);
    query.addBindValue(isGroup ? 1 : 0);
    if (beforeTimestamp > 0) {
        query.addBindValue(beforeTimestamp);
    }
    query.addBindValue(limit);

    if (!query.exec()) {
        emit databaseError(tr("加载消息失败: %1").arg(query.lastError().text()));
        return messages;
    }

    while (query.next()) {
        IPMsgMessage msg;
        msg.senderId = query.value("sender_id").toString();
        msg.senderName = query.value("sender_name").toString();
        msg.senderIp = query.value("sender_ip").toString();
        msg.content = query.value("content").toString();
        msg.timestamp = query.value("timestamp").toLongLong();
        msg.isFile = query.value("is_file").toBool();
        msg.filePath = query.value("file_path").toString();
        msg.fileSize = query.value("file_size").toLongLong();
        msg.isDirectory = query.value("is_directory").toBool();
        msg.isImage = query.value("is_image").toBool();
        msg.imageData = query.value("image_data").toByteArray();
        msg.imageFileName = query.value("image_file_name").toString();
        msg.replyTo = query.value("reply_to").toString();
        msg.replyContent = query.value("reply_content").toString();
        msg.recallId = query.value("recall_id").toString();
        msg.isRecalled = query.value("is_recalled").toBool();
        messages.prepend(msg); // Reverse to chronological order
    }

    return messages;
}

bool DatabaseManager::deleteMessage(const QString& recallId) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    QSqlQuery query(m_db);
    query.prepare("UPDATE messages SET is_recalled = 1, content = '[已撤回]' WHERE recall_id = ?");
    query.addBindValue(recallId);
    return query.exec();
}

bool DatabaseManager::markMessageRead(const QString& messageId, const QString& readerName) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    QSqlQuery query(m_db);
    query.prepare("UPDATE messages SET is_read = 1, read_by = ? WHERE message_id = ? AND is_read = 0");
    query.addBindValue(readerName);
    query.addBindValue(messageId);
    return query.exec();
}

int DatabaseManager::getUnreadCount(const QString& targetId, bool isGroup) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return 0;

    QSqlQuery query(m_db);
    query.prepare("SELECT COUNT(*) FROM messages WHERE target_id = ? AND is_group = ? AND is_read = 0");
    query.addBindValue(targetId);
    query.addBindValue(isGroup ? 1 : 0);
    if (query.exec() && query.next()) {
        return query.value(0).toInt();
    }
    return 0;
}

// --- Devices/Contacts ---

bool DatabaseManager::saveDevice(const IPMsgDevice& device) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    QSqlQuery query(m_db);
    query.prepare(R"(
        INSERT OR REPLACE INTO devices (
            device_id, name, ip, port, last_seen, is_friend, updated_at
        ) VALUES (?, ?, ?, ?, ?, ?, strftime('%s', 'now'))
    )");
    query.addBindValue(device.id);
    query.addBindValue(device.name);
    query.addBindValue(device.ip);
    query.addBindValue(device.port);
    query.addBindValue(device.lastSeen);
    query.addBindValue(0); // is_friend default

    return query.exec();
}

bool DatabaseManager::updateDeviceLastSeen(const QString& deviceId, qint64 timestamp) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    QSqlQuery query(m_db);
    query.prepare("UPDATE devices SET last_seen = ?, updated_at = strftime('%s', 'now') WHERE device_id = ?");
    query.addBindValue(timestamp);
    query.addBindValue(deviceId);
    return query.exec();
}

QList<IPMsgDevice> DatabaseManager::loadAllDevices() {
    QMutexLocker locker(&m_mutex);
    QList<IPMsgDevice> devices;

    if (!m_db.isOpen()) return devices;

    QSqlQuery query(m_db);
    if (!query.exec("SELECT * FROM devices ORDER BY last_seen DESC")) {
        emit databaseError(tr("加载设备列表失败: %1").arg(query.lastError().text()));
        return devices;
    }

    while (query.next()) {
        IPMsgDevice device;
        device.id = query.value("device_id").toString();
        device.name = query.value("name").toString();
        device.ip = query.value("ip").toString();
        device.port = query.value("port").toInt();
        device.lastSeen = query.value("last_seen").toLongLong();
        devices.append(device);
    }

    return devices;
}

bool DatabaseManager::removeDevice(const QString& deviceId) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    QSqlQuery query(m_db);
    query.prepare("DELETE FROM devices WHERE device_id = ?");
    query.addBindValue(deviceId);
    return query.exec();
}

// --- Friends ---

bool DatabaseManager::addFriend(const QString& deviceId) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    QSqlQuery query(m_db);
    query.prepare("UPDATE devices SET is_friend = 1, updated_at = strftime('%s', 'now') WHERE device_id = ?");
    query.addBindValue(deviceId);
    return query.exec();
}

bool DatabaseManager::removeFriend(const QString& deviceId) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    QSqlQuery query(m_db);
    query.prepare("UPDATE devices SET is_friend = 0, updated_at = strftime('%s', 'now') WHERE device_id = ?");
    query.addBindValue(deviceId);
    return query.exec();
}

QList<QString> DatabaseManager::loadFriends() {
    QMutexLocker locker(&m_mutex);
    QList<QString> friends;

    if (!m_db.isOpen()) return friends;

    QSqlQuery query(m_db);
    if (!query.exec("SELECT device_id FROM devices WHERE is_friend = 1")) {
        emit databaseError(tr("加载好友列表失败: %1").arg(query.lastError().text()));
        return friends;
    }

    while (query.next()) {
        friends.append(query.value(0).toString());
    }

    return friends;
}

bool DatabaseManager::isFriend(const QString& deviceId) const {
    QMutexLocker locker(const_cast<QRecursiveMutex*>(&m_mutex));
    if (!m_db.isOpen()) return false;

    QSqlQuery query(m_db);
    query.prepare("SELECT 1 FROM devices WHERE device_id = ? AND is_friend = 1");
    query.addBindValue(deviceId);
    if (query.exec() && query.next()) {
        return true;
    }
    return false;
}

// --- Groups ---

bool DatabaseManager::saveGroup(const IPMsgGroup& group) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    QSqlQuery query(m_db);
    QJsonArray memberIdsArray, memberNamesArray;
    for (const QString& id : group.memberIds) memberIdsArray.append(id);
    for (const QString& name : group.memberNames) memberNamesArray.append(name);

    query.prepare(R"(
        INSERT OR REPLACE INTO groups (
            group_id, name, member_ids, member_names, owner_id, created_at, updated_at
        ) VALUES (?, ?, ?, ?, ?, ?, strftime('%s', 'now'))
    )");
    query.addBindValue(group.id);
    query.addBindValue(group.name);
    query.addBindValue(QJsonDocument(memberIdsArray).toJson(QJsonDocument::Compact));
    query.addBindValue(QJsonDocument(memberNamesArray).toJson(QJsonDocument::Compact));
    query.addBindValue(group.memberIds.isEmpty() ? "" : group.memberIds.first()); // owner = first member
    query.addBindValue(group.createdAt);

    return query.exec();
}

bool DatabaseManager::updateGroup(const IPMsgGroup& group) {
    return saveGroup(group);
}

bool DatabaseManager::deleteGroup(const QString& groupId) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    QSqlQuery query(m_db);
    query.prepare("DELETE FROM groups WHERE group_id = ?");
    query.addBindValue(groupId);
    return query.exec();
}

QList<IPMsgGroup> DatabaseManager::loadAllGroups() {
    QMutexLocker locker(&m_mutex);
    QList<IPMsgGroup> groups;

    if (!m_db.isOpen()) return groups;

    QSqlQuery query(m_db);
    if (!query.exec("SELECT * FROM groups ORDER BY created_at DESC")) {
        emit databaseError(tr("加载群组列表失败: %1").arg(query.lastError().text()));
        return groups;
    }

    while (query.next()) {
        IPMsgGroup group;
        group.id = query.value("group_id").toString();
        group.name = query.value("name").toString();
        
        QJsonArray idsArray = QJsonDocument::fromJson(query.value("member_ids").toByteArray()).array();
        QJsonArray namesArray = QJsonDocument::fromJson(query.value("member_names").toByteArray()).array();
        
        for (const auto& v : idsArray) group.memberIds.append(v.toString());
        for (const auto& v : namesArray) group.memberNames.append(v.toString());
        
        group.createdAt = query.value("created_at").toLongLong();
        groups.append(group);
    }

    return groups;
}

IPMsgGroup DatabaseManager::loadGroup(const QString& groupId) {
    QMutexLocker locker(&m_mutex);
    IPMsgGroup group;

    if (!m_db.isOpen()) return group;

    QSqlQuery query(m_db);
    query.prepare("SELECT * FROM groups WHERE group_id = ?");
    query.addBindValue(groupId);

    if (query.exec() && query.next()) {
        group.id = query.value("group_id").toString();
        group.name = query.value("name").toString();
        
        QJsonArray idsArray = QJsonDocument::fromJson(query.value("member_ids").toByteArray()).array();
        QJsonArray namesArray = QJsonDocument::fromJson(query.value("member_names").toByteArray()).array();
        
        for (const auto& v : idsArray) group.memberIds.append(v.toString());
        for (const auto& v : namesArray) group.memberNames.append(v.toString());
        
        group.createdAt = query.value("created_at").toLongLong();
    }

    return group;
}

// --- Settings ---

bool DatabaseManager::setSetting(const QString& key, const QVariant& value) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    QSqlQuery query(m_db);
    query.prepare("INSERT OR REPLACE INTO settings (key, value, updated_at) VALUES (?, ?, strftime('%s', 'now'))");
    query.addBindValue(key);
    query.addBindValue(value.toString());
    return query.exec();
}

QVariant DatabaseManager::getSetting(const QString& key, const QVariant& defaultValue) const {
    QMutexLocker locker(const_cast<QRecursiveMutex*>(&m_mutex));
    if (!m_db.isOpen()) return defaultValue;

    QSqlQuery query(m_db);
    query.prepare("SELECT value FROM settings WHERE key = ?");
    query.addBindValue(key);
    if (query.exec() && query.next()) {
        return query.value(0);
    }
    return defaultValue;
}

// --- Permission management (schema v5) ---

bool DatabaseManager::addUser(const QString& username, const QString& password, PermLevel level) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen() || username.isEmpty()) return false;

    const QString salt = makePasswordSalt();
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO users (username, password_hash, salt, level, enabled) VALUES (?, ?, ?, ?, 1)");
    query.addBindValue(username);
    query.addBindValue(hashPassword(password, salt));
    query.addBindValue(salt);
    query.addBindValue(static_cast<int>(level));
    if (!query.exec()) {
        emit databaseError(tr("添加用户失败: %1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

bool DatabaseManager::removeUser(const QString& username) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    if (wouldOrphanAdmins(m_db, username)) {
        emit databaseError(tr("无法删除最后一个管理员账户: %1").arg(username));
        return false;
    }
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM users WHERE username = ?");
    query.addBindValue(username);
    return query.exec() && query.numRowsAffected() > 0;
}

QList<UserRecord> DatabaseManager::listUsers() const {
    QMutexLocker locker(const_cast<QRecursiveMutex*>(&m_mutex));
    QList<UserRecord> result;
    if (!m_db.isOpen()) return result;

    QSqlQuery query(m_db);
    if (query.exec("SELECT username, level, enabled, last_login FROM users ORDER BY username")) {
        while (query.next()) {
            UserRecord rec;
            rec.username = query.value(0).toString();
            rec.level = static_cast<uint8_t>(query.value(1).toInt());
            rec.enabled = query.value(2).toBool();
            rec.lastLogin = query.value(3).toLongLong();
            result.append(rec);
        }
    }
    return result;
}

bool DatabaseManager::getUser(const QString& username, UserRecord& out) const {
    QMutexLocker locker(const_cast<QRecursiveMutex*>(&m_mutex));
    if (!m_db.isOpen()) return false;

    QSqlQuery query(m_db);
    query.prepare("SELECT username, level, enabled, last_login FROM users WHERE username = ?");
    query.addBindValue(username);
    if (!query.exec() || !query.next()) return false;

    out.username = query.value(0).toString();
    out.level = static_cast<uint8_t>(query.value(1).toInt());
    out.enabled = query.value(2).toBool();
    out.lastLogin = query.value(3).toLongLong();
    return true;
}

bool DatabaseManager::setUserPassword(const QString& username, const QString& password) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    // Rotate the salt alongside the password so a reused password never yields
    // the same stored hash twice.
    const QString salt = makePasswordSalt();
    QSqlQuery query(m_db);
    query.prepare("UPDATE users SET password_hash = ?, salt = ? WHERE username = ?");
    query.addBindValue(hashPassword(password, salt));
    query.addBindValue(salt);
    query.addBindValue(username);
    return query.exec() && query.numRowsAffected() > 0;
}

bool DatabaseManager::setUserLevel(const QString& username, PermLevel level) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    if (level != PermLevel::Admin && wouldOrphanAdmins(m_db, username)) {
        emit databaseError(tr("无法降级最后一个管理员账户: %1").arg(username));
        return false;
    }
    QSqlQuery query(m_db);
    query.prepare("UPDATE users SET level = ? WHERE username = ?");
    query.addBindValue(static_cast<int>(level));
    query.addBindValue(username);
    return query.exec() && query.numRowsAffected() > 0;
}

bool DatabaseManager::setUserEnabled(const QString& username, bool enabled) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    if (!enabled && wouldOrphanAdmins(m_db, username)) {
        emit databaseError(tr("无法禁用最后一个管理员账户: %1").arg(username));
        return false;
    }
    QSqlQuery query(m_db);
    query.prepare("UPDATE users SET enabled = ? WHERE username = ?");
    query.addBindValue(enabled ? 1 : 0);
    query.addBindValue(username);
    return query.exec() && query.numRowsAffected() > 0;
}

PermLevel DatabaseManager::verifyUser(const QString& username, const QString& password) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return PermLevel::None;

    QSqlQuery query(m_db);
    query.prepare("SELECT password_hash, salt, level, enabled FROM users WHERE username = ?");
    query.addBindValue(username);
    if (!query.exec() || !query.next()) return PermLevel::None;
    if (!query.value(3).toBool()) return PermLevel::None;  // disabled account

    const QByteArray expected = query.value(0).toString().toUtf8();
    const QByteArray actual = hashPassword(password, query.value(1).toString()).toUtf8();
    if (expected.size() != actual.size()) return PermLevel::None;
    // Compare in constant time: this runs on the network-facing auth path.
    int diff = 0;
    for (int i = 0; i < expected.size(); ++i) diff |= (expected[i] ^ actual[i]);
    if (diff != 0) return PermLevel::None;

    // Reject levels a hand-edited database could smuggle in.
    const int level = query.value(2).toInt();
    if (level < 0 || level > static_cast<int>(PermLevel::Admin)) return PermLevel::None;

    QSqlQuery touch(m_db);
    touch.prepare("UPDATE users SET last_login = ? WHERE username = ?");
    touch.addBindValue(QDateTime::currentSecsSinceEpoch());
    touch.addBindValue(username);
    touch.exec();

    return static_cast<PermLevel>(level);
}

quint32 DatabaseManager::getCapabilityToggles() const {
    const QVariant raw = getSetting(QString::fromLatin1(kCapabilityTogglesKey));
    if (!raw.isValid()) return kAllCapabilities;
    bool ok = false;
    const quint32 mask = raw.toString().toUInt(&ok);
    return ok ? (mask & kAllCapabilities) : kAllCapabilities;
}

bool DatabaseManager::setCapabilityToggle(Capability cap, bool enabled) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    const quint32 bit = static_cast<quint32>(cap);
    const quint32 mask = enabled ? (getCapabilityToggles() | bit)
                                 : (getCapabilityToggles() & ~bit);
    return setSetting(QString::fromLatin1(kCapabilityTogglesKey), QString::number(mask));
}

bool DatabaseManager::getDevicePermission(const QString& deviceId, DevicePermission& out) const {
    QMutexLocker locker(const_cast<QRecursiveMutex*>(&m_mutex));
    if (!m_db.isOpen()) return false;

    QSqlQuery query(m_db);
    query.prepare("SELECT device_id, level, cap_mask, note FROM device_permissions WHERE device_id = ?");
    query.addBindValue(deviceId);
    if (!query.exec() || !query.next()) return false;

    out.deviceId = query.value(0).toString();
    out.level = query.value(1).toInt();
    out.capMask = query.value(2).toInt();
    out.note = query.value(3).toString();
    return true;
}

bool DatabaseManager::setDevicePermission(const DevicePermission& perm) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen() || perm.deviceId.isEmpty()) return false;

    QSqlQuery query(m_db);
    query.prepare("INSERT OR REPLACE INTO device_permissions (device_id, level, cap_mask, note, updated_at) "
                  "VALUES (?, ?, ?, ?, strftime('%s','now'))");
    query.addBindValue(perm.deviceId);
    query.addBindValue(perm.level);
    query.addBindValue(perm.capMask);
    query.addBindValue(perm.note);
    return query.exec();
}

bool DatabaseManager::clearDevicePermission(const QString& deviceId) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    QSqlQuery query(m_db);
    query.prepare("DELETE FROM device_permissions WHERE device_id = ?");
    query.addBindValue(deviceId);
    return query.exec() && query.numRowsAffected() > 0;
}

QList<DevicePermission> DatabaseManager::listDevicePermissions() const {
    QMutexLocker locker(const_cast<QRecursiveMutex*>(&m_mutex));
    QList<DevicePermission> result;
    if (!m_db.isOpen()) return result;

    QSqlQuery query(m_db);
    if (query.exec("SELECT device_id, level, cap_mask, note FROM device_permissions ORDER BY device_id")) {
        while (query.next()) {
            DevicePermission perm;
            perm.deviceId = query.value(0).toString();
            perm.level = query.value(1).toInt();
            perm.capMask = query.value(2).toInt();
            perm.note = query.value(3).toString();
            result.append(perm);
        }
    }
    return result;
}

// --- Recent Chats ---

QList<DatabaseManager::RecentChat> DatabaseManager::loadRecentChats(int limit) {
    QMutexLocker locker(&m_mutex);
    QList<RecentChat> chats;

    if (!m_db.isOpen()) return chats;

    QSqlQuery query(m_db);
    query.prepare("SELECT * FROM recent_chats ORDER BY timestamp DESC LIMIT ?");
    query.addBindValue(limit);

    if (!query.exec()) {
        emit databaseError(tr("加载最近会话失败: %1").arg(query.lastError().text()));
        return chats;
    }

    while (query.next()) {
        RecentChat chat;
        chat.targetId = query.value("target_id").toString();
        chat.targetName = query.value("target_name").toString();
        chat.lastMessage = query.value("last_message").toString();
        chat.timestamp = query.value("timestamp").toLongLong();
        chat.unreadCount = query.value("unread_count").toInt();
        chat.isGroup = query.value("is_group").toBool();
        chats.append(chat);
    }

    return chats;
}

bool DatabaseManager::updateRecentChat(const QString& targetId, const QString& targetName, const QString& lastMessage, qint64 timestamp, bool isGroup) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    QSqlQuery query(m_db);
    // Get current unread count
    int unreadCount = 0;
    query.prepare("SELECT unread_count FROM recent_chats WHERE target_id = ?");
    query.addBindValue(targetId);
    if (query.exec() && query.next()) {
        unreadCount = query.value(0).toInt();
    }

    query.prepare(R"(
        INSERT OR REPLACE INTO recent_chats (
            target_id, target_name, last_message, timestamp, unread_count, is_group, updated_at
        ) VALUES (?, ?, ?, ?, ?, ?, strftime('%s', 'now'))
    )");
    query.addBindValue(targetId);
    query.addBindValue(targetName);
    query.addBindValue(lastMessage);
    query.addBindValue(timestamp);
    query.addBindValue(unreadCount);
    query.addBindValue(isGroup ? 1 : 0);

    return query.exec();
}

// --- File Transfers ---

bool DatabaseManager::saveTransferRecord(const TransferRecord& record) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    QSqlQuery query(m_db);
    query.prepare(R"(
        INSERT OR REPLACE INTO file_transfers (
            file_id, file_name, file_size, md5, local_path, remote_path,
            transferred_size, status, start_time, end_time,
            is_sender, target_id, is_group, checksum, resume_offset, session_id
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )");
    query.addBindValue(record.fileId);
    query.addBindValue(record.fileName);
    query.addBindValue(record.fileSize);
    query.addBindValue(record.md5);
    query.addBindValue(record.localPath);
    query.addBindValue(record.remotePath);
    query.addBindValue(record.transferredSize);
    query.addBindValue(record.status);
    query.addBindValue(record.startTime);
    query.addBindValue(record.endTime);
    query.addBindValue(record.isSender ? 1 : 0);
    query.addBindValue(record.targetId);
    query.addBindValue(record.isGroup ? 1 : 0);
    query.addBindValue(record.checksum);
    query.addBindValue(record.resumeOffset);
    query.addBindValue(record.sessionId);

    return query.exec();
}

bool DatabaseManager::updateTransferProgress(const QString& fileId, qint64 transferred, int status) {
    return updateTransferProgress(fileId, transferred, status, 0);
}

bool DatabaseManager::updateTransferProgress(const QString& fileId, qint64 transferred, int status, qint64 resumeOffset) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    QSqlQuery query(m_db);
    if (status == 2) { // completed
        query.prepare("UPDATE file_transfers SET transferred_size = ?, status = ?, end_time = strftime('%s', 'now'), resume_offset = ? WHERE file_id = ?");
    } else {
        query.prepare("UPDATE file_transfers SET transferred_size = ?, status = ?, resume_offset = ? WHERE file_id = ?");
    }
    query.addBindValue(transferred);
    query.addBindValue(status);
    query.addBindValue(resumeOffset);
    query.addBindValue(fileId);
    return query.exec();
}

QList<DatabaseManager::TransferRecord> DatabaseManager::loadTransferRecords(int limit) {
    QMutexLocker locker(&m_mutex);
    QList<TransferRecord> records;

    if (!m_db.isOpen()) return records;

    QSqlQuery query(m_db);
    query.prepare("SELECT * FROM file_transfers ORDER BY start_time DESC LIMIT ?");
    query.addBindValue(limit);

    if (!query.exec()) {
        emit databaseError(tr("加载传输记录失败: %1").arg(query.lastError().text()));
        return records;
    }

    while (query.next()) {
        TransferRecord record;
        record.fileId = query.value("file_id").toString();
        record.fileName = query.value("file_name").toString();
        record.fileSize = query.value("file_size").toLongLong();
        record.md5 = query.value("md5").toString();
        record.localPath = query.value("local_path").toString();
        record.remotePath = query.value("remote_path").toString();
        record.transferredSize = query.value("transferred_size").toLongLong();
        record.status = query.value("status").toInt();
        record.startTime = query.value("start_time").toLongLong();
        record.endTime = query.value("end_time").toLongLong();
        record.isSender = query.value("is_sender").toBool();
        record.targetId = query.value("target_id").toString();
        record.isGroup = query.value("is_group").toBool();
        record.checksum = query.value("checksum").toByteArray();
        record.resumeOffset = query.value("resume_offset").toLongLong();
        record.sessionId = query.value("session_id").toString();
        records.append(record);
    }

    return records;
}

QList<DatabaseManager::TransferRecord> DatabaseManager::loadPendingTransfers(const QString& targetId, bool isGroup) {
    QMutexLocker locker(&m_mutex);
    QList<TransferRecord> records;

    if (!m_db.isOpen()) return records;

    QSqlQuery query(m_db);
    query.prepare("SELECT * FROM file_transfers WHERE target_id = ? AND is_group = ? AND status IN (0, 1, 4) ORDER BY start_time DESC");
    query.addBindValue(targetId);
    query.addBindValue(isGroup ? 1 : 0);

    if (!query.exec()) {
        emit databaseError(tr("加载待续传记录失败: %1").arg(query.lastError().text()));
        return records;
    }

    while (query.next()) {
        TransferRecord record;
        record.fileId = query.value("file_id").toString();
        record.fileName = query.value("file_name").toString();
        record.fileSize = query.value("file_size").toLongLong();
        record.md5 = query.value("md5").toString();
        record.localPath = query.value("local_path").toString();
        record.remotePath = query.value("remote_path").toString();
        record.transferredSize = query.value("transferred_size").toLongLong();
        record.status = query.value("status").toInt();
        record.startTime = query.value("start_time").toLongLong();
        record.endTime = query.value("end_time").toLongLong();
        record.isSender = query.value("is_sender").toBool();
        record.targetId = query.value("target_id").toString();
        record.isGroup = query.value("is_group").toBool();
        record.checksum = query.value("checksum").toByteArray();
        record.resumeOffset = query.value("resume_offset").toLongLong();
        record.sessionId = query.value("session_id").toString();
        records.append(record);
    }

    return records;
}

// --- Offline Messages ---

bool DatabaseManager::saveOfflineMessage(const OfflineMessage& msg) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    QSqlQuery query(m_db);
    query.prepare(R"(
        INSERT OR REPLACE INTO offline_messages (
            id, target_device_id, payload, retry_count, max_retries, created_at, expires_at
        ) VALUES (?, ?, ?, ?, ?, ?, ?)
    )");
    query.addBindValue(msg.id);
    query.addBindValue(msg.targetDeviceId);
    query.addBindValue(msg.payload);
    query.addBindValue(msg.retryCount);
    query.addBindValue(10); // max_retries
    query.addBindValue(msg.createdAt);
    query.addBindValue(msg.expiresAt);

    return query.exec();
}

QList<DatabaseManager::OfflineMessage> DatabaseManager::loadPendingOfflineMessages(const QString& deviceId, int limit) {
    QMutexLocker locker(&m_mutex);
    QList<OfflineMessage> messages;

    if (!m_db.isOpen()) return messages;

    QSqlQuery query(m_db);
    query.prepare(R"(
        SELECT * FROM offline_messages 
        WHERE target_device_id = ? AND delivered_at = 0 AND retry_count < max_retries AND expires_at > ?
        ORDER BY created_at ASC LIMIT ?
    )");
    query.addBindValue(deviceId);
    query.addBindValue(QDateTime::currentSecsSinceEpoch());
    query.addBindValue(limit);

    if (!query.exec()) {
        emit databaseError(tr("加载离线消息失败: %1").arg(query.lastError().text()));
        return messages;
    }

    while (query.next()) {
        OfflineMessage msg;
        msg.id = query.value("id").toString();
        msg.targetDeviceId = query.value("target_device_id").toString();
        msg.payload = query.value("payload").toByteArray();
        msg.retryCount = query.value("retry_count").toInt();
        msg.createdAt = query.value("created_at").toLongLong();
        msg.expiresAt = query.value("expires_at").toLongLong();
        messages.append(msg);
    }

    return messages;
}

bool DatabaseManager::markOfflineMessageDelivered(const QString& id) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    QSqlQuery query(m_db);
    query.prepare("UPDATE offline_messages SET delivered_at = strftime('%s', 'now') WHERE id = ?");
    query.addBindValue(id);
    return query.exec();
}

bool DatabaseManager::incrementOfflineRetry(const QString& id) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    QSqlQuery query(m_db);
    query.prepare("UPDATE offline_messages SET retry_count = retry_count + 1 WHERE id = ?");
    query.addBindValue(id);
    return query.exec();
}

void DatabaseManager::cleanupExpiredOfflineMessages() {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return;

    QSqlQuery query(m_db);
    query.exec("DELETE FROM offline_messages WHERE expires_at < strftime('%s', 'now') OR delivered_at > 0");
}

// --- E2EE Sessions ---

bool DatabaseManager::saveE2EESession(const E2EESessionRow& session) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    QSqlQuery query(m_db);
    query.prepare(R"(
        INSERT OR REPLACE INTO e2ee_sessions (
            device_id, public_key, shared_secret, session_id,
            established_at, last_activity, encryption_algorithm, nonce, is_active, updated_at
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, strftime('%s', 'now'))
    )");
    query.addBindValue(session.deviceId);
    query.addBindValue(session.publicKey);
    query.addBindValue(session.sharedSecret);
    query.addBindValue(session.sessionId);
    query.addBindValue(session.establishedAt);
    query.addBindValue(session.lastActivity);
    query.addBindValue(session.encryptionAlgorithm);
    query.addBindValue(session.nonce);
    query.addBindValue(session.isActive ? 1 : 0);

    return query.exec();
}

DatabaseManager::E2EESessionRow DatabaseManager::loadE2EESession(const QString& deviceId) {
    QMutexLocker locker(&m_mutex);
    E2EESessionRow session;

    if (!m_db.isOpen()) return session;

    QSqlQuery query(m_db);
    query.prepare("SELECT * FROM e2ee_sessions WHERE device_id = ?");
    query.addBindValue(deviceId);

    if (query.exec() && query.next()) {
        session.deviceId = query.value("device_id").toString();
        session.publicKey = query.value("public_key").toByteArray();
        session.sharedSecret = query.value("shared_secret").toByteArray();
        session.sessionId = query.value("session_id").toString();
        session.establishedAt = query.value("established_at").toLongLong();
        session.lastActivity = query.value("last_activity").toLongLong();
        session.encryptionAlgorithm = query.value("encryption_algorithm").toString();
        session.nonce = query.value("nonce").toByteArray();
        session.isActive = query.value("is_active").toInt() != 0;
    }

    return session;
}

QList<DatabaseManager::E2EESessionRow> DatabaseManager::loadAllE2EESessions() {
    QMutexLocker locker(&m_mutex);
    QList<E2EESessionRow> sessions;

    if (!m_db.isOpen()) return sessions;

    QSqlQuery query(m_db);
    if (!query.exec("SELECT * FROM e2ee_sessions ORDER BY established_at DESC")) {
        emit databaseError(tr("加载E2EE会话列表失败: %1").arg(query.lastError().text()));
        return sessions;
    }

    while (query.next()) {
        E2EESessionRow session;
        session.deviceId = query.value("device_id").toString();
        session.publicKey = query.value("public_key").toByteArray();
        session.sharedSecret = query.value("shared_secret").toByteArray();
        session.sessionId = query.value("session_id").toString();
        session.establishedAt = query.value("established_at").toLongLong();
        session.lastActivity = query.value("last_activity").toLongLong();
        session.encryptionAlgorithm = query.value("encryption_algorithm").toString();
        session.nonce = query.value("nonce").toByteArray();
        session.isActive = query.value("is_active").toInt() != 0;
        sessions.append(session);
    }

    return sessions;
}

bool DatabaseManager::updateE2EESession(const E2EESessionRow& session) {
    return saveE2EESession(session);
}

bool DatabaseManager::removeE2EESession(const QString& deviceId) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    QSqlQuery query(m_db);
    query.prepare("DELETE FROM e2ee_sessions WHERE device_id = ?");
    query.addBindValue(deviceId);
    return query.exec();
}

bool DatabaseManager::deactivateE2EESession(const QString& deviceId) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    QSqlQuery query(m_db);
    query.prepare("UPDATE e2ee_sessions SET is_active = 0, updated_at = strftime('%s', 'now') WHERE device_id = ?");
    query.addBindValue(deviceId);
    return query.exec();
}

// --- Voice Messages ---

bool DatabaseManager::saveVoiceMessage(const VoiceMessageRow& msg) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    QSqlQuery query(m_db);
    query.prepare(R"(
        INSERT OR REPLACE INTO voice_messages (
            message_id, sender_id, sender_name, voice_data, voice_file_name,
            duration, timestamp, is_read, target_id, is_group
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )");
    query.addBindValue(msg.messageId);
    query.addBindValue(msg.senderId);
    query.addBindValue(msg.senderName);
    query.addBindValue(msg.voiceData);
    query.addBindValue(msg.voiceFileName);
    query.addBindValue(msg.duration);
    query.addBindValue(msg.timestamp);
    query.addBindValue(msg.isRead ? 1 : 0);
    query.addBindValue(msg.targetId);
    query.addBindValue(msg.isGroup ? 1 : 0);

    if (!query.exec()) {
        emit databaseError(tr("保存语音消息失败: %1").arg(query.lastError().text()));
        return false;
    }

    updateRecentChat(msg.targetId, msg.senderName, "[语音消息]", msg.timestamp, msg.isGroup);
    return true;
}

QList<DatabaseManager::VoiceMessageRow> DatabaseManager::loadVoiceMessages(const QString& targetId, int limit, bool isGroup) {
    QMutexLocker locker(&m_mutex);
    QList<VoiceMessageRow> messages;

    if (!m_db.isOpen()) return messages;

    QSqlQuery query(m_db);
    QString sql = "SELECT * FROM voice_messages WHERE target_id = ? AND is_group = ?";
    if (limit > 0) {
        sql += " ORDER BY timestamp DESC LIMIT ?";
    }

    query.prepare(sql);
    query.addBindValue(targetId);
    query.addBindValue(isGroup ? 1 : 0);
    if (limit > 0) {
        query.addBindValue(limit);
    }

    if (!query.exec()) {
        emit databaseError(tr("加载语音消息失败: %1").arg(query.lastError().text()));
        return messages;
    }

    while (query.next()) {
        VoiceMessageRow msg;
        msg.messageId = query.value("message_id").toString();
        msg.senderId = query.value("sender_id").toString();
        msg.senderName = query.value("sender_name").toString();
        msg.voiceData = query.value("voice_data").toByteArray();
        msg.voiceFileName = query.value("voice_file_name").toString();
        msg.duration = query.value("duration").toInt();
        msg.timestamp = query.value("timestamp").toLongLong();
        msg.isRead = query.value("is_read").toInt() != 0;
        msg.targetId = query.value("target_id").toString();
        msg.isGroup = query.value("is_group").toInt() != 0;
        messages.prepend(msg); // Reverse to chronological order
    }

    return messages;
}

bool DatabaseManager::markVoiceMessageRead(const QString& messageId) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    QSqlQuery query(m_db);
    query.prepare("UPDATE voice_messages SET is_read = 1 WHERE message_id = ?");
    query.addBindValue(messageId);
    return query.exec();
}

// --- Video Messages ---

bool DatabaseManager::saveVideoMessage(const VideoMessageRow& msg) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    QSqlQuery query(m_db);
    query.prepare(R"(
        INSERT OR REPLACE INTO video_messages (
            message_id, sender_id, sender_name, video_data, video_file_name,
            duration, width, height, timestamp, is_read, target_id, is_group
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )");
    query.addBindValue(msg.messageId);
    query.addBindValue(msg.senderId);
    query.addBindValue(msg.senderName);
    query.addBindValue(msg.videoData);
    query.addBindValue(msg.videoFileName);
    query.addBindValue(msg.duration);
    query.addBindValue(msg.width);
    query.addBindValue(msg.height);
    query.addBindValue(msg.timestamp);
    query.addBindValue(msg.isRead ? 1 : 0);
    query.addBindValue(msg.targetId);
    query.addBindValue(msg.isGroup ? 1 : 0);

    if (!query.exec()) {
        emit databaseError(tr("保存视频消息失败: %1").arg(query.lastError().text()));
        return false;
    }

    updateRecentChat(msg.targetId, msg.senderName, "[视频消息]", msg.timestamp, msg.isGroup);
    return true;
}

QList<DatabaseManager::VideoMessageRow> DatabaseManager::loadVideoMessages(const QString& targetId, int limit, bool isGroup) {
    QMutexLocker locker(&m_mutex);
    QList<VideoMessageRow> messages;

    if (!m_db.isOpen()) return messages;

    QSqlQuery query(m_db);
    QString sql = "SELECT * FROM video_messages WHERE target_id = ? AND is_group = ?";
    if (limit > 0) {
        sql += " ORDER BY timestamp DESC LIMIT ?";
    }

    query.prepare(sql);
    query.addBindValue(targetId);
    query.addBindValue(isGroup ? 1 : 0);
    if (limit > 0) {
        query.addBindValue(limit);
    }

    if (!query.exec()) {
        emit databaseError(tr("加载视频消息失败: %1").arg(query.lastError().text()));
        return messages;
    }

    while (query.next()) {
        VideoMessageRow msg;
        msg.messageId = query.value("message_id").toString();
        msg.senderId = query.value("sender_id").toString();
        msg.senderName = query.value("sender_name").toString();
        msg.videoData = query.value("video_data").toByteArray();
        msg.videoFileName = query.value("video_file_name").toString();
        msg.duration = query.value("duration").toInt();
        msg.width = query.value("width").toDouble();
        msg.height = query.value("height").toDouble();
        msg.timestamp = query.value("timestamp").toLongLong();
        msg.isRead = query.value("is_read").toInt() != 0;
        msg.targetId = query.value("target_id").toString();
        msg.isGroup = query.value("is_group").toInt() != 0;
        messages.prepend(msg); // Reverse to chronological order
    }

    return messages;
}

bool DatabaseManager::markVideoMessageRead(const QString& messageId) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    QSqlQuery query(m_db);
    query.prepare("UPDATE video_messages SET is_read = 1 WHERE message_id = ?");
    query.addBindValue(messageId);
    return query.exec();
}

// --- Location Messages ---

bool DatabaseManager::saveLocationMessage(const LocationMessageRow& msg) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    QSqlQuery query(m_db);
    query.prepare(R"(
        INSERT OR REPLACE INTO location_messages (
            message_id, sender_id, sender_name, latitude, longitude,
            location_name, timestamp, is_read, target_id, is_group
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )");
    query.addBindValue(msg.messageId);
    query.addBindValue(msg.senderId);
    query.addBindValue(msg.senderName);
    query.addBindValue(msg.latitude);
    query.addBindValue(msg.longitude);
    query.addBindValue(msg.locationName);
    query.addBindValue(msg.timestamp);
    query.addBindValue(msg.isRead ? 1 : 0);
    query.addBindValue(msg.targetId);
    query.addBindValue(msg.isGroup ? 1 : 0);

    if (!query.exec()) {
        emit databaseError(tr("保存位置消息失败: %1").arg(query.lastError().text()));
        return false;
    }

    updateRecentChat(msg.targetId, msg.senderName, "[位置消息]", msg.timestamp, msg.isGroup);
    return true;
}

QList<DatabaseManager::LocationMessageRow> DatabaseManager::loadLocationMessages(const QString& targetId, int limit, bool isGroup) {
    QMutexLocker locker(&m_mutex);
    QList<LocationMessageRow> messages;

    if (!m_db.isOpen()) return messages;

    QSqlQuery query(m_db);
    QString sql = "SELECT * FROM location_messages WHERE target_id = ? AND is_group = ?";
    if (limit > 0) {
        sql += " ORDER BY timestamp DESC LIMIT ?";
    }

    query.prepare(sql);
    query.addBindValue(targetId);
    query.addBindValue(isGroup ? 1 : 0);
    if (limit > 0) {
        query.addBindValue(limit);
    }

    if (!query.exec()) {
        emit databaseError(tr("加载位置消息失败: %1").arg(query.lastError().text()));
        return messages;
    }

    while (query.next()) {
        LocationMessageRow msg;
        msg.messageId = query.value("message_id").toString();
        msg.senderId = query.value("sender_id").toString();
        msg.senderName = query.value("sender_name").toString();
        msg.latitude = query.value("latitude").toDouble();
        msg.longitude = query.value("longitude").toDouble();
        msg.locationName = query.value("location_name").toString();
        msg.timestamp = query.value("timestamp").toLongLong();
        msg.isRead = query.value("is_read").toInt() != 0;
        msg.targetId = query.value("target_id").toString();
        msg.isGroup = query.value("is_group").toInt() != 0;
        messages.prepend(msg);
    }

    return messages;
}

bool DatabaseManager::markLocationMessageRead(const QString& messageId) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    QSqlQuery query(m_db);
    query.prepare("UPDATE location_messages SET is_read = 1 WHERE message_id = ?");
    query.addBindValue(messageId);
    return query.exec();
}

// --- Card Messages ---

bool DatabaseManager::saveCardMessage(const CardMessageRow& msg) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    QSqlQuery query(m_db);
    query.prepare(R"(
        INSERT OR REPLACE INTO card_messages (
            message_id, sender_id, sender_name, vcard_data,
            timestamp, is_read, target_id, is_group
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?)
    )");
    query.addBindValue(msg.messageId);
    query.addBindValue(msg.senderId);
    query.addBindValue(msg.senderName);
    query.addBindValue(msg.vCardData);
    query.addBindValue(msg.timestamp);
    query.addBindValue(msg.isRead ? 1 : 0);
    query.addBindValue(msg.targetId);
    query.addBindValue(msg.isGroup ? 1 : 0);

    if (!query.exec()) {
        emit databaseError(tr("保存名片消息失败: %1").arg(query.lastError().text()));
        return false;
    }

    updateRecentChat(msg.targetId, msg.senderName, "[名片消息]", msg.timestamp, msg.isGroup);
    return true;
}

QList<DatabaseManager::CardMessageRow> DatabaseManager::loadCardMessages(const QString& targetId, int limit, bool isGroup) {
    QMutexLocker locker(&m_mutex);
    QList<CardMessageRow> messages;

    if (!m_db.isOpen()) return messages;

    QSqlQuery query(m_db);
    QString sql = "SELECT * FROM card_messages WHERE target_id = ? AND is_group = ?";
    if (limit > 0) {
        sql += " ORDER BY timestamp DESC LIMIT ?";
    }

    query.prepare(sql);
    query.addBindValue(targetId);
    query.addBindValue(isGroup ? 1 : 0);
    if (limit > 0) {
        query.addBindValue(limit);
    }

    if (!query.exec()) {
        emit databaseError(tr("加载名片消息失败: %1").arg(query.lastError().text()));
        return messages;
    }

    while (query.next()) {
        CardMessageRow msg;
        msg.messageId = query.value("message_id").toString();
        msg.senderId = query.value("sender_id").toString();
        msg.senderName = query.value("sender_name").toString();
        msg.vCardData = query.value("vcard_data").toString();
        msg.timestamp = query.value("timestamp").toLongLong();
        msg.isRead = query.value("is_read").toInt() != 0;
        msg.targetId = query.value("target_id").toString();
        msg.isGroup = query.value("is_group").toInt() != 0;
        messages.prepend(msg);
    }

    return messages;
}

bool DatabaseManager::markCardMessageRead(const QString& messageId) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    QSqlQuery query(m_db);
    query.prepare("UPDATE card_messages SET is_read = 1 WHERE message_id = ?");
    query.addBindValue(messageId);
    return query.exec();
}

// --- Merge Forward Messages ---

bool DatabaseManager::saveMergeForwardMessage(const MergeForwardMessageRow& msg) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    QSqlQuery query(m_db);
    query.prepare(R"(
        INSERT OR REPLACE INTO merge_forward_messages (
            message_id, sender_id, sender_name, merged_data,
            timestamp, is_read, target_id, is_group
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?)
    )");
    query.addBindValue(msg.messageId);
    query.addBindValue(msg.senderId);
    query.addBindValue(msg.senderName);
    query.addBindValue(msg.mergedData);
    query.addBindValue(msg.timestamp);
    query.addBindValue(msg.isRead ? 1 : 0);
    query.addBindValue(msg.targetId);
    query.addBindValue(msg.isGroup ? 1 : 0);

    if (!query.exec()) {
        emit databaseError(tr("保存合并转发消息失败: %1").arg(query.lastError().text()));
        return false;
    }

    updateRecentChat(msg.targetId, msg.senderName, "[合并转发]", msg.timestamp, msg.isGroup);
    return true;
}

QList<DatabaseManager::MergeForwardMessageRow> DatabaseManager::loadMergeForwardMessages(const QString& targetId, int limit, bool isGroup) {
    QMutexLocker locker(&m_mutex);
    QList<MergeForwardMessageRow> messages;

    if (!m_db.isOpen()) return messages;

    QSqlQuery query(m_db);
    QString sql = "SELECT * FROM merge_forward_messages WHERE target_id = ? AND is_group = ?";
    if (limit > 0) {
        sql += " ORDER BY timestamp DESC LIMIT ?";
    }

    query.prepare(sql);
    query.addBindValue(targetId);
    query.addBindValue(isGroup ? 1 : 0);
    if (limit > 0) {
        query.addBindValue(limit);
    }

    if (!query.exec()) {
        emit databaseError(tr("加载合并转发消息失败: %1").arg(query.lastError().text()));
        return messages;
    }

    while (query.next()) {
        MergeForwardMessageRow msg;
        msg.messageId = query.value("message_id").toString();
        msg.senderId = query.value("sender_id").toString();
        msg.senderName = query.value("sender_name").toString();
        msg.mergedData = query.value("merged_data").toByteArray();
        msg.timestamp = query.value("timestamp").toLongLong();
        msg.isRead = query.value("is_read").toInt() != 0;
        msg.targetId = query.value("target_id").toString();
        msg.isGroup = query.value("is_group").toInt() != 0;
        messages.prepend(msg);
    }

    return messages;
}

bool DatabaseManager::markMergeForwardMessageRead(const QString& messageId) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    QSqlQuery query(m_db);
    query.prepare("UPDATE merge_forward_messages SET is_read = 1 WHERE message_id = ?");
    query.addBindValue(messageId);
    return query.exec();
}

// --- Group Announcements ---

bool DatabaseManager::saveGroupAnnouncement(const GroupAnnouncementRow& announcement) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    QSqlQuery query(m_db);
    query.prepare(R"(
        INSERT OR REPLACE INTO group_announcements (
            id, group_id, group_name, announcement, announcer_id, announcer_name, timestamp
        ) VALUES (?, ?, ?, ?, ?, ?, ?)
    )");
    query.addBindValue(announcement.id);
    query.addBindValue(announcement.groupId);
    query.addBindValue(announcement.groupName);
    query.addBindValue(announcement.announcement);
    query.addBindValue(announcement.announcerId);
    query.addBindValue(announcement.announcerName);
    query.addBindValue(announcement.timestamp);

    if (!query.exec()) {
        emit databaseError(tr("保存群公告失败: %1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

QList<DatabaseManager::GroupAnnouncementRow> DatabaseManager::loadGroupAnnouncements(const QString& groupId, int limit) {
    QMutexLocker locker(&m_mutex);
    QList<GroupAnnouncementRow> announcements;

    if (!m_db.isOpen()) return announcements;

    QSqlQuery query(m_db);
    QString sql = "SELECT * FROM group_announcements WHERE group_id = ?";
    if (limit > 0) {
        sql += " ORDER BY timestamp DESC LIMIT ?";
    }

    query.prepare(sql);
    query.addBindValue(groupId);
    if (limit > 0) {
        query.addBindValue(limit);
    }

    if (!query.exec()) {
        emit databaseError(tr("加载群公告失败: %1").arg(query.lastError().text()));
        return announcements;
    }

    while (query.next()) {
        GroupAnnouncementRow announcement;
        announcement.id = query.value("id").toString();
        announcement.groupId = query.value("group_id").toString();
        announcement.groupName = query.value("group_name").toString();
        announcement.announcement = query.value("announcement").toString();
        announcement.announcerId = query.value("announcer_id").toString();
        announcement.announcerName = query.value("announcer_name").toString();
        announcement.timestamp = query.value("timestamp").toLongLong();
        announcements.prepend(announcement);
    }

    return announcements;
}

// --- Group Mentions ---

bool DatabaseManager::saveGroupMention(const GroupMentionRow& mention) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    QSqlQuery query(m_db);
    query.prepare(R"(
        INSERT OR REPLACE INTO group_mentions (
            id, group_id, group_name, message, mentioned_member_ids, mentioned_member_names,
            sender_id, sender_name, timestamp
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
    )");
    QJsonArray idsArray, namesArray;
    for (const QString& id : mention.mentionedMemberIds) idsArray.append(id);
    for (const QString& name : mention.mentionedMemberNames) namesArray.append(name);

    query.addBindValue(mention.id);
    query.addBindValue(mention.groupId);
    query.addBindValue(mention.groupName);
    query.addBindValue(mention.message);
    query.addBindValue(QJsonDocument(idsArray).toJson(QJsonDocument::Compact));
    query.addBindValue(QJsonDocument(namesArray).toJson(QJsonDocument::Compact));
    query.addBindValue(mention.senderId);
    query.addBindValue(mention.senderName);
    query.addBindValue(mention.timestamp);

    if (!query.exec()) {
        emit databaseError(tr("保存群@提及失败: %1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

QList<DatabaseManager::GroupMentionRow> DatabaseManager::loadGroupMentions(const QString& groupId, int limit) {
    QMutexLocker locker(&m_mutex);
    QList<GroupMentionRow> mentions;

    if (!m_db.isOpen()) return mentions;

    QSqlQuery query(m_db);
    QString sql = "SELECT * FROM group_mentions WHERE group_id = ?";
    if (limit > 0) {
        sql += " ORDER BY timestamp DESC LIMIT ?";
    }

    query.prepare(sql);
    query.addBindValue(groupId);
    if (limit > 0) {
        query.addBindValue(limit);
    }

    if (!query.exec()) {
        emit databaseError(tr("加载群@提及失败: %1").arg(query.lastError().text()));
        return mentions;
    }

    while (query.next()) {
        GroupMentionRow mention;
        mention.id = query.value("id").toString();
        mention.groupId = query.value("group_id").toString();
        mention.groupName = query.value("group_name").toString();
        mention.message = query.value("message").toString();
        
        QJsonArray idsArray = QJsonDocument::fromJson(query.value("mentioned_member_ids").toByteArray()).array();
        QJsonArray namesArray = QJsonDocument::fromJson(query.value("mentioned_member_names").toByteArray()).array();
        for (const auto& v : idsArray) mention.mentionedMemberIds.append(v.toString());
        for (const auto& v : namesArray) mention.mentionedMemberNames.append(v.toString());
        
        mention.senderId = query.value("sender_id").toString();
        mention.senderName = query.value("sender_name").toString();
        mention.timestamp = query.value("timestamp").toLongLong();
        mentions.prepend(mention);
    }

    return mentions;
}

// --- Group Votes ---

bool DatabaseManager::saveGroupVote(const GroupVoteRow& vote) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    QSqlQuery query(m_db);
    query.prepare(R"(
        INSERT OR REPLACE INTO group_votes (
            id, group_id, group_name, vote_title, options, duration_seconds,
            creator_id, creator_name, timestamp
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
    )");
    QJsonArray optionsArray;
    for (const QString& opt : vote.options) optionsArray.append(opt);

    query.addBindValue(vote.id);
    query.addBindValue(vote.groupId);
    query.addBindValue(vote.groupName);
    query.addBindValue(vote.voteTitle);
    query.addBindValue(QJsonDocument(optionsArray).toJson(QJsonDocument::Compact));
    query.addBindValue(vote.durationSeconds);
    query.addBindValue(vote.creatorId);
    query.addBindValue(vote.creatorName);
    query.addBindValue(vote.timestamp);

    if (!query.exec()) {
        emit databaseError(tr("保存群投票失败: %1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

QList<DatabaseManager::GroupVoteRow> DatabaseManager::loadGroupVotes(const QString& groupId, int limit) {
    QMutexLocker locker(&m_mutex);
    QList<GroupVoteRow> votes;

    if (!m_db.isOpen()) return votes;

    QSqlQuery query(m_db);
    QString sql = "SELECT * FROM group_votes WHERE group_id = ?";
    if (limit > 0) {
        sql += " ORDER BY timestamp DESC LIMIT ?";
    }

    query.prepare(sql);
    query.addBindValue(groupId);
    if (limit > 0) {
        query.addBindValue(limit);
    }

    if (!query.exec()) {
        emit databaseError(tr("加载群投票失败: %1").arg(query.lastError().text()));
        return votes;
    }

    while (query.next()) {
        GroupVoteRow vote;
        vote.id = query.value("id").toString();
        vote.groupId = query.value("group_id").toString();
        vote.groupName = query.value("group_name").toString();
        vote.voteTitle = query.value("vote_title").toString();
        
        QJsonArray optionsArray = QJsonDocument::fromJson(query.value("options").toByteArray()).array();
        for (const auto& v : optionsArray) vote.options.append(v.toString());
        
        vote.durationSeconds = query.value("duration_seconds").toInt();
        vote.creatorId = query.value("creator_id").toString();
        vote.creatorName = query.value("creator_name").toString();
        vote.timestamp = query.value("timestamp").toLongLong();
        votes.prepend(vote);
    }

    return votes;
}

// --- Group Files ---

bool DatabaseManager::saveGroupFile(const GroupFileRow& file) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    QSqlQuery query(m_db);
    query.prepare(R"(
        INSERT OR REPLACE INTO group_files (
            id, group_id, group_name, file_id, file_name, file_size, md5,
            uploader_id, uploader_name, timestamp
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )");
    query.addBindValue(file.id);
    query.addBindValue(file.groupId);
    query.addBindValue(file.groupName);
    query.addBindValue(file.fileId);
    query.addBindValue(file.fileName);
    query.addBindValue(file.fileSize);
    query.addBindValue(file.md5);
    query.addBindValue(file.uploaderId);
    query.addBindValue(file.uploaderName);
    query.addBindValue(file.timestamp);

    if (!query.exec()) {
        emit databaseError(tr("保存群文件失败: %1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

QList<DatabaseManager::GroupFileRow> DatabaseManager::loadGroupFiles(const QString& groupId, int limit) {
    QMutexLocker locker(&m_mutex);
    QList<GroupFileRow> files;

    if (!m_db.isOpen()) return files;

    QSqlQuery query(m_db);
    QString sql = "SELECT * FROM group_files WHERE group_id = ?";
    if (limit > 0) {
        sql += " ORDER BY timestamp DESC LIMIT ?";
    }

    query.prepare(sql);
    query.addBindValue(groupId);
    if (limit > 0) {
        query.addBindValue(limit);
    }

    if (!query.exec()) {
        emit databaseError(tr("加载群文件失败: %1").arg(query.lastError().text()));
        return files;
    }

    while (query.next()) {
        GroupFileRow file;
        file.id = query.value("id").toString();
        file.groupId = query.value("group_id").toString();
        file.groupName = query.value("group_name").toString();
        file.fileId = query.value("file_id").toString();
        file.fileName = query.value("file_name").toString();
        file.fileSize = query.value("file_size").toLongLong();
        file.md5 = query.value("md5").toString();
        file.uploaderId = query.value("uploader_id").toString();
        file.uploaderName = query.value("uploader_name").toString();
        file.timestamp = query.value("timestamp").toLongLong();
        files.prepend(file);
    }

    return files;
}

// --- Group Albums ---

bool DatabaseManager::saveGroupAlbum(const GroupAlbumRow& album) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    QSqlQuery query(m_db);
    query.prepare(R"(
        INSERT OR REPLACE INTO group_albums (
            id, group_id, group_name, album_id, album_name, file_ids, file_names,
            creator_id, creator_name, timestamp
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )");
    QJsonArray fileIdsArray, fileNamesArray;
    for (const QString& id : album.fileIds) fileIdsArray.append(id);
    for (const QString& name : album.fileNames) fileNamesArray.append(name);

    query.addBindValue(album.id);
    query.addBindValue(album.groupId);
    query.addBindValue(album.groupName);
    query.addBindValue(album.albumId);
    query.addBindValue(album.albumName);
    query.addBindValue(QJsonDocument(fileIdsArray).toJson(QJsonDocument::Compact));
    query.addBindValue(QJsonDocument(fileNamesArray).toJson(QJsonDocument::Compact));
    query.addBindValue(album.creatorId);
    query.addBindValue(album.creatorName);
    query.addBindValue(album.timestamp);

    if (!query.exec()) {
        emit databaseError(tr("保存群相册失败: %1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

QList<DatabaseManager::GroupAlbumRow> DatabaseManager::loadGroupAlbums(const QString& groupId, int limit) {
    QMutexLocker locker(&m_mutex);
    QList<GroupAlbumRow> albums;

    if (!m_db.isOpen()) return albums;

    QSqlQuery query(m_db);
    QString sql = "SELECT * FROM group_albums WHERE group_id = ?";
    if (limit > 0) {
        sql += " ORDER BY timestamp DESC LIMIT ?";
    }

    query.prepare(sql);
    query.addBindValue(groupId);
    if (limit > 0) {
        query.addBindValue(limit);
    }

    if (!query.exec()) {
        emit databaseError(tr("加载群相册失败: %1").arg(query.lastError().text()));
        return albums;
    }

    while (query.next()) {
        GroupAlbumRow album;
        album.id = query.value("id").toString();
        album.groupId = query.value("group_id").toString();
        album.groupName = query.value("group_name").toString();
        album.albumId = query.value("album_id").toString();
        album.albumName = query.value("album_name").toString();

        QJsonArray idsArray = QJsonDocument::fromJson(query.value("file_ids").toByteArray()).array();
        QJsonArray namesArray = QJsonDocument::fromJson(query.value("file_names").toByteArray()).array();
        for (const auto& v : idsArray) album.fileIds.append(v.toString());
        for (const auto& v : namesArray) album.fileNames.append(v.toString());

        album.creatorId = query.value("creator_id").toString();
        album.creatorName = query.value("creator_name").toString();
        album.timestamp = query.value("timestamp").toLongLong();
        albums.prepend(album);
    }

    return albums;
}

// --- Group Todos ---

bool DatabaseManager::saveGroupTodo(const GroupTodoRow& todo) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    QSqlQuery query(m_db);
    query.prepare(R"(
        INSERT OR REPLACE INTO group_todos (
            id, group_id, group_name, todo_id, title, description, status, priority,
            assignee_id, assignee_name, creator_id, creator_name, due_date, timestamp
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )");
    query.addBindValue(todo.id);
    query.addBindValue(todo.groupId);
    query.addBindValue(todo.groupName);
    query.addBindValue(todo.todoId);
    query.addBindValue(todo.title);
    query.addBindValue(todo.description);
    query.addBindValue(todo.status);
    query.addBindValue(todo.priority);
    query.addBindValue(todo.assigneeId);
    query.addBindValue(todo.assigneeName);
    query.addBindValue(todo.creatorId);
    query.addBindValue(todo.creatorName);
    query.addBindValue(todo.dueDate);
    query.addBindValue(todo.timestamp);

    if (!query.exec()) {
        emit databaseError(tr("保存群待办失败: %1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

QList<DatabaseManager::GroupTodoRow> DatabaseManager::loadGroupTodos(const QString& groupId, int limit) {
    QMutexLocker locker(&m_mutex);
    QList<GroupTodoRow> todos;

    if (!m_db.isOpen()) return todos;

    QSqlQuery query(m_db);
    QString sql = "SELECT * FROM group_todos WHERE group_id = ?";
    if (limit > 0) {
        sql += " ORDER BY timestamp DESC LIMIT ?";
    }

    query.prepare(sql);
    query.addBindValue(groupId);
    if (limit > 0) {
        query.addBindValue(limit);
    }

    if (!query.exec()) {
        emit databaseError(tr("加载群待办失败: %1").arg(query.lastError().text()));
        return todos;
    }

    while (query.next()) {
        GroupTodoRow todo;
        todo.id = query.value("id").toString();
        todo.groupId = query.value("group_id").toString();
        todo.groupName = query.value("group_name").toString();
        todo.todoId = query.value("todo_id").toString();
        todo.title = query.value("title").toString();
        todo.description = query.value("description").toString();
        todo.status = query.value("status").toInt();
        todo.priority = query.value("priority").toInt();
        todo.assigneeId = query.value("assignee_id").toString();
        todo.assigneeName = query.value("assignee_name").toString();
        todo.creatorId = query.value("creator_id").toString();
        todo.creatorName = query.value("creator_name").toString();
        todo.dueDate = query.value("due_date").toLongLong();
        todo.timestamp = query.value("timestamp").toLongLong();
        todos.prepend(todo);
    }

    return todos;
}

bool DatabaseManager::updateGroupTodoStatus(const QString& todoId, int status) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    QSqlQuery query(m_db);
    query.prepare("UPDATE group_todos SET status = ? WHERE id = ?");
    query.addBindValue(status);
    query.addBindValue(todoId);
    return query.exec();
}

// --- Multi-device sync (Phase E1) - LWW merge support ---

bool DatabaseManager::saveSyncRequest(const DatabaseManager::SyncRequestRow& request) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    QSqlQuery query(m_db);
    query.prepare(R"(
        INSERT OR REPLACE INTO sync_requests (
            request_id, account_hash, sync_key_hash, timestamp
        ) VALUES (?, ?, ?, ?)
    )");
    query.addBindValue(request.requestId);
    query.addBindValue(request.accountHash);
    query.addBindValue(request.syncKeyHash);
    query.addBindValue(request.timestamp);

    if (!query.exec()) {
        emit databaseError(tr("保存同步请求失败: %1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

DatabaseManager::SyncRequestRow DatabaseManager::loadSyncRequest(const QString& requestId) {
    QMutexLocker locker(&m_mutex);
    DatabaseManager::SyncRequestRow request;

    if (!m_db.isOpen()) return request;

    QSqlQuery query(m_db);
    query.prepare("SELECT * FROM sync_requests WHERE request_id = ?");
    query.addBindValue(requestId);

    if (!query.exec() || !query.next()) {
        return request;
    }

    request.requestId = query.value("request_id").toString();
    request.accountHash = query.value("account_hash").toString();
    request.syncKeyHash = query.value("sync_key_hash").toString();
    request.timestamp = query.value("timestamp").toLongLong();

    return request;
}

bool DatabaseManager::saveSyncSnapshot(const DatabaseManager::SyncSnapshotRow& snapshot) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    QSqlQuery query(m_db);
    query.prepare(R"(
        INSERT OR REPLACE INTO sync_snapshots (
            version, data, timestamp
        ) VALUES (?, ?, ?)
    )");

    QByteArray data;
    QDataStream stream(&data, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::BigEndian);

    stream << snapshot.version;
    stream << static_cast<uint32_t>(snapshot.devices.size());
    for (const auto& d : snapshot.devices) {
        stream << d.deviceId;
        stream << d.name;
        stream << d.ip;
        stream << d.port;
        stream << d.lastSeen;
        stream << d.updatedAt;
        stream << static_cast<uint8_t>(d.isFriend ? 1 : 0);
    }

    stream << static_cast<uint32_t>(snapshot.groups.size());
    for (const auto& g : snapshot.groups) {
        stream << g.groupId;
        stream << g.name;
        stream << static_cast<uint32_t>(g.memberIds.size());
        for (const QString& id : g.memberIds) stream << id;
        stream << static_cast<uint32_t>(g.memberNames.size());
        for (const QString& name : g.memberNames) stream << name;
        stream << g.createdAt;
        stream << g.updatedAt;
    }

    stream << static_cast<uint32_t>(snapshot.settings.size());
    for (const auto& s : snapshot.settings) {
        stream << s.key;
        stream << s.value;
        stream << s.updatedAt;
    }

    stream << static_cast<uint32_t>(snapshot.messages.size());
    for (const auto& m : snapshot.messages) {
        stream << m.messageId;
        stream << m.senderId;
        stream << m.senderName;
        stream << m.senderIp;
        stream << m.content;
        stream << m.timestamp;
        stream << static_cast<uint8_t>(m.isFile ? 1 : 0);
        stream << m.filePath;
        stream << m.fileSize;
        stream << static_cast<uint8_t>(m.isDirectory ? 1 : 0);
        stream << static_cast<uint8_t>(m.isImage ? 1 : 0);
        stream << m.imageFileName;
        stream << m.replyTo;
        stream << m.replyContent;
        stream << m.recallId;
        stream << static_cast<uint8_t>(m.isRecalled ? 1 : 0);
        stream << m.targetId;
        stream << static_cast<uint8_t>(m.isGroup ? 1 : 0);
        stream << static_cast<uint8_t>(m.isRead ? 1 : 0);
        stream << m.readBy;
    }

    query.addBindValue(snapshot.version);
    query.addBindValue(data);
    query.addBindValue(QDateTime::currentMSecsSinceEpoch());

    if (!query.exec()) {
        emit databaseError(tr("保存同步快照失败: %1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

DatabaseManager::SyncSnapshotRow DatabaseManager::loadSyncSnapshot(qint64 minVersion) {
    DatabaseManager::SyncSnapshotRow snapshot;

    if (!m_db.isOpen()) return snapshot;

    QSqlQuery query(m_db);
    QString sql = "SELECT * FROM sync_snapshots WHERE version >= ? ORDER BY version DESC LIMIT 1";
    query.prepare(sql);
    query.addBindValue(minVersion);

    if (!query.exec() || !query.next()) {
        return snapshot;
    }

    snapshot.version = query.value("version").toLongLong();
    QByteArray data = query.value("data").toByteArray();

    QDataStream stream(data);
    stream.setByteOrder(QDataStream::BigEndian);

    stream >> snapshot.version;

    uint32_t count;
    stream >> count;
    for (uint32_t i = 0; i < count; ++i) {
        DatabaseManager::SyncDeviceRow d;
        stream >> d.deviceId;
        stream >> d.name;
        stream >> d.ip;
        stream >> d.port;
        stream >> d.lastSeen;
        stream >> d.updatedAt;
        uint8_t isFriend;
        stream >> isFriend;
        d.isFriend = (isFriend != 0);
        snapshot.devices.append(d);
    }

    uint32_t gCount;
    stream >> gCount;
    for (uint32_t i = 0; i < gCount; ++i) {
        DatabaseManager::SyncGroupRow g;
        stream >> g.groupId;
        stream >> g.name;
        uint32_t idCount;
        stream >> idCount;
        for (uint32_t i = 0; i < idCount; ++i) {
            QString id;
            stream >> id;
            g.memberIds.append(id);
        }
        uint32_t nameCount;
        stream >> nameCount;
        for (uint32_t i = 0; i < nameCount; ++i) {
            QString name;
            stream >> name;
            g.memberNames.append(name);
        }
        stream >> g.createdAt;
        stream >> g.updatedAt;
        snapshot.groups.append(g);
    }

    uint32_t sCount;
    stream >> sCount;
    for (uint32_t i = 0; i < sCount; ++i) {
        DatabaseManager::SyncSettingRow s;
        stream >> s.key;
        stream >> s.value;
        stream >> s.updatedAt;
        snapshot.settings.append(s);
    }

    uint32_t mCount;
    stream >> mCount;
    for (uint32_t i = 0; i < mCount; ++i) {
        DatabaseManager::SyncMessageRow m;
        stream >> m.messageId;
        stream >> m.senderId;
        stream >> m.senderName;
        stream >> m.senderIp;
        stream >> m.content;
        stream >> m.timestamp;
        uint8_t isFile;
        stream >> isFile;
        m.isFile = (isFile != 0);
        stream >> m.filePath;
        stream >> m.fileSize;
        uint8_t isDir;
        stream >> isDir;
        m.isDirectory = (isDir != 0);
        uint8_t isImg;
        stream >> isImg;
        m.isImage = (isImg != 0);
        stream >> m.imageFileName;
        stream >> m.replyTo;
        stream >> m.replyContent;
        stream >> m.recallId;
        uint8_t isRecalled;
        stream >> isRecalled;
        m.isRecalled = (isRecalled != 0);
        stream >> m.targetId;
        uint8_t isGroup;
        stream >> isGroup;
        m.isGroup = (isGroup != 0);
        uint8_t isRead;
        stream >> isRead;
        m.isRead = (isRead != 0);
        stream >> m.readBy;
        snapshot.messages.append(m);
    }

    return snapshot;
}

bool DatabaseManager::saveSyncAck(const DatabaseManager::SyncAckRow& ack) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;

    QSqlQuery query(m_db);
    query.prepare(R"(
        INSERT OR REPLACE INTO sync_acks (
            request_id, success, applied_count, error_message, timestamp
        ) VALUES (?, ?, ?, ?, ?)
    )");
    query.addBindValue(ack.requestId);
    query.addBindValue(ack.success ? 1 : 0);
    query.addBindValue(ack.appliedCount);
    query.addBindValue(ack.errorMessage);
    query.addBindValue(QDateTime::currentMSecsSinceEpoch());

    if (!query.exec()) {
        emit databaseError(tr("保存同步ACK失败: %1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

DatabaseManager::SyncAckRow DatabaseManager::loadSyncAck(const QString& requestId) {
    DatabaseManager::SyncAckRow ack;

    if (!m_db.isOpen()) return ack;

    QSqlQuery query(m_db);
    query.prepare("SELECT * FROM sync_acks WHERE request_id = ?");
    query.addBindValue(requestId);

    if (!query.exec() || !query.next()) {
        return ack;
    }

    ack.requestId = query.value("request_id").toString();
    ack.success = query.value("success").toInt() != 0;
    ack.appliedCount = query.value("applied_count").toInt();
    ack.errorMessage = query.value("error_message").toString();

    return ack;
}

// --- Sync (multi-device) ---

DatabaseManager::SyncSnapshot DatabaseManager::buildSyncSnapshot(int messageLimit) {
    QMutexLocker locker(&m_mutex);
    DatabaseManager::SyncSnapshot snap;
    if (!m_db.isOpen()) return snap;

    // Contacts / devices
    QSqlQuery q(m_db);
    if (q.exec("SELECT device_id, name, ip, port, last_seen, is_friend, updated_at FROM devices")) {
        while (q.next()) {
            DatabaseManager::SyncDeviceRow row;
            row.deviceId = q.value("device_id").toString();
            row.name = q.value("name").toString();
            row.ip = q.value("ip").toString();
            row.port = q.value("port").toUInt();
            row.lastSeen = q.value("last_seen").toLongLong();
            row.isFriend = q.value("is_friend").toInt() != 0;
            row.updatedAt = q.value("updated_at").toLongLong();
            snap.devices.append(row);
            snap.version = qMax(snap.version, row.updatedAt);
        }
    }

    // Groups
    if (q.exec("SELECT group_id, name, member_ids, member_names, created_at, updated_at FROM groups")) {
        while (q.next()) {
            DatabaseManager::SyncGroupRow row;
            row.groupId = q.value("group_id").toString();
            row.name = q.value("name").toString();
            QJsonArray ids = QJsonDocument::fromJson(q.value("member_ids").toByteArray()).array();
            QJsonArray names = QJsonDocument::fromJson(q.value("member_names").toByteArray()).array();
            for (const auto& v : ids) row.memberIds.append(v.toString());
            for (const auto& v : names) row.memberNames.append(v.toString());
            row.createdAt = q.value("created_at").toLongLong();
            row.updatedAt = q.value("updated_at").toLongLong();
            snap.groups.append(row);
            snap.version = qMax(snap.version, row.updatedAt);
        }
    }

    // Settings
    if (q.exec("SELECT key, value, updated_at FROM settings")) {
        while (q.next()) {
            DatabaseManager::SyncSettingRow row;
            row.key = q.value("key").toString();
            row.value = q.value("value").toString();
            row.updatedAt = q.value("updated_at").toLongLong();
            snap.settings.append(row);
            snap.version = qMax(snap.version, row.updatedAt);
        }
    }

    // Recent messages (dedup on apply by message_id)
    QSqlQuery mq(m_db);
    mq.prepare("SELECT * FROM messages ORDER BY timestamp DESC LIMIT ?");
    mq.addBindValue(messageLimit);
    if (mq.exec()) {
        while (mq.next()) {
            DatabaseManager::SyncMessageRow row;
            row.messageId = mq.value("message_id").toString();
            row.senderId = mq.value("sender_id").toString();
            row.senderName = mq.value("sender_name").toString();
            row.senderIp = mq.value("sender_ip").toString();
            row.content = mq.value("content").toString();
            row.timestamp = mq.value("timestamp").toLongLong();
            row.isFile = mq.value("is_file").toInt() != 0;
            row.filePath = mq.value("file_path").toString();
            row.fileSize = mq.value("file_size").toLongLong();
            row.isDirectory = mq.value("is_directory").toInt() != 0;
            row.isImage = mq.value("is_image").toInt() != 0;
            row.imageFileName = mq.value("image_file_name").toString();
            row.replyTo = mq.value("reply_to").toString();
            row.replyContent = mq.value("reply_content").toString();
            row.recallId = mq.value("recall_id").toString();
            row.isRecalled = mq.value("is_recalled").toInt() != 0;
            row.targetId = mq.value("target_id").toString();
            row.isGroup = mq.value("is_group").toInt() != 0;
            row.isRead = mq.value("is_read").toInt() != 0;
            row.readBy = mq.value("read_by").toString();
            snap.messages.append(row);
        }
    }

    return snap;
}

int DatabaseManager::applySyncSnapshot(const DatabaseManager::SyncSnapshot& snap) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return 0;

    int applied = 0;

    // Devices / contacts: last-write-wins by updated_at
    for (const DatabaseManager::SyncDeviceRow& row : snap.devices) {
        qint64 localUpdated = -1;
        QSqlQuery sel(m_db);
        sel.prepare("SELECT updated_at FROM devices WHERE device_id = ?");
        sel.addBindValue(row.deviceId);
        if (sel.exec() && sel.next()) localUpdated = sel.value(0).toLongLong();
        if (localUpdated >= 0 && localUpdated > row.updatedAt) continue; // local is newer

        QSqlQuery ins(m_db);
        ins.prepare(R"(INSERT OR REPLACE INTO devices
            (device_id, name, ip, port, last_seen, is_friend, updated_at)
            VALUES (?, ?, ?, ?, ?, ?, ?))");
        ins.addBindValue(row.deviceId);
        ins.addBindValue(row.name);
        ins.addBindValue(row.ip);
        ins.addBindValue(row.port);
        ins.addBindValue(row.lastSeen);
        ins.addBindValue(row.isFriend ? 1 : 0);
        ins.addBindValue(row.updatedAt);
        if (ins.exec()) ++applied;
    }

    // Groups: last-write-wins by updated_at
    for (const DatabaseManager::SyncGroupRow& row : snap.groups) {
        qint64 localUpdated = -1;
        QSqlQuery sel(m_db);
        sel.prepare("SELECT updated_at FROM groups WHERE group_id = ?");
        sel.addBindValue(row.groupId);
        if (sel.exec() && sel.next()) localUpdated = sel.value(0).toLongLong();
        if (localUpdated >= 0 && localUpdated > row.updatedAt) continue;

        QJsonArray ids, names;
        for (const QString& s : row.memberIds) ids.append(s);
        for (const QString& s : row.memberNames) names.append(s);

        QSqlQuery ins(m_db);
        ins.prepare(R"(INSERT OR REPLACE INTO groups
            (group_id, name, member_ids, member_names, owner_id, created_at, updated_at)
            VALUES (?, ?, ?, ?, ?, ?, ?))");
        ins.addBindValue(row.groupId);
        ins.addBindValue(row.name);
        ins.addBindValue(QJsonDocument(ids).toJson(QJsonDocument::Compact));
        ins.addBindValue(QJsonDocument(names).toJson(QJsonDocument::Compact));
        ins.addBindValue(row.memberIds.isEmpty() ? "" : row.memberIds.first());
        ins.addBindValue(row.createdAt);
        ins.addBindValue(row.updatedAt);
        if (ins.exec()) ++applied;
    }

    // Settings: last-write-wins by updated_at
    for (const DatabaseManager::SyncSettingRow& row : snap.settings) {
        qint64 localUpdated = -1;
        QSqlQuery sel(m_db);
        sel.prepare("SELECT updated_at FROM settings WHERE key = ?");
        sel.addBindValue(row.key);
        if (sel.exec() && sel.next()) localUpdated = sel.value(0).toLongLong();
        if (localUpdated >= 0 && localUpdated > row.updatedAt) continue;

        QSqlQuery ins(m_db);
        ins.prepare("INSERT OR REPLACE INTO settings (key, value, updated_at) VALUES (?, ?, ?)");
        ins.addBindValue(row.key);
        ins.addBindValue(row.value);
        ins.addBindValue(row.updatedAt);
        if (ins.exec()) ++applied;
    }

    // Messages: idempotent by message_id (INSERT OR REPLACE dedups)
    for (const DatabaseManager::SyncMessageRow& row : snap.messages) {
        QString messageId = row.messageId;
        if (messageId.isEmpty()) {
            messageId = row.recallId.isEmpty()
                ? QString("%1_%2").arg(row.senderId).arg(row.timestamp)
                : row.recallId;
        }

        QSqlQuery ins(m_db);
        ins.prepare(R"(INSERT OR REPLACE INTO messages (
            message_id, sender_id, sender_name, sender_ip, content, timestamp,
            is_file, file_path, file_size, is_directory, is_image, image_data, image_file_name,
            reply_to, reply_content, recall_id, is_recalled,
            target_id, is_group, is_read, read_by
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?))");
        ins.addBindValue(messageId);
        ins.addBindValue(row.senderId);
        ins.addBindValue(row.senderName);
        ins.addBindValue(row.senderIp);
        ins.addBindValue(row.content);
        ins.addBindValue(row.timestamp);
        ins.addBindValue(row.isFile ? 1 : 0);
        ins.addBindValue(row.filePath);
        ins.addBindValue(row.fileSize);
        ins.addBindValue(row.isDirectory ? 1 : 0);
        ins.addBindValue(row.isImage ? 1 : 0);
        ins.addBindValue(QByteArray()); // image_data not synced (too heavy for E1)
        ins.addBindValue(row.imageFileName);
        ins.addBindValue(row.replyTo);
        ins.addBindValue(row.replyContent);
        ins.addBindValue(row.recallId);
        ins.addBindValue(row.isRecalled ? 1 : 0);
        ins.addBindValue(row.targetId);
        ins.addBindValue(row.isGroup ? 1 : 0);
        ins.addBindValue(row.isRead ? 1 : 0);
        ins.addBindValue(row.readBy);
        if (ins.exec()) ++applied;
    }

    return applied;
}

// --- Device Notes ---

bool DatabaseManager::saveDeviceNote(const QString& deviceId, const QString& note) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;
    QSqlQuery query(m_db);
    query.prepare("UPDATE devices SET friend_remark = ? WHERE device_id = ?");
    query.addBindValue(note);
    query.addBindValue(deviceId);
    return query.exec();
}

QString DatabaseManager::loadDeviceNote(const QString& deviceId) const {
    QMutexLocker locker(const_cast<QRecursiveMutex*>(&m_mutex));
    if (!m_db.isOpen()) return QString();
    QSqlQuery query(m_db);
    query.prepare("SELECT friend_remark FROM devices WHERE device_id = ?");
    query.addBindValue(deviceId);
    if (query.exec() && query.next()) return query.value(0).toString();
    return QString();
}

// --- Blocked Users ---

bool DatabaseManager::blockUser(const QString& deviceId, const QString& reason) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;
    QSqlQuery query(m_db);
    query.prepare("INSERT OR REPLACE INTO blocked_users (device_id, reason) VALUES (?, ?)");
    query.addBindValue(deviceId);
    query.addBindValue(reason);
    return query.exec();
}

bool DatabaseManager::unblockUser(const QString& deviceId) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM blocked_users WHERE device_id = ?");
    query.addBindValue(deviceId);
    return query.exec();
}

bool DatabaseManager::isBlocked(const QString& deviceId) const {
    QMutexLocker locker(const_cast<QRecursiveMutex*>(&m_mutex));
    if (!m_db.isOpen()) return false;
    QSqlQuery query(m_db);
    query.prepare("SELECT COUNT(*) FROM blocked_users WHERE device_id = ?");
    query.addBindValue(deviceId);
    if (query.exec() && query.next()) return query.value(0).toInt() > 0;
    return false;
}

QList<QString> DatabaseManager::loadBlockedUsers() {
    QMutexLocker locker(&m_mutex);
    QList<QString> result;
    if (!m_db.isOpen()) return result;
    QSqlQuery query(m_db);
    if (query.exec("SELECT device_id FROM blocked_users ORDER BY blocked_at DESC")) {
        while (query.next()) result.append(query.value(0).toString());
    }
    return result;
}

// --- Message Pinning ---

bool DatabaseManager::pinMessage(const QString& messageId) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;
    QSqlQuery query(m_db);
    query.prepare("UPDATE messages SET is_pinned = 1 WHERE message_id = ?");
    query.addBindValue(messageId);
    return query.exec();
}

bool DatabaseManager::unpinMessage(const QString& messageId) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;
    QSqlQuery query(m_db);
    query.prepare("UPDATE messages SET is_pinned = 0 WHERE message_id = ?");
    query.addBindValue(messageId);
    return query.exec();
}

bool DatabaseManager::isMessagePinned(const QString& messageId) const {
    QMutexLocker locker(const_cast<QRecursiveMutex*>(&m_mutex));
    if (!m_db.isOpen()) return false;
    QSqlQuery query(m_db);
    query.prepare("SELECT is_pinned FROM messages WHERE message_id = ?");
    query.addBindValue(messageId);
    if (query.exec() && query.next()) return query.value(0).toInt() != 0;
    return false;
}

QList<IPMsgMessage> DatabaseManager::loadPinnedMessages(const QString& targetId, bool isGroup) {
    QMutexLocker locker(&m_mutex);
    QList<IPMsgMessage> result;
    if (!m_db.isOpen()) return result;
    QSqlQuery query(m_db);
    query.prepare("SELECT * FROM messages WHERE target_id = ? AND is_group = ? AND is_pinned = 1 ORDER BY timestamp DESC");
    query.addBindValue(targetId);
    query.addBindValue(isGroup ? 1 : 0);
    if (query.exec()) {
        while (query.next()) {
            IPMsgMessage msg;
            msg.senderId = query.value("sender_id").toString();
            msg.senderName = query.value("sender_name").toString();
            msg.senderIp = query.value("sender_ip").toString();
            msg.content = query.value("content").toString();
            msg.timestamp = query.value("timestamp").toLongLong();
            msg.isFile = query.value("is_file").toBool();
            msg.filePath = query.value("file_path").toString();
            msg.fileSize = query.value("file_size").toLongLong();
            msg.isImage = query.value("is_image").toBool();
            msg.imageData = query.value("image_data").toByteArray();
            msg.imageFileName = query.value("image_file_name").toString();
            msg.replyTo = query.value("reply_to").toString();
            msg.replyContent = query.value("reply_content").toString();
            msg.recallId = query.value("recall_id").toString();
            msg.isRecalled = query.value("is_recalled").toBool();
            result.append(msg);
        }
    }
    return result;
}

// --- Chat Backup/Restore ---

bool DatabaseManager::exportDatabase(const QString& filePath) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;
    // Use SQLite backup API via QSqlDatabase
    QSqlDatabase backupDb = QSqlDatabase::addDatabase("QSQLITE", "backup_connection");
    backupDb.setDatabaseName(filePath);
    if (!backupDb.open()) {
        QSqlDatabase::removeDatabase("backup_connection");
        return false;
    }
    // Copy all data via SQL dump approach
    QSqlQuery src(m_db);
    QSqlQuery dst(backupDb);
    // Create tables in backup
    dst.exec("PRAGMA journal_mode=WAL");
    QStringList tables = {"messages", "devices", "groups", "settings", "recent_chats",
                          "file_transfers", "offline_messages", "e2ee_sessions",
                          "voice_messages", "video_messages", "location_messages",
                          "card_messages", "merge_forward_messages",
                          "group_announcements", "group_mentions", "group_votes",
                          "group_files", "group_albums", "group_todos",
                          "blocked_users", "blacklisted_ips"};
    for (const QString& table : tables) {
        if (src.exec(QString("SELECT sql FROM sqlite_master WHERE type='table' AND name='%1'").arg(table)) && src.next()) {
            dst.exec(src.value(0).toString());
        }
        if (src.exec(QString("SELECT * FROM %1").arg(table))) {
            QSqlRecord rec = src.record();
            int cols = rec.count();
            QString placeholders = QString("?,").repeated(cols).chopped(1);
            dst.exec(QString("DELETE FROM %1").arg(table));
            QSqlQuery ins(backupDb);
            ins.prepare(QString("INSERT OR REPLACE INTO %1 VALUES (%2)").arg(table, placeholders));
            while (src.next()) {
                ins.bindValue(0, src.value(0));
                for (int i = 1; i < cols; ++i) ins.bindValue(i, src.value(i));
                ins.exec();
            }
        }
    }
    backupDb.close();
    QSqlDatabase::removeDatabase("backup_connection");
    return true;
}

bool DatabaseManager::importDatabase(const QString& filePath) {
    QMutexLocker locker(&m_mutex);
    if (!QFile::exists(filePath)) return false;
    // Close current, copy file, reopen
    QString currentPath = m_dbPath;
    m_db.close();
    if (!QFile::remove(currentPath)) return false;
    if (!QFile::copy(filePath, currentPath)) return false;
    m_db = QSqlDatabase::database("ipmsg_connection");
    m_db.setDatabaseName(currentPath);
    if (!m_db.open()) return false;
    QSqlQuery(m_db).exec("PRAGMA journal_mode=WAL");
    return true;
}

// --- IP Blacklist ---

bool DatabaseManager::addBlacklistedIp(const QString& ip, const QString& reason) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;
    QSqlQuery query(m_db);
    query.prepare("INSERT OR REPLACE INTO blacklisted_ips (ip, reason) VALUES (?, ?)");
    query.addBindValue(ip);
    query.addBindValue(reason);
    return query.exec();
}

bool DatabaseManager::removeBlacklistedIp(const QString& ip) {
    QMutexLocker locker(&m_mutex);
    if (!m_db.isOpen()) return false;
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM blacklisted_ips WHERE ip = ?");
    query.addBindValue(ip);
    return query.exec();
}

bool DatabaseManager::isIpBlacklisted(const QString& ip) const {
    QMutexLocker locker(const_cast<QRecursiveMutex*>(&m_mutex));
    if (!m_db.isOpen()) return false;
    QSqlQuery query(m_db);
    query.prepare("SELECT COUNT(*) FROM blacklisted_ips WHERE ip = ?");
    query.addBindValue(ip);
    if (query.exec() && query.next()) return query.value(0).toInt() > 0;
    return false;
}

QList<QPair<QString, QString>> DatabaseManager::loadBlacklistedIps() {
    QMutexLocker locker(&m_mutex);
    QList<QPair<QString, QString>> result;
    if (!m_db.isOpen()) return result;
    QSqlQuery query(m_db);
    if (query.exec("SELECT ip, reason FROM blacklisted_ips ORDER BY blocked_at DESC")) {
        while (query.next()) {
            result.append({query.value(0).toString(), query.value(1).toString()});
        }
    }
    return result;
}

} // namespace xrk