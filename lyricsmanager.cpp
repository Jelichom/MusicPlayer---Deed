#include "lyricsmanager.h"

#include <QDateTime>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <functional>

Q_LOGGING_CATEGORY(velionLyricsLog, "velion.lyrics")

namespace {
constexpr int kNetworkTimeoutMs = 8000;
constexpr int kMusixmatchTokenTtlMs = 10 * 60 * 1000;
constexpr int kMusixmatchTokenPollDelayMs = 250;
}

LyricsManager::LyricsManager(QObject *parent)
    : QObject(parent),
      m_networkManager(new QNetworkAccessManager(this))
{
}

void LyricsManager::fetchLyrics(const QString &title, const QString &artist, const QString &album)
{
    const QString cleanTitle = stripFeat(title.trimmed());
    const QString cleanArtist = normalizedArtistName(artist.trimmed());
    const QString cleanAlbum = album.trimmed();

    qCInfo(velionLyricsLog) << "Starting lyrics fetch"
                            << "title=" << cleanTitle
                            << "artist=" << cleanArtist
                            << "album=" << cleanAlbum;

    clear();
    m_activeTrackKey = trackKey(cleanTitle, cleanArtist, cleanAlbum);

    if (cleanTitle.isEmpty() || cleanArtist.isEmpty()) {
        qCWarning(velionLyricsLog) << "Cannot fetch lyrics because metadata is incomplete";
        finishWithError(QStringLiteral("Missing track metadata"));
        return;
    }

    ++m_requestId;

    m_loading = true;
    emit loadingChanged(true);
    emit lyricsChanged();

    PendingRequest request;
    request.title = cleanTitle;
    request.artist = cleanArtist;
    request.album = cleanAlbum;
    request.key = m_activeTrackKey;
    request.requestId = m_requestId;

    startLrclibRequest(request);
}

void LyricsManager::clear()
{
    const bool wasLoading = m_loading;

    m_plainLyrics.clear();
    m_syncedLyrics.clear();
    m_lyricLines.clear();
    m_error.clear();
    m_activeTrackKey.clear();
    m_currentSource.clear();
    m_currentLyricsType.clear();
    m_playbackPositionMs = 0;
    m_loading = false;

    setCurrentLyricIndex(-1);
    emit lyricsChanged();
    emit errorChanged(m_error);
    if (wasLoading) {
        emit loadingChanged(false);
    }
}

void LyricsManager::setPlaybackPositionMs(qint64 positionMs)
{
    m_playbackPositionMs = qMax<qint64>(0, positionMs);
    updateCurrentLyricIndex();
}

void LyricsManager::setSyncOffsetMs(qint64 offsetMs)
{
    const qint64 clamped = std::clamp<qint64>(offsetMs, -15000, 15000);
    if (m_syncOffsetMs == clamped) {
        return;
    }

    m_syncOffsetMs = clamped;
    qCInfo(velionLyricsLog) << "Lyrics sync offset set to" << m_syncOffsetMs << "ms";
    emit syncOffsetChanged(m_syncOffsetMs);
    updateCurrentLyricIndex();
}

void LyricsManager::adjustSyncOffsetMs(qint64 deltaMs)
{
    setSyncOffsetMs(m_syncOffsetMs + deltaMs);
}

bool LyricsManager::isLoading() const { return m_loading; }
QString LyricsManager::errorString() const { return m_error; }
QString LyricsManager::plainLyrics() const { return m_plainLyrics; }
QString LyricsManager::syncedLyrics() const { return m_syncedLyrics; }
QList<LyricsManager::LyricLine> LyricsManager::lyricLines() const { return m_lyricLines; }
int LyricsManager::currentLyricIndex() const { return m_currentLyricIndex; }
bool LyricsManager::hasSyncedLyrics() const { return !m_syncedLyrics.isEmpty() && !m_lyricLines.isEmpty(); }
bool LyricsManager::hasAnyLyrics() const { return hasSyncedLyrics() || !m_plainLyrics.isEmpty(); }
QString LyricsManager::currentSource() const { return m_currentSource.isEmpty() ? QStringLiteral("Unknown") : m_currentSource; }
QString LyricsManager::lyricsType() const { return m_currentLyricsType.isEmpty() ? QStringLiteral("None") : m_currentLyricsType; }
qint64 LyricsManager::syncOffsetMs() const { return m_syncOffsetMs; }

QString LyricsManager::encodeQuery(const QHash<QString, QString> &params) const
{
    QUrlQuery query;
    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        if (!it.value().trimmed().isEmpty()) {
            query.addQueryItem(it.key(), it.value());
        }
    }
    return query.query(QUrl::FullyEncoded);
}

QString LyricsManager::stripFeat(const QString &text) const
{
    QString result = text;
    result.replace(QRegularExpression(QStringLiteral(R"(\s*\((feat|ft)\..*?\)\s*)"),
                                      QRegularExpression::CaseInsensitiveOption), QStringLiteral(" "));
    result.replace(QRegularExpression(QStringLiteral(R"(\s*\[(feat|ft)\..*?\]\s*)"),
                                      QRegularExpression::CaseInsensitiveOption), QStringLiteral(" "));
    result.replace(QRegularExpression(QStringLiteral(R"(\s+)")), QStringLiteral(" "));
    return result.trimmed();
}

QString LyricsManager::normalizedArtistName(const QString &text) const
{
    QString result = text;
    result = result.split(QRegularExpression(QStringLiteral(R"(\s+(feat\.?|ft\.?)\s+)"),
                                             QRegularExpression::CaseInsensitiveOption)).value(0);
    result = result.split(QRegularExpression(QStringLiteral(R"([,&;])")), Qt::SkipEmptyParts).value(0).trimmed();
    return result.isEmpty() ? text.trimmed() : result;
}

QString LyricsManager::trackKey(const QString &title, const QString &artist, const QString &album) const
{
    return title.trimmed() + QStringLiteral("::") + artist.trimmed() + QStringLiteral("::") + album.trimmed();
}

QList<LyricsManager::LyricLine> LyricsManager::parseLrc(const QString &text) const
{
    QList<LyricLine> lines;
    if (text.trimmed().isEmpty()) {
        return lines;
    }

    const QStringList rows = text.split(QRegularExpression(QStringLiteral(R"(\r?\n)")));
    const QRegularExpression timeRe(QStringLiteral(R"(\[(\d{1,2}):(\d{2})(?:\.(\d{1,3}))?\])"));

    for (const QString &row : rows) {
        QRegularExpressionMatchIterator it = timeRe.globalMatch(row);
        QList<double> stamps;
        while (it.hasNext()) {
            const QRegularExpressionMatch match = it.next();
            const int minutes = match.captured(1).toInt();
            const int seconds = match.captured(2).toInt();
            const QString fracRaw = match.captured(3);
            double frac = 0.0;
            if (!fracRaw.isEmpty()) {
                if (fracRaw.length() == 3) {
                    frac = fracRaw.toInt() / 1000.0;
                } else if (fracRaw.length() == 2) {
                    frac = fracRaw.toInt() / 100.0;
                } else {
                    frac = fracRaw.toInt() / 10.0;
                }
            }
            stamps.append(minutes * 60 + seconds + frac);
        }

        QString textPart = row;
        textPart.remove(timeRe);
        textPart = textPart.trimmed();

        if (stamps.isEmpty() || textPart.isEmpty()) {
            continue;
        }

        for (double stamp : stamps) {
            LyricLine line;
            line.timeSeconds = stamp;
            line.text = textPart;
            lines.append(line);
        }
    }

    std::sort(lines.begin(), lines.end(), [](const LyricLine &a, const LyricLine &b) {
        return a.timeSeconds < b.timeSeconds;
    });
    return lines;
}

void LyricsManager::setCurrentLyricIndex(int index)
{
    if (m_currentLyricIndex == index) {
        return;
    }
    m_currentLyricIndex = index;
    emit currentLyricIndexChanged(index);
}

void LyricsManager::updateCurrentLyricIndex()
{
    if (!hasSyncedLyrics()) {
        setCurrentLyricIndex(-1);
        return;
    }

    const double positionSeconds = (m_playbackPositionMs - m_syncOffsetMs) / 1000.0;
    int index = -1;
    for (int i = 0; i < m_lyricLines.size(); ++i) {
        if (m_lyricLines.at(i).timeSeconds <= positionSeconds) {
            index = i;
        } else {
            break;
        }
    }
    setCurrentLyricIndex(index);
}

bool LyricsManager::applySyncedLyrics(const QString &text, const QString &sourceName)
{
    if (text.trimmed().isEmpty()) {
        return false;
    }

    const QList<LyricLine> parsed = parseLrc(text);
    if (parsed.isEmpty()) {
        return false;
    }

    m_error.clear();
    m_plainLyrics.clear();
    m_syncedLyrics = text.trimmed();
    m_lyricLines = parsed;
    m_currentSource = sourceName.trimmed().isEmpty() ? QStringLiteral("Unknown") : sourceName.trimmed();
    m_currentLyricsType = QStringLiteral("Synced");

    emit errorChanged(m_error);
    emit lyricsChanged();
    updateCurrentLyricIndex();
    return true;
}

bool LyricsManager::applyPlainLyrics(const QString &text, const QString &sourceName)
{
    QString cleaned = decodeHtmlEntities(text).trimmed();
    cleaned.replace(QRegularExpression(QStringLiteral(R"(\n\s*\*\*+.*$)")), QString());
    if (cleaned.isEmpty()) {
        return false;
    }

    m_error.clear();
    m_syncedLyrics.clear();
    m_lyricLines.clear();
    m_plainLyrics = cleaned;
    m_currentSource = sourceName.trimmed().isEmpty() ? QStringLiteral("Unknown") : sourceName.trimmed();
    m_currentLyricsType = QStringLiteral("Plain");

    setCurrentLyricIndex(-1);
    emit errorChanged(m_error);
    emit lyricsChanged();
    return true;
}

void LyricsManager::finishLoading()
{
    if (!m_loading) {
        return;
    }
    m_loading = false;
    qCInfo(velionLyricsLog) << "Lyrics loading finished"
                            << "source=" << m_currentSource
                            << "type=" << m_currentLyricsType;
    emit loadingChanged(false);
    emit lyricsChanged();
}

void LyricsManager::finishWithError(const QString &errorText)
{
    m_error = errorText;
    qCWarning(velionLyricsLog) << "Lyrics loading failed:" << errorText;
    emit errorChanged(m_error);
    finishLoading();
}

bool LyricsManager::isRequestCurrent(const PendingRequest &request) const
{
    return request.requestId == m_requestId && request.key == m_activeTrackKey;
}

void LyricsManager::armReplyTimeout(QNetworkReply *reply, int timeoutMs) const
{
    if (!reply) {
        return;
    }
    QTimer::singleShot(timeoutMs, reply, [reply]() {
        if (reply->isRunning()) {
            reply->abort();
        }
    });
}

void LyricsManager::setCommonHeaders(QNetworkRequest &request) const
{
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36"));
}

QString LyricsManager::decodeHtmlEntities(QString text) const
{
    return text.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "))
        .replace(QStringLiteral("&lt;"), QStringLiteral("<"))
        .replace(QStringLiteral("&gt;"), QStringLiteral(">"))
        .replace(QStringLiteral("&amp;"), QStringLiteral("&"))
        .replace(QStringLiteral("&#x27;"), QStringLiteral("'"))
        .replace(QStringLiteral("&#39;"), QStringLiteral("'"))
        .replace(QStringLiteral("&quot;"), QStringLiteral("\""));
}

QString LyricsManager::extractGeniusLyrics(const QString &html) const
{
    QString rawLyrics;
    QRegularExpression lyricsRegex(QStringLiteral(R"(<div[^>]*data-lyrics-container="true"[^>]*>([\s\S]*?)</div>)"));
    QRegularExpressionMatchIterator it = lyricsRegex.globalMatch(html);
    while (it.hasNext()) {
        rawLyrics += it.next().captured(1) + QStringLiteral("\n");
    }

    if (rawLyrics.isEmpty()) {
        QRegularExpression fallbackRegex(QStringLiteral(R"(<div[^>]*class="lyrics"[^>]*>([\s\S]*?)</div>)"),
                                         QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch match = fallbackRegex.match(html);
        if (match.hasMatch()) {
            rawLyrics = match.captured(1);
        }
    }

    if (rawLyrics.isEmpty()) {
        return QString();
    }

    rawLyrics.replace(QRegularExpression(QStringLiteral(R"(<br\s*/?>)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("\n"));
    rawLyrics.replace(QRegularExpression(QStringLiteral(R"(</p>)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("\n"));
    rawLyrics.replace(QRegularExpression(QStringLiteral(R"(<[^>]+>)")), QStringLiteral(""));
    rawLyrics = decodeHtmlEntities(rawLyrics).trimmed();
    rawLyrics.replace(QRegularExpression(QStringLiteral(R"(\[(Chorus|Verse|Bridge|Intro|Outro|Hook|Pre-Chorus|Post-Chorus)[^\]]*\])"),
                                         QRegularExpression::CaseInsensitiveOption), QStringLiteral(""));
    return rawLyrics.trimmed();
}

void LyricsManager::startLrclibRequest(const PendingRequest &request)
{
    qCInfo(velionLyricsLog) << "Trying LRCLIB for" << request.artist << "-" << request.title;
    QUrl url(QStringLiteral("https://lrclib.net/api/get"));
    url.setQuery(encodeQuery({
        {QStringLiteral("track_name"), request.title},
        {QStringLiteral("artist_name"), request.artist},
        {QStringLiteral("album_name"), request.album},
    }));

    QNetworkRequest networkRequest(url);
    setCommonHeaders(networkRequest);

    QNetworkReply *reply = m_networkManager->get(networkRequest);
    armReplyTimeout(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, request]() {
        const QByteArray payload = reply->readAll();
        reply->deleteLater();

        if (!isRequestCurrent(request)) {
            return;
        }

        QString lrclibPlain;
        if (reply->error() == QNetworkReply::NoError) {
            qCDebug(velionLyricsLog) << "LRCLIB request succeeded";
            const QJsonDocument json = QJsonDocument::fromJson(payload);
            if (json.isObject()) {
                const QJsonObject data = json.object();
                if (applySyncedLyrics(data.value(QStringLiteral("syncedLyrics")).toString(), QStringLiteral("LRCLIB"))) {
                    finishLoading();
                    return;
                }
                lrclibPlain = data.value(QStringLiteral("plainLyrics")).toString();
            }
        } else {
            qCWarning(velionLyricsLog) << "LRCLIB request failed:" << reply->errorString();
        }

        continueAfterLrclib(request, lrclibPlain);
    });
}

void LyricsManager::continueAfterLrclib(const PendingRequest &request, const QString &lrclibPlainLyrics)
{
    requestMusixmatchToken(request, [this, request, lrclibPlainLyrics](const QString &token) {
        if (!isRequestCurrent(request)) {
            return;
        }

        if (token.isEmpty()) {
            if (applyPlainLyrics(lrclibPlainLyrics, QStringLiteral("LRCLIB"))) {
                finishLoading();
                return;
            }
            continueWithFallbacks(request);
            return;
        }

        fetchMusixmatchSynced(request, token, lrclibPlainLyrics);
    });
}

void LyricsManager::requestMusixmatchToken(const PendingRequest &request, const std::function<void(const QString &)> &callback)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (!m_cachedMusixmatchToken.isEmpty() && (now - m_musixmatchTokenFetchMs) < kMusixmatchTokenTtlMs) {
        qCDebug(velionLyricsLog) << "Using cached Musixmatch token";
        callback(m_cachedMusixmatchToken);
        return;
    }

    if (m_isFetchingToken) {
        QTimer::singleShot(kMusixmatchTokenPollDelayMs, this, [this, request, callback]() {
            if (!isRequestCurrent(request)) {
                return;
            }
            requestMusixmatchToken(request, callback);
        });
        return;
    }

    m_isFetchingToken = true;
    qCInfo(velionLyricsLog) << "Requesting Musixmatch token";

    QUrl url(QStringLiteral("https://apic-desktop.musixmatch.com/ws/1.1/token.get"));
    url.setQuery(encodeQuery({
        {QStringLiteral("format"), QStringLiteral("json")},
        {QStringLiteral("app_id"), QStringLiteral("web-desktop-app-v1.0")},
    }));

    QNetworkRequest networkRequest(url);
    setCommonHeaders(networkRequest);

    QNetworkReply *reply = m_networkManager->get(networkRequest);
    armReplyTimeout(reply, 5000);
    connect(reply, &QNetworkReply::finished, this, [this, reply, request, callback]() {
        const QByteArray payload = reply->readAll();
        reply->deleteLater();
        m_isFetchingToken = false;

        if (!isRequestCurrent(request)) {
            return;
        }

        QString token;
        if (reply->error() == QNetworkReply::NoError) {
            qCDebug(velionLyricsLog) << "Musixmatch token request succeeded";
            const QJsonDocument json = QJsonDocument::fromJson(payload);
            if (json.isObject()) {
                token = json.object().value(QStringLiteral("message")).toObject()
                    .value(QStringLiteral("body")).toObject()
                    .value(QStringLiteral("user_token")).toString().trimmed();
            }
        }

        if (!token.isEmpty()) {
            m_cachedMusixmatchToken = token;
            m_musixmatchTokenFetchMs = QDateTime::currentMSecsSinceEpoch();
            qCInfo(velionLyricsLog) << "Musixmatch token acquired";
        } else {
            qCWarning(velionLyricsLog) << "Musixmatch token request failed or returned no token"
                                       << (reply->error() == QNetworkReply::NoError ? QStringLiteral("empty token") : reply->errorString());
        }

        callback(token);
    });
}

QString LyricsManager::bestMusixmatchTrackIdFromSearch(const QByteArray &jsonData,
                                                       const QString &title,
                                                       QString *commonTrackIdOut) const
{
    const QJsonDocument json = QJsonDocument::fromJson(jsonData);
    if (!json.isObject()) {
        return QString();
    }

    const QJsonArray tracks = json.object().value(QStringLiteral("message")).toObject()
        .value(QStringLiteral("body")).toObject()
        .value(QStringLiteral("track_list")).toArray();
    if (tracks.isEmpty()) {
        return QString();
    }

    QJsonObject bestTrack = tracks.first().toObject().value(QStringLiteral("track")).toObject();
    const QString searchTitle = title.trimmed().toLower();
    for (const QJsonValue &value : tracks) {
        const QJsonObject track = value.toObject().value(QStringLiteral("track")).toObject();
        if (track.value(QStringLiteral("track_name")).toString().trimmed().toLower() == searchTitle) {
            bestTrack = track;
            break;
        }
    }

    if (commonTrackIdOut) {
        *commonTrackIdOut = QString::number(bestTrack.value(QStringLiteral("commontrack_id")).toInt());
    }
    const int trackId = bestTrack.value(QStringLiteral("track_id")).toInt();
    return trackId > 0 ? QString::number(trackId) : QString();
}

QUrl LyricsManager::musixmatchBaseUrl(const QString &path) const
{
    return QUrl(QStringLiteral("https://apic-desktop.musixmatch.com/ws/1.1/") + path);
}

void LyricsManager::fetchMusixmatchSynced(const PendingRequest &request,
                                          const QString &token,
                                          const QString &lrclibPlainLyrics)
{
    qCInfo(velionLyricsLog) << "Trying Musixmatch synced lyrics";
    QUrl searchUrl = musixmatchBaseUrl(QStringLiteral("track.search"));
    searchUrl.setQuery(encodeQuery({
        {QStringLiteral("format"), QStringLiteral("json")},
        {QStringLiteral("namespace"), QStringLiteral("lyrics_synched")},
        {QStringLiteral("q"), request.artist + QStringLiteral(" ") + request.title},
        {QStringLiteral("q_artist"), request.artist},
        {QStringLiteral("q_track"), request.title},
        {QStringLiteral("page_size"), QStringLiteral("5")},
        {QStringLiteral("page"), QStringLiteral("1")},
        {QStringLiteral("f_has_lyrics"), QStringLiteral("1")},
        {QStringLiteral("usertoken"), token},
        {QStringLiteral("app_id"), QStringLiteral("web-desktop-app-v1.0")},
    }));

    QNetworkRequest networkRequest(searchUrl);
    setCommonHeaders(networkRequest);
    QNetworkReply *reply = m_networkManager->get(networkRequest);
    armReplyTimeout(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, request, token, lrclibPlainLyrics]() {
        const QByteArray payload = reply->readAll();
        reply->deleteLater();

        if (!isRequestCurrent(request)) {
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            qCWarning(velionLyricsLog) << "Musixmatch synced search failed:" << reply->errorString();
            if (applyPlainLyrics(lrclibPlainLyrics, QStringLiteral("LRCLIB"))) {
                finishLoading();
                return;
            }
            continueWithFallbacks(request);
            return;
        }

        QString commonTrackId;
        const QString trackId = bestMusixmatchTrackIdFromSearch(payload, request.title, &commonTrackId);
        if (trackId.isEmpty()) {
            qCWarning(velionLyricsLog) << "Musixmatch synced search returned no track id";
            if (applyPlainLyrics(lrclibPlainLyrics, QStringLiteral("LRCLIB"))) {
                finishLoading();
                return;
            }
            continueWithFallbacks(request);
            return;
        }

        QUrl lyricsUrl = musixmatchBaseUrl(QStringLiteral("track.subtitle.get"));
        lyricsUrl.setQuery(encodeQuery({
            {QStringLiteral("format"), QStringLiteral("json")},
            {QStringLiteral("namespace"), QStringLiteral("lyrics_synched")},
            {QStringLiteral("track_id"), trackId},
            {QStringLiteral("commontrack_id"), commonTrackId},
            {QStringLiteral("subtitle_format"), QStringLiteral("lrc")},
            {QStringLiteral("usertoken"), token},
            {QStringLiteral("app_id"), QStringLiteral("web-desktop-app-v1.0")},
        }));

        QNetworkRequest lyricsRequest(lyricsUrl);
        setCommonHeaders(lyricsRequest);
        QNetworkReply *lyricsReply = m_networkManager->get(lyricsRequest);
        armReplyTimeout(lyricsReply);
        connect(lyricsReply, &QNetworkReply::finished, this, [this, lyricsReply, request, token, lrclibPlainLyrics]() {
            const QByteArray lyricsPayload = lyricsReply->readAll();
            lyricsReply->deleteLater();

            if (!isRequestCurrent(request)) {
                return;
            }

            if (lyricsReply->error() == QNetworkReply::NoError) {
                qCDebug(velionLyricsLog) << "Musixmatch subtitle request succeeded";
                const QJsonDocument json = QJsonDocument::fromJson(lyricsPayload);
                const QString subtitle = json.object().value(QStringLiteral("message")).toObject()
                    .value(QStringLiteral("body")).toObject()
                    .value(QStringLiteral("subtitle")).toObject()
                    .value(QStringLiteral("subtitle_body")).toString();
                if (applySyncedLyrics(subtitle, QStringLiteral("Musixmatch"))) {
                    finishLoading();
                    return;
                }
            } else {
                qCWarning(velionLyricsLog) << "Musixmatch subtitle request failed:" << lyricsReply->errorString();
            }

            if (applyPlainLyrics(lrclibPlainLyrics, QStringLiteral("LRCLIB"))) {
                finishLoading();
                return;
            }
            fetchMusixmatchPlain(request, token);
        });
    });
}

void LyricsManager::fetchMusixmatchPlain(const PendingRequest &request, const QString &token)
{
    qCInfo(velionLyricsLog) << "Trying Musixmatch plain lyrics";
    QUrl searchUrl = musixmatchBaseUrl(QStringLiteral("track.search"));
    searchUrl.setQuery(encodeQuery({
        {QStringLiteral("format"), QStringLiteral("json")},
        {QStringLiteral("q"), request.artist + QStringLiteral(" ") + request.title},
        {QStringLiteral("q_artist"), request.artist},
        {QStringLiteral("q_track"), request.title},
        {QStringLiteral("page_size"), QStringLiteral("3")},
        {QStringLiteral("page"), QStringLiteral("1")},
        {QStringLiteral("f_has_lyrics"), QStringLiteral("1")},
        {QStringLiteral("usertoken"), token},
        {QStringLiteral("app_id"), QStringLiteral("web-desktop-app-v1.0")},
    }));

    QNetworkRequest networkRequest(searchUrl);
    setCommonHeaders(networkRequest);
    QNetworkReply *reply = m_networkManager->get(networkRequest);
    armReplyTimeout(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, request, token]() {
        const QByteArray payload = reply->readAll();
        reply->deleteLater();

        if (!isRequestCurrent(request)) {
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            qCWarning(velionLyricsLog) << "Musixmatch plain search failed:" << reply->errorString();
            continueWithFallbacks(request);
            return;
        }

        QString commonTrackId;
        const QString trackId = bestMusixmatchTrackIdFromSearch(payload, request.title, &commonTrackId);
        if (trackId.isEmpty()) {
            qCWarning(velionLyricsLog) << "Musixmatch plain search returned no track id";
            continueWithFallbacks(request);
            return;
        }

        QUrl lyricsUrl = musixmatchBaseUrl(QStringLiteral("track.lyrics.get"));
        lyricsUrl.setQuery(encodeQuery({
            {QStringLiteral("format"), QStringLiteral("json")},
            {QStringLiteral("track_id"), trackId},
            {QStringLiteral("commontrack_id"), commonTrackId},
            {QStringLiteral("usertoken"), token},
            {QStringLiteral("app_id"), QStringLiteral("web-desktop-app-v1.0")},
        }));

        QNetworkRequest lyricsRequest(lyricsUrl);
        setCommonHeaders(lyricsRequest);
        QNetworkReply *lyricsReply = m_networkManager->get(lyricsRequest);
        armReplyTimeout(lyricsReply);
        connect(lyricsReply, &QNetworkReply::finished, this, [this, lyricsReply, request]() {
            const QByteArray lyricsPayload = lyricsReply->readAll();
            lyricsReply->deleteLater();

            if (!isRequestCurrent(request)) {
                return;
            }

            if (lyricsReply->error() == QNetworkReply::NoError) {
                qCDebug(velionLyricsLog) << "Musixmatch lyrics request succeeded";
                const QJsonDocument json = QJsonDocument::fromJson(lyricsPayload);
                const QString lyricsBody = json.object().value(QStringLiteral("message")).toObject()
                    .value(QStringLiteral("body")).toObject()
                    .value(QStringLiteral("lyrics")).toObject()
                    .value(QStringLiteral("lyrics_body")).toString();
                if (applyPlainLyrics(lyricsBody, QStringLiteral("Musixmatch"))) {
                    finishLoading();
                    return;
                }
            } else {
                qCWarning(velionLyricsLog) << "Musixmatch lyrics request failed:" << lyricsReply->errorString();
            }

            continueWithFallbacks(request);
        });
    });
}

void LyricsManager::continueWithFallbacks(const PendingRequest &request)
{
    qCInfo(velionLyricsLog) << "Trying plain-lyrics fallback providers";
    fetchGeniusLyrics(request);
}

void LyricsManager::fetchGeniusLyrics(const PendingRequest &request)
{
    qCInfo(velionLyricsLog) << "Trying Genius lyrics";
    if (QString::fromUtf8(kGeniusApiKey).trimmed().isEmpty()) {
        fetchLyricsOvhPlain(request);
        return;
    }

    QUrl url(QStringLiteral("https://api.genius.com/search"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("q"), request.artist + QStringLiteral(" ") + request.title);
    url.setQuery(query);

    QNetworkRequest networkRequest(url);
    setCommonHeaders(networkRequest);
    networkRequest.setRawHeader("Authorization", QByteArray("Bearer ") + QByteArray(kGeniusApiKey));

    QNetworkReply *reply = m_networkManager->get(networkRequest);
    armReplyTimeout(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, request]() {
        const QByteArray payload = reply->readAll();
        reply->deleteLater();

        if (!isRequestCurrent(request)) {
            return;
        }

        if (reply->error() != QNetworkReply::NoError) {
            qCWarning(velionLyricsLog) << "Genius search failed:" << reply->errorString();
            fetchLyricsOvhPlain(request);
            return;
        }

        const QJsonDocument json = QJsonDocument::fromJson(payload);
        const QJsonArray hits = json.object().value(QStringLiteral("response")).toObject()
            .value(QStringLiteral("hits")).toArray();
        if (hits.isEmpty()) {
            qCWarning(velionLyricsLog) << "Genius search returned no hits";
            fetchLyricsOvhPlain(request);
            return;
        }

        QJsonObject bestResult = hits.first().toObject().value(QStringLiteral("result")).toObject();
        const QString searchTitle = request.title.trimmed().toLower();
        for (const QJsonValue &value : hits) {
            const QJsonObject result = value.toObject().value(QStringLiteral("result")).toObject();
            if (result.value(QStringLiteral("title")).toString().trimmed().toLower() == searchTitle) {
                bestResult = result;
                break;
            }
        }

        const QUrl songUrl(bestResult.value(QStringLiteral("url")).toString());
        if (!songUrl.isValid() || songUrl.isEmpty()) {
            qCWarning(velionLyricsLog) << "Genius search returned invalid song url";
            fetchLyricsOvhPlain(request);
            return;
        }

        scrapeGeniusLyrics(request, songUrl);
    });
}

void LyricsManager::scrapeGeniusLyrics(const PendingRequest &request, const QUrl &url)
{
    qCDebug(velionLyricsLog) << "Scraping Genius page:" << url;
    QNetworkRequest networkRequest(url);
    setCommonHeaders(networkRequest);

    QNetworkReply *reply = m_networkManager->get(networkRequest);
    armReplyTimeout(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, request]() {
        const QString html = QString::fromUtf8(reply->readAll());
        reply->deleteLater();

        if (!isRequestCurrent(request)) {
            return;
        }

        if (reply->error() == QNetworkReply::NoError) {
            const QString lyrics = extractGeniusLyrics(html);
            if (lyrics.size() > 20 && applyPlainLyrics(lyrics, QStringLiteral("Genius"))) {
                finishLoading();
                return;
            }
        }

        qCWarning(velionLyricsLog) << "Genius scrape yielded no usable lyrics";
        fetchLyricsOvhPlain(request);
    });
}

void LyricsManager::fetchLyricsOvhPlain(const PendingRequest &request)
{
    qCInfo(velionLyricsLog) << "Trying lyrics.ovh";
    const QUrl url(QStringLiteral("https://api.lyrics.ovh/v1/%1/%2")
                       .arg(QString::fromUtf8(QUrl::toPercentEncoding(request.artist)),
                            QString::fromUtf8(QUrl::toPercentEncoding(request.title))));

    QNetworkRequest networkRequest(url);
    setCommonHeaders(networkRequest);

    QNetworkReply *reply = m_networkManager->get(networkRequest);
    armReplyTimeout(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, request]() {
        const QByteArray payload = reply->readAll();
        reply->deleteLater();

        if (!isRequestCurrent(request)) {
            return;
        }

        if (reply->error() == QNetworkReply::NoError) {
            const QJsonDocument json = QJsonDocument::fromJson(payload);
            const QString lyrics = json.object().value(QStringLiteral("lyrics")).toString();
            if (applyPlainLyrics(lyrics, QStringLiteral("lyrics.ovh"))) {
                finishLoading();
                return;
            }
        }

        qCWarning(velionLyricsLog) << "lyrics.ovh returned no usable lyrics";
        fetchTextylPlain(request);
    });
}

void LyricsManager::fetchTextylPlain(const PendingRequest &request)
{
    qCInfo(velionLyricsLog) << "Trying Textyl";
    QUrl url(QStringLiteral("https://api.textyl.co/api/lyrics"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("q"), request.artist + QStringLiteral(" ") + request.title);
    url.setQuery(query);

    QNetworkRequest networkRequest(url);
    setCommonHeaders(networkRequest);

    QNetworkReply *reply = m_networkManager->get(networkRequest);
    armReplyTimeout(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, request]() {
        const QByteArray payload = reply->readAll();
        reply->deleteLater();

        if (!isRequestCurrent(request)) {
            return;
        }

        QString plain;
        if (reply->error() == QNetworkReply::NoError) {
            const QJsonDocument json = QJsonDocument::fromJson(payload);
            if (json.isObject()) {
                const QJsonObject data = json.object();
                if (data.value(QStringLiteral("lyrics")).isString()) {
                    plain = data.value(QStringLiteral("lyrics")).toString();
                } else if (data.value(QStringLiteral("text")).isString()) {
                    plain = data.value(QStringLiteral("text")).toString();
                } else if (data.value(QStringLiteral("lyrics")).isArray()) {
                    QStringList lines;
                    const QJsonArray arr = data.value(QStringLiteral("lyrics")).toArray();
                    for (const QJsonValue &value : arr) {
                        if (value.isString()) {
                            lines.append(value.toString());
                        } else if (value.isObject()) {
                            const QJsonObject obj = value.toObject();
                            const QString line = obj.value(QStringLiteral("text")).toString(obj.value(QStringLiteral("lyric")).toString());
                            if (!line.isEmpty()) {
                                lines.append(line);
                            }
                        }
                    }
                    plain = lines.join(QStringLiteral("\n"));
                }
            } else if (json.isArray()) {
                QStringList lines;
                for (const QJsonValue &value : json.array()) {
                    if (value.isString()) {
                        lines.append(value.toString());
                    }
                }
                plain = lines.join(QStringLiteral("\n"));
            } else {
                plain = QString::fromUtf8(payload);
            }
        }

        if (applyPlainLyrics(plain, QStringLiteral("Textyl"))) {
            finishLoading();
            return;
        }

        qCWarning(velionLyricsLog) << "Textyl returned no usable lyrics";
        finishWithError(QStringLiteral("No lyrics found"));
    });
}
