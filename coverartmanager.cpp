#include "coverartmanager.h"

#include "trackmetadata.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <taglib/attachedpictureframe.h>
#include <taglib/flacfile.h>
#include <taglib/flacpicture.h>
#include <taglib/id3v2tag.h>
#include <taglib/mpegfile.h>

Q_LOGGING_CATEGORY(deedCoverLog, "deed.coverart")

namespace {
constexpr const char *kUserAgent = "Deed/1.3 (Qt6; Linux)";
constexpr int kNetworkTimeoutMs = 8000;
constexpr qint64 kMaxImageBytes = 5 * 1024 * 1024; // 5 MiB
constexpr int kMaxCachedCoverFiles = 256;

enum CoverSearchStage {
    ExactAlbumArtist = 0,
    SimplifiedAlbumArtist = 1,
    ExactAlbumOnly = 2,
    SimplifiedAlbumOnly = 3,
    FolderAlbumArtist = 4,
    SimplifiedFolderAlbumArtist = 5,
    FolderAlbumOnly = 6,
    SimplifiedFolderAlbumOnly = 7,
    SearchStageCount = 8
};

QString coverCacheDirPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/covers");
}

QString normalizedMimeType(const QString &mimeType)
{
    return mimeType.trimmed().toLower();
}

QString cleanedFolderAlbumName(const QString &filePath)
{
    QString folder = QFileInfo(filePath).dir().dirName().trimmed();

    folder.remove(QRegularExpression(QStringLiteral(R"(^\d{4}\s*[-_]\s*)")));
    folder.replace(QRegularExpression(QStringLiteral(R"([_/]+)")), QStringLiteral(" "));
    folder.replace(QRegularExpression(QStringLiteral(R"(\s+-\s+)")), QStringLiteral(" "));
    folder.replace(QRegularExpression(QStringLiteral(R"(\s{2,})")), QStringLiteral(" "));

    return folder.trimmed();
}

QString simplifiedAlbumTitle(QString album)
{
    album = album.trimmed();

    album.remove(QRegularExpression(QStringLiteral(R"(\([^)]*\))")));
    album.remove(QRegularExpression(QStringLiteral(R"(\[[^\]]*\])")));
    album.remove(QRegularExpression(
        QStringLiteral(R"(\b(deluxe|expanded|special|anniversary|remaster(ed)?|version|edition|bonus|disc\s*\d+|cd\s*\d+)\b)"),
        QRegularExpression::CaseInsensitiveOption));

    album.replace(QRegularExpression(QStringLiteral(R"([_\-]+)")), QStringLiteral(" "));
    album.replace(QRegularExpression(QStringLiteral(R"(\s{2,})")), QStringLiteral(" "));
    return album.trimmed();
}

QString quotedTerm(const QString &value)
{
    QString escaped = value;
    escaped.replace('"', QStringLiteral("\\\""));
    return QStringLiteral("\"%1\"").arg(escaped);
}

QString buildMusicBrainzQuery(const QString &album,
                              const QString &artist,
                              const QString &folderAlbum,
                              int stage)
{
    const QString simplifiedAlbum = simplifiedAlbumTitle(album);
    const QString simplifiedFolder = simplifiedAlbumTitle(folderAlbum);

    switch (stage) {
    case ExactAlbumArtist:
        return artist.isEmpty()
            ? QStringLiteral("release:%1").arg(quotedTerm(album))
            : QStringLiteral("release:%1 AND artist:%2").arg(quotedTerm(album), quotedTerm(artist));
    case SimplifiedAlbumArtist:
        if (!artist.isEmpty() && !simplifiedAlbum.isEmpty() && simplifiedAlbum != album) {
            return QStringLiteral("release:%1 AND artist:%2")
                .arg(quotedTerm(simplifiedAlbum), quotedTerm(artist));
        }
        break;
    case ExactAlbumOnly:
        if (!album.isEmpty()) {
            return QStringLiteral("release:%1").arg(quotedTerm(album));
        }
        break;
    case SimplifiedAlbumOnly:
        if (!simplifiedAlbum.isEmpty() && simplifiedAlbum != album) {
            return QStringLiteral("release:%1").arg(quotedTerm(simplifiedAlbum));
        }
        break;
    case FolderAlbumArtist:
        if (!artist.isEmpty() && !folderAlbum.isEmpty() && folderAlbum != album) {
            return QStringLiteral("release:%1 AND artist:%2")
                .arg(quotedTerm(folderAlbum), quotedTerm(artist));
        }
        break;
    case SimplifiedFolderAlbumArtist:
        if (!artist.isEmpty() &&
            !simplifiedFolder.isEmpty() &&
            simplifiedFolder != folderAlbum &&
            simplifiedFolder != simplifiedAlbum) {
            return QStringLiteral("release:%1 AND artist:%2")
                .arg(quotedTerm(simplifiedFolder), quotedTerm(artist));
        }
        break;
    case FolderAlbumOnly:
        if (!folderAlbum.isEmpty() && folderAlbum != album) {
            return QStringLiteral("release:%1").arg(quotedTerm(folderAlbum));
        }
        break;
    case SimplifiedFolderAlbumOnly:
        if (!simplifiedFolder.isEmpty() &&
            simplifiedFolder != folderAlbum &&
            simplifiedFolder != simplifiedAlbum) {
            return QStringLiteral("release:%1").arg(quotedTerm(simplifiedFolder));
        }
        break;
    default:
        break;
    }

    return QString();
}
}

CoverArtManager::CoverArtManager(QObject *parent)
    : QObject(parent),
      m_networkManager(new QNetworkAccessManager(this))
{
    cleanupCoverCacheDir();
}

QString CoverArtManager::coverArtPath(const QString &filePath) const
{
    return m_coverArtCache.value(filePath);
}

bool CoverArtManager::isAcceptedImageMimeType(const QString &mimeType) const
{
    const QString type = normalizedMimeType(mimeType);
    return type == QStringLiteral("image/jpeg") ||
           type == QStringLiteral("image/jpg") ||
           type == QStringLiteral("image/png") ||
           type == QStringLiteral("image/webp") ||
           type == QStringLiteral("image/bmp");
}

bool CoverArtManager::isAcceptableImageSize(qint64 sizeBytes) const
{
    return sizeBytes > 0 && sizeBytes <= kMaxImageBytes;
}

void CoverArtManager::armReplyTimeout(QNetworkReply *reply) const
{
    if (!reply) {
        return;
    }

    QTimer::singleShot(kNetworkTimeoutMs, reply, [reply]() {
        if (reply->isRunning()) {
            qCWarning(deedCoverLog) << "Network timeout, aborting request:" << reply->url();
            reply->abort();
        }
    });
}

void CoverArtManager::cleanupCoverCacheDir() const
{
    const QDir dir(coverCacheDirPath());
    if (!dir.exists()) {
        return;
    }

    const QFileInfoList entries = dir.entryInfoList(
        QDir::Files | QDir::Readable,
        QDir::Time | QDir::Reversed
    );

    if (entries.size() <= kMaxCachedCoverFiles) {
        return;
    }

    const int removeCount = entries.size() - kMaxCachedCoverFiles;
    for (int i = 0; i < removeCount; ++i) {
        QFile::remove(entries.at(i).absoluteFilePath());
    }

    qCDebug(deedCoverLog) << "Cover cache cleanup removed" << removeCount << "old files";
}

void CoverArtManager::ensureCoverArt(const QString &filePath)
{
    if (filePath.isEmpty()) {
        qCDebug(deedCoverLog) << "Skipping cover lookup: empty path";
        return;
    }

    qCDebug(deedCoverLog) << "Ensuring cover art for" << filePath;

    if (!m_coverArtCache.contains(filePath)) {
        m_coverArtCache.insert(filePath, extractCoverArtToCache(filePath));
    }

    const QString cachedPath = m_coverArtCache.value(filePath);
    if (!cachedPath.isEmpty()) {
        qCDebug(deedCoverLog) << "Cover art already cached for" << filePath << "->" << cachedPath;
        emit coverArtUpdated(filePath, cachedPath);
        return;
    }

    const QString folderArt = findFolderCoverArt(filePath);
    if (!folderArt.isEmpty()) {
        qCDebug(deedCoverLog) << "Found folder cover art for" << filePath << "->" << folderArt;
        m_coverArtCache.insert(filePath, folderArt);
        emit coverArtUpdated(filePath, folderArt);
        return;
    }

    if (m_attemptedOnlineCoverFetches.contains(filePath)) {
        qCDebug(deedCoverLog) << "Skipping duplicate online lookup for" << filePath;
        return;
    }
    m_attemptedOnlineCoverFetches.insert(filePath);

    const TrackMetadata::Info info = TrackMetadata::read(filePath);
    const QString artist = info.artist.trimmed();
    const QString album = info.album.trimmed();
    const QString folderAlbum = cleanedFolderAlbumName(filePath);

    qCDebug(deedCoverLog) << "Metadata for cover search:"
                          << "album=" << album
                          << "artist=" << artist
                          << "simplifiedAlbum=" << simplifiedAlbumTitle(album)
                          << "folderAlbum=" << folderAlbum
                          << "simplifiedFolderAlbum=" << simplifiedAlbumTitle(folderAlbum);

    if (album.isEmpty() && folderAlbum.isEmpty()) {
        qCWarning(deedCoverLog) << "Cannot search for cover art: empty album tag and empty folder name for" << filePath;
        m_attemptedOnlineCoverFetches.remove(filePath);
        return;
    }

    startCoverSearch(filePath, album, artist, ExactAlbumArtist);
}

void CoverArtManager::startCoverSearch(const QString &filePath,
                                       const QString &album,
                                       const QString &artist,
                                       int searchStage)
{
    if (searchStage < 0 || searchStage >= SearchStageCount) {
        qCWarning(deedCoverLog) << "No cover search result after all fallback queries for" << filePath;
        m_attemptedOnlineCoverFetches.remove(filePath);
        return;
    }

    const QString folderAlbum = cleanedFolderAlbumName(filePath);
    const QString queryText = buildMusicBrainzQuery(album, artist, folderAlbum, searchStage);
    if (queryText.isEmpty()) {
        startCoverSearch(filePath, album, artist, searchStage + 1);
        return;
    }

    QUrl mbUrl(QStringLiteral("https://musicbrainz.org/ws/2/release/"));
    QUrlQuery mbQuery;
    mbQuery.addQueryItem(QStringLiteral("query"), queryText);
    mbQuery.addQueryItem(QStringLiteral("fmt"), QStringLiteral("json"));
    mbQuery.addQueryItem(QStringLiteral("limit"), QStringLiteral("8"));
    mbUrl.setQuery(mbQuery);

    qCDebug(deedCoverLog) << "Searching MusicBrainz for cover:" << queryText;

    QNetworkRequest request(mbUrl);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader("User-Agent", kUserAgent);
    request.setRawHeader("Accept", "application/json");

    QNetworkReply *reply = m_networkManager->get(request);
    reply->setProperty("filePath", filePath);
    reply->setProperty("album", album);
    reply->setProperty("artist", artist);
    reply->setProperty("searchStage", searchStage);
    armReplyTimeout(reply);
    connect(reply, &QNetworkReply::finished, this, &CoverArtManager::onCoverSearchFinished);
}

void CoverArtManager::removeForFile(const QString &filePath)
{
    m_coverArtCache.remove(filePath);
    m_attemptedOnlineCoverFetches.remove(filePath);
}

void CoverArtManager::clear()
{
    m_coverArtCache.clear();
    m_attemptedOnlineCoverFetches.clear();
    cleanupCoverCacheDir();
}

QString CoverArtManager::cacheCoverArt(const QString &filePath,
                                       const QByteArray &imageData,
                                       const QString &mimeType) const
{
    if (imageData.isEmpty() || !isAcceptableImageSize(imageData.size())) {
        qCWarning(deedCoverLog) << "Rejecting cached cover art for" << filePath
                                << "- invalid size:" << imageData.size();
        return QString();
    }

    const QString normalizedType = normalizedMimeType(mimeType);
    if (!isAcceptedImageMimeType(normalizedType)) {
        qCWarning(deedCoverLog) << "Rejecting cached cover art for" << filePath
                                << "- unsupported mime type:" << mimeType;
        return QString();
    }

    const QString baseDir = coverCacheDirPath();
    QDir().mkpath(baseDir);

    QString ext = QStringLiteral("img");
    if (normalizedType.contains(QStringLiteral("png"))) {
        ext = QStringLiteral("png");
    } else if (normalizedType.contains(QStringLiteral("jpeg")) ||
               normalizedType.contains(QStringLiteral("jpg"))) {
        ext = QStringLiteral("jpg");
    } else if (normalizedType.contains(QStringLiteral("webp"))) {
        ext = QStringLiteral("webp");
    } else if (normalizedType.contains(QStringLiteral("bmp"))) {
        ext = QStringLiteral("bmp");
    }

    const QByteArray hash = QCryptographicHash::hash(
        QFileInfo(filePath).absoluteFilePath().toUtf8(),
        QCryptographicHash::Sha1
    ).toHex();

    const QString outPath =
        baseDir + QStringLiteral("/") + QString::fromUtf8(hash) + QStringLiteral(".") + ext;

    QFile f(outPath);
    if (f.open(QIODevice::WriteOnly)) {
        if (f.write(imageData) == imageData.size()) {
            f.close();
            const_cast<CoverArtManager *>(this)->cleanupCoverCacheDir();
            qCDebug(deedCoverLog) << "Cached cover art for" << filePath << "->" << outPath;
            return outPath;
        }
        f.close();
        QFile::remove(outPath);
    }

    qCWarning(deedCoverLog) << "Failed to write cached cover art for" << filePath << "to" << outPath;
    return QString();
}

QString CoverArtManager::findFolderCoverArt(const QString &filePath) const
{
    const QDir dir = QFileInfo(filePath).dir();

    const QStringList imageFilters = {
        QStringLiteral("*.jpg"),
        QStringLiteral("*.jpeg"),
        QStringLiteral("*.png"),
        QStringLiteral("*.webp"),
        QStringLiteral("*.bmp")
    };

    const QFileInfoList imageFiles = dir.entryInfoList(
        imageFilters,
        QDir::Files | QDir::Readable,
        QDir::Name
    );

    if (imageFiles.isEmpty()) {
        return QString();
    }

    if (imageFiles.size() == 1) {
        return imageFiles.first().absoluteFilePath();
    }

    for (const QFileInfo &info : imageFiles) {
        const QString baseName = info.completeBaseName();
        if (baseName.contains(QStringLiteral("cover"), Qt::CaseInsensitive) ||
            baseName.contains(QStringLiteral("front"), Qt::CaseInsensitive)) {
            return info.absoluteFilePath();
        }
    }

    return QString();
}

QString CoverArtManager::saveDownloadedCoverArt(const QString &filePath,
                                                const QByteArray &imageData,
                                                const QString &contentType) const
{
    if (imageData.isEmpty() || !isAcceptableImageSize(imageData.size())) {
        qCWarning(deedCoverLog) << "Rejecting downloaded cover for" << filePath
                                << "- invalid size:" << imageData.size();
        return QString();
    }

    const QString normalizedType = normalizedMimeType(contentType);
    if (!isAcceptedImageMimeType(normalizedType)) {
        qCWarning(deedCoverLog) << "Rejecting downloaded cover for" << filePath
                                << "- unsupported mime type:" << contentType;
        return QString();
    }

    QString ext = QStringLiteral("jpg");
    if (normalizedType.contains(QStringLiteral("png"))) {
        ext = QStringLiteral("png");
    } else if (normalizedType.contains(QStringLiteral("jpeg")) ||
               normalizedType.contains(QStringLiteral("jpg"))) {
        ext = QStringLiteral("jpg");
    } else if (normalizedType.contains(QStringLiteral("webp"))) {
        ext = QStringLiteral("webp");
    } else if (normalizedType.contains(QStringLiteral("bmp"))) {
        ext = QStringLiteral("bmp");
    }

    const QDir songDir = QFileInfo(filePath).dir();
    const QString folderPath = songDir.filePath(QStringLiteral("cover.") + ext);

    QFile folderFile(folderPath);
    if (folderFile.open(QIODevice::WriteOnly)) {
        if (folderFile.write(imageData) == imageData.size()) {
            folderFile.close();
            qCDebug(deedCoverLog) << "Saved downloaded cover next to track:" << folderPath;
            return folderPath;
        }
        folderFile.close();
        QFile::remove(folderPath);
        qCWarning(deedCoverLog) << "Failed writing downloaded cover to song folder, falling back to cache:" << folderPath;
    }

    return cacheCoverArt(filePath, imageData, normalizedType);
}

void CoverArtManager::startCoverDownloadAttempt(const QString &filePath,
                                                const QStringList &mbids,
                                                int index)
{
    if (filePath.isEmpty() || index < 0 || index >= mbids.size()) {
        qCWarning(deedCoverLog) << "No downloadable cover art found after trying all candidate releases for" << filePath;
        m_attemptedOnlineCoverFetches.remove(filePath);
        return;
    }

    const QString mbid = mbids[index];
    const QUrl coverUrl(
        QStringLiteral("https://coverartarchive.org/release/%1/front-500").arg(mbid));

    qCDebug(deedCoverLog) << "Trying cover download" << (index + 1) << "/" << mbids.size()
                          << "for" << filePath << "MBID=" << mbid;

    QNetworkRequest request(coverUrl);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader("User-Agent", kUserAgent);
    request.setRawHeader("Accept", "image/jpeg,image/png,image/webp,image/bmp,*/*;q=0.1");

    QNetworkReply *reply = m_networkManager->get(request);
    reply->setProperty("filePath", filePath);
    reply->setProperty("mbids", mbids);
    reply->setProperty("mbidIndex", index);
    armReplyTimeout(reply);
    connect(reply, &QNetworkReply::finished, this, &CoverArtManager::onCoverDownloadFinished);
}

void CoverArtManager::onCoverSearchFinished()
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) {
        return;
    }

    const QString filePath = reply->property("filePath").toString();
    const QString album = reply->property("album").toString();
    const QString artist = reply->property("artist").toString();
    const int searchStage = reply->property("searchStage").toInt();

    if (reply->error() != QNetworkReply::NoError) {
        qCWarning(deedCoverLog) << "MusicBrainz search failed for" << filePath
                                << "stage=" << searchStage
                                << "error=" << reply->errorString();
        reply->deleteLater();
        startCoverSearch(filePath, album, artist, searchStage + 1);
        return;
    }

    const QByteArray jsonData = reply->readAll();
    reply->deleteLater();

    const QJsonDocument doc = QJsonDocument::fromJson(jsonData);
    if (!doc.isObject()) {
        qCWarning(deedCoverLog) << "MusicBrainz returned invalid JSON for" << filePath
                                << "stage=" << searchStage;
        startCoverSearch(filePath, album, artist, searchStage + 1);
        return;
    }

    const QJsonArray releases = doc.object().value(QStringLiteral("releases")).toArray();
    if (releases.isEmpty()) {
        qCDebug(deedCoverLog) << "No MusicBrainz release matches for" << filePath
                              << "stage=" << searchStage;
        startCoverSearch(filePath, album, artist, searchStage + 1);
        return;
    }

    QStringList mbids;
    mbids.reserve(releases.size());

    for (const QJsonValue &value : releases) {
        const QString mbid = value.toObject().value(QStringLiteral("id")).toString();
        if (!mbid.isEmpty() && !mbids.contains(mbid)) {
            mbids.append(mbid);
        }
    }

    if (mbids.isEmpty()) {
        qCDebug(deedCoverLog) << "MusicBrainz results had no usable MBIDs for" << filePath
                              << "stage=" << searchStage;
        startCoverSearch(filePath, album, artist, searchStage + 1);
        return;
    }

    qCDebug(deedCoverLog) << "MusicBrainz found" << mbids.size()
                          << "candidate releases for" << filePath
                          << "stage=" << searchStage;

    startCoverDownloadAttempt(filePath, mbids, 0);
}

void CoverArtManager::onCoverDownloadFinished()
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) {
        return;
    }

    const QString filePath = reply->property("filePath").toString();
    const QStringList mbids = reply->property("mbids").toStringList();
    const int index = reply->property("mbidIndex").toInt();

    if (reply->error() != QNetworkReply::NoError) {
        qCWarning(deedCoverLog) << "Cover download failed for" << filePath
                                << "MBID index" << index
                                << "error=" << reply->errorString();
        reply->deleteLater();
        startCoverDownloadAttempt(filePath, mbids, index + 1);
        return;
    }

    const QVariant declaredLength = reply->header(QNetworkRequest::ContentLengthHeader);
    if (declaredLength.isValid()) {
        const qint64 contentLength = declaredLength.toLongLong();
        if (!isAcceptableImageSize(contentLength)) {
            qCWarning(deedCoverLog) << "Rejecting downloaded cover by declared size for" << filePath
                                    << "size=" << contentLength;
            reply->deleteLater();
            startCoverDownloadAttempt(filePath, mbids, index + 1);
            return;
        }
    }

    const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
    if (!isAcceptedImageMimeType(contentType)) {
        qCWarning(deedCoverLog) << "Rejecting downloaded cover by mime type for" << filePath
                                << "mime=" << contentType;
            reply->deleteLater();
            startCoverDownloadAttempt(filePath, mbids, index + 1);
            return;
    }

    const QByteArray imageData = reply->readAll();
    reply->deleteLater();

    if (!isAcceptableImageSize(imageData.size())) {
        qCWarning(deedCoverLog) << "Rejecting downloaded cover by actual size for" << filePath
                                << "size=" << imageData.size();
        startCoverDownloadAttempt(filePath, mbids, index + 1);
        return;
    }

    const QString savedPath = saveDownloadedCoverArt(filePath, imageData, contentType);
    if (savedPath.isEmpty()) {
        qCWarning(deedCoverLog) << "Cover download succeeded but saving failed for" << filePath;
        startCoverDownloadAttempt(filePath, mbids, index + 1);
        return;
    }

    qCDebug(deedCoverLog) << "Cover download succeeded for" << filePath << "->" << savedPath;
    m_coverArtCache.insert(filePath, savedPath);
    m_attemptedOnlineCoverFetches.remove(filePath);
    emit coverArtUpdated(filePath, savedPath);
}

QString CoverArtManager::extractCoverArtToCache(const QString &filePath) const
{
    const QString suffix = QFileInfo(filePath).suffix().toLower();

    if (suffix == QStringLiteral("mp3")) {
        TagLib::MPEG::File file(filePath.toUtf8().constData());
        if (auto *tag = file.ID3v2Tag()) {
            const auto frames = tag->frameList("APIC");

            TagLib::ID3v2::AttachedPictureFrame *bestFrame = nullptr;

            for (auto *frame : frames) {
                auto *pic = dynamic_cast<TagLib::ID3v2::AttachedPictureFrame *>(frame);
                if (!pic) {
                    continue;
                }

                if (!bestFrame) {
                    bestFrame = pic;
                }

                if (pic->type() == TagLib::ID3v2::AttachedPictureFrame::FrontCover) {
                    bestFrame = pic;
                    break;
                }
            }

            if (bestFrame) {
                const auto picData = bestFrame->picture();
                const QString cached = cacheCoverArt(
                    filePath,
                    QByteArray(picData.data(), static_cast<int>(picData.size())),
                    QString::fromStdString(bestFrame->mimeType().to8Bit(true))
                );
                if (!cached.isEmpty()) {
                    qCDebug(deedCoverLog) << "Using embedded MP3 cover art for" << filePath;
                    return cached;
                }
            }
        }
    } else if (suffix == QStringLiteral("flac")) {
        TagLib::FLAC::File file(filePath.toUtf8().constData());
        const auto pictures = file.pictureList();
        if (!pictures.isEmpty() && pictures.front()) {
            auto *pic = pictures.front();
            const auto data = pic->data();
            const QString cached = cacheCoverArt(
                filePath,
                QByteArray(data.data(), static_cast<int>(data.size())),
                QString::fromStdString(pic->mimeType().to8Bit(true))
            );
            if (!cached.isEmpty()) {
                qCDebug(deedCoverLog) << "Using embedded FLAC cover art for" << filePath;
                return cached;
            }
        }
    }

    return findFolderCoverArt(filePath);
}
