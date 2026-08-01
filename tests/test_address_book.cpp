#include <gtest/gtest.h>
#include <QTemporaryDir>
#include <QDateTime>
#include "address_book.h"

namespace xrk {

TEST(AddressBook, JsonRoundTripKeepsTagsAndLastSeen) {
    AddressBookEntry e;
    e.id = "id-1";
    e.name = "Office PC";
    e.ip = "192.168.1.10";
    e.port = 9999;
    e.group = "Office";
    e.favorite = true;
    e.tags = QStringList() << "server" << "win11";
    e.lastSeen = QDateTime::fromString("2026-08-01T10:00:00", Qt::ISODate);

    QJsonObject obj = e.toJson();
    AddressBookEntry back = AddressBookEntry::fromJson(obj);

    EXPECT_EQ(back.id, e.id);
    EXPECT_EQ(back.name, e.name);
    EXPECT_EQ(back.ip, e.ip);
    EXPECT_EQ(back.group, e.group);
    EXPECT_TRUE(back.favorite);
    EXPECT_EQ(back.tags, e.tags);
    EXPECT_EQ(back.lastSeen, e.lastSeen);
}

TEST(AddressBook, CsvRoundTripPreservesEntries) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QString path = dir.filePath("book.csv");

    AddressBook book;
    AddressBookEntry a;
    a.name = "Alpha";
    a.ip = "10.0.0.1";
    a.port = 9999;
    a.group = "Lab";
    a.favorite = true;
    a.tags = QStringList() << "gpu";
    book.addEntry(a);
    AddressBookEntry b;
    b.name = "Beta, with comma";
    b.ip = "10.0.0.2";
    b.port = 1234;
    b.notes = "has \"quote\" and\nnewline";
    book.addEntry(b);

    ASSERT_TRUE(book.exportCsv(path));

    AddressBook loaded;
    int n = loaded.importCsv(path, false);
    ASSERT_EQ(n, 2);

    // Find by ip (ids are regenerated on import, so match on ip).
    auto find = [&](const QString& ip) -> AddressBookEntry {
        for (const auto& e : loaded.allEntries())
            if (e.ip == ip) return e;
        return AddressBookEntry();
    };

    AddressBookEntry ra = find("10.0.0.1");
    EXPECT_EQ(ra.name, "Alpha");
    EXPECT_EQ(ra.group, "Lab");
    EXPECT_TRUE(ra.favorite);
    EXPECT_EQ(ra.tags, QStringList() << "gpu");

    AddressBookEntry rb = find("10.0.0.2");
    EXPECT_EQ(rb.name, "Beta, with comma");
    EXPECT_EQ(rb.port, 1234);
    EXPECT_EQ(rb.notes, "has \"quote\" and\nnewline");
}

TEST(AddressBook, GroupRenameAndRemove) {
    AddressBook book;
    AddressBookEntry a; a.name = "A"; a.ip = "1.1.1.1"; a.group = "Office";
    AddressBookEntry b; b.name = "B"; b.ip = "1.1.1.2"; b.group = "Office";
    AddressBookEntry c; c.name = "C"; b.ip = "1.1.1.3"; c.group = "Home";
    book.addEntry(a); book.addEntry(b); book.addEntry(c);

    EXPECT_TRUE(book.renameGroup("Office", "Work"));
    EXPECT_FALSE(book.groups().contains("Office"));
    EXPECT_TRUE(book.groups().contains("Work"));
    for (const auto& e : book.entriesByGroup("Work"))
        EXPECT_EQ(e.group, "Work");

    EXPECT_TRUE(book.removeGroup("Work"));
    EXPECT_FALSE(book.groups().contains("Work"));
    // Remaining group still present.
    EXPECT_TRUE(book.groups().contains("Home"));
}

TEST(AddressBook, RecentSortsByLastConnected) {
    AddressBook book;
    AddressBookEntry a; a.name = "A"; a.ip = "1.1.1.1";
    a.lastConnected = QDateTime::fromString("2026-01-01T00:00:00", Qt::ISODate);
    AddressBookEntry b; b.name = "B"; b.ip = "1.1.1.2";
    b.lastConnected = QDateTime::fromString("2026-08-01T00:00:00", Qt::ISODate);
    AddressBookEntry c; c.name = "C"; c.ip = "1.1.1.3";
    c.lastConnected = QDateTime::fromString("2026-04-01T00:00:00", Qt::ISODate);
    book.addEntry(a); book.addEntry(b); book.addEntry(c);

    QList<AddressBookEntry> rec = book.recent(2);
    ASSERT_EQ(rec.size(), 2);
    EXPECT_EQ(rec[0].ip, "1.1.1.2");   // most recent first
    EXPECT_EQ(rec[1].ip, "1.1.1.3");
}

TEST(AddressBook, ImportMergeByIpUpdatesExisting) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    QString path = dir.filePath("book.json");

    AddressBook book;
    AddressBookEntry a; a.id = "keep"; a.name = "Old"; a.ip = "2.2.2.2"; a.port = 9999;
    book.addEntry(a);
    ASSERT_TRUE(book.exportJson(path));

    // Modify the exported file's name, then re-import with merge.
    AddressBook src;
    src.importJson(path, false);
    for (const auto& e : src.allEntries()) {
        if (e.ip == "2.2.2.2") {
            AddressBookEntry m = e;
            m.name = "New";
            src.updateEntry(m);
        }
    }
    src.exportJson(path);

    int n = book.importJson(path, true);
    EXPECT_EQ(n, 1);
    EXPECT_EQ(book.entry("keep").name, "New");  // updated by id
    EXPECT_EQ(book.allEntries().size(), 1);      // no duplicate
}

} // namespace xrk
