#include "HistoryManager.h"
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>
#include <QDateTime>
#include <QMutexLocker>
#include <QUuid>
#include <QThread>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>

static QString connName() { return QStringLiteral("history_%1").arg(QUuid::createUuid().toString()); }

// SQLite/QSqlDatabase handles are bound to the thread that opened them. 1.5.1 kept one long-lived
// connection opened on the UI thread and *every* method used it, while the first-run importer ran
// on a QtConcurrent worker -- any call reaching the long-lived handle from another thread is
// undefined behaviour in Qt's SQL module (intermittent crashes / "database is locked"). Now the
// long-lived connection is used only on its owner thread; any other thread gets its own short-lived
// connection (dbForThisThread / releaseThreadDb). WAL + busy_timeout let both coexist.
HistoryManager::HistoryManager(const QString& dbPath) : m_conn(connName()), m_dbPath(dbPath), m_owner(QThread::currentThread()) {
    auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_conn);
    db.setDatabaseName(m_dbPath);
    db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=3000"));
    db.open();
    initDb();
    m_flushTimer = new QTimer;
    m_flushTimer->setSingleShot(true);
    m_flushTimer->setInterval(1500);
    QObject::connect(m_flushTimer, &QTimer::timeout, m_flushTimer, [this]{
        // The SQL itself runs on a pool thread (own connection); m_mutex keeps it ordered with reads.
        if (m_writer.isRunning()) { m_flushTimer->start(); return; }
        m_writer = QtConcurrent::run([this]{ writeQueue(); });
    });
}

HistoryManager::~HistoryManager() {
    delete m_flushTimer; m_flushTimer = nullptr;
    m_writer.waitForFinished();
    writeQueue();                // pending visits are never lost on exit
    QMutexLocker lk(&m_mutex);   // waits for a worker that is still inside a call
    {
        auto db = QSqlDatabase::database(m_conn, false);
        if (db.isOpen()) db.close();
    }   // every QSqlDatabase handle is out of scope before removeDatabase -> no "still in use" warning
    QSqlDatabase::removeDatabase(m_conn);
}

QString HistoryManager::threadConn() const {
    if (QThread::currentThread() == m_owner) return m_conn;
    const QString cn = m_conn + QStringLiteral("_t%1").arg(quintptr(QThread::currentThreadId()));
    if (!QSqlDatabase::contains(cn)) {
        auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), cn);
        db.setDatabaseName(m_dbPath);
        db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=3000"));
        db.open();
    }
    return cn;
}
void HistoryManager::releaseThreadConn(const QString& cn) const {
    if (cn == m_conn) return;
    { auto db = QSqlDatabase::database(cn, false); if (db.isOpen()) db.close(); }
    QSqlDatabase::removeDatabase(cn);
}

void HistoryManager::initDb() const {
    QMutexLocker lk(&m_mutex);
    const QString cn = threadConn();
    struct Rel { const HistoryManager* h; QString c; ~Rel(){ h->releaseThreadConn(c); } } rel{this, cn};
    auto db = QSqlDatabase::database(cn);
    QSqlQuery q(db);
    q.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    q.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));   // WAL + NORMAL: no fsync per visit on the UI thread
    q.exec(R"(CREATE TABLE IF NOT EXISTS history (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        url TEXT NOT NULL, title TEXT DEFAULT '', visit_time REAL NOT NULL,
        visit_count INTEGER DEFAULT 1, typed_count INTEGER DEFAULT 0,
        last_visit_time REAL, from_omnibox INTEGER DEFAULT 0))");
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_history_url ON history(url)"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_history_time ON history(visit_time)"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_history_title ON history(title)"));
}

void HistoryManager::addVisit(const QString& url, const QString& title, bool typed) {
    if (url.isEmpty() || url.startsWith(QStringLiteral("about:"))) return;
    {
        QMutexLocker q(&m_queueMutex);
        const double now = QDateTime::currentSecsSinceEpoch();
        // Coalesce repeats of the last URL (typed visit + its urlChanged, redirects back to the same page).
        if (!m_queue.isEmpty() && m_queue.last().url == url && m_queue.last().visit) {
            auto& p = m_queue.last(); p.typed |= typed; p.at = now; if (!title.isEmpty()) p.title = title;
        } else m_queue.push_back({url, title, typed, true, 1, now});
    }
    scheduleFlush();
}

void HistoryManager::updateTitle(const QString& url, const QString& title) {
    if (url.isEmpty() || title.isEmpty() || url.startsWith(QStringLiteral("about:"))) return;
    {
        QMutexLocker q(&m_queueMutex);
        for (int i = m_queue.size() - 1; i >= 0; --i)
            if (m_queue[i].url == url) { m_queue[i].title = title; return; }   // still queued: no extra row
        m_queue.push_back({url, title, false, false, 0, 0});
    }
    scheduleFlush();
}

void HistoryManager::scheduleFlush() {
    if (m_flushTimer && QThread::currentThread() == m_owner) { if (!m_flushTimer->isActive()) m_flushTimer->start(); }
    else writeQueue();   // other threads (or after shutdown started): write directly
}

void HistoryManager::flush() { writeQueue(); }

void HistoryManager::writeQueue() const {
    QMutexLocker lk(&m_mutex);
    QVector<Pending> items;
    { QMutexLocker q(&m_queueMutex); items.swap(m_queue); }
    if (items.isEmpty()) return;
    const QString cn = threadConn();
    struct Rel { const HistoryManager* h; QString c; ~Rel(){ h->releaseThreadConn(c); } } rel{this, cn};
    auto db = QSqlDatabase::database(cn);
    db.transaction();
    {
        QSqlQuery sel(db), upd(db), ins(db), ttl(db);
        sel.prepare(QStringLiteral("SELECT id FROM history WHERE url=?"));
        upd.prepare(QStringLiteral("UPDATE history SET visit_count=visit_count+?, last_visit_time=?, title=CASE WHEN ?='' THEN title ELSE ? END, visit_time=?, typed_count=typed_count+? WHERE id=?"));
        ins.prepare(QStringLiteral("INSERT INTO history (url,title,visit_time,last_visit_time,typed_count) VALUES (?,?,?,?,?)"));
        ttl.prepare(QStringLiteral("UPDATE history SET title=? WHERE id=?"));
        for (const auto& p : items) {
            const QString title = p.title.left(500);
            sel.addBindValue(p.url); sel.exec();
            const bool found = sel.next();
            const qlonglong id = found ? sel.value(0).toLongLong() : -1;
            sel.finish();
            if (!p.visit) { if (found) { ttl.addBindValue(title); ttl.addBindValue(id); ttl.exec(); } continue; }
            if (found) {
                upd.addBindValue(p.visits); upd.addBindValue(p.at); upd.addBindValue(title); upd.addBindValue(title);
                upd.addBindValue(p.at); upd.addBindValue(p.typed ? 1 : 0); upd.addBindValue(id); upd.exec();
            } else {
                ins.addBindValue(p.url); ins.addBindValue(title); ins.addBindValue(p.at); ins.addBindValue(p.at); ins.addBindValue(p.typed ? 1 : 0);
                ins.exec();
            }
        }
    }
    db.commit();
}

int HistoryManager::importVisits(const QVector<QPair<QString, QString>>& items) {
    // Runs on a worker thread (first-run import): QSqlDatabase handles are thread-bound, so this
    // uses its own short-lived connection, fully out of scope before removeDatabase().
    QMutexLocker lk(&m_mutex);
    int n = 0;
    const QString cn = connName();
    {
        auto db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), cn);
        db.setDatabaseName(m_dbPath);
        db.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=3000"));
        if (db.open()) {
            db.transaction();
            const double now = QDateTime::currentSecsSinceEpoch();
            QSqlQuery sel(db), ins(db);
            sel.prepare(QStringLiteral("SELECT 1 FROM history WHERE url=?"));
            ins.prepare(QStringLiteral("INSERT INTO history (url,title,visit_time,last_visit_time) VALUES (?,?,?,?)"));
            for (const auto& it : items) {
                if (!it.second.startsWith(QStringLiteral("http"))) continue;
                sel.addBindValue(it.second); sel.exec();
                if (sel.next()) continue;
                ins.addBindValue(it.second); ins.addBindValue(it.first.left(500)); ins.addBindValue(now); ins.addBindValue(now);
                if (ins.exec()) ++n;
            }
            sel.finish(); ins.finish();
            db.commit();
        }
        db.close();
    }
    QSqlDatabase::removeDatabase(cn);
    return n;
}

QVector<HistoryEntry> HistoryManager::search(const QString& query, int limit) const {
    writeQueue();
    QMutexLocker lk(&m_mutex);
    const QString cn = threadConn();
    struct Rel { const HistoryManager* h; QString c; ~Rel(){ h->releaseThreadConn(c); } } rel{this, cn};
    auto db = QSqlDatabase::database(cn);
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT DISTINCT url,title,visit_time FROM history WHERE url LIKE ? OR title LIKE ? ORDER BY visit_time DESC LIMIT ?"));
    q.addBindValue(QStringLiteral("%")+query+QStringLiteral("%"));
    q.addBindValue(QStringLiteral("%")+query+QStringLiteral("%"));
    q.addBindValue(limit);
    q.exec();
    QVector<HistoryEntry> r; while(q.next()) r.push_back({q.value(0).toString(), q.value(1).toString(), q.value(2).toDouble(),1});
     return r;
}

QVector<HistoryEntry> HistoryManager::recent(int limit) const {
    writeQueue();
    QMutexLocker lk(&m_mutex);
    const QString cn = threadConn();
    struct Rel { const HistoryManager* h; QString c; ~Rel(){ h->releaseThreadConn(c); } } rel{this, cn};
    auto db = QSqlDatabase::database(cn);
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT DISTINCT url,title,visit_time FROM history ORDER BY visit_time DESC LIMIT ?"));
    q.addBindValue(limit); q.exec();
    QVector<HistoryEntry> r; while(q.next()) r.push_back({q.value(0).toString(), q.value(1).toString(), q.value(2).toDouble(),1});
     return r;
}

QVector<HistoryEntry> HistoryManager::topSites(int limit) const {
    writeQueue();
    QMutexLocker lk(&m_mutex);
    const QString cn = threadConn();
    struct Rel { const HistoryManager* h; QString c; ~Rel(){ h->releaseThreadConn(c); } } rel{this, cn};
    auto db = QSqlDatabase::database(cn);
    QSqlQuery q(db);
    q.prepare(QStringLiteral("SELECT url,title,visit_count FROM history GROUP BY url ORDER BY visit_count DESC LIMIT ?"));
    q.addBindValue(limit); q.exec();
    QVector<HistoryEntry> r; while(q.next()) r.push_back({q.value(0).toString(), q.value(1).toString(),0,q.value(2).toInt()});
     return r;
}

void HistoryManager::clear() {
    writeQueue();
    QMutexLocker lk(&m_mutex);
    const QString cn = threadConn();
    struct Rel { const HistoryManager* h; QString c; ~Rel(){ h->releaseThreadConn(c); } } rel{this, cn};
    auto db = QSqlDatabase::database(cn);
    QSqlQuery q(db); q.exec(QStringLiteral("DELETE FROM history")); q.exec(QStringLiteral("VACUUM"));
    
}
void HistoryManager::deleteUrl(const QString& url) {
    writeQueue();
    QMutexLocker lk(&m_mutex);
    const QString cn = threadConn();
    struct Rel { const HistoryManager* h; QString c; ~Rel(){ h->releaseThreadConn(c); } } rel{this, cn};
    auto db = QSqlDatabase::database(cn);
    QSqlQuery q(db); q.prepare(QStringLiteral("DELETE FROM history WHERE url=?")); q.addBindValue(url); q.exec();
    
}
void HistoryManager::cleanupOld(int days) {
    double cutoff = QDateTime::currentSecsSinceEpoch() - days*86400.0;
    QMutexLocker lk(&m_mutex);
    const QString cn = threadConn();
    struct Rel { const HistoryManager* h; QString c; ~Rel(){ h->releaseThreadConn(c); } } rel{this, cn};
    auto db = QSqlDatabase::database(cn);
    QSqlQuery q(db); q.prepare(QStringLiteral("DELETE FROM history WHERE visit_time<?")); q.addBindValue(cutoff); q.exec();
    
}
int HistoryManager::count() const {
    writeQueue();
    QMutexLocker lk(&m_mutex);
    const QString cn = threadConn();
    struct Rel { const HistoryManager* h; QString c; ~Rel(){ h->releaseThreadConn(c); } } rel{this, cn};
    auto db = QSqlDatabase::database(cn);
    QSqlQuery q(db); q.exec(QStringLiteral("SELECT COUNT(*) FROM history")); int c=0; if(q.next()) c=q.value(0).toInt();
     return c;
}
