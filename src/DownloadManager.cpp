#include "DownloadManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QProgressBar>
#include <QPushButton>
#include <QLabel>
#include <QListWidgetItem>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>
#include <QDir>

static QString fmtSize(qint64 b) {
    if (b < 1024) return QString::number(b) + QStringLiteral(" B");
    if (b < 1024*1024) return QString::number(b/1024.0, 'f', 1) + QStringLiteral(" KB");
    return QString::number(b/(1024.0*1024.0), 'f', 1) + QStringLiteral(" MB");
}

// DownloadItemWidget
DownloadItemWidget::DownloadItemWidget(QWebEngineDownloadRequest* dl, QWidget* parent) : QWidget(parent), m_dl(dl) {
    auto* lay = new QVBoxLayout(this); lay->setContentsMargins(8,4,8,4); lay->setSpacing(4);
    auto* header = new QHBoxLayout();
    m_name = new QLabel(QStringLiteral("Downloading..."), this); m_name->setStyleSheet(QStringLiteral("font-weight:bold; font-size:13px;"));
    m_status = new QLabel(QStringLiteral("Starting..."), this); m_status->setStyleSheet(QStringLiteral("color:#666; font-size:11px;"));
    header->addWidget(m_name, 1); header->addWidget(m_status); lay->addLayout(header);

    m_progress = new QProgressBar(this); m_progress->setFixedHeight(6); m_progress->setTextVisible(false); lay->addWidget(m_progress);

    auto* btnLay = new QHBoxLayout(); btnLay->addStretch();
    m_cancel = new QPushButton(QStringLiteral("Cancel"), this); m_cancel->setFixedSize(70,26);
    m_open = new QPushButton(QStringLiteral("Open File"), this); m_open->setFixedSize(90,26); m_open->hide();
    btnLay->addWidget(m_cancel); btnLay->addWidget(m_open); lay->addLayout(btnLay);

    connect(m_cancel, &QPushButton::clicked, this, [this]{ emit cancelRequested(m_dl); });
    connect(m_open, &QPushButton::clicked, this, [this]{ emit openRequested(m_path); });

    connect(m_dl, &QWebEngineDownloadRequest::receivedBytesChanged, this, &DownloadItemWidget::onProgress);
    connect(m_dl, &QWebEngineDownloadRequest::totalBytesChanged, this, &DownloadItemWidget::onProgress);
    connect(m_dl, &QWebEngineDownloadRequest::isFinishedChanged, this, &DownloadItemWidget::onFinished);
}

void DownloadItemWidget::onProgress() {
    qint64 rec = m_dl->receivedBytes(); qint64 tot = m_dl->totalBytes();
    if (tot > 0) { m_progress->setValue(int(rec*100/tot)); m_status->setText(fmtSize(rec)+QStringLiteral(" / ")+fmtSize(tot)); }
    else m_status->setText(fmtSize(rec));
}

void DownloadItemWidget::onFinished() {
    if (!m_dl->isFinished()) { m_status->setText(QStringLiteral("Cancelled")); m_progress->setValue(0); m_cancel->hide(); return; }
    // downloadDirectory() returns QString in Qt 6.9 (or QUrl in older) - handle both
    QString dir = m_dl->downloadDirectory();
    m_path = QDir(dir).filePath(m_dl->downloadFileName());
    m_status->setText(QStringLiteral("Completed")); m_progress->setValue(100); m_cancel->hide(); m_open->show();
    m_name->setText(m_dl->downloadFileName());
}

// DownloadManager
DownloadManager::DownloadManager(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QStringLiteral("Downloads")); setMinimumSize(600,450); resize(600,500);
    auto* lay = new QVBoxLayout(this); lay->setContentsMargins(16,16,16,16); lay->setSpacing(12);
    auto* header = new QLabel(QStringLiteral("Downloads"), this); header->setStyleSheet(QStringLiteral("font-size:20px; font-weight:bold;")); lay->addWidget(header);
    m_list = new QListWidget(this);
    m_list->setStyleSheet(QStringLiteral("QListWidget{background:transparent;border:none;} QListWidget::item{border:1px solid #e0e0e0;border-radius:8px;margin:4px 0;background:white;}"));
    lay->addWidget(m_list, 1);
    auto* btnLay = new QHBoxLayout();
    auto* clearBtn = new QPushButton(QStringLiteral("Clear All"), this); clearBtn->setFixedSize(100,34);
    auto* closeBtn = new QPushButton(QStringLiteral("Close"), this); closeBtn->setFixedSize(100,34);
    connect(clearBtn, &QPushButton::clicked, this, &DownloadManager::clearDownloads);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);
    btnLay->addWidget(clearBtn); btnLay->addStretch(); btnLay->addWidget(closeBtn); lay->addLayout(btnLay);
}

void DownloadManager::addDownload(QWebEngineDownloadRequest* dl) {
    auto* item = new QListWidgetItem(m_list);
    auto* w = new DownloadItemWidget(dl, m_list);
    connect(w, &DownloadItemWidget::cancelRequested, this, &DownloadManager::cancelDownload);
    connect(w, &DownloadItemWidget::openRequested, this, &DownloadManager::openFile);
    item->setSizeHint(w->sizeHint()); m_list->addItem(item); m_list->setItemWidget(item, w);
    m_downloads.append(dl); dl->accept();
}

void DownloadManager::cancelDownload(QWebEngineDownloadRequest* dl) { dl->cancel(); }
void DownloadManager::openFile(const QString& path) { QDesktopServices::openUrl(QUrl::fromLocalFile(path)); }
void DownloadManager::clearDownloads() { m_list->clear(); m_downloads.clear(); }
