#pragma once
#include <QFrame>
#include <QLineEdit>
#include <QLabel>
#include <QToolButton>
#include <QCompleter>
#include <QStringListModel>
#include <QTimer>

class HistoryManager;
class ThemeManager;

// Pill-shaped omnibox (design-spec §4 "Omnibox"): leading security/search icon,
// URL field and trailing bookmark star.
class AddressBar : public QFrame {
    Q_OBJECT
public:
    explicit AddressBar(HistoryManager* hm = nullptr, QWidget* parent = nullptr);
    void applyTheme(ThemeManager* tm);
    void setUrl(const QString& url);
    QString url() const;
    void focusUrl();
    void setBookmarked(bool b);
    void setSuggestionsEnabled(bool e) { m_suggest = e; }

signals:
    void returnPressed(const QString& url);
    void starClicked();
    void securityIconClicked();

protected:
    bool eventFilter(QObject* o, QEvent* e) override;

private slots:
    void onReturn();
    void onTextChanged(const QString& text);
    void updateSuggestions();
    void onSuggestionActivated(const QString& value);

private:
    void updateSecurityIcon(const QString& url);
    void updateStar();

    HistoryManager* m_history = nullptr;
    ThemeManager* m_theme = nullptr;
    QLabel* m_securityIcon = nullptr;
    QLineEdit* m_input = nullptr;
    QToolButton* m_star = nullptr;
    QCompleter* m_completer = nullptr;
    QTimer m_timer;
    QString m_currentUrl;
    bool m_bookmarked = false;
    bool m_suggest = true;
    bool m_selectOnClick = false;
};
