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

private:
    QString filePath() const;
    QList<AddressBookEntry> m_entries;
};

} // namespace xrk
