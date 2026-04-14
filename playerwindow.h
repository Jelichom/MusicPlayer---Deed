#ifndef PLAYERWINDOW_H
#define PLAYERWINDOW_H

#include <QHash>
#include <QMainWindow>
#include <QStringList>
#include <QSystemTrayIcon>
#include <QListWidget>
#include <QMenu>

#include "trackmetadata.h"

class QPushButton;
class QLabel;
class QTableWidgetItem;
class GstPlayerBackend;
class QDragEnterEvent;
class QDropEvent;
class QCloseEvent;
class QPoint;
class QWidget;
class QResizeEvent;


class ClickableSlider;
class PlaylistWidget;
class MprisService;
class CoverArtManager;
class PlaybackNotificationManager;
class LyricsManager;
class QSplitter;
class QStackedWidget;

class PlayerWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit PlayerWindow(QWidget *parent = nullptr);
    ~PlayerWindow() override;

    void handleCommandLineLaunch(const QStringList &files, bool fromRunningInstance);

    QString currentFilePath() const;
    QString currentTrackTitle() const;
    QString currentTrackArtist() const;
    QString currentTrackAlbum() const;
    QString currentTrackNumber() const;
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
    QString loopStatusString() const;
    bool shuffleEnabled() const;

public slots:
    void mprisPlay();
    void mprisPause();
    void mprisPlayPause();
    void mprisStop();
    void mprisNext();
    void mprisPrevious();
    void mprisSetPosition(qint64 position);
    void mprisSetVolume(double volume);
    void mprisSetLoopStatus(const QString &loopStatus);
    void mprisSetShuffle(bool enabled);
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

private:
    enum RepeatMode {
        RepeatNone,
        RepeatAll,
        RepeatOne
    };


    void setupUi();
    void setupConnections();

    void updateTimeLabel(qint64 position, qint64 duration);
    QString formatTime(qint64 ms) const;

    void openOpenFilesDialog();
    void addFilesToPlaylist(const QStringList &files);
    void insertFilesIntoPlaylist(const QStringList &files, int row, bool allowDuplicates = false);
    void replacePlaylist(const QStringList &files, bool autoPlay);
    void queueFilesNext(const QStringList &files);
    void resetPlayerUi();
    void loadTrack(int index, bool autoPlay, qint64 startPosition = 0, bool notifyTrackChange = true);
    void updatePlayPauseButton();
    void updateRepeatButton();
    void updateShuffleButton();
    void updatePlaylistSummary();
    qint64 totalPlaylistDurationMs() const;
    int nextTrackIndex(bool fromUserAction) const;
    int previousTrackIndex() const;

    void refreshPlaylistWidget();
    void updatePlaylistSelection();
    void updatePlaylistHighlight();
    void rebuildPlaylistFromWidgetOrder();
    void showPlaylistContextMenu(const QPoint &pos);

    void clearCurrentTrackDisplay(bool rememberForResume);
    void removeSelectedTracks();
    void moveSelectedTracksUp();
    void moveSelectedTracksDown();
    void clearPlaylist();

    void loadSettings();
    void saveSettings();

    void updateTrackInfoLabels();
    void updateCoverArt();
    void updateVolumeLabel();
    void updateHeaderSizing();
    void updateLyricsView();
    void updateLyricsCurrentLine();
    void refreshLyricsPlaylistView();
    void toggleLyricsPage();
    void updateLyricsStatusLabel();
    void updateLyricsSyncButtons();

    void requestPlay();
    void requestPause();
    void requestPlayPauseToggle();
    void notifyPlaybackStartedIfAvailable();
    void notifyPlaybackPausedIfAvailable();

    QList<int> selectedRowsSorted() const;
    void ensureMetadataCached(const QString &filePath);
    void rebuildMetadataCache();
    const TrackMetadata::Info *cachedMetadata(const QString &filePath) const;
    QString playlistDisplayTextForFile(const QString &filePath) const;
    QString playlistArtistForFile(const QString &filePath) const;
    QString playlistAlbumForFile(const QString &filePath) const;
    QString playlistLengthForFile(const QString &filePath) const;
    QString playlistBitrateForFile(const QString &filePath) const;
    QString playlistFileTypeForFile(const QString &filePath) const;
    QString playlistTrackNumberForFile(const QString &filePath) const;

    QPushButton *openButton;
    QPushButton *previousButton;
    QPushButton *playPauseButton;
    QPushButton *nextButton;
    QPushButton *stopButton;
    QPushButton *repeatButton;
    QPushButton *shuffleButton;
    QPushButton *lyricsDelayButton;
    QPushButton *lyricsResetSyncButton;
    QPushButton *lyricsFastenButton;
    QPushButton *lyricsToggleButton;

    QSystemTrayIcon *trayIcon;
    QMenu *trayMenu;
    QWidget *headerWidget;
    QWidget *playbackPanel;
    QLabel *coverLabel;
    QLabel *titleLabel;
    QLabel *albumLabel;
    QLabel *playlistSummaryLabel;
    QLabel *lyricsStatusLabel;
    QLabel *currentTimeLabel;
    QLabel *remainingTimeLabel;
    QLabel *volumePercentLabel;

    ClickableSlider *positionSlider;
    QStackedWidget *contentStack;
    QWidget *lyricsPage;
    QSplitter *lyricsSplitter;
    QListWidget *lyricsListWidget;
    QListWidget *lyricsPlaylistWidget;
    ClickableSlider *volumeSlider;
    PlaylistWidget *playlistWidget;

    GstPlayerBackend *player;
    MprisService *mprisService;
    CoverArtManager *coverArtManager;
    PlaybackNotificationManager *notificationManager;
    LyricsManager *lyricsManager;

    bool updatingVolumeFromMixer;
    QStringList playlist;
    int currentIndex;
    int resumeIndex;
    qint64 pendingRestorePosition;
    bool pendingRestoreAutoPlay;
    bool isUserSeeking;
    RepeatMode repeatMode;
    bool shuffleOn;
    QList<int> shuffleHistory;
    bool suppressShuffleHistoryRecording;

    QHash<QString, TrackMetadata::Info> metadataCache;
};

#endif
