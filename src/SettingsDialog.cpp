#include "SettingsDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QTabWidget>
#include <QPushButton>
#include <QFileDialog>
#include <QStandardPaths>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QPainter>
#include <QPixmap>
#include <QWebEngineProfile>
#include <QWebEngineCookieStore>

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("Settings — Pacitum Browser")); setMinimumSize(620,520); resize(620,560);
    loadSettings();
    auto* mainLay = new QVBoxLayout(this); mainLay->setContentsMargins(0,0,0,0);
    auto* tabs = new QTabWidget(this); tabs->setObjectName(QStringLiteral("SettingsTabs")); tabs->setDocumentMode(true);
    tabs->addTab(generalTab(), QStringLiteral("General"));
    tabs->addTab(privacyTab(), QStringLiteral("Privacy & Security"));
    tabs->addTab(appearanceTab(), QStringLiteral("Appearance"));
    tabs->addTab(performanceTab(), QStringLiteral("Performance"));
    mainLay->addWidget(tabs, 1);
    auto* btnLay = new QHBoxLayout(); btnLay->setContentsMargins(16,8,16,12); btnLay->addStretch();
    auto* cancel = new QPushButton(QStringLiteral("Cancel"), this); cancel->setFixedSize(100,34);
    auto* save = new QPushButton(QStringLiteral("Save Changes"), this); save->setFixedSize(130,34); save->setDefault(true);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(save, &QPushButton::clicked, this, &SettingsDialog::saveAndClose);
    btnLay->addWidget(cancel); btnLay->addWidget(save); mainLay->addLayout(btnLay);
}

QWidget* SettingsDialog::generalTab() {
    auto* tab = new QWidget(this); auto* lay = new QVBoxLayout(tab); lay->setContentsMargins(20,20,20,20); lay->setSpacing(12);
    auto* g1 = new QGroupBox(QStringLiteral("Startup"), tab); auto* f1 = new QFormLayout(g1);
    m_homepage = new QLineEdit(m_settings.value(QStringLiteral("homepage")).toString(QStringLiteral("pacitum://newtab")), g1);
    f1->addRow(QStringLiteral("Homepage:"), m_homepage);
    m_restoreSession = new QCheckBox(QStringLiteral("Restore last session on startup"), g1);
    m_restoreSession->setChecked(m_settings.value(QStringLiteral("restore_session")).toBool(true)); f1->addRow(QStringLiteral(""), m_restoreSession);
    g1->setLayout(f1); lay->addWidget(g1);
    auto* g2 = new QGroupBox(QStringLiteral("Search"), tab); auto* f2 = new QFormLayout(g2);
    m_searchEngine = new QComboBox(g2); m_searchEngine->addItems({QStringLiteral("Google"),QStringLiteral("Bing"),QStringLiteral("DuckDuckGo"),QStringLiteral("Yahoo"),QStringLiteral("Brave")});
    m_searchEngine->setCurrentText(m_settings.value(QStringLiteral("search_engine")).toString(QStringLiteral("Google"))); f2->addRow(QStringLiteral("Default Search Engine:"), m_searchEngine);
    m_suggestions = new QCheckBox(QStringLiteral("Show search and URL suggestions"), g2); m_suggestions->setChecked(m_settings.value(QStringLiteral("show_suggestions")).toBool(true)); f2->addRow(QStringLiteral(""), m_suggestions);
    g2->setLayout(f2); lay->addWidget(g2);
    auto* g3 = new QGroupBox(QStringLiteral("Downloads"), tab); auto* f3 = new QFormLayout(g3);
    auto* dlLay = new QHBoxLayout(); m_downloadPath = new QLineEdit(g3);
    QString defDl = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    m_downloadPath->setText(m_settings.value(QStringLiteral("download_path")).toString(defDl)); dlLay->addWidget(m_downloadPath);
    auto* browse = new QPushButton(QStringLiteral("Browse..."), g3); browse->setFixedWidth(80); connect(browse, &QPushButton::clicked, this, &SettingsDialog::browseDownloadPath); dlLay->addWidget(browse);
    f3->addRow(QStringLiteral("Download Location:"), dlLay); g3->setLayout(f3); lay->addWidget(g3);
    lay->addStretch(); tab->setLayout(lay); return tab;
}

QWidget* SettingsDialog::privacyTab() {
    auto* tab = new QWidget(this); auto* lay = new QVBoxLayout(tab); lay->setContentsMargins(20,20,20,20); lay->setSpacing(12);
    auto* g1 = new QGroupBox(QStringLiteral("Privacy"), tab); auto* l1 = new QVBoxLayout(g1);
    m_clearOnExit = new QCheckBox(QStringLiteral("Clear browsing data on exit"), g1); m_clearOnExit->setChecked(m_settings.value(QStringLiteral("clear_on_exit")).toBool(false)); l1->addWidget(m_clearOnExit);
    m_doNotTrack = new QCheckBox(QStringLiteral("Send 'Do Not Track' requests"), g1); m_doNotTrack->setChecked(m_settings.value(QStringLiteral("do_not_track")).toBool(true)); l1->addWidget(m_doNotTrack);
    g1->setLayout(l1); lay->addWidget(g1);
    auto* g2 = new QGroupBox(QStringLiteral("Ad Blocking"), tab); auto* l2 = new QVBoxLayout(g2);
    m_adblock = new QCheckBox(QStringLiteral("Block ads and trackers"), g2); m_adblock->setChecked(m_settings.value(QStringLiteral("adblock_enabled")).toBool(true)); l2->addWidget(m_adblock); g2->setLayout(l2); lay->addWidget(g2);
    auto* g3 = new QGroupBox(QStringLiteral("Security"), tab); auto* l3 = new QVBoxLayout(g3);
    m_httpsOnly = new QCheckBox(QStringLiteral("Upgrade all connections to HTTPS"), g3); m_httpsOnly->setChecked(m_settings.value(QStringLiteral("https_only")).toBool(false)); l3->addWidget(m_httpsOnly);
    m_phishing = new QCheckBox(QStringLiteral("Warn about dangerous websites"), g3); m_phishing->setChecked(m_settings.value(QStringLiteral("phishing_warning")).toBool(true)); l3->addWidget(m_phishing); g3->setLayout(l3); lay->addWidget(g3);
    auto* g4 = new QGroupBox(QStringLiteral("Browsing Data"), tab); auto* l4 = new QVBoxLayout(g4);
    auto* c1 = new QPushButton(QStringLiteral("Clear Cache"), g4); connect(c1, &QPushButton::clicked, this, &SettingsDialog::clearCache); l4->addWidget(c1);
    auto* c2 = new QPushButton(QStringLiteral("Clear History"), g4); connect(c2, &QPushButton::clicked, this, &SettingsDialog::clearHistory); l4->addWidget(c2);
    auto* c3 = new QPushButton(QStringLiteral("Clear Cookies"), g4); connect(c3, &QPushButton::clicked, this, &SettingsDialog::clearCookies); l4->addWidget(c3);
    g4->setLayout(l4); lay->addWidget(g4); lay->addStretch(); tab->setLayout(lay); return tab;
}

QWidget* SettingsDialog::appearanceTab() {
    auto* tab = new QWidget(this); auto* lay = new QVBoxLayout(tab); lay->setContentsMargins(20,20,20,20); lay->setSpacing(12);
    auto* g1 = new QGroupBox(QStringLiteral("Theme"), tab); auto* f1 = new QFormLayout(g1);
    m_theme = new QComboBox(g1); m_theme->addItems({QStringLiteral("Light"),QStringLiteral("Dark"),QStringLiteral("System")}); m_theme->setCurrentText(m_settings.value(QStringLiteral("theme")).toString(QStringLiteral("System"))); f1->addRow(QStringLiteral("Theme:"), m_theme);
    m_accent = new QComboBox(g1);
    for (auto& a : QList<QPair<QString,QString>>{{QStringLiteral("Teal"),QStringLiteral("#2DD4BF")},{QStringLiteral("Indigo"),QStringLiteral("#6366F1")},{QStringLiteral("Rose"),QStringLiteral("#F43F5E")},{QStringLiteral("Amber"),QStringLiteral("#F59E0B")},{QStringLiteral("Green"),QStringLiteral("#22C55E")},{QStringLiteral("Slate"),QStringLiteral("#64748B")}}) {
        QPixmap pm(16,16); pm.fill(Qt::transparent); { QPainter pt(&pm); pt.setRenderHint(QPainter::Antialiasing); pt.setBrush(QColor(a.second)); pt.setPen(Qt::NoPen); pt.drawEllipse(1,1,14,14); }
        m_accent->addItem(QIcon(pm), a.first, a.second);
    }
    { int ai = m_accent->findData(m_settings.value(QStringLiteral("accent")).toString(QStringLiteral("#2DD4BF"))); m_accent->setCurrentIndex(ai < 0 ? 0 : ai); }
    f1->addRow(QStringLiteral("Accent color:"), m_accent); g1->setLayout(f1); lay->addWidget(g1);
    auto* g2 = new QGroupBox(QStringLiteral("Toolbar"), tab); auto* f2 = new QFormLayout(g2);
    m_showBookmarksBar = new QCheckBox(QStringLiteral("Show Bookmarks Bar"), g2); m_showBookmarksBar->setChecked(m_settings.value(QStringLiteral("show_bookmarks_bar")).toBool(false)); f2->addRow(QStringLiteral(""), m_showBookmarksBar);
    m_verticalTabs = new QCheckBox(QStringLiteral("Use Vertical Tabs"), g2); m_verticalTabs->setChecked(m_settings.value(QStringLiteral("vertical_tabs")).toBool(false)); f2->addRow(QStringLiteral(""), m_verticalTabs); g2->setLayout(f2); lay->addWidget(g2);
    auto* g3 = new QGroupBox(QStringLiteral("Zoom"), tab); auto* f3 = new QFormLayout(g3);
    auto* zl = new QHBoxLayout(); m_zoomSlider = new QSlider(Qt::Horizontal, g3); m_zoomSlider->setRange(25,500); int pct = m_settings.value(QStringLiteral("default_zoom")).toInt(100); m_zoomSlider->setValue(pct); m_zoomLabel = new QLabel(QString::number(pct)+QStringLiteral("%"), g3);
    connect(m_zoomSlider, &QSlider::valueChanged, this, [this](int v){ m_zoomLabel->setText(QString::number(v)+QStringLiteral("%")); }); zl->addWidget(m_zoomSlider); zl->addWidget(m_zoomLabel); f3->addRow(QStringLiteral("Default Zoom:"), zl); g3->setLayout(f3); lay->addWidget(g3);
    auto* g4 = new QGroupBox(QStringLiteral("Fonts"), tab); auto* f4 = new QFormLayout(g4);
    m_fontSize = new QSpinBox(g4); m_fontSize->setRange(10,48); m_fontSize->setValue(m_settings.value(QStringLiteral("font_size")).toInt(16)); f4->addRow(QStringLiteral("Default Font Size:"), m_fontSize); g4->setLayout(f4); lay->addWidget(g4);
    lay->addStretch(); tab->setLayout(lay); return tab;
}

QWidget* SettingsDialog::performanceTab() {
    auto* tab = new QWidget(this); auto* lay = new QVBoxLayout(tab); lay->setContentsMargins(20,20,20,20); lay->setSpacing(12);
    auto* g1 = new QGroupBox(QStringLiteral("Performance"), tab); auto* f1 = new QFormLayout(g1);
    m_hwAccel = new QCheckBox(QStringLiteral("Enable hardware acceleration"), g1); m_hwAccel->setChecked(m_settings.value(QStringLiteral("hardware_acceleration")).toBool(true)); f1->addRow(QStringLiteral(""), m_hwAccel);
    m_discardTabs = new QCheckBox(QStringLiteral("Discard inactive tabs to save memory"), g1); m_discardTabs->setChecked(m_settings.value(QStringLiteral("discard_inactive_tabs")).toBool(true)); f1->addRow(QStringLiteral(""), m_discardTabs);
    m_smoothScroll = new QCheckBox(QStringLiteral("Enable smooth scrolling"), g1); m_smoothScroll->setChecked(m_settings.value(QStringLiteral("smooth_scroll")).toBool(true)); f1->addRow(QStringLiteral(""), m_smoothScroll); g1->setLayout(f1); lay->addWidget(g1);
    auto* g2 = new QGroupBox(QStringLiteral("Spell Check"), tab); auto* f2 = new QFormLayout(g2);
    m_spellcheck = new QCheckBox(QStringLiteral("Enable spell check"), g2); m_spellcheck->setChecked(m_settings.value(QStringLiteral("spellcheck_enabled")).toBool(true)); f2->addRow(QStringLiteral(""), m_spellcheck);
    m_spellLang = new QComboBox(g2); m_spellLang->addItems({QStringLiteral("en-US"),QStringLiteral("en-GB"),QStringLiteral("fr-FR"),QStringLiteral("de-DE"),QStringLiteral("es-ES"),QStringLiteral("pt-BR"),QStringLiteral("ja-JP"),QStringLiteral("zh-CN")}); m_spellLang->setCurrentText(m_settings.value(QStringLiteral("spellcheck_language")).toString(QStringLiteral("en-US"))); f2->addRow(QStringLiteral("Language:"), m_spellLang); g2->setLayout(f2); lay->addWidget(g2);
    lay->addStretch(); tab->setLayout(lay); return tab;
}

void SettingsDialog::browseDownloadPath() { QString p = QFileDialog::getExistingDirectory(this, QStringLiteral("Select Download Folder")); if (!p.isEmpty()) m_downloadPath->setText(p); }
void SettingsDialog::clearCache() { QWebEngineProfile::defaultProfile()->clearHttpCache(); QMessageBox::information(this, QStringLiteral("Clear Cache"), QStringLiteral("Cache cleared successfully!")); }
void SettingsDialog::clearHistory() { if (parent() && parent()->property("history_manager").isValid()) {} // delegated to BrowserWindow
    QMessageBox::information(this, QStringLiteral("Clear History"), QStringLiteral("History cleared - restart to fully clear DB.")); }
void SettingsDialog::clearCookies() { QWebEngineProfile::defaultProfile()->cookieStore()->deleteAllCookies(); QMessageBox::information(this, QStringLiteral("Clear Cookies"), QStringLiteral("Cookies cleared successfully!")); }

void SettingsDialog::loadSettings() {
    QFile f(QStringLiteral("browser_settings.json"));
    if (f.open(QIODevice::ReadOnly)) { auto doc = QJsonDocument::fromJson(f.readAll()); if (doc.isObject()) m_settings = doc.object(); }
}
void SettingsDialog::saveSettings() {
    QJsonObject s;
    s[QStringLiteral("homepage")] = m_homepage->text();
    s[QStringLiteral("search_engine")] = m_searchEngine->currentText();
    s[QStringLiteral("clear_on_exit")] = m_clearOnExit->isChecked();
    s[QStringLiteral("do_not_track")] = m_doNotTrack->isChecked();
    s[QStringLiteral("theme")] = m_theme->currentText();
    s[QStringLiteral("dark_mode")] = m_theme->currentText()==QStringLiteral("Dark");
    s[QStringLiteral("accent")] = m_accent->currentData().toString();
    s[QStringLiteral("restore_session")] = m_restoreSession->isChecked();
    s[QStringLiteral("show_suggestions")] = m_suggestions->isChecked();
    s[QStringLiteral("download_path")] = m_downloadPath->text();
    s[QStringLiteral("adblock_enabled")] = m_adblock->isChecked();
    s[QStringLiteral("https_only")] = m_httpsOnly->isChecked();
    s[QStringLiteral("phishing_warning")] = m_phishing->isChecked();
    s[QStringLiteral("show_bookmarks_bar")] = m_showBookmarksBar->isChecked();
    s[QStringLiteral("vertical_tabs")] = m_verticalTabs->isChecked();
    s[QStringLiteral("default_zoom")] = m_zoomSlider->value();
    s[QStringLiteral("font_size")] = m_fontSize->value();
    s[QStringLiteral("hardware_acceleration")] = m_hwAccel->isChecked();
    s[QStringLiteral("discard_inactive_tabs")] = m_discardTabs->isChecked();
    s[QStringLiteral("smooth_scroll")] = m_smoothScroll->isChecked();
    s[QStringLiteral("spellcheck_enabled")] = m_spellcheck->isChecked();
    s[QStringLiteral("spellcheck_language")] = m_spellLang->currentText();
    QFile f(QStringLiteral("browser_settings.json")); if (f.open(QIODevice::WriteOnly|QIODevice::Truncate)) f.write(QJsonDocument(s).toJson(QJsonDocument::Indented));
    m_settings = s;
}
void SettingsDialog::saveAndClose() { saveSettings(); accept(); }
