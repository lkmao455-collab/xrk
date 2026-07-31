#pragma once

#include <QObject>
#include <QTranslator>
#include <QLocale>
#include <QString>
#include <QStringList>

namespace xrk {

class TranslationManager : public QObject {
    Q_OBJECT
public:
    static TranslationManager& instance();

    void initialize();
    QString currentLanguage() const;
    QString currentLanguageName() const;
    QStringList availableLanguages() const;
    bool setLanguage(const QString& languageCode);
    QString systemLanguage() const;

signals:
    void languageChanged(const QString& languageCode);

private:
    TranslationManager(QObject* parent = nullptr);
    ~TranslationManager();
    TranslationManager(const TranslationManager&) = delete;
    TranslationManager& operator=(const TranslationManager&) = delete;

    void loadTranslation(const QString& lang);
    void installQtTranslation(const QString& lang);

    QTranslator* m_appTranslator = nullptr;
    QTranslator* m_qtTranslator = nullptr;
    QString m_currentLanguage;
    QString m_dataDir;

    struct LanguageInfo {
        QString code;
        QString name;
        QString nativeName;
    };
    static const QVector<LanguageInfo>& supportedLanguages();
};

} // namespace xrk
