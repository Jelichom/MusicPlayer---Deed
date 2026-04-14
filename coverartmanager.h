#ifndef COVERARTMANAGER_H
#define COVERARTMANAGER_H

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

class QNetworkAccessManager;
class QNetworkReply;

class CoverArtManager : public QObject
{
    Q_OBJECT

public:
    explicit CoverArtManager(QObject *parent = nullptr);

    QString coverArtPath(const QString &filePath) const;
    void ensureCoverArt(const QString &filePath);
    void removeForFile(const QString &filePath);
    void clear();

signals:
    void coverArtUpdated(const QString &filePath, const QString &artPath);

private slots:
    void onCoverSearchFinished();
    void onCoverDownloadFinished();

private:
    QString extractCoverArtToCache(const QString &filePath) const;
    QString cacheCoverArt(const QString &filePath,
                          const QByteArray &imageData,
                          const QString &mimeType) const;
    QString findFolderCoverArt(const QString &filePath) const;
    QString saveDownloadedCoverArt(const QString &filePath,
                                   const QByteArray &imageData,
                                   const QString &contentType) const;

    bool isAcceptedImageMimeType(const QString &mimeType) const;
    bool isAcceptableImageSize(qint64 sizeBytes) const;
    void armReplyTimeout(QNetworkReply *reply) const;
    void cleanupCoverCacheDir() const;

    void startCoverSearch(const QString &filePath,
                          const QString &album,
                          const QString &artist,
                          int searchStage);
    void startCoverDownloadAttempt(const QString &filePath,
                                   const QStringList &mbids,
                                   int index);

    QHash<QString, QString> m_coverArtCache;
    QSet<QString> m_attemptedOnlineCoverFetches;
    QNetworkAccessManager *m_networkManager;
};

#endif
