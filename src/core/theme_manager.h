#pragma once

#include <QObject>
#include <QString>
#include <QColor>
#include <QPixmap>
#include <QMap>
#include <QSettings>

namespace xrk {

struct Theme {
    QString id;
    QString name;
    QString description;
    QColor primaryColor;
    QColor secondaryColor;
    QColor accentColor;
    QColor backgroundColor;
    QColor surfaceColor;
    QColor cardColor;
    QColor textColor;
    QColor mutedColor;
    QString backgroundPixmap;  // 背景图像路径
    bool hasBackground;
};

class ThemeManager : public QObject {
    Q_OBJECT
public:
    static ThemeManager& instance();

    void loadThemes();
    void applyTheme(const QString& themeId);
    Theme currentTheme() const;
    QString currentThemeId() const;
    QList<Theme> availableThemes() const;

    // 自定义背景
    void setCustomBackground(const QString& imagePath);
    void clearCustomBackground();
    QString customBackgroundPath() const;
    bool hasCustomBackground() const;

    // 生成QSS样式表
    QString generateQSS() const;

signals:
    void themeChanged(const QString& themeId);

private:
    ThemeManager();
    void initThemes();
    QString generateButtonQSS(const Theme& theme) const;
    QString generateInputQSS(const Theme& theme) const;
    QString generateCardQSS(const Theme& theme) const;

    QMap<QString, Theme> m_themes;
    QString m_currentThemeId;
    QSettings m_settings;
};

} // namespace xrk