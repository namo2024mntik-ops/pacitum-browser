#pragma once
#include <QWidget>
#include <QLineEdit>
#include <QLabel>
#include <QCompleter>
#include <QStringListModel>
#include <QTimer>

class HistoryManager;
class ThemeManager;

class AddressBar : public QWidget {
    Q_OBJECT
public:
    explicit AddressBar(HistoryManager* hm = nullptr, QWidget* parent = nullptr);
    void applyTheme(ThemeManager* tm);
    void setUrl(const QString& url);
    QString url() const;
    void focusUrl();

signals:
    void returnPressed(const QString& url);

private slots:
    void onReturn();
    void onTextChanged(const QString& text);
    void updateSuggestions();
    void onSuggestionActivated(const QString& value);

private:
    void updateSecurityIcon(const QString& url);

    HistoryManager* m_history = nullptr;
    QLabel* m_securityIcon = nullptr;
    QLineEdit* m_input = nullptr;
    QCompleter* m_completer = nullptr;
    QTimer m_timer;
    QString m_currentUrl;
};
