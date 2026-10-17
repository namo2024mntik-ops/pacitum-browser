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
#include "PermissionUi.h"
class QHBoxLayout;

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
    QString currentUrl() const { return m_currentUrl; }   // the URL last set (not the display text)
    void focusUrl();
    void setBookmarked(bool b, bool animate = false);
    // Omnibox progress bar (animations-spec §7)
    void progressStarted();
    void progressChanged(int value);
    void progressFinished(bool ok);
    void syncProgress(bool loading, int value);   // tab switch: jump to that tab's state
    void setSuggestionsEnabled(bool e) { m_suggest = e; }
    // 1.7.5 permission chips (permissions-spec §1/§4/§5/§6/§9)
    void setPermissionChips(const QList<OmniChip>& chips);
    QPoint bubbleAnchor() const;   // global: omnibox left − 2, toolbar bottom + 6

signals:
    void returnPressed(const QString& url);
    void starClicked();
    void securityIconClicked();
    void permissionChipClicked(const QString& id);

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
    QHBoxLayout* m_permLeft = nullptr; QHBoxLayout* m_permRight = nullptr;
    QHash<QString, QWidget*> m_permChips;
    QLabel* m_display = nullptr;
    bool m_secureHttps = false;

    HistoryManager* m_history = nullptr;
    ThemeManager* m_theme = nullptr;
    QLabel* m_securityIcon = nullptr;
    QLineEdit* m_input = nullptr;
    StarButton* m_star = nullptr;
    void initProgress();
    void repaintProgress();
    QVariantAnimation m_trickle, m_progAnim, m_fadeAnim, m_glowAnim;
    qreal m_glow = 0;
    void animateGlow(qreal to);
    QTimer m_fadeDelay;
    qreal m_prog = 0, m_progOpacity = 0;
    bool m_progLoading = false, m_progError = false;
    QCompleter* m_completer = nullptr;
    QTimer m_timer;
    QString m_currentUrl;
    bool m_bookmarked = false;
    bool m_suggest = true;
    bool m_selectOnClick = false;
    bool inlineHintActive() const;
};
