#pragma once

#include <QWidget>
#include <QGridLayout>
#include <QLineEdit>
#include <QScrollArea>
#include <QPushButton>
#include <QLabel>
#include <QMap>
#include <QList>

namespace xrk {

class EmojiPickerWidget : public QWidget {
    Q_OBJECT
public:
    explicit EmojiPickerWidget(QWidget* parent = nullptr);
    ~EmojiPickerWidget();

    void setRecentEmojis(const QStringList& emojis);
    QStringList recentEmojis() const;

signals:
    void emojiSelected(const QString& emoji);
    void closed();

private slots:
    void onEmojiClicked(const QString& emoji);
    void onSearchChanged(const QString& text);
    void onCategoryClicked(int categoryIndex);

private:
    void setupUI();
    void createCategories();
    void populateEmojis(const QString& category);
    void populateAllEmojis();
    void updateRecentList(const QString& emoji);
    QPushButton* createEmojiButton(const QString& emoji);
    QFrame* createCategoryBar();

    QGridLayout* m_gridLayout = nullptr;
    QScrollArea* m_scrollArea = nullptr;
    QWidget* m_contentWidget = nullptr;
    QLineEdit* m_searchInput = nullptr;
    QLabel* m_categoryLabel = nullptr;
    QFrame* m_categoryBar = nullptr;

    QStringList m_recentEmojis;
    QStringList m_currentEmojis;
    QList<QPushButton*> m_emojiButtons;
    QList<QPushButton*> m_categoryButtons;
    int m_currentCategory = -1;

    struct EmojiCategory {
        QString name;
        QString icon;
        QStringList emojis;
    };
    QList<EmojiCategory> m_categories;
};

} // namespace xrk
