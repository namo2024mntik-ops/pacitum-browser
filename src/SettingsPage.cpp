#include "SettingsPage.h"
#include "ThemeManager.h"
#include "Widgets.h"
#include <QListWidget>
#include <QStackedWidget>
#include <QScrollArea>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QFileDialog>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QFile>
#include <QTimer>
#include <QCoreApplication>
#include <QWebEngineProfile>
#include <QtWebEngineCore/qtwebenginecoreglobal.h>
#include "WebProfile.h"

static const char* kFile = "browser_settings.json";

QJsonObject SettingsPage::read() {
    QFile f(QString::fromLatin1(kFile));
    if (!f.open(QIODevice::ReadOnly)) return {};
    const auto d = QJsonDocument::fromJson(f.readAll());
    return d.isObject() ? d.object() : QJsonObject{};
}

void SettingsPage::write(const QString& key, const QJsonValue& v) {
    QJsonObject s = read();
    s[key] = v;
    QFile f(QString::fromLatin1(kFile));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(QJsonDocument(s).toJson(QJsonDocument::Indented));
}

SettingsPage::SettingsPage(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("SettingsPage"));
    setAttribute(Qt::WA_StyledBackground, true);
    m_s = read();
    auto* lay = new QHBoxLayout(this); lay->setContentsMargins(0,0,0,0); lay->setSpacing(0);

    auto* navWrap = new QWidget(this); navWrap->setFixedWidth(260);
    navWrap->setObjectName(QStringLiteral("SettingsContent")); navWrap->setAttribute(Qt::WA_StyledBackground, true);
    auto* nl = new QVBoxLayout(navWrap); nl->setContentsMargins(0,20,0,12); nl->setSpacing(8);
    auto* h1 = new QLabel(QStringLiteral("Settings"), navWrap); h1->setObjectName(QStringLiteral("SettingsH1")); h1->setContentsMargins(24,0,0,8);
    nl->addWidget(h1);
    m_nav = new QListWidget(navWrap); m_nav->setObjectName(QStringLiteral("SettingsNav"));
    m_nav->setIconSize(QSize(18,18)); m_nav->setFrameShape(QFrame::NoFrame);
    nl->addWidget(m_nav, 1);
    lay->addWidget(navWrap);

    m_stack = new QStackedWidget(this);
    lay->addWidget(m_stack, 1);

    struct S { const char* icon; const char* name; QWidget* (SettingsPage::*fn)(); };
    const S sections[] = {
        {"settings-appearance", "Appearance", &SettingsPage::appearancePage},
        {"settings-search", "Search engine", &SettingsPage::searchPage},
        {"settings-startup", "On startup", &SettingsPage::startupPage},
        {"settings-privacy", "Privacy and security", &SettingsPage::privacyPage},
        {"site-info", "Site settings", &SettingsPage::contentPage},
        {"download", "Downloads", &SettingsPage::downloadsPage},
        {"settings-system", "About Pacitum", &SettingsPage::aboutPage},
    };
    const auto& pal = ThemeManager::current();
    for (const auto& sct : sections) {
        auto* it = new QListWidgetItem(ThemeManager::icon(QString::fromLatin1(sct.icon), pal.textSecondary, 18), QString::fromLatin1(sct.name), m_nav);
        it->setSizeHint(QSize(0, 38));
        m_stack->addWidget((this->*sct.fn)());
    }
    connect(m_nav, &QListWidget::currentRowChanged, m_stack, &QStackedWidget::setCurrentIndex);
    m_nav->setCurrentRow(0);
}

void SettingsPage::showSection(const QString& name) {
    for (int i = 0; i < m_nav->count(); ++i)
        if (m_nav->item(i)->text().startsWith(name, Qt::CaseInsensitive)) { m_nav->setCurrentRow(i); return; }
}

QWidget* SettingsPage::section(const QString& title, QVBoxLayout** outLayout) {
    auto* scroll = new QScrollArea(this); scroll->setObjectName(QStringLiteral("SettingsScroll"));
    scroll->setWidgetResizable(true); scroll->setFrameShape(QFrame::NoFrame);
    auto* inner = new QWidget(scroll); inner->setObjectName(QStringLiteral("SettingsContent"));
    auto* outer = new QHBoxLayout(inner); outer->setContentsMargins(48,24,48,24);
    auto* col = new QWidget(inner); col->setMaximumWidth(760);
    auto* l = new QVBoxLayout(col); l->setContentsMargins(0,0,0,0); l->setSpacing(10);
    auto* h = new QLabel(title, col); h->setObjectName(QStringLiteral("SettingsH1"));
    l->addWidget(h); l->addSpacing(6);
    outer->addWidget(col, 1); outer->addStretch(0);
    scroll->setWidget(inner);
    *outLayout = l;
    QTimer::singleShot(0, l, [l]{ l->addStretch(1); });
    return scroll;
}

QWidget* SettingsPage::card(QVBoxLayout* parentLayout, const QString& heading) {
    if (!heading.isEmpty()) {
        auto* h2 = new QLabel(heading); h2->setObjectName(QStringLiteral("SettingsH2"));
        parentLayout->addWidget(h2);
    }
    auto* c = new QFrame(); c->setObjectName(QStringLiteral("Card"));
    auto* l = new QVBoxLayout(c); l->setContentsMargins(0,0,0,0); l->setSpacing(0);
    parentLayout->addWidget(c);
    parentLayout->addSpacing(14);
    return c;
}

void SettingsPage::addRow(QWidget* c, const QString& title, const QString& sub, QWidget* control) {
    auto* l = static_cast<QVBoxLayout*>(c->layout());
    if (l->count() > 0) { auto* d = new QFrame(c); d->setObjectName(QStringLiteral("Divider")); l->addWidget(d); }
    auto* row = new QWidget(c); row->setMinimumHeight(56);
    auto* rl = new QHBoxLayout(row); rl->setContentsMargins(20,10,20,10); rl->setSpacing(16);
    auto* texts = new QVBoxLayout(); texts->setSpacing(2);
    auto* t = new QLabel(title, row); t->setWordWrap(true); texts->addWidget(t);
    if (!sub.isEmpty()) { auto* s = new QLabel(sub, row); s->setObjectName(QStringLiteral("RowSub")); s->setWordWrap(true); texts->addWidget(s); }
    rl->addLayout(texts, 1);
    if (control) { control->setParent(row); rl->addWidget(control, 0, Qt::AlignVCenter); }
    l->addWidget(row);
}

QWidget* SettingsPage::makeSwitch(const QString& key, bool def) {
    auto* sw = new ToggleSwitch();
    sw->setChecked(m_s.value(key).toBool(def));
    connect(sw, &ToggleSwitch::toggled, this, [this, key](bool on){ write(key, on); emit settingChanged(key); });
    return sw;
}

QWidget* SettingsPage::appearancePage() {
    QVBoxLayout* l; QWidget* page = section(QStringLiteral("Appearance"), &l);
    auto* c = card(l, QString());

    auto* seg = new QFrame(); seg->setObjectName(QStringLiteral("Segmented"));
    auto* sl = new QHBoxLayout(seg); sl->setContentsMargins(2,2,2,2); sl->setSpacing(2);
    auto* grp = new QButtonGroup(seg);
    const QString cur = m_s.value(QStringLiteral("theme")).toString(QStringLiteral("System"));
    for (const QString& t : {QStringLiteral("System"), QStringLiteral("Light"), QStringLiteral("Dark")}) {
        auto* b = new QPushButton(t, seg); b->setCheckable(true); b->setChecked(t == cur); b->setCursor(Qt::PointingHandCursor);
        grp->addButton(b); sl->addWidget(b);
        connect(b, &QPushButton::clicked, this, [this, t]{
            write(QStringLiteral("theme"), t); write(QStringLiteral("dark_mode"), t == QStringLiteral("Dark"));
            emit settingChanged(QStringLiteral("theme")); });
    }
    addRow(c, QStringLiteral("Theme"), QStringLiteral("Follow the system color scheme or choose light/dark"), seg);

    auto* sw = new QWidget(); auto* swl = new QHBoxLayout(sw); swl->setContentsMargins(0,0,0,0); swl->setSpacing(8);
    auto* sg = new QButtonGroup(sw);
    const QString acc = m_s.value(QStringLiteral("accent")).toString(QStringLiteral("#2DD4BF"));
    for (const QString& col : {QStringLiteral("#2DD4BF"), QStringLiteral("#6366F1"), QStringLiteral("#F43F5E"),
                               QStringLiteral("#F59E0B"), QStringLiteral("#22C55E"), QStringLiteral("#64748B")}) {
        auto* b = new SwatchButton(col, sw); b->setChecked(col.compare(acc, Qt::CaseInsensitive) == 0);
        sg->addButton(b); swl->addWidget(b);
        connect(b, &SwatchButton::clicked, this, [this, col]{ write(QStringLiteral("accent"), col); emit settingChanged(QStringLiteral("accent")); });
    }
    addRow(c, QStringLiteral("Accent color"), QStringLiteral("Used for focus rings, toggles and selection"), sw);

    addRow(c, QStringLiteral("Show bookmarks bar"), QStringLiteral("Ctrl+Shift+B"), makeSwitch(QStringLiteral("show_bookmarks_bar"), true));
    addRow(c, QStringLiteral("Animations"), QStringLiteral("Tab, button and page transitions"), makeSwitch(QStringLiteral("animations"), true));

    auto* zoom = new QComboBox(); zoom->setMinimumWidth(160);
    const int curZoom = m_s.value(QStringLiteral("default_zoom")).toInt(100);
    for (int z : {50, 67, 75, 80, 90, 100, 110, 125, 150, 175, 200}) zoom->addItem(QStringLiteral("%1%").arg(z), z);
    { int i = zoom->findData(curZoom); zoom->setCurrentIndex(i < 0 ? zoom->findData(100) : i); }
    connect(zoom, &QComboBox::currentIndexChanged, this, [this, zoom]{ write(QStringLiteral("default_zoom"), zoom->currentData().toInt()); emit settingChanged(QStringLiteral("default_zoom")); });
    addRow(c, QStringLiteral("Page zoom"), QStringLiteral("Default zoom for all pages"), zoom);
    return page;
}

QWidget* SettingsPage::searchPage() {
    QVBoxLayout* l; QWidget* page = section(QStringLiteral("Search engine"), &l);
    auto* c = card(l, QString());
    auto* cb = new QComboBox(); cb->setMinimumWidth(180);
    cb->addItems({QStringLiteral("Google"), QStringLiteral("Bing"), QStringLiteral("DuckDuckGo"), QStringLiteral("Yahoo"), QStringLiteral("Brave")});
    cb->setCurrentText(m_s.value(QStringLiteral("search_engine")).toString(QStringLiteral("Google")));
    connect(cb, &QComboBox::currentTextChanged, this, [this](const QString& t){ write(QStringLiteral("search_engine"), t); emit settingChanged(QStringLiteral("search_engine")); });
    addRow(c, QStringLiteral("Search engine used in the address bar"), QStringLiteral("Also used by the New Tab page search box"), cb);
    addRow(c, QStringLiteral("Show suggestions"), QStringLiteral("Suggest pages from your history while typing"), makeSwitch(QStringLiteral("show_suggestions"), true));
    return page;
}

QWidget* SettingsPage::startupPage() {
    QVBoxLayout* l; QWidget* page = section(QStringLiteral("On startup"), &l);
    auto* c = card(l, QString());
    auto* cb = new QComboBox(); cb->setMinimumWidth(220);
    cb->addItem(QStringLiteral("Open the New Tab page"), QStringLiteral("newtab"));
    cb->addItem(QStringLiteral("Continue where you left off"), QStringLiteral("restore"));
    cb->addItem(QStringLiteral("Open the home page"), QStringLiteral("homepage"));
    QString cur = m_s.value(QStringLiteral("startup")).toString();
    if (cur.isEmpty()) cur = m_s.value(QStringLiteral("restore_session")).toBool(true) ? QStringLiteral("restore") : QStringLiteral("newtab");
    cb->setCurrentIndex(qMax(0, cb->findData(cur)));
    connect(cb, &QComboBox::currentIndexChanged, this, [this, cb]{
        const QString v = cb->currentData().toString();
        write(QStringLiteral("startup"), v); write(QStringLiteral("restore_session"), v == QStringLiteral("restore"));
        emit settingChanged(QStringLiteral("startup")); });
    addRow(c, QStringLiteral("When Pacitum starts"), QString(), cb);

    auto* home = new QLineEdit(); home->setMinimumWidth(280);
    home->setPlaceholderText(QStringLiteral("pacitum://newtab"));
    home->setText(m_s.value(QStringLiteral("homepage")).toString(QStringLiteral("pacitum://newtab")));
    connect(home, &QLineEdit::editingFinished, this, [this, home]{
        QString v = home->text().trimmed(); if (v.isEmpty()) v = QStringLiteral("pacitum://newtab");
        write(QStringLiteral("homepage"), v); emit settingChanged(QStringLiteral("homepage")); });
    addRow(c, QStringLiteral("Home page"), QStringLiteral("Opened by the Home button and by \"Open the home page\""), home);
    return page;
}

QWidget* SettingsPage::privacyPage() {
    QVBoxLayout* l; QWidget* page = section(QStringLiteral("Privacy and security"), &l);
    auto* c = card(l, QStringLiteral("Clear browsing data"));
    auto* hist = new ToggleSwitch(); hist->setChecked(true);
    auto* cook = new ToggleSwitch(); cook->setChecked(true);
    auto* cache = new ToggleSwitch(); cache->setChecked(true);
    addRow(c, QStringLiteral("Browsing history"), QStringLiteral("Pages you visited and address bar suggestions"), hist);
    addRow(c, QStringLiteral("Cookies and other site data"), QStringLiteral("Signs you out of most sites"), cook);
    addRow(c, QStringLiteral("Cached images and files"), QStringLiteral("Frees up space; some sites may load slower next time"), cache);
    auto* btnRow = new QWidget(); auto* bl = new QHBoxLayout(btnRow); bl->setContentsMargins(0,0,0,0); bl->setSpacing(10);
    auto* status = new QLabel(btnRow); status->setObjectName(QStringLiteral("RowSub"));
    auto* clear = new QPushButton(QStringLiteral("Clear data"), btnRow); clear->setObjectName(QStringLiteral("PrimaryButton")); clear->setCursor(Qt::PointingHandCursor);
    bl->addWidget(status); bl->addWidget(clear);
    connect(clear, &QPushButton::clicked, this, [this, hist, cook, cache, status]{
        emit clearDataRequested(hist->isChecked(), cook->isChecked(), cache->isChecked());
        status->setText(QStringLiteral("Cleared"));
        QTimer::singleShot(3000, status, [status]{ status->clear(); });
    });
    addRow(c, QStringLiteral("Clear selected data now"), QString(), btnRow);

    auto* c2 = card(l, QStringLiteral("Security"));
    addRow(c2, QStringLiteral("Block ads and trackers"), QStringLiteral("Built-in blocker for common ad and tracking hosts"), makeSwitch(QStringLiteral("adblock_enabled"), true));
    addRow(c2, QStringLiteral("Send a \"Do Not Track\" request"), QStringLiteral("Adds DNT: 1 and Sec-GPC: 1 headers to your requests"), makeSwitch(QStringLiteral("do_not_track"), false));
    addRow(c2, QStringLiteral("Clear history, cookies and cache on exit"), QString(), makeSwitch(QStringLiteral("clear_on_exit"), false));
    return page;
}

QWidget* SettingsPage::contentPage() {
    QVBoxLayout* l; QWidget* page = section(QStringLiteral("Site settings"), &l);
    auto* c = card(l, QString());
    addRow(c, QStringLiteral("JavaScript"), QStringLiteral("Sites can use JavaScript (most sites need it)"), makeSwitch(QStringLiteral("javascript_enabled"), true));
    addRow(c, QStringLiteral("Pop-ups and redirects"), QStringLiteral("Allow sites to open new windows from scripts (links with target=_blank always open in a new tab)"), makeSwitch(QStringLiteral("popups_allowed"), false));
    return page;
}

QWidget* SettingsPage::downloadsPage() {
    QVBoxLayout* l; QWidget* page = section(QStringLiteral("Downloads"), &l);
    auto* c = card(l, QString());
    const QString def = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    auto* w = new QWidget(); auto* wl = new QHBoxLayout(w); wl->setContentsMargins(0,0,0,0); wl->setSpacing(10);
    auto* path = new QLabel(m_s.value(QStringLiteral("download_path")).toString(def), w); path->setObjectName(QStringLiteral("RowSub"));
    path->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* change = new QPushButton(QStringLiteral("Change"), w); change->setCursor(Qt::PointingHandCursor);
    wl->addWidget(path); wl->addWidget(change);
    connect(change, &QPushButton::clicked, this, [this, path]{
        const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Download location"), path->text());
        if (d.isEmpty()) return;
        path->setText(d); write(QStringLiteral("download_path"), d); emit settingChanged(QStringLiteral("download_path"));
    });
    addRow(c, QStringLiteral("Location"), QString(), w);
    addRow(c, QStringLiteral("Ask where to save each file before downloading"), QString(), makeSwitch(QStringLiteral("ask_download_location"), false));
    addRow(c, QStringLiteral("Always show downloads button"), QStringLiteral("Otherwise it appears after the first download"), makeSwitch(QStringLiteral("pin_downloads_button"), false));
    return page;
}

QWidget* SettingsPage::aboutPage() {
    QVBoxLayout* l; QWidget* page = section(QStringLiteral("About Pacitum"), &l);
    auto* c = card(l, QString());
    auto* logo = new QLabel(); logo->setPixmap(QIcon(QStringLiteral(":/icons/pacitum-icon-128.png")).pixmap(QSize(48,48), devicePixelRatioF()));
    addRow(c, QStringLiteral("Pacitum Browser %1").arg(QCoreApplication::applicationVersion()),
           QStringLiteral("Qt %1 · Chromium %2").arg(QString::fromLatin1(qVersion()), QString::fromLatin1(qWebEngineChromiumVersion())), logo);
    auto* ua = new QLabel(Pacitum::mainProfile()->httpUserAgent()); ua->setObjectName(QStringLiteral("RowSub")); ua->setWordWrap(true);
    ua->setTextInteractionFlags(Qt::TextSelectableByMouse);
    addRow(c, QStringLiteral("User agent"), QString(), ua);
    auto* dir = new QLabel(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)); dir->setObjectName(QStringLiteral("RowSub"));
    dir->setTextInteractionFlags(Qt::TextSelectableByMouse);
    addRow(c, QStringLiteral("Profile folder"), QString(), dir);
    return page;
}
