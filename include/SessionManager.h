#pragma once
#include <QString>
#include <QVector>
#include <QFuture>

struct SessionTab {
    QString url;
    QString title;
    bool pinned = false;
};

class SessionManager {
public:
    explicit SessionManager(const QString& file = QStringLiteral("browser_session.json"));
    bool save(const QVector<SessionTab>& tabs);
    void saveAsync(const QVector<SessionTab>& tabs);   // JSON + disk write on a worker thread
    void waitForPendingSave();
    QVector<SessionTab> restore();
    void clear();
private:
    QString m_file;
    QFuture<void> m_last;
};
