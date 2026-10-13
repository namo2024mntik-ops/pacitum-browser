#pragma once
#include <QWebEngineUrlRequestInterceptor>
#include <QSet>
#include <QString>

class AdBlocker : public QWebEngineUrlRequestInterceptor {
    Q_OBJECT
public:
    explicit AdBlocker(bool enabled = true, QObject* parent = nullptr);
    void setEnabled(bool e) { m_enabled = e; }
    bool isEnabled() const { return m_enabled; }
    void interceptRequest(QWebEngineUrlRequestInfo &info) override;
private:
    bool m_enabled;
    static const QSet<QString> s_hosts;
    static const QStringList s_patterns;
};
