#ifndef PLAYBACKNOTIFICATIONMANAGER_H
#define PLAYBACKNOTIFICATIONMANAGER_H

#include <QObject>
#include <QString>

class QDBusInterface;
class QElapsedTimer;

class PlaybackNotificationManager : public QObject
{
    Q_OBJECT

public:
    explicit PlaybackNotificationManager(QObject *parent = nullptr);
    ~PlaybackNotificationManager() override;

    void notifyTrackChanged(const QString &title,
                            const QString &artist,
                            const QString &album,
                            const QString &trackNumber,
                            const QString &artPath);
    void notifyPlaybackStarted(const QString &title,
                               const QString &artist,
                               const QString &album,
                               const QString &trackNumber,
                               const QString &artPath);
    void notifyPlaybackPaused(const QString &title,
                              const QString &artist,
                              const QString &album,
                              const QString &trackNumber,
                              const QString &artPath);
    void notifyPlaylistFinished();

private:
    QString notificationSummary(const QString &artist, const QString &title) const;
    QString composePlayingBody(const QString &album, const QString &trackNumber) const;
    void sendNotification(const QString &summary,
                          const QString &body,
                          const QString &artPath);
    int currentExpireTimeoutMs() const;
    bool canReuseNotification() const;

    QDBusInterface *m_notificationsInterface;
    QElapsedTimer *m_notificationTimer;
    uint m_notificationId;

    static constexpr int kNotificationTimeoutMs = 4000;
    static constexpr int kMinimumRemainingTimeoutMs = 500;
};

#endif
