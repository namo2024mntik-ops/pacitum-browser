#pragma once
#include <QPair>
#include <QString>
#include <QVector>
#include <QMutex>
#include <QFuture>

struct HistoryEntry {
    QString url;
    QString title;
    double visitTime = 0; // epoch seconds
    int visitCount = 1;
};

class QThread;
class QTimer;
class HistoryManager {
public:
    explicit HistoryManager(const QString& dbPath = QStringLiteral("browser_history.db"));
    ~HistoryManager();

    // Visits are queued and written in one transaction on a worker thread (~1.5 s debounce), so
    // navigation never waits for SQLite on the UI thread. Reads flush the queue first.
    void addVisit(const QString& url, const QString& title = {}, bool typed = false);
    void updateTitle(const QString& url, const QString& title);   // queued like addVisit
    void flush();                                                  // write the queue now (blocking)
    QVector<HistoryEntry> search(const QString& query, int limit = 50) const;
    QVector<HistoryEntry> recent(int limit = 30) const;
    QVector<HistoryEntry> topSites(int limit = 10) const;
    // Bulk import (one transaction; thread-safe). Pairs are (title, url), oldest first. Returns rows added.
    int importVisits(const QVector<QPair<QString, QString>>& titleUrl);
    void clear();
    void deleteUrl(const QString& url);
    void cleanupOld(int days = 90);
    int count() const;

private:
    void initDb() const;
    QString threadConn() const;                       // m_conn on the owner thread, a per-thread one elsewhere
    void releaseThreadConn(const QString& cn) const;
    QString m_conn;   // one long-lived connection (opening SQLite per call was slow and leaked 'still in use' warnings)
    QString m_dbPath;
    QThread* m_owner = nullptr;
    mutable QMutex m_mutex;
    struct Pending { QString url, title; bool typed = false; bool visit = true; int visits = 0; double at = 0; };
    mutable QMutex m_queueMutex;
    mutable QVector<Pending> m_queue;
    QTimer* m_flushTimer = nullptr;
    QFuture<void> m_writer;
    void scheduleFlush();
    void writeQueue() const;   // drains m_queue (any thread)
};
