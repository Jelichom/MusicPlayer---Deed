#ifndef PLAYERWINDOW_H
#define PLAYERWINDOW_H

#include <QHash>
#include <QMainWindow>
#include <QSet>
#include <QStringList>
#include <pulse/subscribe.h>

class QPushButton;
class QLabel;
class QListWidgetItem;
class QMediaPlayer;
class QAudioOutput;
class QDragEnterEvent;
class QDropEvent;
class QCloseEvent;
class QPoint;
class QWidget;
class QResizeEvent;
class QNetworkAccessManager;
class QNetworkReply;

class ClickableSlider;
class PlaylistWidget;
class MprisService;

struct pa_threaded_mainloop;
struct pa_context;
struct pa_sink_input_info;

class PlayerWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit PlayerWindow(QWidget *parent = nullptr);
    ~PlayerWindow();

    QString currentFilePath() const;
    QString currentTrackTitle() const;
    QString currentTrackArtist() const;
    QString currentTrackAlbum() const;
    QString currentCoverArtPath() const;
    qint64 currentPosition() const;
    qint64 currentDuration() const;
    double currentVolume() const;
    bool canGoNext() const;
    bool canGoPrevious() const;
    bool canPlay() const;
    bool canPause() const;
    bool canControl() const;
    QString playbackStatusString() const;

public slots:
    void mprisPlay();
    void mprisPause();
    void mprisPlayPause();
    void mprisStop();
    void mprisNext();
    void mprisPrevious();
    void mprisSetPosition(qint64 position);
    void mprisSetVolume(double volume);
    void mprisRaise();

signals:
    void mprisPlaybackStateChanged();
    void mprisMetadataChanged();
    void mprisPositionChanged();
    void mprisVolumeChanged();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onCoverSearchFinished();
    void onCoverDownloadFinished();

private:
    void setupUi();
    void setupConnections();

    void updateTimeLabel(qint64 position, qint64 duration);
    QString formatTime(qint64 ms) const;

    void addFilesToPlaylist(const QStringList &files);
    void insertFilesIntoPlaylist(const QStringList &files, int row);
    void loadTrack(int index, bool autoPlay, qint64 startPosition = 0);
    void updatePlayPauseButton();

    void refreshPlaylistWidget();
    void updatePlaylistSelection();
    void updatePlaylistHighlight();
    void rebuildPlaylistFromWidgetOrder();
    void showPlaylistContextMenu(const QPoint &pos);

    void removeSelectedTracks();
    void moveSelectedTracksUp();
    void moveSelectedTracksDown();

    void loadSettings();
    void saveSettings();

    void updateTrackInfoLabels();
    void updateCoverArt();
    void updateVolumeLabel();
    void updateHeaderSizing();

    QList<int> selectedRowsSorted() const;

    QString readTitleWithTagLib(const QString &filePath) const;
    QString readArtistWithTagLib(const QString &filePath) const;
    QString readAlbumWithTagLib(const QString &filePath) const;

    QString extractCoverArtToCache(const QString &filePath) const;
    QString cacheCoverArt(const QString &filePath,
                          const QByteArray &imageData,
                          const QString &mimeType) const;
    QString findFolderCoverArt(const QString &filePath) const;
    QString saveDownloadedCoverArt(const QString &filePath,
                                   const QByteArray &imageData,
                                   const QString &contentType) const;

    void queueOnlineCoverArtFetch(const QString &filePath);
    void startCoverDownloadAttempt(const QString &filePath,
                                   const QStringList &mbids,
                                   int index);

    void startPulseSync();
    void stopPulseSync();
    void requestPulseSinkInputRefresh();
    void setPulseStreamVolume(int value);

    void applyPulseVolumeToUi(int value);
    void updatePulseSinkInputFromInfo(const pa_sink_input_info *info);

    static void pulseContextStateCallback(pa_context *context, void *userdata);
    static void pulseSubscribeCallback(pa_context *context,
                                        pa_subscription_event_type_t eventType,
                                        uint32_t index,
                                        void *userdata);
    static void pulseSinkInputInfoListCallback(pa_context *context,
                                               const pa_sink_input_info *info,
                                               int eol,
                                               void *userdata);

    QString playlistDisplayTextForFile(const QString &filePath) const;
    void rebuildTitleCache();

    QPushButton *openButton;
    QPushButton *previousButton;
    QPushButton *playPauseButton;
    QPushButton *nextButton;
    QPushButton *stopButton;

    QWidget *headerWidget;
    QLabel *coverLabel;
    QLabel *titleLabel;
    QLabel *albumLabel;
    QLabel *timeLabel;
    QLabel *volumePercentLabel;

    ClickableSlider *positionSlider;
    ClickableSlider *volumeSlider;

    PlaylistWidget *playlistWidget;

    QMediaPlayer *player;
    QAudioOutput *audioOutput;
    MprisService *mprisService;
    QNetworkAccessManager *networkManager;

    pa_threaded_mainloop *pulseMainloop;
    pa_context *pulseContext;
    uint32_t currentSinkInputIndex;
    bool pulseReady;
    bool updatingVolumeFromMixer;

    QStringList playlist;
    int currentIndex;

    QHash<QString, QString> titleCache;
    QHash<QString, QString> coverArtCache;
    QSet<QString> attemptedOnlineCoverFetches;

    qint64 pendingRestorePosition;
    bool pendingRestoreAutoPlay;

    bool isUserSeeking;
};

#endif