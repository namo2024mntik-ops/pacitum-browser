#pragma once
#include <QPair>
#include <QString>
#include <QVector>
#include <QMutex>

struct HistoryEntry {
    QString url;
    QString title;
    double visitTime = 0; // epoch seconds
    int visitCount = 1;
};

class HistoryManager {
public:
    explicit HistoryManager(const QString& dbPath = QStringLiteral("browser_history.db"));

    void addVisit(const QString& url, const QString& title = {}, bool typed = false);
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
    QString m_dbPath;
    mutable QMutex m_mutex;
};
