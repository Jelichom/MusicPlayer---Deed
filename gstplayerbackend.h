#ifndef GSTPLAYERBACKEND_H
#define GSTPLAYERBACKEND_H

#include <QObject>
#include <QUrl>

#include <glib-object.h>
#include <gst/gst.h>

class QTimer;

class GstPlayerBackend : public QObject
{
    Q_OBJECT

public:
    enum PlaybackState {
        StoppedState,
        PlayingState,
        PausedState
    };
    Q_ENUM(PlaybackState)

    enum MediaStatus {
        NoMedia,
        LoadedMedia,
        BufferedMedia,
        EndOfMedia,
        InvalidMedia
    };
    Q_ENUM(MediaStatus)

    explicit GstPlayerBackend(QObject *parent = nullptr);
    ~GstPlayerBackend() override;

    void setSource(const QUrl &url);
    void play();
    void pause();
    void stop();

    void setPosition(qint64 positionMs);
    qint64 position() const;
    qint64 duration() const;

    void setVolume(double volume);
    double volume() const;

    PlaybackState playbackState() const;

    void handleVolumeNotify();
    void handleDeepElementAdded(GstElement *element);
    void handleDeepElementRemoved(GstElement *element);

signals:
    void durationChanged(qint64 duration);
    void positionChanged(qint64 position);
    void playbackStateChanged(GstPlayerBackend::PlaybackState state);
    void mediaStatusChanged(GstPlayerBackend::MediaStatus status);
    void metaDataChanged();
    void volumeChanged(double volume);

private:
    bool setupAudioBin();
    void clearAudioBin();
    void connectVolumeNotifications();
    void disconnectVolumeNotifications();
    void setControlledVolumeElement(GstElement *element);
    void setFallbackVolumeValue(double volume);

    void drainBus();
    void handleMessage(GstMessage *message);
    void pollPositionAndDuration();

    void updatePlaybackState(PlaybackState state);
    void updateMediaStatus(MediaStatus status);
    void syncPlaybackStateFromPipeline();
    bool requestPipelineState(GstState state);
    void resetPositionAndDuration();

    GstElement *m_playbin = nullptr;
    GstElement *m_audioBin = nullptr;
    GstElement *m_audioSink = nullptr;
    GstElement *m_fallbackVolumeElement = nullptr;
    GstElement *m_controlledVolumeElement = nullptr;
    GstBus *m_bus = nullptr;
    QTimer *m_busTimer = nullptr;
    QTimer *m_positionTimer = nullptr;
    gulong m_volumeNotifyHandlerId = 0;
    gulong m_deepElementAddedHandlerId = 0;
    gulong m_deepElementRemovedHandlerId = 0;

    QUrl m_source;
    qint64 m_positionMs = 0;
    qint64 m_durationMs = 0;
    double m_cachedVolume = 0.5;
    PlaybackState m_playbackState = StoppedState;
    MediaStatus m_mediaStatus = NoMedia;
};

#endif
