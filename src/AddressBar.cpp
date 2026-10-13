#include "AddressBar.h"
#include "HistoryManager.h"
#include "ThemeManager.h"
#include <QHBoxLayout>
#include <QEvent>
#include <QMouseEvent>
#include <QStyle>
#include <QAbstractItemView>
#include <QUrl>

static bool isInternalNewTab(const QString& url) {
    return url.isEmpty() || url.startsWith(QStringLiteral("pacitum://newtab")) || url == QStringLiteral("about:blank");
}

AddressBar::AddressBar(HistoryManager* hm, QWidget* parent) : QFrame(parent), m_history(hm) {
    setObjectName(QStringLiteral("Omnibox"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedHeight(38);
    setMinimumWidth(240);   // spec §4.4: omnibox shrinks to min 240
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(5,0,4,0);
    layout->setSpacing(6);

    // Leading security / site chip (spec §5.2): h28 r14
    m_chip = new QToolButton(this);
    m_chip->setObjectName(QStringLiteral("OmniChip"));
    m_chip->setFixedHeight(28); m_chip->setMinimumWidth(28);
    m_chip->setIconSize(QSize(16,16));
    m_chip->setCursor(Qt::PointingHandCursor);
    m_chip->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_chip->setFocusPolicy(Qt::NoFocus);
    m_chip->installEventFilter(this);
    connect(m_chip, &QToolButton::clicked, this, [this]{ if (!isInternalNewTab(m_currentUrl)) emit securityIconClicked(); });
    layout->addWidget(m_chip);
    m_securityIcon = nullptr;

    m_input = new QLineEdit(this);
    m_input->setPlaceholderText(QStringLiteral("Search or enter address"));
    m_input->setFrame(false);
    m_input->installEventFilter(this);
    layout->addWidget(m_input, 1);
    // Blurred display: host in text colour, path in text-tertiary, scheme + query hidden.
    m_display = new QLabel(m_input);
    m_display->setObjectName(QStringLiteral("OmniDisplay"));
    m_display->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_display->setTextFormat(Qt::RichText);
    m_display->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_display->hide();

    m_star = new QToolButton(this);
    m_star->setFixedSize(30,30); m_star->setIconSize(QSize(17,17)); m_star->setAutoRaise(true);
    m_star->setToolTip(QStringLiteral("Bookmark this tab (Ctrl+D)"));
    connect(m_star, &QToolButton::clicked, this, &AddressBar::starClicked);
    layout->addWidget(m_star);

    m_completer = new QCompleter(this);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setMaxVisibleItems(8);
    m_input->setCompleter(m_completer);

    connect(m_input, &QLineEdit::returnPressed, this, &AddressBar::onReturn);
    connect(m_input, &QLineEdit::textEdited, this, &AddressBar::onTextChanged);
    connect(m_completer, QOverload<const QString&>::of(&QCompleter::activated), this, &AddressBar::onSuggestionActivated);

    m_timer.setSingleShot(true);
    m_timer.setInterval(200);
    connect(&m_timer, &QTimer::timeout, this, &AddressBar::updateSuggestions);
}

void AddressBar::applyTheme(ThemeManager* tm) {
    m_theme = tm;
    setStyleSheet(tm->addressBarStyle());
    const auto& p = tm->palette();
    m_completer->popup()->setStyleSheet(QStringLiteral(
        "QAbstractItemView{background:%1;color:%2;border:1px solid %3;border-radius:12px;padding:6px;outline:0;}"
        "QAbstractItemView::item{min-height:36px;padding:0 10px;border-radius:6px;}"
        "QAbstractItemView::item:selected{background:%4;color:%2;}").arg(p.toolbar, p.text, p.border, p.surface));
    updateSecurityIcon(m_currentUrl);
    updateStar();
    if (!m_input->hasFocus()) showBlurred();
}

void AddressBar::setUrl(const QString& url) {
    m_currentUrl = url;
    if (!m_input->hasFocus()) showBlurred();
    updateSecurityIcon(url);
}

void AddressBar::showBlurred() {
    const QString full = isInternalNewTab(m_currentUrl) ? QString() : m_currentUrl;
    if (full.isEmpty() || !m_theme) { setDisplayMode(false); m_input->setText(full); m_input->setCursorPosition(0); return; }
    const QUrl u(full);
    QString host, rest;
    if (u.scheme() == QLatin1String("pacitum")) {
        host = u.host(); rest = u.path();                          // pacitum://settings/appearance -> settings/appearance
    } else if (u.scheme() == QLatin1String("http") || u.scheme() == QLatin1String("https")) {
        host = u.host(QUrl::FullyDecoded);
        if (host.startsWith(QLatin1String("www."))) host = host.mid(4);
        if (u.port() > 0) host += QLatin1Char(':') + QString::number(u.port());
        rest = u.path(QUrl::FullyDecoded);
        if (rest == QLatin1String("/")) rest.clear();
    } else {
        host = full; if (host.size() > 200) host = host.left(200) + QStringLiteral("…");
    }
    const auto& p = m_theme->palette();
    m_display->setText(QStringLiteral("<span style=\"color:%1\">%2</span><span style=\"color:%3\">%4</span>")
        .arg(p.text, host.toHtmlEscaped(), p.textTertiary, rest.toHtmlEscaped()));
    m_input->setText(host + rest);    // same text, kept invisible; the label paints the two tones
    m_input->setCursorPosition(0);
    setDisplayMode(true);
}

void AddressBar::setDisplayMode(bool on) {
    m_input->setProperty("urlview", on);
    m_input->style()->unpolish(m_input); m_input->style()->polish(m_input);
    if (on) { m_display->setGeometry(2, 0, m_input->width() - 2, m_input->height()); m_display->show(); }
    else m_display->hide();
}

QString AddressBar::url() const { return m_input->text(); }

void AddressBar::focusUrl() { m_input->setFocus(); m_input->selectAll(); }

void AddressBar::setBookmarked(bool b) { m_bookmarked = b; updateStar(); }

void AddressBar::updateStar() {
    if (!m_theme) return;
    const auto& p = m_theme->palette();
    m_star->setIcon(m_bookmarked ? ThemeManager::icon(QStringLiteral("star-filled"), p.accent, 17)
                                 : ThemeManager::icon(QStringLiteral("star"), p.textSecondary, 17));
}

bool AddressBar::eventFilter(QObject* o, QEvent* e) {
    if (o == m_input) {
        if (e->type() == QEvent::Resize && !m_display->isHidden()) {
            m_display->setGeometry(2, 0, m_input->width() - 2, m_input->height());
        } else if (e->type() == QEvent::FocusIn) {
            setDisplayMode(false);
            if (!isInternalNewTab(m_currentUrl)) m_input->setText(m_currentUrl);   // full URL on focus
            setProperty("focused", true); style()->unpolish(this); style()->polish(this);
            m_selectOnClick = true;
            QTimer::singleShot(0, m_input, [this]{ m_input->selectAll(); });
        } else if (e->type() == QEvent::FocusOut) {
            setProperty("focused", false); style()->unpolish(this); style()->polish(this);
            if (!m_completer->popup()->isVisible()) showBlurred();
        } else if (e->type() == QEvent::MouseButtonRelease && m_selectOnClick) {
            m_selectOnClick = false; m_input->selectAll(); return true;
        }
    } else if (o == m_chip && m_secureHttps) {
        // hover swaps the lock for the site-controls glyph (click opens site controls / info)
        if (e->type() == QEvent::Enter) m_chip->setIcon(ThemeManager::icon(QStringLiteral("site-controls"), m_theme->palette().text, 16));
        else if (e->type() == QEvent::Leave) updateSecurityIcon(m_currentUrl);
    }
    return QFrame::eventFilter(o, e);
}

void AddressBar::onReturn() { emit returnPressed(m_input->text().trimmed()); m_input->clearFocus(); }

void AddressBar::onTextChanged(const QString&) { m_timer.start(); }

void AddressBar::updateSuggestions() {
    QString text = m_input->text().trimmed();
    if (text.isEmpty() || !m_history || !m_suggest) { m_completer->setModel(new QStringListModel(m_completer)); return; }
    auto results = m_history->search(text, 10);
    QStringList suggestions; QSet<QString> seen;
    for (auto &r : results) {
        if (!seen.contains(r.url) && r.url != m_currentUrl) { seen.insert(r.url); suggestions << r.url; }
    }
    m_completer->setModel(new QStringListModel(suggestions, m_completer));
    if (!suggestions.isEmpty()) m_completer->complete();
}

void AddressBar::onSuggestionActivated(const QString& v) { m_input->setText(v); emit returnPressed(v); }

void AddressBar::updateSecurityIcon(const QString& url) {
    if (!m_theme) return;
    const auto& p = m_theme->palette();
    m_secureHttps = false;
    QString text;
    QIcon ic;
    int isz = 16;
    if (url.startsWith(QStringLiteral("https://"))) {
        ic = ThemeManager::icon(QStringLiteral("lock"), p.secure, 14); isz = 14; m_secureHttps = true;
        m_chip->setToolTip(QStringLiteral("Connection is secure · Site controls"));
    } else if (url.startsWith(QStringLiteral("http://"))) {
        ic = ThemeManager::icon(QStringLiteral("not-secure"), p.danger, 14); isz = 14; text = QStringLiteral("Not secure");
        m_chip->setToolTip(QStringLiteral("Your connection to this site is not secure"));
    } else if (url.startsWith(QStringLiteral("pacitum:")) && !isInternalNewTab(url)) {
        ic = ThemeManager::icon(QStringLiteral("site-info"), p.accent, 16); text = QStringLiteral("Pacitum");
        m_chip->setToolTip(QStringLiteral("Pacitum page"));
    } else {
        ic = ThemeManager::icon(QStringLiteral("search"), p.textTertiary, 16);
        m_chip->setToolTip(QString());
    }
    m_chip->setIconSize(QSize(isz, isz));
    m_chip->setIcon(ic);
    m_chip->setText(text);
    m_chip->setToolButtonStyle(text.isEmpty() ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextBesideIcon);
    m_chip->setProperty("kind", text.isEmpty() ? "icon" : (m_secureHttps ? "secure" : url.startsWith(QStringLiteral("http://")) ? "danger" : "internal"));
    m_chip->style()->unpolish(m_chip); m_chip->style()->polish(m_chip);
    if (text.isEmpty()) m_chip->setFixedWidth(28); else { m_chip->setMinimumWidth(28); m_chip->setMaximumWidth(200); }
}
