#pragma once
#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QSlider>
#include <QSpinBox>
#include <QLabel>
#include <QJsonObject>

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

private slots:
    void browseDownloadPath();
    void clearCache();
    void clearHistory();
    void clearCookies();
    void saveAndClose();

private:
    QWidget* generalTab();
    QWidget* privacyTab();
    QWidget* appearanceTab();
    QWidget* performanceTab();
    void loadSettings();
    void saveSettings();

    QJsonObject m_settings;

    // General
    QLineEdit* m_homepage = nullptr;
    QCheckBox* m_restoreSession = nullptr;
    QComboBox* m_searchEngine = nullptr;
    QCheckBox* m_suggestions = nullptr;
    QLineEdit* m_downloadPath = nullptr;

    // Privacy
    QCheckBox* m_clearOnExit = nullptr;
    QCheckBox* m_doNotTrack = nullptr;
    QCheckBox* m_adblock = nullptr;
    QCheckBox* m_httpsOnly = nullptr;
    QCheckBox* m_phishing = nullptr;

    // Appearance
    QComboBox* m_theme = nullptr;
    QComboBox* m_accent = nullptr;
    QCheckBox* m_showBookmarksBar = nullptr;
    QCheckBox* m_verticalTabs = nullptr;
    QSlider* m_zoomSlider = nullptr;
    QLabel* m_zoomLabel = nullptr;
    QSpinBox* m_fontSize = nullptr;

    // Performance
    QCheckBox* m_hwAccel = nullptr;
    QCheckBox* m_discardTabs = nullptr;
    QCheckBox* m_smoothScroll = nullptr;
    QCheckBox* m_spellcheck = nullptr;
    QComboBox* m_spellLang = nullptr;
};
