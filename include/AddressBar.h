#pragma once
#include <QFrame>
#include <QLineEdit>
#include <QLabel>
#include <QToolButton>
#include <QCompleter>
#include <QStringListModel>
#include <QTimer>
#include <QVariantAnimation>
class StarButton;

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
    void setBookmarked(bool b, bool animate = false);
    // Omnibox progress bar (animations-spec §7)
    void progressStarted();
    void progressChanged(int value);
    void progressFinished(bool ok);
    void syncProgress(bool loading, int value);   // tab switch: jump to that tab's state
    void setSuggestionsEnabled(bool e) { m_suggest = e; }

signals:
    void returnPressed(const QString& url);
    void starClicked();
    void securityIconClicked();

protected:
    void paintEvent(QPaintEvent* e) override;
    bool eventFilter(QObject* o, QEvent* e) override;

private slots:
    void onReturn();
    void onTextChanged(const QString& text);
    void updateSuggestions();
    void onSuggestionActivated(const QString& value);

private:
    void updateSecurityIcon(const QString& url);
    void updateStar();
    void showBlurred();
    void setDisplayMode(bool on);
    QToolButton* m_chip = nullptr;
    QLabel* m_display = nullptr;
    bool m_secureHttps = false;

    HistoryManager* m_history = nullptr;
    ThemeManager* m_theme = nullptr;
    QLabel* m_securityIcon = nullptr;
    QLineEdit* m_input = nullptr;
    StarButton* m_star = nullptr;
    void initProgress();
    void repaintProgress();
    QVariantAnimation m_trickle, m_progAnim, m_fadeAnim;
    QTimer m_fadeDelay;
    qreal m_prog = 0, m_progOpacity = 0;
    bool m_progLoading = false, m_progError = false;
    QCompleter* m_completer = nullptr;
    QTimer m_timer;
    QString m_currentUrl;
    bool m_bookmarked = false;
    bool m_suggest = true;
    bool m_selectOnClick = false;
};
