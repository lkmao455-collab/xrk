#include "emoji_picker_widget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QFrame>
#include <QEvent>
#include <QScrollBar>

namespace xrk {

EmojiPickerWidget::EmojiPickerWidget(QWidget* parent) : QWidget(parent) {
    setWindowFlags(Qt::Popup | Qt::FramelessWindowHint);
    setFixedSize(360, 420);
    createCategories();
    setupUI();
    populateAllEmojis();
}

EmojiPickerWidget::~EmojiPickerWidget() {}

void EmojiPickerWidget::createCategories() {
    m_categories = {
        {"Recent", "\xf0\x9f\x94\x99", {}},
        {"Smileys", "\xf0\x9f\x98\x80", {
            "\xf0\x9f\x98\x80", "\xf0\x9f\x98\x81", "\xf0\x9f\x98\x82", "\xf0\x9f\x98\x83",
            "\xf0\x9f\x98\x84", "\xf0\x9f\x98\x85", "\xf0\x9f\x98\x86", "\xf0\x9f\x98\x87",
            "\xf0\x9f\x98\x89", "\xf0\x9f\x98\x8a", "\xf0\x9f\x98\x8b", "\xf0\x9f\x98\x8c",
            "\xf0\x9f\x98\x8d", "\xf0\x9f\x98\x8e", "\xf0\x9f\x98\x8f", "\xf0\x9f\x98\x90",
            "\xf0\x9f\x98\x91", "\xf0\x9f\x98\x92", "\xf0\x9f\x98\x93", "\xf0\x9f\x98\x94",
            "\xf0\x9f\x98\x95", "\xf0\x9f\x98\x96", "\xf0\x9f\x98\x97", "\xf0\x9f\x98\x98",
            "\xf0\x9f\x98\x99", "\xf0\x9f\x98\x9a", "\xf0\x9f\x98\x9b", "\xf0\x9f\x98\x9c",
            "\xf0\x9f\x98\x9d", "\xf0\x9f\x98\x9e", "\xf0\x9f\x98\x9f", "\xf0\x9f\x98\xa0",
            "\xf0\x9f\x98\xa1", "\xf0\x9f\x98\xa2", "\xf0\x9f\x98\xa3", "\xf0\x9f\x98\xa4",
            "\xf0\x9f\x98\xa5", "\xf0\x9f\x98\xa6", "\xf0\x9f\x98\xa7", "\xf0\x9f\x98\xa8",
            "\xf0\x9f\x98\xa9", "\xf0\x9f\x98\xaa", "\xf0\x9f\x98\xab", "\xf0\x9f\x98\xac",
            "\xf0\x9f\x98\xad", "\xf0\x9f\x98\xae", "\xf0\x9f\x98\xaf"
        }},
        {"Gestures", "\xf0\x9f\x91\x8b", {
            "\xf0\x9f\x91\x8b", "\xf0\x9f\x91\x8c", "\xf0\x9f\x91\x8d", "\xf0\x9f\x91\x8e",
            "\xf0\x9f\x91\x8f", "\xf0\x9f\x91\x90", "\xf0\x9f\x91\x91", "\xf0\x9f\x91\x92",
            "\xf0\x9f\x91\x93", "\xf0\x9f\x91\x94", "\xf0\x9f\x91\x95", "\xf0\x9f\x91\x96",
            "\xf0\x9f\x91\x97", "\xf0\x9f\x91\x98", "\xf0\x9f\x91\x99", "\xf0\x9f\x91\x9a",
            "\xf0\x9f\x91\x9b", "\xf0\x9f\x91\x9c", "\xf0\x9f\x91\x9d"
        }},
        {"Hearts", "\xe2\x9d\xa4", {
            "\xe2\x9d\xa4", "\xf0\x9f\x92\x94", "\xf0\x9f\x92\x95", "\xf0\x9f\x92\x96",
            "\xf0\x9f\x92\x97", "\xf0\x9f\x92\x98", "\xf0\x9f\x92\x99", "\xf0\x9f\x92\x9a",
            "\xf0\x9f\x92\x9b", "\xf0\x9f\x92\x9c", "\xf0\x9f\x92\x9d", "\xf0\x9f\x92\x9e",
            "\xf0\x9f\x92\x9f"
        }},
        {"Animals", "\xf0\x9f\x90\xb1", {
            "\xf0\x9f\x90\xb1", "\xf0\x9f\x90\xb2", "\xf0\x9f\x90\xb3", "\xf0\x9f\x90\xb4",
            "\xf0\x9f\x90\xb5", "\xf0\x9f\x90\xb6", "\xf0\x9f\x90\xb7", "\xf0\x9f\x90\xb8",
            "\xf0\x9f\x90\xb9", "\xf0\x9f\x90\xba", "\xf0\x9f\x90\xbb", "\xf0\x9f\x90\xbc",
            "\xf0\x9f\x90\xbd", "\xf0\x9f\x90\xbe", "\xf0\x9f\x90\xbf", "\xf0\x9f\x91\x80",
            "\xf0\x9f\x91\x81", "\xf0\x9f\x91\x82", "\xf0\x9f\x91\x83", "\xf0\x9f\x91\x84",
            "\xf0\x9f\x91\x85", "\xf0\x9f\x91\x86", "\xf0\x9f\x91\x87", "\xf0\x9f\x91\x88",
            "\xf0\x9f\x91\x89", "\xf0\x9f\x91\x8a"
        }},
        {"Food", "\xf0\x9f\x8d\x8e", {
            "\xf0\x9f\x8d\x8e", "\xf0\x9f\x8d\x8f", "\xf0\x9f\x8d\x90", "\xf0\x9f\x8d\x91",
            "\xf0\x9f\x8d\x92", "\xf0\x9f\x8d\x93", "\xf0\x9f\x8d\x94", "\xf0\x9f\x8d\x95",
            "\xf0\x9f\x8d\x96", "\xf0\x9f\x8d\x97", "\xf0\x9f\x8d\x98", "\xf0\x9f\x8d\x99",
            "\xf0\x9f\x8d\x9a", "\xf0\x9f\x8d\x9b", "\xf0\x9f\x8d\x9c", "\xf0\x9f\x8d\x9d",
            "\xf0\x9f\x8d\x9e", "\xf0\x9f\x8d\x9f", "\xf0\x9f\x8d\xa0"
        }},
        {"Objects", "\xe2\x9c\xa8", {
            "\xe2\x9c\xa8", "\xf0\x9f\x92\xa1", "\xf0\x9f\x92\xa2", "\xf0\x9f\x92\xa3",
            "\xf0\x9f\x92\xa4", "\xf0\x9f\x92\xa5", "\xf0\x9f\x92\xa6", "\xf0\x9f\x92\xa7",
            "\xf0\x9f\x92\xa8", "\xf0\x9f\x92\xa9", "\xf0\x9f\x92\xaa", "\xf0\x9f\x92\xab",
            "\xf0\x9f\x92\xac", "\xf0\x9f\x92\xad", "\xf0\x9f\x92\xae", "\xf0\x9f\x92\xaf",
            "\xf0\x9f\x92\xb0", "\xf0\x9f\x92\xb1", "\xf0\x9f\x92\xb2", "\xf0\x9f\x92\xb3",
            "\xf0\x9f\x92\xb4", "\xf0\x9f\x92\xb5", "\xf0\x9f\x92\xb6", "\xf0\x9f\x92\xb7",
            "\xf0\x9f\x92\xb8", "\xf0\x9f\x92\xb9", "\xf0\x9f\x92\xba"
        }},
        {"Symbols", "\xe2\x9a\xa0", {
            "\xe2\x9a\xa0", "\xe2\x9a\xa1", "\xe2\x9a\xa2", "\xe2\x9a\xa3",
            "\xe2\x9a\xa4", "\xe2\x9a\xa5", "\xe2\x9a\xa6", "\xe2\x9a\xa7",
            "\xe2\x9a\xa8", "\xe2\x9a\xa9", "\xe2\x9a\xaa", "\xe2\x9a\xab",
            "\xe2\x9a\xac", "\xe2\x9a\xad", "\xe2\x9a\xae", "\xe2\x9a\xaf",
            "\xe2\x9a\xb0", "\xe2\x9a\xb1", "\xe2\x9a\xb2", "\xe2\x9a\xb3"
        }}
    };
}

void EmojiPickerWidget::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(6);

    m_searchInput = new QLineEdit(this);
    m_searchInput->setPlaceholderText(tr("Search emoji..."));
    m_searchInput->setClearButtonEnabled(true);
    connect(m_searchInput, &QLineEdit::textChanged, this, &EmojiPickerWidget::onSearchChanged);
    mainLayout->addWidget(m_searchInput);

    m_categoryBar = createCategoryBar();
    mainLayout->addWidget(m_categoryBar);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    mainLayout->addWidget(m_scrollArea);

    m_contentWidget = new QWidget();
    m_gridLayout = new QGridLayout(m_contentWidget);
    m_gridLayout->setContentsMargins(4, 4, 4, 4);
    m_gridLayout->setSpacing(2);
    m_scrollArea->setWidget(m_contentWidget);
}

QFrame* EmojiPickerWidget::createCategoryBar() {
    QFrame* bar = new QFrame(this);
    bar->setFixedHeight(32);
    QHBoxLayout* layout = new QHBoxLayout(bar);
    layout->setContentsMargins(4, 0, 4, 0);
    layout->setSpacing(2);

    for (int i = 0; i < m_categories.size(); ++i) {
        QPushButton* btn = new QPushButton(m_categories[i].icon, bar);
        btn->setFixedSize(32, 28);
        btn->setToolTip(m_categories[i].name);
        btn->setCheckable(true);
        connect(btn, &QPushButton::clicked, this, [this, i]() { onCategoryClicked(i); });
        layout->addWidget(btn);
        m_categoryButtons.append(btn);
    }
    layout->addStretch();
    return bar;
}

void EmojiPickerWidget::populateAllEmojis() {
    qDeleteAll(m_emojiButtons);
    m_emojiButtons.clear();

    int row = 0, col = 0;
    const int cols = 8;

    if (!m_recentEmojis.isEmpty()) {
        QLabel* label = new QLabel(tr("Recent"), m_contentWidget);
        label->setStyleSheet("color: gray; font-size: 11px; padding: 4px;");
        m_gridLayout->addWidget(label, row, 0, 1, cols);
        ++row;
        for (const QString& emoji : m_recentEmojis) {
            QPushButton* btn = createEmojiButton(emoji);
            m_gridLayout->addWidget(btn, row, col);
            if (++col >= cols) { col = 0; ++row; }
        }
        ++row;
    }

    for (const auto& cat : m_categories) {
        if (cat.name == "Recent") continue;
        QLabel* label = new QLabel(cat.name, m_contentWidget);
        label->setStyleSheet("color: gray; font-size: 11px; padding: 4px;");
        m_gridLayout->addWidget(label, row, 0, 1, cols);
        ++row;
        col = 0;
        for (const QString& emoji : cat.emojis) {
            QPushButton* btn = createEmojiButton(emoji);
            m_gridLayout->addWidget(btn, row, col);
            if (++col >= cols) { col = 0; ++row; }
        }
        ++row;
    }
    m_gridLayout->setRowStretch(row, 1);
}

void EmojiPickerWidget::populateEmojis(const QString& category) {
    qDeleteAll(m_emojiButtons);
    m_emojiButtons.clear();

    int row = 0, col = 0;
    const int cols = 8;

    if (category == "Recent") {
        for (const QString& emoji : m_recentEmojis) {
            QPushButton* btn = createEmojiButton(emoji);
            m_gridLayout->addWidget(btn, row, col);
            if (++col >= cols) { col = 0; ++row; }
        }
    } else {
        for (const auto& cat : m_categories) {
            if (cat.name == category) {
                for (const QString& emoji : cat.emojis) {
                    QPushButton* btn = createEmojiButton(emoji);
                    m_gridLayout->addWidget(btn, row, col);
                    if (++col >= cols) { col = 0; ++row; }
                }
                break;
            }
        }
    }
    m_gridLayout->setRowStretch(row + 1, 1);
}

QPushButton* EmojiPickerWidget::createEmojiButton(const QString& emoji) {
    QPushButton* btn = new QPushButton(emoji, m_contentWidget);
    btn->setFixedSize(40, 40);
    btn->setStyleSheet("QPushButton { border: none; font-size: 22px; }"
                        "QPushButton:hover { background-color: rgba(255,255,255,0.15); border-radius: 6px; }");
    connect(btn, &QPushButton::clicked, this, [this, emoji]() { onEmojiClicked(emoji); });
    m_emojiButtons.append(btn);
    return btn;
}

void EmojiPickerWidget::onEmojiClicked(const QString& emoji) {
    updateRecentList(emoji);
    emit emojiSelected(emoji);
    emit closed();
    hide();
}

void EmojiPickerWidget::onSearchChanged(const QString& text) {
    if (text.isEmpty()) {
        if (m_currentCategory >= 0 && m_currentCategory < m_categories.size())
            populateEmojis(m_categories[m_currentCategory].name);
        else
            populateAllEmojis();
        return;
    }

    qDeleteAll(m_emojiButtons);
    m_emojiButtons.clear();
    int row = 0, col = 0;
    const int cols = 8;

    for (const auto& cat : m_categories) {
        for (const QString& emoji : cat.emojis) {
            QPushButton* btn = createEmojiButton(emoji);
            m_gridLayout->addWidget(btn, row, col);
            if (++col >= cols) { col = 0; ++row; }
        }
    }
    m_gridLayout->setRowStretch(row + 1, 1);
}

void EmojiPickerWidget::onCategoryClicked(int categoryIndex) {
    if (categoryIndex < 0 || categoryIndex >= m_categories.size()) return;
    m_currentCategory = categoryIndex;
    for (int i = 0; i < m_categoryButtons.size(); ++i)
        m_categoryButtons[i]->setChecked(i == categoryIndex);
    populateEmojis(m_categories[categoryIndex].name);
}

void EmojiPickerWidget::updateRecentList(const QString& emoji) {
    m_recentEmojis.removeAll(emoji);
    m_recentEmojis.prepend(emoji);
    while (m_recentEmojis.size() > 16)
        m_recentEmojis.removeLast();
}

void EmojiPickerWidget::setRecentEmojis(const QStringList& emojis) {
    m_recentEmojis = emojis;
}

QStringList EmojiPickerWidget::recentEmojis() const {
    return m_recentEmojis;
}

} // namespace xrk
