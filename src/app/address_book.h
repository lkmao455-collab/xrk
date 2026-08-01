#pragma once

#include <QString>
#include <QList>
#include <QDateTime>
#include <QJsonObject>
#include <QJsonArray>

namespace xrk {

struct AddressBookEntry {
    QString id;
    QString name;
    QString ip;
    uint16_t port = 9999;
    QString mac;
    QString group;
    QString notes;
    QString accessCode;
    bool favorite = false;
    QDateTime lastConnected;
    QStringList tags;       // free-form labels (e.g. "server", "win11")
    QDateTime lastSeen;     // last time the device responded to a probe

    QJsonObject toJson() const;
    static AddressBookEntry fromJson(const QJsonObject& obj);
};

class AddressBook {
public:
    AddressBook();

    void load();
    void save();

    QList<AddressBookEntry> allEntries() const;
    AddressBookEntry entry(const QString& id) const;
    void addEntry(const AddressBookEntry& entry);
    void updateEntry(const AddressBookEntry& entry);
    void removeEntry(const QString& id);
    bool contains(const QString& id) const;

    QStringList groups() const;
    QList<AddressBookEntry> entriesByGroup(const QString& group) const;
    QList<AddressBookEntry> favorites() const;
    QList<AddressBookEntry> search(const QString& keyword) const;
    // Most-recently-connected entries first (up to `n`), for quick access.
    QList<AddressBookEntry> recent(int n = 10) const;

    // Group management (groups are derived from entries' `group` field).
    bool renameGroup(const QString& oldName, const QString& newName);
    bool removeGroup(const QString& name);  // reassigns members to ""

    // Bulk import/export for IT distribution. merge=true updates existing
    // records (by id for JSON, by ip for CSV) instead of duplicating.
    bool exportJson(const QString& path) const;
    int importJson(const QString& path, bool merge);
    bool exportCsv(const QString& path) const;
    int importCsv(const QString& path, bool merge);

private:
    QString filePath() const;
    QList<AddressBookEntry> m_entries;
};

} // namespace xrk
