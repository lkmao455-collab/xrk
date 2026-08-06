#include "theme_manager.h"
#include <QFile>
#include <QTextStream>
#include <QStandardPaths>
#include <QDir>

namespace xrk {

ThemeManager& ThemeManager::instance() {
    static ThemeManager manager;
    return manager;
}

ThemeManager::ThemeManager()
    : m_settings("XRK", "Themes") {
    initThemes();
    m_currentThemeId = m_settings.value("currentTheme", "cute_pink").toString();
    if (!m_themes.contains(m_currentThemeId)) {
        m_currentThemeId = "cute_pink";
    }
}

void ThemeManager::initThemes() {
    // 1. 粉色少女主题 (默认)
    Theme cute_pink;
    cute_pink.id = "cute_pink";
    cute_pink.name = "粉色少女";
    cute_pink.description = "甜美可爱的粉色主题";
    cute_pink.primaryColor = QColor("#FF6B9D");
    cute_pink.secondaryColor = QColor("#C44569");
    cute_pink.accentColor = QColor("#FF85A1");
    cute_pink.backgroundColor = QColor("#FFF0F5");
    cute_pink.surfaceColor = QColor("#FFE4EC");
    cute_pink.cardColor = QColor("#FFFFFF");
    cute_pink.textColor = QColor("#5D4E6D");
    cute_pink.mutedColor = QColor("#9B8AA0");
    cute_pink.backgroundPixmap = ":/themes/pink_bg.png";
    cute_pink.hasBackground = true;
    m_themes["cute_pink"] = cute_pink;

    // 2. 宅男主题 (深色科技风)
    Theme otaku;
    otaku.id = "otaku";
    otaku.name = "宅男";
    otaku.description = "暗黑科技风格";
    otaku.primaryColor = QColor("#00D4FF");
    otaku.secondaryColor = QColor("#0099CC");
    otaku.accentColor = QColor("#00FFCC");
    otaku.backgroundColor = QColor("#0A0E1A");
    otaku.surfaceColor = QColor("#121828");
    otaku.cardColor = QColor("#1A2035");
    otaku.textColor = QColor("#E0E0E0");
    otaku.mutedColor = QColor("#6B7A99");
    otaku.backgroundPixmap = ":/themes/otaku_bg.png";
    otaku.hasBackground = true;
    m_themes["otaku"] = otaku;

    // 3. 学生主题 (清新蓝)
    Theme student;
    student.id = "student";
    student.name = "学生";
    student.description = "清新活力的蓝色";
    student.primaryColor = QColor("#4A90E2");
    student.secondaryColor = QColor("#357ABD");
    student.accentColor = QColor("#5DADE2");
    student.backgroundColor = QColor("#F0F8FF");
    student.surfaceColor = QColor("#E8F4FD");
    student.cardColor = QColor("#FFFFFF");
    student.textColor = QColor("#2C3E50");
    student.mutedColor = QColor("#7F8C8D");
    student.backgroundPixmap = ":/themes/student_bg.png";
    student.hasBackground = true;
    m_themes["student"] = student;

    // 4. 教师主题 (稳重棕)
    Theme teacher;
    teacher.id = "teacher";
    teacher.name = "教师";
    teacher.description = "沉稳典雅的棕色";
    teacher.primaryColor = QColor("#8B7355");
    teacher.secondaryColor = QColor("#6B5344");
    teacher.accentColor = QColor("#A0826D");
    teacher.backgroundColor = QColor("#FDF5E6");
    teacher.surfaceColor = QColor("#F5E6D3");
    teacher.cardColor = QColor("#FFFFFF");
    teacher.textColor = QColor("#4A3728");
    teacher.mutedColor = QColor("#8B7355");
    teacher.backgroundPixmap = ":/themes/teacher_bg.png";
    teacher.hasBackground = true;
    m_themes["teacher"] = teacher;

    // 5. 老板主题 (奢华金)
    Theme boss;
    boss.id = "boss";
    boss.name = "老板";
    boss.description = "奢华尊贵的金色";
    boss.primaryColor = QColor("#D4AF37");
    boss.secondaryColor = QColor("#B8860B");
    boss.accentColor = QColor("#FFD700");
    boss.backgroundColor = QColor("#1A1A1A");
    boss.surfaceColor = QColor("#2D2D2D");
    boss.cardColor = QColor("#3D3D3D");
    boss.textColor = QColor("#FFD700");
    boss.mutedColor = QColor("#8B7500");
    boss.backgroundPixmap = ":/themes/boss_bg.png";
    boss.hasBackground = true;
    m_themes["boss"] = boss;

    // 6. 道士主题 (仙气紫)
    Theme taoist;
    taoist.id = "taoist";
    taoist.name = "道士";
    taoist.description = "道法自然的仙气紫";
    taoist.primaryColor = QColor("#9B59B6");
    taoist.secondaryColor = QColor("#8E44AD");
    taoist.accentColor = QColor("#BB8FCE");
    taoist.backgroundColor = QColor("#F4F0F7");
    taoist.surfaceColor = QColor("#EBE4F0");
    taoist.cardColor = QColor("#FFFFFF");
    taoist.textColor = QColor("#4A3B5C");
    taoist.mutedColor = QColor("#8E7BA0");
    taoist.backgroundPixmap = ":/themes/taoist_bg.png";
    taoist.hasBackground = true;
    m_themes["taoist"] = taoist;

    // 7. 悟空主题 (热血橙)
    Theme wukong;
    wukong.id = "wukong";
    wukong.name = "悟空";
    wukong.description = "热血激昂的橙色";
    wukong.primaryColor = QColor("#E74C3C");
    wukong.secondaryColor = QColor("#C0392B");
    wukong.accentColor = QColor("#FF6B35");
    wukong.backgroundColor = QColor("#FFF8F0");
    wukong.surfaceColor = QColor("#FFE8D6");
    wukong.cardColor = QColor("#FFFFFF");
    wukong.textColor = QColor("#5D3A1A");
    wukong.mutedColor = QColor("#A0652F");
    wukong.backgroundPixmap = ":/themes/wukong_bg.png";
    wukong.hasBackground = true;
    m_themes["wukong"] = wukong;

    // 8. 宝贝主题 (糖果色)
    Theme baby;
    baby.id = "baby";
    baby.name = "宝贝";
    baby.description = "梦幻糖果色";
    baby.primaryColor = QColor("#FF69B4");
    baby.secondaryColor = QColor("#FF1493");
    baby.accentColor = QColor("#87CEEB");
    baby.backgroundColor = QColor("#FFF5EE");
    baby.surfaceColor = QColor("#FFEFD5");
    baby.cardColor = QColor("#FFFFFF");
    baby.textColor = QColor("#614051");
    baby.mutedColor = QColor("#BC8F8F");
    baby.backgroundPixmap = ":/themes/baby_bg.png";
    baby.hasBackground = true;
    m_themes["baby"] = baby;
}

void ThemeManager::loadThemes() {
    m_themes.clear();
    initThemes();
}

void ThemeManager::applyTheme(const QString& themeId) {
    if (!m_themes.contains(themeId)) return;

    m_currentThemeId = themeId;
    m_settings.setValue("currentTheme", themeId);
    emit themeChanged(themeId);
}

Theme ThemeManager::currentTheme() const {
    return m_themes.value(m_currentThemeId, m_themes.value("cute_pink"));
}

QString ThemeManager::currentThemeId() const {
    return m_currentThemeId;
}

QList<Theme> ThemeManager::availableThemes() const {
    return m_themes.values();
}

void ThemeManager::setCustomBackground(const QString& imagePath) {
    if (QFile::exists(imagePath)) {
        m_settings.setValue("customBackground", imagePath);
        emit themeChanged(m_currentThemeId);
    }
}

void ThemeManager::clearCustomBackground() {
    m_settings.remove("customBackground");
    emit themeChanged(m_currentThemeId);
}

QString ThemeManager::customBackgroundPath() const {
    return m_settings.value("customBackground", "").toString();
}

bool ThemeManager::hasCustomBackground() const {
    return QFile::exists(customBackgroundPath());
}

QString ThemeManager::generateQSS() const {
    Theme theme = currentTheme();
    QString qss;

    // 全局样式
    qss += QString(R"(
        QWidget {
            background-color: %1;
            color: %2;
            font-family: "Segoe UI", "Microsoft YaHei UI", sans-serif;
            font-size: 13px;
        }
        QMainWindow {
            background-color: %1;
        }
    )").arg(theme.backgroundColor.name(), theme.textColor.name());

    // 按钮样式
    qss += generateButtonQSS(theme);

    // 输入框样式
    qss += generateInputQSS(theme);

    // 卡片样式
    qss += generateCardQSS(theme);

    // 菜单栏
    qss += QString(R"(
        QMenuBar {
            background-color: %1;
            color: %2;
            border-bottom: 1px solid %3;
            padding: 2px;
        }
        QMenuBar::item {
            background: transparent;
            padding: 6px 12px;
            border-radius: 4px;
        }
        QMenuBar::item:selected {
            background-color: %4;
        }
        QMenu {
            background-color: %1;
            color: %2;
            border: 1px solid %3;
            border-radius: 8px;
            padding: 4px;
        }
        QMenu::item {
            padding: 8px 24px;
            border-radius: 4px;
        }
        QMenu::item:selected {
            background-color: %4;
        }
    )").arg(theme.surfaceColor.name(), theme.textColor.name(),
           theme.mutedColor.name(), theme.primaryColor.name());

    // 状态栏
    qss += QString(R"(
        QStatusBar {
            background-color: %1;
            color: %2;
            border-top: 1px solid %3;
            font-size: 12px;
        }
    )").arg(theme.surfaceColor.name(), theme.mutedColor.name(),
           theme.mutedColor.name());

    // 列表和表格
    qss += QString(R"(
        QListWidget, QTableWidget, QTreeWidget {
            background-color: %1;
            border: 1px solid %2;
            border-radius: 8px;
            padding: 4px;
        }
        QListWidget::item, QTableWidget::item {
            padding: 8px;
            border-radius: 4px;
        }
        QListWidget::item:selected, QTableWidget::item:selected {
            background-color: %3;
            color: white;
        }
    )").arg(theme.cardColor.name(), theme.mutedColor.name(),
           theme.primaryColor.name());

    // 滚动条
    qss += QString(R"(
        QScrollBar:vertical {
            background: %1;
            width: 10px;
            border-radius: 5px;
        }
        QScrollBar::handle:vertical {
            background: %2;
            border-radius: 5px;
            min-height: 30px;
        }
        QScrollBar::handle:vertical:hover {
            background: %3;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0px;
        }
    )").arg(theme.surfaceColor.name(), theme.mutedColor.name(),
           theme.primaryColor.name());

    return qss;
}

QString ThemeManager::generateButtonQSS(const Theme& theme) const {
    return QString(R"(
        QPushButton {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 %1,
                stop:1 %2);
            color: white;
            border: none;
            border-radius: 16px;
            padding: 10px 20px;
            font-weight: 600;
            font-size: 13px;
            min-height: 20px;
        }
        QPushButton:hover {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 %3,
                stop:1 %1);
        }
        QPushButton:pressed {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 %4,
                stop:1 %5);
        }
        QPushButton:disabled {
            background-color: %6;
            color: %7;
        }
        QPushButton#accentButton {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 %3,
                stop:1 %1);
            font-size: 18px;
            border-radius: 22px;
            padding: 14px 32px;
            min-height: 28px;
        }
        QPushButton#accentButton:hover {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 %8,
                stop:1 %3);
        }
        QPushButton#hostButton {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 %9,
                stop:1 %10);
            border-radius: 14px;
        }
        QPushButton#settingsButton {
            background: transparent;
            color: %11;
            border: 2px solid %12;
            border-radius: 14px;
        }
        QPushButton#settingsButton:hover {
            color: %13;
            border-color: %1;
        }
    )").arg(theme.primaryColor.name(),
           theme.secondaryColor.name(),
           theme.accentColor.name(),
           theme.secondaryColor.name(),
           theme.primaryColor.darker(120).name(),
           theme.surfaceColor.name(),
           theme.mutedColor.name(),
           theme.accentColor.lighter(110).name(),
           QColor("#4CAF50").name(),
           QColor("#388E3C").name(),
           theme.mutedColor.name(),
           theme.mutedColor.name(),
           theme.textColor.name());
}

QString ThemeManager::generateInputQSS(const Theme& theme) const {
    return QString(R"(
        QLineEdit, QSpinBox, QComboBox {
            background-color: %1;
            color: %2;
            border: 2px solid %3;
            border-radius: 20px;
            padding: 12px 20px;
            font-size: 16px;
        }
        QLineEdit:focus, QSpinBox:focus, QComboBox:focus {
            border-color: %4;
        }
        QLineEdit::placeholder {
            color: %5;
        }
    )").arg(theme.surfaceColor.name(),
           theme.textColor.name(),
           theme.mutedColor.name(),
           theme.primaryColor.name(),
           theme.mutedColor.lighter(130).name());
}

QString ThemeManager::generateCardQSS(const Theme& theme) const {
    return QString(R"(
        #connectCard {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 %1,
                stop:1 %2);
            border-radius: 20px;
            border: 2px solid %3;
        }
        #deviceCard {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1,
                stop:0 %4,
                stop:1 %2);
            border-radius: 12px;
            border: 2px solid %3;
        }
        #deviceCard:hover {
            border-color: %5;
        }
        #appLogo {
            color: %5;
        }
        #sectionTitle {
            color: %6;
        }
    )").arg(theme.cardColor.name(),
           theme.surfaceColor.name(),
           theme.mutedColor.name(),
           theme.surfaceColor.lighter(110).name(),
           theme.primaryColor.name(),
           theme.mutedColor.name());
}

} // namespace xrk