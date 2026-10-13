#pragma once
#include <QWebEngineUrlSchemeHandler>
#include <QMap>
#include <QString>

class QWebEngineProfile;

// Serves internal pages such as pacitum://newtab (design-spec §4 "New Tab Page").
class PacitumSchemeHandler : public QWebEngineUrlSchemeHandler {
    Q_OBJECT
public:
    static void registerScheme();            // call before QApplication
    static PacitumSchemeHandler* instance();
    static void install(QWebEngineProfile* profile);
    static QString newTabUrl() { return QStringLiteral("pacitum://newtab"); }

    void setThemeVars(const QMap<QString, QString>& vars) { m_vars = vars; }
    void setSearchBase(const QString& s) { m_search = s; }
    void requestStarted(QWebEngineUrlRequestJob* job) override;

private:
    using QWebEngineUrlSchemeHandler::QWebEngineUrlSchemeHandler;
    QMap<QString, QString> m_vars;
    QString m_search = QStringLiteral("https://www.google.com/search?q=");
};
