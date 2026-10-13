#include "AddressBar.h"
#include "HistoryManager.h"
#include "ThemeManager.h"
#include <QHBoxLayout>

AddressBar::AddressBar(HistoryManager* hm, QWidget* parent) : QWidget(parent), m_history(hm) {
    setFixedHeight(44);
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12,4,12,4);
    layout->setSpacing(8);

    m_securityIcon = new QLabel(this);
    m_securityIcon->setFixedSize(20,20);
    m_securityIcon->setAlignment(Qt::AlignCenter);
    m_securityIcon->hide();
    layout->addWidget(m_securityIcon);

    m_input = new QLineEdit(this);
    m_input->setPlaceholderText(QStringLiteral("Search or enter website name"));
    m_input->setMinimumHeight(36);
    layout->addWidget(m_input);

    m_completer = new QCompleter(this);
    m_completer->setCaseSensitivity(Qt::CaseInsensitive);
    m_completer->setMaxVisibleItems(8);
    m_input->setCompleter(m_completer);

    connect(m_input, &QLineEdit::returnPressed, this, &AddressBar::onReturn);
    connect(m_input, &QLineEdit::textChanged, this, &AddressBar::onTextChanged);
    connect(m_completer, QOverload<const QString&>::of(&QCompleter::activated), this, &AddressBar::onSuggestionActivated);

    m_timer.setSingleShot(true);
    m_timer.setInterval(200);
    connect(&m_timer, &QTimer::timeout, this, &AddressBar::updateSuggestions);
}

void AddressBar::applyTheme(ThemeManager* tm) { setStyleSheet(tm->addressBarStyle()); }

void AddressBar::setUrl(const QString& url) {
    m_currentUrl = url;
    if (!m_input->hasFocus()) m_input->setText(url);
    updateSecurityIcon(url);
}

QString AddressBar::url() const { return m_input->text(); }

void AddressBar::focusUrl() { m_input->selectAll(); m_input->setFocus(); }

void AddressBar::onReturn() { emit returnPressed(m_input->text().trimmed()); }

void AddressBar::onTextChanged(const QString&) { m_timer.start(); }

void AddressBar::updateSuggestions() {
    QString text = m_input->text().trimmed();
    if (text.isEmpty() || !m_history) { m_completer->setModel(new QStringListModel(m_completer)); return; }
    auto results = m_history->search(text, 10);
    QStringList suggestions; QSet<QString> seen;
    for (auto &r : results) {
        if (!seen.contains(r.url) && r.url != m_currentUrl) { seen.insert(r.url); suggestions << r.url; }
    }
    if (!suggestions.isEmpty()) m_completer->setModel(new QStringListModel(suggestions, m_completer));
    else m_completer->setModel(new QStringListModel(m_completer));
}

void AddressBar::onSuggestionActivated(const QString& v) { m_input->setText(v); emit returnPressed(v); }

void AddressBar::updateSecurityIcon(const QString& url) {
    if (url.startsWith(QStringLiteral("https://"))) { m_securityIcon->setText(QStringLiteral("🔒")); m_securityIcon->show(); }
    else if (url.startsWith(QStringLiteral("http://"))) { m_securityIcon->setText(QStringLiteral("⚠️")); m_securityIcon->show(); }
    else m_securityIcon->hide();
}
