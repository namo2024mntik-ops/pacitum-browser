#include "AddressBar.h"
#include "HistoryManager.h"
#include "ThemeManager.h"
#include <QHBoxLayout>
#include <QEvent>
#include <QMouseEvent>
#include <QStyle>
#include <QAbstractItemView>

static bool isInternalNewTab(const QString& url) {
    return url.isEmpty() || url.startsWith(QStringLiteral("pacitum://newtab")) || url == QStringLiteral("about:blank");
}

AddressBar::AddressBar(HistoryManager* hm, QWidget* parent) : QFrame(parent), m_history(hm) {
    setObjectName(QStringLiteral("Omnibox"));
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedHeight(34);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12,0,6,0);
    layout->setSpacing(8);

    m_securityIcon = new QLabel(this);
    m_securityIcon->setFixedSize(16,16);
    m_securityIcon->setAlignment(Qt::AlignCenter);
    m_securityIcon->setCursor(Qt::PointingHandCursor);
    m_securityIcon->installEventFilter(this);
    layout->addWidget(m_securityIcon);

    m_input = new QLineEdit(this);
    m_input->setPlaceholderText(QStringLiteral("Search or enter address"));
    m_input->setFrame(false);
    m_input->installEventFilter(this);
    layout->addWidget(m_input, 1);

    m_star = new QToolButton(this);
    m_star->setFixedSize(28,28); m_star->setIconSize(QSize(16,16)); m_star->setAutoRaise(true);
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
}

void AddressBar::setUrl(const QString& url) {
    m_currentUrl = url;
    if (!m_input->hasFocus()) m_input->setText(isInternalNewTab(url) ? QString() : url);
    m_input->setCursorPosition(0);
    updateSecurityIcon(url);
}

QString AddressBar::url() const { return m_input->text(); }

void AddressBar::focusUrl() { m_input->setFocus(); m_input->selectAll(); }

void AddressBar::setBookmarked(bool b) { m_bookmarked = b; updateStar(); }

void AddressBar::updateStar() {
    if (!m_theme) return;
    const auto& p = m_theme->palette();
    m_star->setIcon(m_bookmarked ? ThemeManager::icon(QStringLiteral("star-filled"), p.accent, 16)
                                 : ThemeManager::icon(QStringLiteral("star"), p.textSecondary, 16));
}

bool AddressBar::eventFilter(QObject* o, QEvent* e) {
    if (o == m_input) {
        if (e->type() == QEvent::FocusIn) {
            setProperty("focused", true); style()->unpolish(this); style()->polish(this);
            m_selectOnClick = true;
            QTimer::singleShot(0, m_input, [this]{ m_input->selectAll(); });
        } else if (e->type() == QEvent::FocusOut) {
            setProperty("focused", false); style()->unpolish(this); style()->polish(this);
            if (!m_completer->popup()->isVisible()) {
                m_input->setText(isInternalNewTab(m_currentUrl) ? QString() : m_currentUrl);
                m_input->setCursorPosition(0);
            }
        } else if (e->type() == QEvent::MouseButtonRelease && m_selectOnClick) {
            m_selectOnClick = false; m_input->selectAll(); return true;
        }
    } else if (o == m_securityIcon && e->type() == QEvent::MouseButtonRelease) {
        emit securityIconClicked();
    }
    return QFrame::eventFilter(o, e);
}

void AddressBar::onReturn() { emit returnPressed(m_input->text().trimmed()); m_input->clearFocus(); }

void AddressBar::onTextChanged(const QString&) { m_timer.start(); }

void AddressBar::updateSuggestions() {
    QString text = m_input->text().trimmed();
    if (text.isEmpty() || !m_history) { m_completer->setModel(new QStringListModel(m_completer)); return; }
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
    QIcon ic;
    if (url.startsWith(QStringLiteral("https://"))) { ic = ThemeManager::icon(QStringLiteral("lock"), p.secure, 16); m_securityIcon->setToolTip(QStringLiteral("Connection is secure")); }
    else if (url.startsWith(QStringLiteral("http://"))) { ic = ThemeManager::icon(QStringLiteral("unlock-warning"), p.danger, 16); m_securityIcon->setToolTip(QStringLiteral("Not secure")); }
    else { ic = ThemeManager::icon(QStringLiteral("search"), p.textSecondary, 16); m_securityIcon->setToolTip(QString()); }
    m_securityIcon->setPixmap(ic.pixmap(QSize(16,16), devicePixelRatioF()));
}
