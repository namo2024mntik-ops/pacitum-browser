#pragma once
#include <QDialog>
#include <QListWidget>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QWebEngineDownloadRequest>

class DownloadItemWidget : public QWidget {
    Q_OBJECT
public:
    explicit DownloadItemWidget(QWebEngineDownloadRequest* dl, QWidget* parent = nullptr);

signals:
    void cancelRequested(QWebEngineDownloadRequest* dl);
    void openRequested(const QString& path);

private slots:
    void onProgress();
    void onFinished();

private:
    static QString formatSize(qint64 bytes);
    QWebEngineDownloadRequest* m_dl = nullptr;
    QLabel* m_name = nullptr;
    QLabel* m_status = nullptr;
    QProgressBar* m_progress = nullptr;
    QPushButton* m_cancel = nullptr;
    QPushButton* m_open = nullptr;
    QString m_path;
};

class DownloadManager : public QDialog {
    Q_OBJECT
public:
    explicit DownloadManager(QWidget* parent = nullptr);
    void addDownload(QWebEngineDownloadRequest* dl);
    void clearDownloads();

private slots:
    void cancelDownload(QWebEngineDownloadRequest* dl);
    void openFile(const QString& path);

private:
    QListWidget* m_list = nullptr;
    QList<QWebEngineDownloadRequest*> m_downloads;
};
