#include "playbacknotificationmanager.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QStringList>
#include <QVariant>

namespace {
constexpr const char *kNotificationsService = "org.freedesktop.Notifications";
constexpr const char *kNotificationsPath = "/org/freedesktop/Notifications";
constexpr const char *kNotificationsInterface = "org.freedesktop.Notifications";
constexpr const char *kApplicationName = "Velion";
constexpr const char *kDesktopEntry = "velion";
}

PlaybackNotificationManager::PlaybackNotificationManager(QObject *parent)
    : QObject(parent),
      m_notificationsInterface(new QDBusInterface(
          QString::fromUtf8(kNotificationsService),
          QString::fromUtf8(kNotificationsPath),
          QString::fromUtf8(kNotificationsInterface),
          QDBusConnection::sessionBus(),
          this)),
      m_notificationTimer(new QElapsedTimer()),
      m_notificationId(0)
{
}

PlaybackNotificationManager::~PlaybackNotificationManager()
{
    delete m_notificationTimer;
}

void PlaybackNotificationManager::notifyTrackChanged(const QString &title,
                                                     const QString &artist,
                                                     const QString &album,
                                                     const QString &trackNumber,
                                                     const QString &artPath)
{
    sendNotification(
        notificationSummary(artist, title),
        composePlayingBody(album, trackNumber),
        artPath);
}

void PlaybackNotificationManager::notifyPlaybackStarted(const QString &title,
                                                        const QString &artist,
                                                        const QString &album,
                                                        const QString &trackNumber,
                                                        const QString &artPath)
{
    sendNotification(
        notificationSummary(artist, title),
        composePlayingBody(album, trackNumber),
        artPath);
}

void PlaybackNotificationManager::notifyPlaybackPaused(const QString &title,
                                                       const QString &artist,
                                                       const QString &album,
                                                       const QString &trackNumber,
                                                       const QString &artPath)
{
    Q_UNUSED(album)
    Q_UNUSED(trackNumber)

    sendNotification(
        notificationSummary(artist, title),
        QStringLiteral("Paused"),
        artPath);
}

void PlaybackNotificationManager::notifyPlaylistFinished()
{
    sendNotification(
        QStringLiteral("Playlist finished"),
        QStringLiteral("Reached the end of the playlist."),
        QString());
}

QString PlaybackNotificationManager::notificationSummary(const QString &artist, const QString &title) const
{
    const QString cleanArtist = artist.trimmed();
    const QString cleanTitle = title.trimmed().isEmpty() ? QStringLiteral("Unknown track") : title.trimmed();

    if (cleanArtist.isEmpty()) {
        return cleanTitle;
    }

    return QStringLiteral("%1 - %2").arg(cleanArtist, cleanTitle);
}

QString PlaybackNotificationManager::composePlayingBody(const QString &album, const QString &trackNumber) const
{
    QStringList parts;

    if (!album.trimmed().isEmpty()) {
        parts.append(album.trimmed().toHtmlEscaped());
    }

    if (!trackNumber.trimmed().isEmpty()) {
        parts.append(QStringLiteral("Track %1").arg(trackNumber.trimmed().toHtmlEscaped()));
    }

    if (parts.isEmpty()) {
        return QStringLiteral("Playing");
    }

    return parts.join(QStringLiteral(", "));
}

void PlaybackNotificationManager::sendNotification(const QString &summary,
                                                   const QString &body,
                                                   const QString &artPath)
{
    if (!m_notificationsInterface || !m_notificationsInterface->isValid()) {
        return;
    }

    QVariantMap hints;
    hints.insert(QStringLiteral("desktop-entry"), QString::fromUtf8(kDesktopEntry));

    const QString appIcon = QString::fromUtf8(kDesktopEntry);
    const QString cleanArtPath = artPath.trimmed();
    if (!cleanArtPath.isEmpty() && QFileInfo::exists(cleanArtPath)) {
        hints.insert(QStringLiteral("image-path"), cleanArtPath);
        hints.insert(QStringLiteral("image_path"), cleanArtPath);
    }

    const uint replacesId = canReuseNotification() ? m_notificationId : 0;
    const int expireTimeoutMs = currentExpireTimeoutMs();

    QDBusReply<uint> reply = m_notificationsInterface->call(
        QStringLiteral("Notify"),
        QString::fromUtf8(kApplicationName),
        replacesId,
        appIcon,
        summary.toHtmlEscaped(),
        body,
        QStringList{},
        hints,
        expireTimeoutMs);

    if (!reply.isValid()) {
        return;
    }

    m_notificationId = reply.value();
    if (!canReuseNotification()) {
        m_notificationTimer->restart();
    }
}

int PlaybackNotificationManager::currentExpireTimeoutMs() const
{
    if (!canReuseNotification()) {
        return kNotificationTimeoutMs;
    }

    const qint64 elapsedMs = m_notificationTimer->elapsed();
    const int remainingMs = kNotificationTimeoutMs - static_cast<int>(elapsedMs);
    return remainingMs > kMinimumRemainingTimeoutMs
        ? remainingMs
        : kMinimumRemainingTimeoutMs;
}

bool PlaybackNotificationManager::canReuseNotification() const
{
    return m_notificationId != 0 &&
           m_notificationTimer->isValid() &&
           m_notificationTimer->elapsed() < kNotificationTimeoutMs;
}
