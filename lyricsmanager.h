#ifndef LYRICSMANAGER_H
#define LYRICSMANAGER_H

#include <QObject>
#include <QHash>
#include <QList>
#include <QString>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QNetworkRequest;

class LyricsManager : public QObject
{
    Q_OBJECT

public:
    struct LyricLine {
        double timeSeconds = 0.0;
        QString text;
    };

    explicit LyricsManager(QObject *parent = nullptr);

    void fetchLyrics(const QString &title, const QString &artist, const QString &album);
    void clear();
    void setPlaybackPositionMs(qint64 positionMs);
    void setSyncOffsetMs(qint64 offsetMs);
    void adjustSyncOffsetMs(qint64 deltaMs);

    bool isLoading() const;
    QString errorString() const;
    QString plainLyrics() const;
    QString syncedLyrics() const;
    QList<LyricLine> lyricLines() const;
    int currentLyricIndex() const;
    bool hasSyncedLyrics() const;
    bool hasAnyLyrics() const;
    QString currentSource() const;
    QString lyricsType() const;
    qint64 syncOffsetMs() const;

signals:
    void lyricsChanged();
    void currentLyricIndexChanged(int index);
    void loadingChanged(bool loading);
    void errorChanged(const QString &error);
    void syncOffsetChanged(qint64 offsetMs);

private:
    struct PendingRequest {
        QString title;
        QString artist;
        QString album;
        QString key;
        int requestId = 0;
    };

    QString encodeQuery(const QHash<QString, QString> &params) const;
    QString stripFeat(const QString &text) const;
    QString normalizedArtistName(const QString &text) const;
    QString trackKey(const QString &title, const QString &artist, const QString &album) const;

    QList<LyricLine> parseLrc(const QString &text) const;
    void setCurrentLyricIndex(int index);
    void updateCurrentLyricIndex();

    bool applySyncedLyrics(const QString &text, const QString &sourceName);
    bool applyPlainLyrics(const QString &text, const QString &sourceName);
    void finishLoading();
    void finishWithError(const QString &errorText);
    bool isRequestCurrent(const PendingRequest &request) const;

    void armReplyTimeout(QNetworkReply *reply, int timeoutMs = 8000) const;
    void setCommonHeaders(QNetworkRequest &request) const;
    QString decodeHtmlEntities(QString text) const;
    QString extractGeniusLyrics(const QString &html) const;

    void startLrclibRequest(const PendingRequest &request);
    void continueAfterLrclib(const PendingRequest &request, const QString &lrclibPlainLyrics);
    void requestMusixmatchToken(const PendingRequest &request, const std::function<void(const QString &)> &callback);
    void fetchMusixmatchSynced(const PendingRequest &request,
                               const QString &token,
                               const QString &lrclibPlainLyrics);
    void fetchMusixmatchPlain(const PendingRequest &request,
                              const QString &token);
    void continueWithFallbacks(const PendingRequest &request);
    void fetchGeniusLyrics(const PendingRequest &request);
    void scrapeGeniusLyrics(const PendingRequest &request, const QUrl &url);
    void fetchLyricsOvhPlain(const PendingRequest &request);
    void fetchTextylPlain(const PendingRequest &request);

    QString bestMusixmatchTrackIdFromSearch(const QByteArray &jsonData,
                                            const QString &title,
                                            QString *commonTrackIdOut) const;
    QUrl musixmatchBaseUrl(const QString &path) const;

    QNetworkAccessManager *m_networkManager = nullptr;
    QList<LyricLine> m_lyricLines;
    QString m_plainLyrics;
    QString m_syncedLyrics;
    QString m_error;
    QString m_activeTrackKey;
    QString m_currentSource;
    QString m_currentLyricsType;
    QString m_cachedMusixmatchToken;
    qint64 m_playbackPositionMs = 0;
    qint64 m_syncOffsetMs = 0;
    qint64 m_musixmatchTokenFetchMs = 0;
    int m_currentLyricIndex = -1;
    int m_requestId = 0;
    bool m_loading = false;
    bool m_isFetchingToken = false;

    static constexpr const char *kGeniusApiKey = "1EwASVqwVHVFy7awmh-KLCrC_Uw27LwgGxV9gSiHQtj47o3Qfm5Pr2auB3DuvI0f";
};

#endif
