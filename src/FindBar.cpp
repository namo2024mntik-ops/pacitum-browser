#include "FindBar.h"
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QWebEngineFindTextResult>
#include <QHBoxLayout>

FindBar::FindBar(QWidget* parent) : QWidget(parent) {
    setFixedHeight(48); hide();
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(12,6,12,6); layout->setSpacing(8);

    m_result = new QLabel(this); m_result->setMinimumWidth(80);
    m_input = new QLineEdit(this); m_input->setPlaceholderText(QStringLiteral("Find in page..."));
    m_input->setMinimumWidth(200); m_input->setMaximumWidth(400);
    m_prev = new QPushButton(QStringLiteral("<"), this); m_prev->setFixedSize(32,32);
    m_next = new QPushButton(QStringLiteral(">"), this); m_next->setFixedSize(32,32);
    m_close = new QPushButton(QStringLiteral("x"), this); m_close->setFixedSize(32,32);

    layout->addWidget(m_result); layout->addWidget(m_input);
    layout->addWidget(m_prev); layout->addWidget(m_next);
    layout->addStretch(); layout->addWidget(m_close);

    connect(m_input, &QLineEdit::textChanged, this, &FindBar::onTextChanged);
    connect(m_input, &QLineEdit::returnPressed, this, &FindBar::findNext);
    connect(m_prev, &QPushButton::clicked, this, &FindBar::findPrev);
    connect(m_next, &QPushButton::clicked, this, &FindBar::findNext);
    connect(m_close, &QPushButton::clicked, this, &FindBar::closeBar);
}

void FindBar::showFind() { m_input->clear(); m_input->setFocus(); show(); }

void FindBar::onTextChanged(const QString& t) {
    if (!m_view || t.isEmpty()) { m_result->setText({}); return; }
    doFind(true);
}
void FindBar::findNext() { doFind(true); }
void FindBar::findPrev() { doFind(false); }

void FindBar::doFind(bool forward) {
    if (!m_view) return;
    QString text = m_input->text();
    if (text.isEmpty()) { m_result->setText({}); return; }
    auto flags = QWebEnginePage::FindFlags();
    if (!forward) flags |= QWebEnginePage::FindBackward;
    m_view->findText(text, flags, [this](const QWebEngineFindTextResult &result){ m_result->setText(result.numberOfMatches() > 0 ? QString{} : QStringLiteral("Not found")); });
}

void FindBar::closeBar() {
    if (m_view) m_view->findText({});
    hide(); emit closed();
}
