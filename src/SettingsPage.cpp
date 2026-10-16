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
#include "Updater.h"
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QPointer>
#include <algorithm>

#include "AppSettings.h"
#include "PermissionSettings.h"
#include "SitePermissions.h"

QJsonObject SettingsPage::read() { return AppSettings::read(); }
void SettingsPage::write(const QString& key, const QJsonValue& v) { AppSettings::set(key, v); }

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
    connect(m_nav, &QListWidget::currentRowChanged, this, [this](int row){ m_stack->setCurrentIndex(row); animateSection(m_stack->currentWidget()); });
    m_nav->setCurrentRow(0);
}

// Settings items: the section title and each card fade in with a 35 ms stagger (and rise 6 px via
// a top margin tween). Effects are removed when done, so idle pages cost nothing.
void SettingsPage::animateSection(QWidget* page) {
    if (!page || Pacitum::reducedMotion() || Pacitum::motionMs(200) <= 0) return;
    QList<QWidget*> items;
    for (auto* w : page->findChildren<QWidget*>()) {
        const QString n = w->objectName();
        if (n == QLatin1String("Card") || n == QLatin1String("SettingsH1") || n == QLatin1String("SettingsH2")) items << w;
    }
    std::sort(items.begin(), items.end(), [](QWidget* a, QWidget* b){ return a->mapToGlobal(QPoint()).y() < b->mapToGlobal(QPoint()).y(); });
    int i = 0;
    for (auto* w : items) {
        if (w->graphicsEffect()) continue;
        auto* eff = new QGraphicsOpacityEffect(w); eff->setOpacity(0.0); w->setGraphicsEffect(eff);
        auto* a = new QPropertyAnimation(eff, "opacity", w);
        a->setDuration(Pacitum::motionMs(200)); a->setStartValue(0.0); a->setEndValue(1.0); a->setEasingCurve(QEasingCurve::OutCubic);
        QPointer<QWidget> guard(w);
        connect(a, &QPropertyAnimation::finished, w, [guard]{ if (guard) guard->setGraphicsEffect(nullptr); });
        QTimer::singleShot(qMin(i++, 10) * 35, a, [a]{ a->start(QAbstractAnimation::DeleteWhenStopped); });
    }
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
    // 1.7.5: Site settings (permissions-spec §8) with its own list / permission / site-details pages.
    auto* wrap = new QWidget(this);
    auto* l = new QVBoxLayout(wrap); l->setContentsMargins(0, 0, 0, 0); l->setSpacing(0);
    auto* title = new QLabel(QStringLiteral("Site settings"), wrap); title->setObjectName(QStringLiteral("SettingsH1"));
    auto* tw = new QWidget(wrap); tw->setObjectName(QStringLiteral("SettingsContent")); tw->setAttribute(Qt::WA_StyledBackground, true);
    auto* tl = new QHBoxLayout(tw); tl->setContentsMargins(48, 24, 48, 0); tl->addWidget(title); tl->addStretch(1);
    l->addWidget(tw);
    m_sitePerms = new SitePermissionsPage(Perm::Store::forProfile(Pacitum::mainProfile()), wrap);
    connect(m_sitePerms, &SitePermissionsPage::settingChanged, this, &SettingsPage::settingChanged);
    l->addWidget(m_sitePerms, 1);
    return wrap;
}

void SettingsPage::showSiteDetails(const QString& origin) {
    showSection(QStringLiteral("Site settings"));
    if (!m_sitePerms) return;
    if (origin.isEmpty()) m_sitePerms->showList(); else m_sitePerms->showSite(origin);
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

    // Updates (1.5.2 built-in updater)
    auto* uc = card(l, QStringLiteral("Updates"));
    auto* w = new QWidget(); auto* wl = new QHBoxLayout(w); wl->setContentsMargins(0,0,0,0); wl->setSpacing(10);
    auto* status = new QLabel(Updater::instance()->lastStatus().isEmpty() ? QStringLiteral("You have version %1.").arg(QCoreApplication::applicationVersion())
                                                                       : Updater::instance()->lastStatus(), w);
    status->setObjectName(QStringLiteral("RowSub")); status->setWordWrap(true);
    auto* checkBtn = new QPushButton(QStringLiteral("Check for updates"), w); checkBtn->setCursor(Qt::PointingHandCursor);
    wl->addWidget(status, 1); wl->addWidget(checkBtn);
    addRow(uc, QStringLiteral("Pacitum updates"), QString(), w);
    auto* up = Updater::instance();
    connect(up, &Updater::statusChanged, status, &QLabel::setText);
    connect(up, &Updater::checking, checkBtn, [checkBtn]{ checkBtn->setEnabled(false); });
    connect(up, &Updater::upToDate, checkBtn, [checkBtn](bool){ checkBtn->setEnabled(true); });
    connect(up, &Updater::checkFailed, checkBtn, [checkBtn](const QString&, bool){ checkBtn->setEnabled(true); });
    connect(up, &Updater::updateAvailable, checkBtn, [checkBtn](const UpdateInfo&, bool){ checkBtn->setEnabled(true); });
    connect(checkBtn, &QPushButton::clicked, this, [this]{ emit settingChanged(QStringLiteral("check_updates_now")); });
    addRow(uc, QStringLiteral("Check for updates automatically"), QStringLiteral("Looks for a new version in the background a few times a day and asks before installing"),
           makeSwitch(QStringLiteral("auto_check_updates"), true));
    auto* feed = new QLineEdit(m_s.value(QStringLiteral("update_feed_url")).toString());
    feed->setPlaceholderText(Updater::feedUrl().toString()); feed->setMinimumWidth(320);
    connect(feed, &QLineEdit::editingFinished, this, [this, feed]{ write(QStringLiteral("update_feed_url"), feed->text().trimmed()); });
    addRow(uc, QStringLiteral("Update feed"), QStringLiteral("Advanced: a URL returning a GitHub-style release JSON. Leave empty for the default."), feed);

    auto* pc = card(l, QStringLiteral("Performance"));
    addRow(pc, QStringLiteral("Freeze background tabs"), QStringLiteral("Pauses pages you haven't looked at for 10 minutes (never tabs playing audio). They resume instantly when you return."),
           makeSwitch(QStringLiteral("freeze_background_tabs"), true));
    return page;
}
