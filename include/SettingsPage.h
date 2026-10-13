#pragma once
#include <QWidget>
#include <QJsonObject>

class QListWidget;
class QStackedWidget;
class QLabel;
class QVBoxLayout;

// Full-page settings (pacitum://settings), design-spec §4 "Settings": left nav 260, cards, switches.
// Every control writes browser_settings.json immediately and emits settingChanged(key).
class SettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit SettingsPage(QWidget* parent = nullptr);
    static QJsonObject read();
    static void write(const QString& key, const QJsonValue& v);
    void showSection(const QString& name);

signals:
    void settingChanged(const QString& key);
    void clearDataRequested(bool history, bool cookies, bool cache);

private:
    QWidget* section(const QString& title, QVBoxLayout** outLayout);
    QWidget* card(QVBoxLayout* parentLayout, const QString& heading);
    void addRow(QWidget* card, const QString& title, const QString& sub, QWidget* control);
    QWidget* makeSwitch(const QString& key, bool def);
    void animateSection(QWidget* page);

    QWidget* appearancePage();
    QWidget* searchPage();
    QWidget* startupPage();
    QWidget* privacyPage();
    QWidget* contentPage();
    QWidget* downloadsPage();
    QWidget* aboutPage();

    QListWidget* m_nav = nullptr;
    QStackedWidget* m_stack = nullptr;
    QJsonObject m_s;
};
