#include "translation_manager.h"
#include "logger.h"
#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>
#include <QSettings>
#include <QLibraryInfo>

namespace xrk {

TranslationManager& TranslationManager::instance() {
    static TranslationManager manager;
    return manager;
}

TranslationManager::TranslationManager(QObject* parent)
    : QObject(parent) {
    m_appTranslator = new QTranslator(this);
    m_qtTranslator = new QTranslator(this);

    QString baseDir = QCoreApplication::applicationDirPath();
    m_dataDir = baseDir;
    if (QDir(baseDir + "/translations").exists()) {
        m_dataDir = baseDir + "/translations";
    } else if (QDir(baseDir + "/../translations").exists()) {
        m_dataDir = baseDir + "/../translations";
    } else if (QDir(baseDir + "/../../translations").exists()) {
        m_dataDir = baseDir + "/../../translations";
    }
}

TranslationManager::~TranslationManager() {
}

void TranslationManager::initialize() {
    QSettings settings("XRK", "LANRemote");
    QString savedLang = settings.value("language", "").toString();

    if (savedLang.isEmpty()) {
        savedLang = systemLanguage();
    }

    setLanguage(savedLang);
    LOG_INFO("TranslationManager initialized, language: " + m_currentLanguage);
}

QString TranslationManager::currentLanguage() const {
    return m_currentLanguage;
}

QString TranslationManager::currentLanguageName() const {
    for (const auto& lang : supportedLanguages()) {
        if (lang.code == m_currentLanguage) {
            return lang.nativeName;
        }
    }
    return "English";
}

QStringList TranslationManager::availableLanguages() const {
    QStringList list;
    for (const auto& lang : supportedLanguages()) {
        list << lang.code;
    }
    return list;
}

bool TranslationManager::setLanguage(const QString& languageCode) {
    if (m_currentLanguage == languageCode) {
        return true;
    }

    bool found = false;
    for (const auto& lang : supportedLanguages()) {
        if (lang.code == languageCode) {
            found = true;
            break;
        }
    }
    if (!found) {
        LOG_WARNING("Unsupported language: " + languageCode);
        return false;
    }

    if (m_appTranslator && !m_appTranslator->isEmpty()) {
        QCoreApplication::removeTranslator(m_appTranslator);
        delete m_appTranslator;
        m_appTranslator = new QTranslator(this);
    }
    if (m_qtTranslator && !m_qtTranslator->isEmpty()) {
        QCoreApplication::removeTranslator(m_qtTranslator);
        delete m_qtTranslator;
        m_qtTranslator = new QTranslator(this);
    }

    m_currentLanguage = languageCode;
    loadTranslation(languageCode);
    installQtTranslation(languageCode);

    QSettings settings("XRK", "LANRemote");
    settings.setValue("language", languageCode);

    LOG_INFO("Language changed to: " + languageCode);
    emit languageChanged(languageCode);
    return true;
}

QString TranslationManager::systemLanguage() const {
    QLocale locale = QLocale::system();
    QLocale::Language lang = locale.language();

    switch (lang) {
        case QLocale::Chinese:
            if (locale.script() == QLocale::TraditionalChineseScript)
                return "zh_TW";
            return "zh_CN";
        case QLocale::English:
            return "en_US";
        case QLocale::Japanese:
            return "ja_JP";
        case QLocale::Korean:
            return "ko_KR";
        case QLocale::French:
            return "fr_FR";
        case QLocale::German:
            return "de_DE";
        case QLocale::Spanish:
            return "es_ES";
        case QLocale::Russian:
            return "ru_RU";
        case QLocale::Portuguese:
            return "pt_BR";
        default:
            return "en_US";
    }
}

void TranslationManager::loadTranslation(const QString& lang) {
    QString fileName = "xrk_" + lang + ".qm";
    QString filePath = m_dataDir + "/" + fileName;

    if (QFile::exists(filePath)) {
        if (m_appTranslator->load(filePath)) {
            QCoreApplication::installTranslator(m_appTranslator);
            LOG_INFO("Loaded app translation: " + filePath);
        } else {
            LOG_WARNING("Failed to load translation: " + filePath);
        }
    } else {
        LOG_DEBUG("Translation file not found: " + filePath);
    }
}

void TranslationManager::installQtTranslation(const QString& lang) {
    QLocale locale(lang);
    QString qtTranslationPath = QLibraryInfo::path(QLibraryInfo::TranslationsPath);

    if (m_qtTranslator->load(locale, "qt", "_", qtTranslationPath)) {
        QCoreApplication::installTranslator(m_qtTranslator);
        LOG_INFO("Loaded Qt translation for: " + lang);
    }
}

const QVector<TranslationManager::LanguageInfo>& TranslationManager::supportedLanguages() {
    static const QVector<LanguageInfo> languages = {
        {"en_US", "English",    "English"},
        {"zh_CN", "Chinese",    "简体中文"},
        {"zh_TW", "Chinese",    "繁體中文"},
        {"ja_JP", "Japanese",   "日本語"},
        {"ko_KR", "Korean",     "한국어"},
        {"fr_FR", "French",     "Français"},
        {"de_DE", "German",     "Deutsch"},
        {"es_ES", "Spanish",    "Español"},
        {"ru_RU", "Russian",    "Русский"},
        {"pt_BR", "Portuguese", "Português"}
    };
    return languages;
}

} // namespace xrk
