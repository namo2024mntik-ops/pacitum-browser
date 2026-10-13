#pragma once
#include <QString>
#include <QVector>

struct SessionTab {
    QString url;
    QString title;
    bool pinned = false;
};

class SessionManager {
public:
    explicit SessionManager(const QString& file = QStringLiteral("browser_session.json"));
    bool save(const QVector<SessionTab>& tabs);
    QVector<SessionTab> restore();
    void clear();
private:
    QString m_file;
};
