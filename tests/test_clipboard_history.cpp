#include <gtest/gtest.h>
#include "app/clipboard_history.h"

using namespace xrk;

class ClipboardHistoryTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_history = new ClipboardHistory();
        m_history->load();
        m_history->clear();
    }

    void TearDown() override {
        m_history->clear();
    }

    ClipboardHistory* m_history = nullptr;
};

TEST_F(ClipboardHistoryTest, AddEntry) {
    EXPECT_EQ(m_history->count(), 0);

    ClipboardEntry entry = m_history->addEntry("text/plain", "hello world");
    EXPECT_EQ(m_history->count(), 1);
    EXPECT_FALSE(entry.id.isEmpty());
    EXPECT_TRUE(entry.mimeType == "text/plain");
    EXPECT_TRUE(entry.data == QByteArray("hello world"));
    EXPECT_TRUE(entry.preview == "hello world");
    EXPECT_FALSE(entry.isFavorite);
}

TEST_F(ClipboardHistoryTest, AddEmptyData) {
    ClipboardEntry entry = m_history->addEntry("text/plain", QByteArray());
    EXPECT_TRUE(entry.id.isEmpty());
    EXPECT_EQ(m_history->count(), 0);
}

TEST_F(ClipboardHistoryTest, AddEntryDedup) {
    ClipboardEntry first = m_history->addEntry("text/plain", "same data");
    ClipboardEntry second = m_history->addEntry("text/plain", "same data");
    EXPECT_EQ(m_history->count(), 1);
    EXPECT_TRUE(first.id == second.id);
}

TEST_F(ClipboardHistoryTest, PreviewTruncation) {
    QString longText(250, 'a');
    ClipboardEntry entry = m_history->addEntry("text/plain", longText.toUtf8());
    EXPECT_EQ(entry.preview.size(), 203);
    EXPECT_TRUE(entry.preview.endsWith("..."));
}

TEST_F(ClipboardHistoryTest, PreviewImage) {
    ClipboardEntry entry = m_history->addEntry("image/png", QByteArray(123, '\0'));
    EXPECT_EQ(entry.preview, "[Image 123 bytes]");
}

TEST_F(ClipboardHistoryTest, PreviewUriList) {
    ClipboardEntry single = m_history->addEntry("text/uri-list", "file:///C:/a.txt");
    EXPECT_TRUE(single.preview == "file:///C:/a.txt");

    ClipboardEntry multi = m_history->addEntry("text/uri-list", "file:///a\nfile:///b\nfile:///c");
    EXPECT_TRUE(multi.preview == "[3 files/items]") << " actual=[" << multi.preview.toStdString() << "]";
}

TEST_F(ClipboardHistoryTest, Search) {
    m_history->addEntry("text/plain", "Hello XRK");
    m_history->addEntry("text/plain", "nothing here");
    m_history->addEntry("image/png", QByteArray(10, '\0'));

    QList<ClipboardEntry> results = m_history->search("xrk");
    EXPECT_EQ(results.size(), 1);
    EXPECT_TRUE(results.first().preview == "Hello XRK");

    results = m_history->search("png");
    EXPECT_EQ(results.size(), 1);
    EXPECT_TRUE(results.first().mimeType == "image/png");

    results = m_history->search("nomatch");
    EXPECT_TRUE(results.isEmpty());
}

TEST_F(ClipboardHistoryTest, EntryById) {
    ClipboardEntry added = m_history->addEntry("text/plain", "find me");
    ClipboardEntry found = m_history->entry(added.id);
    EXPECT_TRUE(found.id == added.id);
    EXPECT_TRUE(found.data == QByteArray("find me"));

    ClipboardEntry missing = m_history->entry("nonexistent");
    EXPECT_TRUE(missing.id.isEmpty());
}

TEST_F(ClipboardHistoryTest, RemoveEntry) {
    ClipboardEntry a = m_history->addEntry("text/plain", "a");
    ClipboardEntry b = m_history->addEntry("text/plain", "b");
    EXPECT_EQ(m_history->count(), 2);

    m_history->removeEntry(a.id);
    EXPECT_EQ(m_history->count(), 1);
    EXPECT_TRUE(m_history->entry(a.id).id.isEmpty());
    EXPECT_FALSE(m_history->entry(b.id).id.isEmpty());

    m_history->removeEntry("nonexistent");
    EXPECT_EQ(m_history->count(), 1);
}

TEST_F(ClipboardHistoryTest, ToggleFavorite) {
    ClipboardEntry entry = m_history->addEntry("text/plain", "fav me");
    EXPECT_FALSE(m_history->entry(entry.id).isFavorite);

    m_history->toggleFavorite(entry.id);
    EXPECT_TRUE(m_history->entry(entry.id).isFavorite);

    m_history->toggleFavorite(entry.id);
    EXPECT_FALSE(m_history->entry(entry.id).isFavorite);
}

TEST_F(ClipboardHistoryTest, Clear) {
    m_history->addEntry("text/plain", "a");
    m_history->addEntry("text/plain", "b");
    EXPECT_EQ(m_history->count(), 2);

    m_history->clear();
    EXPECT_EQ(m_history->count(), 0);
}

TEST_F(ClipboardHistoryTest, PruneNonFavorite) {
    m_history->setMaxEntries(10);
    for (int i = 0; i < 15; ++i) {
        m_history->addEntry("text/plain", QString::number(i).toUtf8());
    }
    EXPECT_EQ(m_history->count(), 10);
    EXPECT_EQ(m_history->maxEntries(), 10);
}

TEST_F(ClipboardHistoryTest, FavoritesSurvivePrune) {
    m_history->setMaxEntries(10);
    for (int i = 0; i < 10; ++i) {
        m_history->addEntry("text/plain", QString::number(i).toUtf8());
    }

    ClipboardEntry oldest = m_history->entries().last();
    m_history->toggleFavorite(oldest.id);

    for (int i = 0; i < 12; ++i) {
        m_history->addEntry("text/plain", QString("filler%1").arg(i).toUtf8());
    }

    EXPECT_EQ(m_history->count(), 10);
    EXPECT_TRUE(m_history->entry(oldest.id).isFavorite);
    EXPECT_TRUE(m_history->entry(oldest.id).data == QByteArray("0"));
}

TEST_F(ClipboardHistoryTest, SetMaxEntriesClamped) {
    m_history->setMaxEntries(1);
    EXPECT_EQ(m_history->maxEntries(), 10);
}

TEST_F(ClipboardHistoryTest, ToJsonFromJsonRoundtrip) {
    ClipboardEntry added = m_history->addEntry("text/plain", "roundtrip");

    QJsonObject obj = added.toJson();
    ClipboardEntry parsed = ClipboardEntry::fromJson(obj);

    EXPECT_TRUE(parsed.id == added.id);
    EXPECT_TRUE(parsed.mimeType == added.mimeType);
    EXPECT_TRUE(parsed.data == added.data);
    EXPECT_TRUE(parsed.preview == added.preview);
    EXPECT_EQ(parsed.isFavorite, added.isFavorite);
    EXPECT_EQ(parsed.timestamp.toSecsSinceEpoch(), added.timestamp.toSecsSinceEpoch());
}

TEST_F(ClipboardHistoryTest, SaveLoadRoundtrip) {
    m_history->addEntry("text/plain", "persist me");

    ClipboardHistory reloaded;
    reloaded.load();
    EXPECT_EQ(reloaded.count(), 1);
    EXPECT_TRUE(reloaded.entries().first().data == QByteArray("persist me"));
    reloaded.clear();
}
