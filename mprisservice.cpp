#include "mprisservice.h"

#include "playerwindow.h"

#include <QCryptographicHash>
#include <QDBusMessage>
#include <QDebug>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QUrl>

Q_LOGGING_CATEGORY(deedMprisLog, "deed.mpris")

namespace {
constexpr const char *kServiceName = "org.mpris.MediaPlayer2.musicplayer";
constexpr const char *kObjectPath = "/org/mpris/MediaPlayer2";
constexpr const char *kRootInterface = "org.mpris.MediaPlayer2";
constexpr const char *kPlayerInterface = "org.mpris.MediaPlayer2.Player";

QString trackObjectPathForFile(const QString &filePath)
{
    if (filePath.isEmpty()) {
        return "/org/mpris/MediaPlayer2/TrackList/NoTrack";
    }

    const QByteArray hash = QCryptographicHash::hash(
        QFileInfo(filePath).absoluteFilePath().toUtf8(),
        QCryptographicHash::Sha1
    ).toHex();

    return "/org/mpris/MediaPlayer2/TrackList/track_" + QString::fromUtf8(hash);
}
}

MprisRootAdaptor::MprisRootAdaptor(PlayerWindow *player)
    : QDBusAbstractAdaptor(player), m_player(player)
{
    setAutoRelaySignals(true);
}

void MprisRootAdaptor::Raise()
{
    m_player->mprisRaise();
}

bool MprisRootAdaptor::canQuit() const { return false; }
bool MprisRootAdaptor::canRaise() const { return true; }
bool MprisRootAdaptor::hasTrackList() const { return false; }
QString MprisRootAdaptor::identity() const { return "MusicPlayer"; }
QString MprisRootAdaptor::desktopEntry() const { return "musicplayer"; }

QStringList MprisRootAdaptor::supportedUriSchemes() const
{
    return {"file"};
}

QStringList MprisRootAdaptor::supportedMimeTypes() const
{
    return {
        "audio/mpeg",
        "audio/flac",
        "audio/ogg",
        "audio/x-wav",
        "audio/wav"
    };
}

MprisPlayerAdaptor::MprisPlayerAdaptor(PlayerWindow *player)
    : QDBusAbstractAdaptor(player), m_player(player)
{
    setAutoRelaySignals(true);
}

void MprisPlayerAdaptor::Next() { m_player->mprisNext(); }
void MprisPlayerAdaptor::Previous() { m_player->mprisPrevious(); }
void MprisPlayerAdaptor::Pause() { m_player->mprisPause(); }
void MprisPlayerAdaptor::PlayPause() { m_player->mprisPlayPause(); }
void MprisPlayerAdaptor::Stop() { m_player->mprisStop(); }
void MprisPlayerAdaptor::Play() { m_player->mprisPlay(); }

void MprisPlayerAdaptor::Seek(qlonglong Offset)
{
    const qlonglong offsetMs = Offset / 1000;
    m_player->mprisSetPosition(m_player->currentPosition() + offsetMs);
}

void MprisPlayerAdaptor::SetPosition(const QDBusObjectPath &TrackId, qlonglong Position)
{
    const QString expected = trackObjectPathForFile(m_player->currentFilePath());
    if (TrackId.path() == expected) {
        m_player->mprisSetPosition(Position / 1000);
    }
}

QString MprisPlayerAdaptor::playbackStatus() const
{
    return m_player->playbackStatusString();
}

QString MprisPlayerAdaptor::loopStatus() const
{
    return m_player->loopStatusString();
}

void MprisPlayerAdaptor::setLoopStatus(const QString &loopStatus)
{
    m_player->mprisSetLoopStatus(loopStatus);
}

double MprisPlayerAdaptor::rate() const
{
    return 1.0;
}

bool MprisPlayerAdaptor::shuffle() const
{
    return m_player->shuffleEnabled();
}

void MprisPlayerAdaptor::setShuffle(bool shuffle)
{
    m_player->mprisSetShuffle(shuffle);
}

QVariantMap MprisPlayerAdaptor::metadata() const
{
    QVariantMap map;

    const QString filePath = m_player->currentFilePath();
    if (filePath.isEmpty()) {
        return map;
    }

    map.insert("mpris:trackid", QVariant::fromValue(QDBusObjectPath(trackObjectPathForFile(filePath))));
    map.insert("xesam:title", m_player->currentTrackTitle());

    const QString artist = m_player->currentTrackArtist();
    if (!artist.isEmpty()) {
        map.insert("xesam:artist", QStringList{artist});
    }

    const QString album = m_player->currentTrackAlbum();
    if (!album.isEmpty()) {
        map.insert("xesam:album", album);
    }

    const QString artPath = m_player->currentCoverArtPath();
    if (!artPath.isEmpty()) {
        map.insert("mpris:artUrl", QUrl::fromLocalFile(artPath).toString());
    }

    map.insert("mpris:length", static_cast<qlonglong>(m_player->currentDuration() * 1000));
    map.insert("xesam:url", QUrl::fromLocalFile(filePath).toString());

    return map;
}

double MprisPlayerAdaptor::volume() const
{
    return m_player->currentVolume();
}

void MprisPlayerAdaptor::setVolume(double volume)
{
    m_player->mprisSetVolume(volume);
}

qlonglong MprisPlayerAdaptor::position() const
{
    return static_cast<qlonglong>(m_player->currentPosition() * 1000);
}

double MprisPlayerAdaptor::minimumRate() const { return 1.0; }
double MprisPlayerAdaptor::maximumRate() const { return 1.0; }
bool MprisPlayerAdaptor::canGoNext() const { return m_player->canGoNext(); }
bool MprisPlayerAdaptor::canGoPrevious() const { return m_player->canGoPrevious(); }
bool MprisPlayerAdaptor::canPlay() const { return m_player->canPlay(); }
bool MprisPlayerAdaptor::canPause() const { return m_player->canPause(); }
bool MprisPlayerAdaptor::canSeek() const { return !m_player->currentFilePath().isEmpty(); }
bool MprisPlayerAdaptor::canControl() const { return m_player->canControl(); }

MprisService::MprisService(PlayerWindow *player)
    : QObject(player),
      m_player(player),
      m_rootAdaptor(new MprisRootAdaptor(player)),
      m_playerAdaptor(new MprisPlayerAdaptor(player)),
      m_connection(QDBusConnection::sessionBus())
{
    if (!m_connection.isConnected()) {
        qCWarning(deedMprisLog) << "Session bus unavailable; MPRIS integration disabled";
        return;
    }

    m_serviceRegistered = m_connection.registerService(kServiceName);
    if (!m_serviceRegistered) {
        qCWarning(deedMprisLog) << "Failed to register MPRIS service" << kServiceName
                                << "-" << m_connection.lastError().message();
        return;
    }

    m_objectRegistered = m_connection.registerObject(kObjectPath, player, QDBusConnection::ExportAdaptors);
    if (!m_objectRegistered) {
        qCWarning(deedMprisLog) << "Failed to register MPRIS object" << kObjectPath
                                << "-" << m_connection.lastError().message();
        m_connection.unregisterService(kServiceName);
        m_serviceRegistered = false;
        return;
    }

    connect(player, &PlayerWindow::mprisPlaybackStateChanged,
            this, &MprisService::notifyPlaybackStateChanged);
    connect(player, &PlayerWindow::mprisMetadataChanged,
            this, &MprisService::notifyMetadataChanged);
    connect(player, &PlayerWindow::mprisPositionChanged,
            this, &MprisService::notifyPositionChanged);
    connect(player, &PlayerWindow::mprisVolumeChanged,
            this, &MprisService::notifyVolumeChanged);

    m_isAvailable = true;
    qCDebug(deedMprisLog) << "MPRIS registered successfully";
}

MprisService::~MprisService()
{
    if (m_objectRegistered) {
        m_connection.unregisterObject(kObjectPath);
    }
    if (m_serviceRegistered) {
        m_connection.unregisterService(kServiceName);
    }
}

bool MprisService::isAvailable() const
{
    return m_isAvailable;
}

void MprisService::emitPropertiesChanged(const QString &interfaceName, const QVariantMap &changedProperties)
{
    if (!m_isAvailable) {
        return;
    }

    QDBusMessage msg = QDBusMessage::createSignal(
        kObjectPath,
        "org.freedesktop.DBus.Properties",
        "PropertiesChanged");

    msg << interfaceName << changedProperties << QStringList{};
    if (!m_connection.send(msg)) {
        qCWarning(deedMprisLog) << "Failed to send MPRIS PropertiesChanged for" << interfaceName;
    }
}

void MprisService::notifyPlaybackStateChanged()
{
    emitPropertiesChanged(kPlayerInterface, {
        {"PlaybackStatus", m_player->playbackStatusString()},
        {"CanPlay", m_player->canPlay()},
        {"CanPause", m_player->canPause()},
        {"CanGoNext", m_player->canGoNext()},
        {"CanGoPrevious", m_player->canGoPrevious()},
        {"CanControl", m_player->canControl()}
    });
}

void MprisService::notifyMetadataChanged()
{
    emitPropertiesChanged(kPlayerInterface, {
        {"Metadata", m_playerAdaptor->metadata()},
        {"CanSeek", !m_player->currentFilePath().isEmpty()},
        {"Position", static_cast<qlonglong>(m_player->currentPosition() * 1000)},
        {"LoopStatus", m_player->loopStatusString()},
        {"Shuffle", m_player->shuffleEnabled()},
        {"CanGoNext", m_player->canGoNext()},
        {"CanGoPrevious", m_player->canGoPrevious()}
    });
}

void MprisService::notifyPositionChanged()
{
    emitPropertiesChanged(kPlayerInterface, {
        {"Position", static_cast<qlonglong>(m_player->currentPosition() * 1000)}
    });
}

void MprisService::notifyVolumeChanged()
{
    emitPropertiesChanged(kPlayerInterface, {
        {"Volume", m_player->currentVolume()}
    });
}
