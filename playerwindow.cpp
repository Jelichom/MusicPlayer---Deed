#include "playerwindow.h"

#include "clickableslider.h"
#include "coverartmanager.h"
#include "gstplayerbackend.h"
#include "mprisservice.h"
#include "playbacknotificationmanager.h"
#include "playlistwidget.h"
#include "trackmetadata.h"
#include "lyricsmanager.h"

#include <QAction>
#include <QApplication>
#include <QSystemTrayIcon>
#include <QAbstractItemView>
#include <QBrush>
#include <QCloseEvent>
#include <QColor>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QDialog>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QListWidget>
#include <QScreen>
#include <QMenu>
#include <QMimeData>
#include <QPixmap>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QSettings>
#include <QSizePolicy>
#include <QStyle>
#include <QTableWidgetItem>
#include <QTimer>
#include <QUrl>
#include <QVariant>
#include <QVBoxLayout>
#include <QWidget>
#include <QWindow>

#include <algorithm>

namespace {
constexpr int kFilePathRole = Qt::UserRole + 1;
constexpr int kHeaderMinHeight = 120;
constexpr int kHeaderMaxHeight = 200;
constexpr int kHeaderMargin = 10;
constexpr int kCoverMinSize = 96;
constexpr int kCoverMaxSize = 220;

bool isSupportedAudioFile(const QString &filePath)
{
    const QString lower = filePath.toLower();
    return lower.endsWith(".mp3") ||
           lower.endsWith(".wav") ||
           lower.endsWith(".ogg") ||
           lower.endsWith(".flac");
}

QStringList sanitizedAudioFiles(const QStringList &files)
{
    QStringList result;

    for (const QString &filePath : files) {
        const QFileInfo info(filePath);
        if (info.exists() && info.isFile() && isSupportedAudioFile(info.absoluteFilePath())) {
            result.append(info.absoluteFilePath());
        }
    }

    return result;
}

QStringList audioFilesFromMimeData(const QMimeData *mimeData)
{
    QStringList files;
    if (!mimeData || !mimeData->hasUrls()) {
        return files;
    }

    for (const QUrl &url : mimeData->urls()) {
        if (!url.isLocalFile()) {
            continue;
        }

        const QString filePath = QFileInfo(url.toLocalFile()).absoluteFilePath();
        if (isSupportedAudioFile(filePath)) {
            files.append(filePath);
        }
    }

    return files;
}

QVariantList toVariantList(const QList<int> &values)
{
    QVariantList result;
    result.reserve(values.size());
    for (int value : values) {
        result.append(value);
    }
    return result;
}

QList<int> fromVariantList(const QVariant &value)
{
    QList<int> result;
    const QVariantList list = value.toList();
    result.reserve(list.size());
    for (const QVariant &entry : list) {
        result.append(entry.toInt());
    }
    return result;
}



QString remainingTimeText(qint64 remainingMs)
{
    if (remainingMs <= 0) {
        return QStringLiteral("00:00");
    }
    const int totalSeconds = static_cast<int>(remainingMs / 1000);
    const int minutes = totalSeconds / 60;
    const int seconds = totalSeconds % 60;
    return QStringLiteral("-%1:%2")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'));
}

}

class DockedOpenFileDialog : public QFileDialog
{
public:
    explicit DockedOpenFileDialog(QWidget *parent = nullptr)
        : QFileDialog(parent)
    {
        setAcceptDrops(true);
        setOption(QFileDialog::DontUseNativeDialog, true);
        setFileMode(QFileDialog::ExistingFiles);
        setNameFilter(QStringLiteral("Audio Files (*.mp3 *.wav *.ogg *.flac);;All Files (*)"));
        setDirectory(QStringLiteral("/home/nikos/Music"));
        setViewMode(QFileDialog::List);
    }

protected:
    void dragEnterEvent(QDragEnterEvent *event) override
    {
        if (!audioFilesFromMimeData(event->mimeData()).isEmpty()) {
            event->acceptProposedAction();
            return;
        }

        QFileDialog::dragEnterEvent(event);
    }

    void dragMoveEvent(QDragMoveEvent *event) override
    {
        if (!audioFilesFromMimeData(event->mimeData()).isEmpty()) {
            event->acceptProposedAction();
            return;
        }

        QFileDialog::dragMoveEvent(event);
    }

    void dropEvent(QDropEvent *event) override
    {
        const QStringList files = audioFilesFromMimeData(event->mimeData());
        if (files.isEmpty()) {
            QFileDialog::dropEvent(event);
            return;
        }

        setDirectory(QFileInfo(files.first()).absolutePath());
        for (const QString &filePath : files) {
            selectFile(filePath);
        }
        event->acceptProposedAction();
    }
};


PlayerWindow::PlayerWindow(QWidget *parent)
    : QMainWindow(parent),
      openButton(nullptr),
      previousButton(nullptr),
      playPauseButton(nullptr),
      nextButton(nullptr),
      stopButton(nullptr),
      repeatButton(nullptr),
      shuffleButton(nullptr),
      lyricsDelayButton(nullptr),
      lyricsResetSyncButton(nullptr),
      lyricsFastenButton(nullptr),
      lyricsToggleButton(nullptr),
      headerWidget(nullptr),
      playbackPanel(nullptr),
      coverLabel(nullptr),
      titleLabel(nullptr),
      albumLabel(nullptr),
      playlistSummaryLabel(nullptr),
      lyricsStatusLabel(nullptr),
      currentTimeLabel(nullptr),
      remainingTimeLabel(nullptr),
      volumePercentLabel(nullptr),
      positionSlider(nullptr),
      contentStack(nullptr),
      lyricsPage(nullptr),
      lyricsSplitter(nullptr),
      lyricsListWidget(nullptr),
      lyricsPlaylistWidget(nullptr),
      volumeSlider(nullptr),
      playlistWidget(nullptr),
      player(new GstPlayerBackend(this)),
      mprisService(nullptr),
      trayIcon(nullptr),
      trayMenu(nullptr),
      coverArtManager(new CoverArtManager(this)),
      notificationManager(new PlaybackNotificationManager(this)),
      lyricsManager(new LyricsManager(this)),
      updatingVolumeFromMixer(false),
      currentIndex(-1),
      resumeIndex(-1),
      pendingRestorePosition(0),
      pendingRestoreAutoPlay(false),
      isUserSeeking(false),
      repeatMode(RepeatNone),
      shuffleOn(false),
      suppressShuffleHistoryRecording(false)
{
    setAcceptDrops(true);

    setupUi();
    setupConnections();
    loadSettings();
    updatePlayPauseButton();
    updateVolumeLabel();
    updateHeaderSizing();

    setWindowTitle(QStringLiteral("Velion"));

        mprisService = new MprisService(this);

        if (QSystemTrayIcon::isSystemTrayAvailable()) {
            trayIcon = new QSystemTrayIcon(this);
            trayIcon->setIcon(QIcon::fromTheme(QStringLiteral("velion")));

            trayMenu = new QMenu(this);

            QAction *showAction = trayMenu->addAction(QStringLiteral("Show"));
            trayMenu->addSeparator();

            QAction *previousAction = trayMenu->addAction(QStringLiteral("Previous"));
            QAction *playPauseAction = trayMenu->addAction(QStringLiteral("Play / Pause"));
            QAction *nextAction = trayMenu->addAction(QStringLiteral("Next"));
            QAction *stopAction = trayMenu->addAction(QStringLiteral("Stop"));

            trayMenu->addSeparator();
            QAction *quitAction = trayMenu->addAction(QStringLiteral("Quit"));

            connect(showAction, &QAction::triggered, this, [this]() {
                show();
                raise();
                activateWindow();
            });

            connect(previousAction, &QAction::triggered, this, [this]() {
                mprisPrevious();
            });

            connect(playPauseAction, &QAction::triggered, this, [this]() {
                mprisPlayPause();
            });

            connect(stopAction, &QAction::triggered, this, [this]() {
                mprisStop();
            });

            connect(nextAction, &QAction::triggered, this, [this]() {
                mprisNext();
            });

            connect(quitAction, &QAction::triggered, this, [this]() {
                saveSettings();
                qApp->quit();
            });

            connect(trayIcon, &QSystemTrayIcon::activated, this,
                    [this](QSystemTrayIcon::ActivationReason reason) {
                if (reason == QSystemTrayIcon::Trigger) {
                    if (isVisible()) {
                        hide();
                    } else {
                        show();
                        raise();
                        activateWindow();
                    }
                }
            });

            auto updateTrayActions = [this, previousAction, playPauseAction, stopAction, nextAction]() {
                previousAction->setEnabled(canGoPrevious());
                playPauseAction->setEnabled(canPlay() || canPause());
                stopAction->setEnabled(canControl());
                nextAction->setEnabled(canGoNext());
            };

            connect(this, &PlayerWindow::mprisPlaybackStateChanged, this, updateTrayActions);
            connect(this, &PlayerWindow::mprisMetadataChanged, this, updateTrayActions);

            updateTrayActions();

            previousAction->setEnabled(canGoPrevious());
            playPauseAction->setEnabled(canPlay() || canPause());
            stopAction->setEnabled(canControl());
            nextAction->setEnabled(canGoNext());

            trayIcon->setContextMenu(trayMenu);
            trayIcon->setToolTip(QStringLiteral("Velion"));
            trayIcon->show();
        }
}

PlayerWindow::~PlayerWindow() = default;


void PlayerWindow::setupUi()
{
    auto *central = new QWidget(this);
    setCentralWidget(central);

    auto *mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(8);

    openButton = new QPushButton(this);
    previousButton = new QPushButton(this);
    playPauseButton = new QPushButton(this);
    nextButton = new QPushButton(this);
    stopButton = new QPushButton(this);
    repeatButton = new QPushButton(this);
    shuffleButton = new QPushButton(this);
    lyricsDelayButton = new QPushButton(this);
    lyricsResetSyncButton = new QPushButton(this);
    lyricsFastenButton = new QPushButton(this);
    lyricsToggleButton = new QPushButton(this);

    headerWidget = new QWidget(this);
    headerWidget->setMinimumHeight(kHeaderMinHeight);
    headerWidget->setMaximumHeight(kHeaderMaxHeight);
    headerWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto *headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(kHeaderMargin, kHeaderMargin, kHeaderMargin, kHeaderMargin);
    headerLayout->setSpacing(14);

    coverLabel = new QLabel(this);
    coverLabel->setAlignment(Qt::AlignCenter);
    coverLabel->setMinimumSize(kCoverMinSize, kCoverMinSize);
    coverLabel->setMaximumSize(kCoverMaxSize, kCoverMaxSize);
    coverLabel->setText(QStringLiteral("♪"));
    coverLabel->setStyleSheet(
        "QLabel {"
        "  border-radius: 10px;"
        "  background: rgba(255,255,255,0.06);"
        "  font-size: 24px;"
        "}"
    );

    auto styleHeaderToolButton = [](QPushButton *button, const QString &text, const QString &toolTip, int width = 30) {
        button->setFixedWidth(width);
        button->setFixedHeight(24);
        button->setFlat(true);
        button->setText(text);
        button->setToolTip(toolTip);
        button->setStyleSheet(QStringLiteral(
            "QPushButton { color: palette(mid); font-size: 13px; padding: 0px 4px; border: 1px solid transparent; border-radius: 4px; }"
            "QPushButton:hover { border-color: palette(mid); }"
            "QPushButton:checked { color: palette(button-text); border-color: palette(button-text); }"
            "QPushButton:disabled { color: palette(dark); }"
        ));
    };

    styleHeaderToolButton(lyricsDelayButton, QStringLiteral("−"), QStringLiteral("Delay lyrics by 0.5 seconds"));
    styleHeaderToolButton(lyricsResetSyncButton, QStringLiteral("0"), QStringLiteral("Reset lyric sync offset"), 26);
    styleHeaderToolButton(lyricsFastenButton, QStringLiteral("+"), QStringLiteral("Fasten lyrics by 0.5 seconds"));
    styleHeaderToolButton(lyricsToggleButton, QStringLiteral("♫"), QStringLiteral("Toggle lyrics view"), 34);
    lyricsToggleButton->setCheckable(true);

    lyricsStatusLabel = new QLabel(QStringLiteral("No lyrics"), this);
    lyricsStatusLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    lyricsStatusLabel->setMinimumWidth(170);
    lyricsStatusLabel->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);
    lyricsStatusLabel->setStyleSheet(QStringLiteral("QLabel { color: rgb(180, 190, 210); padding-right: 6px; }"));

    titleLabel = new QLabel(QStringLiteral("Drop audio files here or click Open"), this);
    titleLabel->setTextFormat(Qt::RichText);
    titleLabel->setWordWrap(true);

    albumLabel = new QLabel(QString(), this);
    albumLabel->setWordWrap(true);
    albumLabel->setStyleSheet(QStringLiteral("QLabel { color: rgb(180, 190, 210); }"));

    auto *infoLayout = new QVBoxLayout();
    infoLayout->setContentsMargins(0, 0, 0, 0);
    infoLayout->setSpacing(2);
    infoLayout->addStretch(1);
    infoLayout->addWidget(titleLabel);
    infoLayout->addWidget(albumLabel);
    infoLayout->addStretch(1);

    auto *headerControlsLayout = new QHBoxLayout();
    headerControlsLayout->setContentsMargins(0, 0, 0, 0);
    headerControlsLayout->setSpacing(4);
    headerControlsLayout->addStretch(1);
    headerControlsLayout->addWidget(lyricsStatusLabel, 0, Qt::AlignRight | Qt::AlignBottom);
    headerControlsLayout->addSpacing(6);
    headerControlsLayout->addWidget(lyricsDelayButton, 0, Qt::AlignBottom);
    headerControlsLayout->addWidget(lyricsResetSyncButton, 0, Qt::AlignBottom);
    headerControlsLayout->addWidget(lyricsFastenButton, 0, Qt::AlignBottom);
    headerControlsLayout->addWidget(lyricsToggleButton, 0, Qt::AlignBottom);

    auto *infoPanelLayout = new QVBoxLayout();
    infoPanelLayout->setContentsMargins(0, 0, 0, 0);
    infoPanelLayout->setSpacing(4);
    infoPanelLayout->addLayout(infoLayout, 1);
    infoPanelLayout->addLayout(headerControlsLayout, 0);

    headerLayout->addWidget(coverLabel, 0, Qt::AlignLeft | Qt::AlignTop);
    headerLayout->addLayout(infoPanelLayout, 1);

    currentTimeLabel = new QLabel(QStringLiteral("00:00"), this);
    currentTimeLabel->setMinimumWidth(40);

    remainingTimeLabel = new QLabel(QStringLiteral("00:00"), this);
    remainingTimeLabel->setMinimumWidth(48);
    remainingTimeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    playlistSummaryLabel = new QLabel(QStringLiteral("0 tracks - 00:00"), this);
    playlistSummaryLabel->setStyleSheet(QStringLiteral("QLabel { color: rgb(205, 212, 224); }"));
    playlistSummaryLabel->setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Preferred);

    positionSlider = new ClickableSlider(Qt::Horizontal, this);
    positionSlider->setRange(0, 0);
    positionSlider->setTracking(false);
    positionSlider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    volumeSlider = new ClickableSlider(Qt::Horizontal, this);
    volumeSlider->setRange(0, 100);
    volumeSlider->setValue(50);
    volumeSlider->setFixedWidth(120);

    volumePercentLabel = new QLabel(QStringLiteral("50%"), this);
    volumePercentLabel->setMinimumWidth(40);
    volumePercentLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    playlistWidget = new PlaylistWidget(this);
    playlistWidget->setMinimumHeight(220);
    playlistWidget->setSelectionMode(QAbstractItemView::ExtendedSelection);
    playlistWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    playlistWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    contentStack = new QStackedWidget(this);
    contentStack->addWidget(playlistWidget);

    lyricsPage = new QWidget(this);
    auto *lyricsPageLayout = new QVBoxLayout(lyricsPage);
    lyricsPageLayout->setContentsMargins(0, 0, 0, 0);
    lyricsPageLayout->setSpacing(4);

    lyricsSplitter = new QSplitter(Qt::Horizontal, lyricsPage);
    lyricsListWidget = new QListWidget(lyricsSplitter);
    lyricsPlaylistWidget = new QListWidget(lyricsSplitter);

    lyricsListWidget->setSelectionMode(QAbstractItemView::NoSelection);
    lyricsListWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    lyricsListWidget->setWordWrap(true);
    lyricsListWidget->setUniformItemSizes(false);
    lyricsListWidget->setAlternatingRowColors(false);

    lyricsPlaylistWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    lyricsPlaylistWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    lyricsSplitter->addWidget(lyricsListWidget);
    lyricsSplitter->addWidget(lyricsPlaylistWidget);
    lyricsSplitter->setChildrenCollapsible(false);
    lyricsSplitter->setStretchFactor(0, 3);
    lyricsSplitter->setStretchFactor(1, 2);

    lyricsPageLayout->addWidget(lyricsSplitter);
    contentStack->addWidget(lyricsPage);

    openButton->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    previousButton->setIcon(style()->standardIcon(QStyle::SP_MediaSkipBackward));
    playPauseButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    nextButton->setIcon(style()->standardIcon(QStyle::SP_MediaSkipForward));
    stopButton->setIcon(style()->standardIcon(QStyle::SP_MediaStop));

    openButton->setToolTip(QStringLiteral("Open files"));
    previousButton->setToolTip(QStringLiteral("Previous"));
    playPauseButton->setToolTip(QStringLiteral("Play / Pause"));
    nextButton->setToolTip(QStringLiteral("Next"));
    stopButton->setToolTip(QStringLiteral("Stop"));

    repeatButton->setFixedWidth(28);
    repeatButton->setFixedHeight(24);
    repeatButton->setFlat(true);

    shuffleButton->setFixedWidth(28);
    shuffleButton->setFixedHeight(24);
    shuffleButton->setCheckable(true);
    shuffleButton->setFlat(true);

    auto *topRowLayout = new QHBoxLayout();
    topRowLayout->setContentsMargins(0, 0, 0, 0);
    topRowLayout->setSpacing(4);
    topRowLayout->addWidget(openButton);
    topRowLayout->addWidget(previousButton);
    topRowLayout->addWidget(playPauseButton);
    topRowLayout->addWidget(nextButton);
    topRowLayout->addWidget(stopButton);

    auto *controlsSeparator = new QFrame(this);
    controlsSeparator->setFrameShape(QFrame::VLine);
    controlsSeparator->setFrameShadow(QFrame::Plain);
    controlsSeparator->setStyleSheet(QStringLiteral("color: palette(mid);"));
    topRowLayout->addWidget(controlsSeparator);
    topRowLayout->addStretch(1);

    auto *volumeSeparator = new QFrame(this);
    volumeSeparator->setFrameShape(QFrame::VLine);
    volumeSeparator->setFrameShadow(QFrame::Plain);
    volumeSeparator->setStyleSheet(QStringLiteral("color: palette(mid);"));
    topRowLayout->addWidget(volumeSeparator);

    auto *volumeText = new QLabel(QStringLiteral("Vol"), this);
    volumeText->setStyleSheet(QStringLiteral("QLabel { color: palette(mid); }"));

    topRowLayout->addWidget(volumeText);
    topRowLayout->addWidget(volumeSlider);
    topRowLayout->addWidget(volumePercentLabel);

    auto *horizontalSeparator = new QFrame(this);
    horizontalSeparator->setFrameShape(QFrame::HLine);
    horizontalSeparator->setFrameShadow(QFrame::Plain);
    horizontalSeparator->setStyleSheet(QStringLiteral("color: palette(mid);"));

    playbackPanel = new QWidget(this);
    playbackPanel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto *panelLayout = new QHBoxLayout(playbackPanel);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    panelLayout->setSpacing(4);
    panelLayout->addWidget(playlistSummaryLabel, 0);
    panelLayout->addWidget(repeatButton);
    panelLayout->addSpacing(1);
    panelLayout->addWidget(shuffleButton);
    panelLayout->addWidget(currentTimeLabel);
    panelLayout->addWidget(positionSlider, 1);
    panelLayout->addWidget(remainingTimeLabel);

    mainLayout->addWidget(headerWidget);
    mainLayout->addWidget(contentStack, 1);
    mainLayout->addLayout(topRowLayout);
    mainLayout->addWidget(horizontalSeparator);
    mainLayout->addWidget(playbackPanel);

    lyricsDelayButton->setVisible(false);
    lyricsResetSyncButton->setVisible(false);
    lyricsFastenButton->setVisible(false);
    lyricsStatusLabel->setVisible(false);

    updateRepeatButton();
    updateShuffleButton();
    updatePlaylistSummary();
    refreshLyricsPlaylistView();
    updateLyricsView();
}

void PlayerWindow::setupConnections()
{
    connect(openButton, &QPushButton::clicked, this, &PlayerWindow::openOpenFilesDialog);

    connect(previousButton, &QPushButton::clicked, this, [this]() {
        if (shuffleOn && !shuffleHistory.isEmpty()) {
            const int newIndex = shuffleHistory.takeLast();
            suppressShuffleHistoryRecording = true;
            loadTrack(newIndex, true);
            return;
        }

        const int newIndex = previousTrackIndex();
        if (newIndex < 0) {
            return;
        }
        loadTrack(newIndex, true);
    });

    connect(nextButton, &QPushButton::clicked, this, [this]() {
        const int newIndex = nextTrackIndex(true);
        if (newIndex < 0) {
            clearCurrentTrackDisplay(false);
            return;
        }
        loadTrack(newIndex, true);
    });

    connect(playPauseButton, &QPushButton::clicked, this, [this]() {
        requestPlayPauseToggle();
    });

    connect(stopButton, &QPushButton::clicked, this, [this]() {
        clearCurrentTrackDisplay(true);
    });

    connect(repeatButton, &QPushButton::clicked, this, [this]() {
        switch (repeatMode) {
        case RepeatNone:
            mprisSetLoopStatus(QStringLiteral("Playlist"));
            break;
        case RepeatAll:
            mprisSetLoopStatus(QStringLiteral("Track"));
            break;
        case RepeatOne:
        default:
            mprisSetLoopStatus(QStringLiteral("None"));
            break;
        }
    });

    connect(shuffleButton, &QPushButton::clicked, this, [this]() {
        mprisSetShuffle(shuffleButton->isChecked());
    });

    connect(lyricsToggleButton, &QPushButton::toggled, this, [this](bool checked) {
        contentStack->setCurrentWidget(checked ? lyricsPage : playlistWidget);

        lyricsDelayButton->setVisible(checked);
        lyricsResetSyncButton->setVisible(checked);
        lyricsFastenButton->setVisible(checked);
        lyricsStatusLabel->setVisible(checked);
    });
    connect(lyricsResetSyncButton, &QPushButton::clicked, this, [this]() {
        lyricsManager->setSyncOffsetMs(0);
    });
    connect(lyricsFastenButton, &QPushButton::clicked, this, [this]() {
        lyricsManager->adjustSyncOffsetMs(-500);
    });

    connect(volumeSlider, &ClickableSlider::valueChanged, this, [this](int value) {
        if (updatingVolumeFromMixer) {
            return;
        }

        player->setVolume(value / 100.0);
        updateVolumeLabel();
        emit mprisVolumeChanged();
    });

    connect(player, &GstPlayerBackend::volumeChanged, this, [this](double volume) {
        const int sliderValue = std::clamp(static_cast<int>(volume * 100.0 + 0.5),
                                           volumeSlider->minimum(),
                                           volumeSlider->maximum());

        if (volumeSlider->value() == sliderValue) {
            return;
        }

        updatingVolumeFromMixer = true;
        volumeSlider->setValue(sliderValue);
        updateVolumeLabel();
        emit mprisVolumeChanged();
        updatingVolumeFromMixer = false;
    });

    connect(player, &GstPlayerBackend::durationChanged, this, [this](qint64 duration) {
        positionSlider->setRange(0, static_cast<int>(duration));

        if (!isUserSeeking) {
            updateTimeLabel(player->position(), duration);
        }

        emit mprisMetadataChanged();
    });

    connect(player, &GstPlayerBackend::positionChanged, this, [this](qint64 position) {
        if (!isUserSeeking && !positionSlider->isSliderDown()) {
            positionSlider->setValue(static_cast<int>(position));
            updateTimeLabel(position, player->duration());
        }

        lyricsManager->setPlaybackPositionMs(position);
        emit mprisPositionChanged();
    });

    connect(positionSlider, &ClickableSlider::sliderPressed, this, [this]() {
        isUserSeeking = true;
    });

    connect(positionSlider, &ClickableSlider::sliderMoved, this, [this](int position) {
        updateTimeLabel(position, player->duration());
        lyricsManager->setPlaybackPositionMs(position);
    });

    connect(positionSlider, &ClickableSlider::sliderReleased, this, [this]() {
        player->setPosition(positionSlider->sliderPosition());
        lyricsManager->setPlaybackPositionMs(positionSlider->sliderPosition());
        isUserSeeking = false;
        emit mprisPositionChanged();
    });

    connect(player, &GstPlayerBackend::playbackStateChanged, this, [this]() {
        updatePlayPauseButton();
        emit mprisPlaybackStateChanged();
    });

    connect(player, &GstPlayerBackend::mediaStatusChanged, this, [this](GstPlayerBackend::MediaStatus status) {
        if (status == GstPlayerBackend::LoadedMedia || status == GstPlayerBackend::BufferedMedia) {
            if (pendingRestorePosition > 0) {
                player->setPosition(pendingRestorePosition);
                pendingRestorePosition = 0;
            }

            if (pendingRestoreAutoPlay) {
                player->play();
                pendingRestoreAutoPlay = false;
            }

            updateTrackInfoLabels();
            updateCoverArt();

            emit mprisMetadataChanged();
            emit mprisPositionChanged();
            emit mprisPlaybackStateChanged();
        }

        if (status == GstPlayerBackend::EndOfMedia && !playlist.isEmpty()) {
            if (repeatMode == RepeatOne && currentIndex >= 0 && currentIndex < playlist.size()) {
                loadTrack(currentIndex, true);
                return;
            }

            const int newIndex = nextTrackIndex(false);
            if (newIndex < 0) {
                notificationManager->notifyPlaylistFinished();
                clearCurrentTrackDisplay(false);
                return;
            }
            loadTrack(newIndex, true);
        }
    });

    connect(player, &GstPlayerBackend::metaDataChanged, this, [this]() {
        updateTrackInfoLabels();
        updateCoverArt();
        emit mprisMetadataChanged();
    });

    connect(playlistWidget, &PlaylistWidget::rowDoubleClicked, this, [this](int row) {
        loadTrack(row, true);
    });

    connect(playlistWidget, &PlaylistWidget::itemsReordered, this, [this]() {
        rebuildPlaylistFromWidgetOrder();
        emit mprisMetadataChanged();
    });

    connect(playlistWidget, &PlaylistWidget::externalFilesDropped,
            this, [this](const QStringList &files, int row) {
        const bool hadNoCurrentTrack = (currentIndex < 0);
        insertFilesIntoPlaylist(files, row, true);

        if (hadNoCurrentTrack && !playlist.isEmpty()) {
            const int startIndex = std::clamp(row, 0, static_cast<int>(playlist.size()) - 1);
            loadTrack(startIndex, true);
        }
    });

    connect(playlistWidget, &QWidget::customContextMenuRequested,
            this, &PlayerWindow::showPlaylistContextMenu);

    connect(coverArtManager, &CoverArtManager::coverArtUpdated, this,
            [this](const QString &filePath, const QString &) {
        if (currentFilePath() == filePath) {
            updateCoverArt();
            emit mprisMetadataChanged();
        }
    });

    connect(lyricsPlaylistWidget, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        if (!item) {
            return;
        }
        const int row = item->data(Qt::UserRole).toInt();
        if (row >= 0 && row < playlist.size()) {
            loadTrack(row, true);
        }
    });

    connect(lyricsSplitter, &QSplitter::splitterMoved, this, [this](int, int) {
        QSettings settings(QStringLiteral("Nikos"), QStringLiteral("Velion"));
        settings.setValue(QStringLiteral("lyrics/splitterSizes"),
                          lyricsSplitter ? toVariantList(lyricsSplitter->sizes()) : QVariantList{});
    });

    connect(lyricsManager, &LyricsManager::lyricsChanged, this, &PlayerWindow::updateLyricsView);
    connect(lyricsManager, &LyricsManager::loadingChanged, this, [this](bool) {
        updateLyricsView();
        updateLyricsStatusLabel();
        updateLyricsSyncButtons();
    });
    connect(lyricsManager, &LyricsManager::errorChanged, this, [this](const QString &) {
        updateLyricsView();
        updateLyricsStatusLabel();
    });
    connect(lyricsManager, &LyricsManager::currentLyricIndexChanged, this, [this](int) {
        updateLyricsCurrentLine();
    });
    connect(lyricsManager, &LyricsManager::syncOffsetChanged, this, [this](qint64 offsetMs) {
        updateLyricsStatusLabel();
        updateLyricsSyncButtons();
        QSettings settings(QStringLiteral("Nikos"), QStringLiteral("Velion"));
        settings.setValue(QStringLiteral("lyrics/syncOffsetMs"), offsetMs);
    });

    updateLyricsStatusLabel();
    updateLyricsSyncButtons();
}

void PlayerWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    updateHeaderSizing();
    updateCoverArt();
}

void PlayerWindow::updateHeaderSizing()
{
    const int h = height();

    int headerHeight = h / 5;
    if (headerHeight < kHeaderMinHeight) {
        headerHeight = kHeaderMinHeight;
    }
    if (headerHeight > kHeaderMaxHeight) {
        headerHeight = kHeaderMaxHeight;
    }

    headerWidget->setFixedHeight(headerHeight);

    int coverSize = headerHeight - (2 * kHeaderMargin);
    if (coverSize < kCoverMinSize) {
        coverSize = kCoverMinSize;
    }
    if (coverSize > kCoverMaxSize) {
        coverSize = kCoverMaxSize;
    }

    coverLabel->setFixedSize(coverSize, coverSize);
    coverLabel->setStyleSheet(QString(
        "QLabel {"
        "  border-radius: %1px;"
        "  background: rgba(255,255,255,0.06);"
        "  font-size: %2px;"
        "}"
    ).arg(coverSize / 10).arg(coverSize / 3));

    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(11);
    titleLabel->setFont(titleFont);

    QFont albumFont = albumLabel->font();
    albumFont.setPointSize(9);
    albumLabel->setFont(albumFont);
}


void PlayerWindow::toggleLyricsPage()
{
    const bool showLyrics = lyricsToggleButton && lyricsToggleButton->isChecked();
    if (contentStack) {
        contentStack->setCurrentWidget(showLyrics ? lyricsPage : playlistWidget);
    }
}

void PlayerWindow::updateLyricsStatusLabel()
{
    if (!lyricsStatusLabel || !lyricsManager) {
        return;
    }

    QString status;
    if (lyricsManager->isLoading()) {
        status = QStringLiteral("Loading lyrics…");
    } else if (lyricsManager->hasAnyLyrics()) {
        status = QStringLiteral("%1 · %2")
                     .arg(lyricsManager->lyricsType(), lyricsManager->currentSource());
    } else if (!lyricsManager->errorString().trimmed().isEmpty()) {
        status = lyricsManager->errorString();
    } else {
        status = QStringLiteral("No lyrics");
    }

    if (lyricsManager->hasSyncedLyrics()) {
        const qint64 offsetMs = lyricsManager->syncOffsetMs();
        const QString offsetText = (offsetMs == 0)
            ? QStringLiteral("0.0s")
            : QStringLiteral("%1%2s")
                  .arg(offsetMs > 0 ? QStringLiteral("+") : QString())
                  .arg(QString::number(offsetMs / 1000.0, 'f', 1));
        status += QStringLiteral(" · offset %1").arg(offsetText);
    }

    lyricsStatusLabel->setText(status);
}

void PlayerWindow::updateLyricsSyncButtons()
{
    const bool enabled = lyricsManager && lyricsManager->hasSyncedLyrics();
    if (lyricsDelayButton) {
        lyricsDelayButton->setEnabled(enabled);
    }
    if (lyricsResetSyncButton) {
        lyricsResetSyncButton->setEnabled(enabled);
    }
    if (lyricsFastenButton) {
        lyricsFastenButton->setEnabled(enabled);
    }
}

void PlayerWindow::refreshLyricsPlaylistView()
{
    if (!lyricsPlaylistWidget) {
        return;
    }

    lyricsPlaylistWidget->clear();

    for (int row = 0; row < playlist.size(); ++row) {
        const QString &filePath = playlist.at(row);
        ensureMetadataCached(filePath);

        QString text = playlistDisplayTextForFile(filePath).trimmed();
        if (text.isEmpty()) {
            text = QFileInfo(filePath).completeBaseName();
        }

        auto *item = new QListWidgetItem(text, lyricsPlaylistWidget);
        item->setData(Qt::UserRole, row);

        if (row == currentIndex) {
            QFont font = item->font();
            font.setBold(true);
            item->setFont(font);
            item->setBackground(QBrush(QColor(186, 94, 0)));
            item->setForeground(QBrush(Qt::white));
            lyricsPlaylistWidget->setCurrentItem(item);
        }
    }
}

void PlayerWindow::updateLyricsView()
{
    if (!lyricsListWidget) {
        return;
    }

    lyricsListWidget->clear();
    updateLyricsStatusLabel();
    updateLyricsSyncButtons();

    if (lyricsManager->isLoading()) {
        auto *item = new QListWidgetItem(QStringLiteral("Loading lyrics…"), lyricsListWidget);
        item->setTextAlignment(Qt::AlignCenter);
        return;
    }

    if (lyricsManager->hasSyncedLyrics()) {
        const QList<LyricsManager::LyricLine> lines = lyricsManager->lyricLines();
        for (const LyricsManager::LyricLine &line : lines) {
            auto *item = new QListWidgetItem(line.text, lyricsListWidget);
            item->setTextAlignment(Qt::AlignCenter);
        }
        updateLyricsCurrentLine();
        return;
    }

    if (!lyricsManager->plainLyrics().trimmed().isEmpty()) {
        const QStringList lines = lyricsManager->plainLyrics().split(QRegularExpression(QStringLiteral(R"(\r?\n)")),
                                                                     Qt::KeepEmptyParts);
        for (const QString &line : lines) {
            auto *item = new QListWidgetItem(line.trimmed().isEmpty() ? QStringLiteral(" ") : line, lyricsListWidget);
            item->setTextAlignment(Qt::AlignCenter);
        }
        return;
    }

    const QString error = lyricsManager->errorString().trimmed().isEmpty()
        ? QStringLiteral("No lyrics loaded")
        : lyricsManager->errorString();
    lyricsListWidget->addItem(error);
}

void PlayerWindow::updateLyricsCurrentLine()
{
    if (!lyricsListWidget) {
        return;
    }

    for (int i = 0; i < lyricsListWidget->count(); ++i) {
        QListWidgetItem *item = lyricsListWidget->item(i);
        if (!item) {
            continue;
        }

        QFont font = item->font();
        const bool current = (i == lyricsManager->currentLyricIndex() && lyricsManager->hasSyncedLyrics());
        font.setBold(current);
        item->setFont(font);
        item->setForeground(current ? QBrush(Qt::white) : QBrush());
        item->setBackground(current ? QBrush(QColor(186, 94, 0)) : QBrush());
    }

    if (lyricsManager->hasSyncedLyrics() &&
        lyricsManager->currentLyricIndex() >= 0 &&
        lyricsManager->currentLyricIndex() < lyricsListWidget->count()) {
        lyricsListWidget->scrollToItem(lyricsListWidget->item(lyricsManager->currentLyricIndex()),
                                       QAbstractItemView::PositionAtCenter);
    }
}


QString PlayerWindow::formatTime(qint64 ms) const
{
    const int totalSeconds = static_cast<int>(ms / 1000);
    const int minutes = totalSeconds / 60;
    const int seconds = totalSeconds % 60;

    return QStringLiteral("%1:%2")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'));
}

void PlayerWindow::updateTimeLabel(qint64 position, qint64 duration)
{
    const qint64 safePosition = std::max<qint64>(0, position);
    const qint64 safeDuration = std::max<qint64>(0, duration);
    const qint64 remaining = std::max<qint64>(0, safeDuration - safePosition);

    currentTimeLabel->setText(formatTime(safePosition));
    remainingTimeLabel->setText(remainingTimeText(remaining));
}

void PlayerWindow::updateVolumeLabel()
{
    volumePercentLabel->setText(QString::number(volumeSlider->value()) + QStringLiteral("%"));
}

void PlayerWindow::ensureMetadataCached(const QString &filePath)
{
    if (filePath.isEmpty() || metadataCache.contains(filePath)) {
        return;
    }

    metadataCache.insert(filePath, TrackMetadata::read(filePath));
}

QString PlayerWindow::playlistDisplayTextForFile(const QString &filePath) const
{
    const TrackMetadata::Info *info = cachedMetadata(filePath);
    return info ? info->title : QString();
}

QString PlayerWindow::playlistArtistForFile(const QString &filePath) const
{
    const TrackMetadata::Info *info = cachedMetadata(filePath);
    return info ? info->artist : QString();
}

QString PlayerWindow::playlistAlbumForFile(const QString &filePath) const
{
    const TrackMetadata::Info *info = cachedMetadata(filePath);
    return info ? info->album : QString();
}

QString PlayerWindow::playlistLengthForFile(const QString &filePath) const
{
    const TrackMetadata::Info *info = cachedMetadata(filePath);
    return info ? info->length : QString();
}

QString PlayerWindow::playlistBitrateForFile(const QString &filePath) const
{
    const TrackMetadata::Info *info = cachedMetadata(filePath);
    return info ? info->bitrate : QString();
}

QString PlayerWindow::playlistFileTypeForFile(const QString &filePath) const
{
    const TrackMetadata::Info *info = cachedMetadata(filePath);
    return info ? info->fileType : QString();
}

QString PlayerWindow::playlistTrackNumberForFile(const QString &filePath) const
{
    const TrackMetadata::Info *info = cachedMetadata(filePath);
    return info ? info->trackNumber : QString();
}

void PlayerWindow::rebuildMetadataCache()
{
    QHash<QString, TrackMetadata::Info> rebuilt;
    for (const QString &filePath : playlist) {
        if (metadataCache.contains(filePath)) {
            rebuilt.insert(filePath, metadataCache.value(filePath));
        } else {
            rebuilt.insert(filePath, TrackMetadata::read(filePath));
        }
    }
    metadataCache = rebuilt;
}

const TrackMetadata::Info *PlayerWindow::cachedMetadata(const QString &filePath) const
{
    const auto it = metadataCache.constFind(filePath);
    if (it == metadataCache.constEnd()) {
        return nullptr;
    }
    return &it.value();
}


void PlayerWindow::updateRepeatButton()
{
    QString text;
    QString tooltip;
    QString color;

    switch (repeatMode) {
    case RepeatAll:
        text = QStringLiteral("↺");
        tooltip = QStringLiteral("Repeat all tracks");
        color = QStringLiteral("palette(button-text)");
        break;
    case RepeatOne:
        text = QStringLiteral("↺1");
        tooltip = QStringLiteral("Repeat current track");
        color = QStringLiteral("palette(button-text)");
        break;
    case RepeatNone:
    default:
        text = QStringLiteral("↺");
        tooltip = QStringLiteral("Repeat disabled");
        color = QStringLiteral("palette(mid)");
        break;
    }

    repeatButton->setText(text);
    repeatButton->setToolTip(tooltip);
    repeatButton->setStyleSheet(QStringLiteral(
        "QPushButton { color: %1; font-size: 14px; padding: 0px 2px; }"
    ).arg(color));
}

void PlayerWindow::updateShuffleButton()
{
    shuffleButton->setChecked(shuffleOn);
    shuffleButton->setText(QStringLiteral("⇄"));
    shuffleButton->setToolTip(shuffleOn ? QStringLiteral("Shuffle enabled")
                                        : QStringLiteral("Shuffle disabled"));
    shuffleButton->setStyleSheet(QStringLiteral(
        "QPushButton { color: %1; font-size: 14px; padding: 0px 2px; }"
    ).arg(shuffleOn ? QStringLiteral("palette(button-text)")
                    : QStringLiteral("palette(mid)")));
}

qint64 PlayerWindow::totalPlaylistDurationMs() const
{
    qint64 total = 0;
    for (const QString &filePath : playlist) {
        total += metadataCache.value(filePath).durationMs;
    }
    return total;
}

void PlayerWindow::updatePlaylistSummary()
{
    const int count = playlist.size();
    const QString tracksText = (count == 1)
        ? QStringLiteral("1 track")
        : QStringLiteral("%1 tracks").arg(count);
    playlistSummaryLabel->setText(tracksText + QStringLiteral(" - ") + formatTime(totalPlaylistDurationMs()));
}

int PlayerWindow::nextTrackIndex(bool fromUserAction) const
{
    if (playlist.isEmpty()) {
        return -1;
    }

    if (repeatMode == RepeatOne && !fromUserAction && currentIndex >= 0 && currentIndex < playlist.size()) {
        return currentIndex;
    }

    if (shuffleOn) {
        if (playlist.size() == 1) {
            return 0;
        }

        int candidate = currentIndex;
        for (int attempt = 0; attempt < 16 && candidate == currentIndex; ++attempt) {
            candidate = QRandomGenerator::global()->bounded(playlist.size());
        }

        if (candidate == currentIndex) {
            candidate = (currentIndex + 1) % playlist.size();
        }
        return candidate;
    }

    const int sequentialNext = currentIndex + 1;
    if (sequentialNext < playlist.size()) {
        return sequentialNext;
    }

    if (repeatMode == RepeatAll) {
        return 0;
    }

    return -1;
}

int PlayerWindow::previousTrackIndex() const
{
    if (playlist.isEmpty()) {
        return -1;
    }

    if (shuffleOn && !shuffleHistory.isEmpty()) {
        return shuffleHistory.last();
    }

    if (currentIndex > 0) {
        return currentIndex - 1;
    }

    if (repeatMode == RepeatAll && !playlist.isEmpty()) {
        return playlist.size() - 1;
    }

    return -1;
}


void PlayerWindow::refreshPlaylistWidget()
{
    const QList<int> selectedRows = selectedRowsSorted();

    playlistWidget->setRowCount(0);

    for (const QString &filePath : playlist) {
        ensureMetadataCached(filePath);

        const int row = playlistWidget->rowCount();
        playlistWidget->insertRow(row);

        auto makeItem = [&](const QString &text, Qt::Alignment alignment = Qt::AlignLeft | Qt::AlignVCenter) {
            auto *item = new QTableWidgetItem(text);
            item->setFlags((item->flags() | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled) & ~Qt::ItemIsEditable);
            item->setTextAlignment(Qt::Alignment(alignment));
            return item;
        };

        QTableWidgetItem *trackNoItem = makeItem(playlistTrackNumberForFile(filePath), Qt::AlignRight | Qt::AlignVCenter);
        QTableWidgetItem *titleItem = makeItem(playlistDisplayTextForFile(filePath));
        QTableWidgetItem *artistItem = makeItem(playlistArtistForFile(filePath));
        QTableWidgetItem *albumItem = makeItem(playlistAlbumForFile(filePath));
        QTableWidgetItem *lengthItem = makeItem(playlistLengthForFile(filePath), Qt::AlignCenter);
        QTableWidgetItem *bitrateItem = makeItem(playlistBitrateForFile(filePath), Qt::AlignCenter);
        QTableWidgetItem *typeItem = makeItem(playlistFileTypeForFile(filePath), Qt::AlignCenter);

        titleItem->setData(kFilePathRole, filePath);

        playlistWidget->setItem(row, PlaylistWidget::ColumnTrackNumber, trackNoItem);
        playlistWidget->setItem(row, PlaylistWidget::ColumnTitle, titleItem);
        playlistWidget->setItem(row, PlaylistWidget::ColumnArtist, artistItem);
        playlistWidget->setItem(row, PlaylistWidget::ColumnAlbum, albumItem);
        playlistWidget->setItem(row, PlaylistWidget::ColumnLength, lengthItem);
        playlistWidget->setItem(row, PlaylistWidget::ColumnBitrate, bitrateItem);
        playlistWidget->setItem(row, PlaylistWidget::ColumnFileType, typeItem);
    }

    for (const int row : selectedRows) {
        if (row >= 0 && row < playlistWidget->rowCount() && playlistWidget->selectionModel()) {
            const QModelIndex index = playlistWidget->model()->index(row, 0);
            playlistWidget->selectionModel()->select(index,
                QItemSelectionModel::Select | QItemSelectionModel::Rows);
        }
    }

    updatePlaylistSelection();
    updatePlaylistHighlight();
    updatePlaylistSummary();
    refreshLyricsPlaylistView();
}

void PlayerWindow::openOpenFilesDialog()
{
    DockedOpenFileDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("Open Audio Files"));

    dialog.ensurePolished();
    dialog.adjustSize();

    QRect dialogGeometry = dialog.frameGeometry();
    const QRect windowGeometry = frameGeometry();
    QScreen *targetScreen = window()->windowHandle() ? window()->windowHandle()->screen() : this->screen();
    const QRect available = targetScreen ? targetScreen->availableGeometry() : QRect();

    int x = windowGeometry.left() - dialogGeometry.width();
    int y = windowGeometry.top();

    if (targetScreen) {
        if (x < available.left()) {
            x = available.left();
        }
        if (y < available.top()) {
            y = available.top();
        }
        if (y + dialogGeometry.height() > available.bottom() + 1) {
            y = available.bottom() - dialogGeometry.height() + 1;
        }
    }

    dialog.move(x, y);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QStringList files = dialog.selectedFiles();
    if (files.isEmpty()) {
        return;
    }

    const bool hadNoCurrentTrack = (currentIndex < 0);
    addFilesToPlaylist(files);

    if (hadNoCurrentTrack && !playlist.isEmpty()) {
        loadTrack(0, true);
    }
}

void PlayerWindow::addFilesToPlaylist(const QStringList &files)
{
    insertFilesIntoPlaylist(files, playlist.size(), true);
}

void PlayerWindow::insertFilesIntoPlaylist(const QStringList &files, int row, bool allowDuplicates)
{
    const QStringList cleanFiles = sanitizedAudioFiles(files);
    if (cleanFiles.isEmpty()) {
        return;
    }

    const int insertStart = std::clamp(row, 0, static_cast<int>(playlist.size()));
    int insertRow = insertStart;
    QStringList insertedFiles;

    for (const QString &filePath : cleanFiles) {
        if (!allowDuplicates && playlist.contains(filePath)) {
            continue;
        }

        playlist.insert(insertRow, filePath);
        ensureMetadataCached(filePath);
        insertedFiles.append(filePath);
        ++insertRow;
    }

    if (insertedFiles.isEmpty()) {
        return;
    }

    if (currentIndex >= insertStart) {
        currentIndex += insertedFiles.size();
    }

    shuffleHistory.clear();
    suppressShuffleHistoryRecording = false;
    refreshPlaylistWidget();
    emit mprisMetadataChanged();
}

void PlayerWindow::replacePlaylist(const QStringList &files, bool autoPlay)
{
    const QStringList cleanFiles = sanitizedAudioFiles(files);
    if (cleanFiles.isEmpty()) {
        return;
    }

    player->stop();
    player->setSource(QUrl());

    playlist = cleanFiles;
    currentIndex = -1;
    resumeIndex = -1;
    pendingRestorePosition = 0;
    pendingRestoreAutoPlay = false;
    isUserSeeking = false;
    shuffleHistory.clear();
    suppressShuffleHistoryRecording = false;

    metadataCache.clear();
    coverArtManager->clear();
    rebuildMetadataCache();
    refreshPlaylistWidget();

    if (autoPlay) {
        loadTrack(0, true);
    } else {
        resetPlayerUi();
    }

    emit mprisMetadataChanged();
    emit mprisPlaybackStateChanged();
    emit mprisPositionChanged();
}

void PlayerWindow::queueFilesNext(const QStringList &files)
{
    const QStringList cleanFiles = sanitizedAudioFiles(files);
    if (cleanFiles.isEmpty()) {
        return;
    }

    if (playlist.isEmpty() || currentIndex < 0) {
        replacePlaylist(cleanFiles, true);
        return;
    }

    insertFilesIntoPlaylist(cleanFiles, currentIndex + 1, true);
}

void PlayerWindow::resetPlayerUi()
{
    positionSlider->blockSignals(true);
    positionSlider->setRange(0, 0);
    positionSlider->setValue(0);
    positionSlider->blockSignals(false);
    updateTimeLabel(0, 0);

    titleLabel->setText(QStringLiteral("Drop audio files here or click Open"));
    albumLabel->clear();
    coverLabel->setPixmap(QPixmap());
    coverLabel->setText(QStringLiteral("♪"));
    setWindowTitle(QStringLiteral("Velion"));
    lyricsManager->clear();
    updateLyricsView();
}

void PlayerWindow::clearCurrentTrackDisplay(bool rememberForResume)
{
    resumeIndex = rememberForResume ? currentIndex : -1;
    currentIndex = -1;
    pendingRestorePosition = 0;
    pendingRestoreAutoPlay = false;
    isUserSeeking = false;
    shuffleHistory.clear();
    suppressShuffleHistoryRecording = false;

    player->stop();
    player->setSource(QUrl());

    positionSlider->blockSignals(true);
    positionSlider->setRange(0, 0);
    positionSlider->setValue(0);
    positionSlider->blockSignals(false);

    updateTimeLabel(0, 0);

    titleLabel->setText(QStringLiteral("Drop audio files here or click Open"));
    albumLabel->clear();
    coverLabel->setPixmap(QPixmap());
    coverLabel->setText(QStringLiteral("♪"));
    setWindowTitle(QStringLiteral("Velion"));

    updatePlaylistSelection();
    updatePlaylistHighlight();
    refreshLyricsPlaylistView();
    lyricsManager->clear();
    updateLyricsView();

    emit mprisMetadataChanged();
    emit mprisPlaybackStateChanged();
    emit mprisPositionChanged();
}

void PlayerWindow::loadTrack(int index, bool autoPlay, qint64 startPosition, bool notifyTrackChange)
{
    if (index < 0 || index >= playlist.size()) {
        return;
    }

    const int previousIndexValue = currentIndex;
    currentIndex = index;
    resumeIndex = -1;

    if (shuffleOn && !suppressShuffleHistoryRecording && previousIndexValue >= 0 && previousIndexValue != currentIndex) {
        shuffleHistory.append(previousIndexValue);
        while (shuffleHistory.size() > playlist.size()) {
            shuffleHistory.removeFirst();
        }
    }

    suppressShuffleHistoryRecording = false;

    const QString &filePath = playlist[currentIndex];
    const QFileInfo info(filePath);

    updatePlaylistSelection();
    updatePlaylistHighlight();

    isUserSeeking = false;

    titleLabel->setText(QStringLiteral("<b>%1</b>").arg(info.completeBaseName().toHtmlEscaped()));
    albumLabel->clear();

    coverArtManager->ensureCoverArt(filePath);
    updateCoverArt();

    player->setSource(QUrl::fromLocalFile(filePath));

    if (notifyTrackChange && previousIndexValue != currentIndex && notificationManager) {
        notificationManager->notifyTrackChanged(
            currentTrackTitle(),
            currentTrackArtist(),
            currentTrackAlbum(),
            currentTrackNumber(),
            currentCoverArtPath());
    }

    lyricsManager->fetchLyrics(currentTrackTitle(), currentTrackArtist(), currentTrackAlbum());
    lyricsManager->setPlaybackPositionMs(startPosition > 0 ? startPosition : 0);
    emit mprisMetadataChanged();
    emit mprisPlaybackStateChanged();
    emit mprisPositionChanged();

    QTimer::singleShot(300, this, [this]() {
    });

    if (autoPlay && startPosition == 0) {
        pendingRestorePosition = 0;
        pendingRestoreAutoPlay = false;
        player->play();

        QTimer::singleShot(900, this, [this]() {
            });

        return;
    }

    pendingRestorePosition = startPosition;
    pendingRestoreAutoPlay = autoPlay;
}

void PlayerWindow::updatePlayPauseButton()
{
    if (player->playbackState() == GstPlayerBackend::PlayingState) {
        playPauseButton->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
    } else {
        playPauseButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    }
}

QList<int> PlayerWindow::selectedRowsSorted() const
{
    QList<int> rows;
    const QModelIndexList selected = playlistWidget->selectionModel() ?
        playlistWidget->selectionModel()->selectedRows() : QModelIndexList{};
    rows.reserve(selected.size());

    for (const QModelIndex &index : selected) {
        rows.append(index.row());
    }

    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    return rows;
}

void PlayerWindow::updatePlaylistSelection()
{
    if (currentIndex >= 0 && currentIndex < playlistWidget->rowCount() && playlistWidget->selectionModel()) {
        const QModelIndex index = playlistWidget->model()->index(currentIndex, 0);
        playlistWidget->selectionModel()->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
    }
}

void PlayerWindow::updatePlaylistHighlight()
{
    for (int row = 0; row < playlistWidget->rowCount(); ++row) {
        for (int column = 0; column < playlistWidget->columnCount(); ++column) {
            QTableWidgetItem *item = playlistWidget->item(row, column);
            if (!item) {
                continue;
            }

            QFont font = item->font();
            font.setBold(row == currentIndex);
            item->setFont(font);

            if (row == currentIndex) {
                item->setBackground(QBrush(QColor(186, 94, 0)));
                item->setForeground(QBrush(Qt::white));
            } else {
                item->setBackground(QBrush());
                item->setForeground(QBrush());
            }
        }
    }
}

void PlayerWindow::rebuildPlaylistFromWidgetOrder()
{
    QString currentFile;
    if (currentIndex >= 0 && currentIndex < playlist.size()) {
        currentFile = playlist[currentIndex];
    }

    QStringList newPlaylist;
    newPlaylist.reserve(playlistWidget->rowCount());

    for (int row = 0; row < playlistWidget->rowCount(); ++row) {
        QTableWidgetItem *item = playlistWidget->item(row, PlaylistWidget::ColumnTitle);
        if (!item) {
            continue;
        }
        const QString filePath = item->data(kFilePathRole).toString();
        if (!filePath.isEmpty()) {
            newPlaylist.append(filePath);
        }
    }

    playlist = newPlaylist;

    if (currentFile.isEmpty()) {
        currentIndex = -1;
    } else {
        currentIndex = playlist.indexOf(currentFile);
    }

    rebuildMetadataCache();
    shuffleHistory.clear();
    suppressShuffleHistoryRecording = false;
    updatePlaylistSelection();
    updatePlaylistHighlight();
    updatePlaylistSummary();
    refreshLyricsPlaylistView();
}

void PlayerWindow::showPlaylistContextMenu(const QPoint &pos)
{
    const QModelIndex index = playlistWidget->indexAt(pos);

    if (index.isValid()) {
        const bool clickedRowAlreadySelected = playlistWidget->selectionModel() &&
                                               playlistWidget->selectionModel()->isRowSelected(index.row(), QModelIndex());
        if (!clickedRowAlreadySelected) {
            playlistWidget->clearSelection();
            playlistWidget->selectRow(index.row());
        }
    }

    QMenu menu(this);

    QAction *removeAction = nullptr;
    QAction *moveUpAction = nullptr;
    QAction *moveDownAction = nullptr;

    if (!selectedRowsSorted().isEmpty()) {
        removeAction = menu.addAction(QStringLiteral("Remove"));
        moveUpAction = menu.addAction(QStringLiteral("Move Up"));
        moveDownAction = menu.addAction(QStringLiteral("Move Down"));
    }

    QAction *clearAllAction = nullptr;
    if (!playlist.isEmpty()) {
        if (!menu.actions().isEmpty()) {
            menu.addSeparator();
        }
        clearAllAction = menu.addAction(QStringLiteral("Clear All Playlist"));
    }

    if (menu.actions().isEmpty()) {
        return;
    }

    QAction *chosen = menu.exec(playlistWidget->viewport()->mapToGlobal(pos));
    if (!chosen) {
        return;
    }

    if (chosen == removeAction) {
        removeSelectedTracks();
    } else if (chosen == moveUpAction) {
        moveSelectedTracksUp();
    } else if (chosen == moveDownAction) {
        moveSelectedTracksDown();
    } else if (chosen == clearAllAction) {
        clearPlaylist();
    }
}

void PlayerWindow::removeSelectedTracks()
{
    QList<int> rows = selectedRowsSorted();
    if (rows.isEmpty()) {
        return;
    }

    const bool removedCurrent = rows.contains(currentIndex);
    const int oldCurrentIndex = currentIndex;

    for (int i = rows.size() - 1; i >= 0; --i) {
        const int row = rows[i];
        if (row < 0 || row >= playlist.size()) {
            continue;
        }

        const QString removedFile = playlist[row];

        playlist.removeAt(row);
        metadataCache.remove(removedFile);
        coverArtManager->removeForFile(removedFile);

        if (row < currentIndex) {
            --currentIndex;
        }
    }

    if (playlist.isEmpty()) {
        metadataCache.clear();
        coverArtManager->clear();
        shuffleHistory.clear();
        suppressShuffleHistoryRecording = false;

        currentIndex = -1;
        resumeIndex = -1;
        pendingRestorePosition = 0;
        pendingRestoreAutoPlay = false;
        isUserSeeking = false;
        player->stop();
        player->setSource(QUrl());
        resetPlayerUi();
        refreshPlaylistWidget();
        emit mprisMetadataChanged();
        emit mprisPlaybackStateChanged();
        emit mprisPositionChanged();
        return;
    }

    rebuildMetadataCache();
    shuffleHistory.clear();
    suppressShuffleHistoryRecording = false;

    if (removedCurrent) {
        int newIndex = oldCurrentIndex;
        int removedBeforeOrAt = 0;

        for (const int row : rows) {
            if (row <= oldCurrentIndex) {
                ++removedBeforeOrAt;
            }
        }

        newIndex = oldCurrentIndex - removedBeforeOrAt + 1;

        if (newIndex >= playlist.size()) {
            newIndex = static_cast<int>(playlist.size()) - 1;
        }
        if (newIndex < 0) {
            newIndex = 0;
        }

        refreshPlaylistWidget();
        loadTrack(newIndex, true);
        return;
    }

    refreshPlaylistWidget();
    emit mprisMetadataChanged();
}

void PlayerWindow::moveSelectedTracksUp()
{
    QList<int> rows = selectedRowsSorted();
    if (rows.isEmpty() || rows.first() == 0) {
        return;
    }

    for (const int row : rows) {
        playlist.swapItemsAt(row, row - 1);

        if (currentIndex == row) {
            --currentIndex;
        } else if (currentIndex == row - 1) {
            ++currentIndex;
        }
    }

    shuffleHistory.clear();
    suppressShuffleHistoryRecording = false;
    refreshPlaylistWidget();

    playlistWidget->clearSelection();
    for (const int row : rows) {
        const QModelIndex modelIndex = playlistWidget->model()->index(row - 1, 0);
        playlistWidget->selectionModel()->select(
            modelIndex,
            QItemSelectionModel::Select | QItemSelectionModel::Rows
        );
    }

    updatePlaylistHighlight();
    emit mprisMetadataChanged();
}

void PlayerWindow::moveSelectedTracksDown()
{
    QList<int> rows = selectedRowsSorted();
    if (rows.isEmpty() || rows.last() >= playlist.size() - 1) {
        return;
    }

    for (int i = rows.size() - 1; i >= 0; --i) {
        const int row = rows[i];
        playlist.swapItemsAt(row, row + 1);

        if (currentIndex == row) {
            ++currentIndex;
        } else if (currentIndex == row + 1) {
            --currentIndex;
        }
    }

    refreshPlaylistWidget();

    playlistWidget->clearSelection();
    for (const int row : rows) {
        const QModelIndex modelIndex = playlistWidget->model()->index(row + 1, 0);
        playlistWidget->selectionModel()->select(
            modelIndex,
            QItemSelectionModel::Select | QItemSelectionModel::Rows
        );
    }

    updatePlaylistHighlight();
    emit mprisMetadataChanged();
}

void PlayerWindow::updateTrackInfoLabels()
{
    if (currentIndex < 0 || currentIndex >= playlist.size()) {
        titleLabel->setText(QStringLiteral("Drop audio files here or click Open"));
        albumLabel->clear();
        setWindowTitle(QStringLiteral("Velion"));
        return;
    }

    const QString filePath = playlist[currentIndex];
    ensureMetadataCached(filePath);
    const QFileInfo info(filePath);
    const TrackMetadata::Info *cache = cachedMetadata(filePath);

    QString title = cache ? cache->title : QString();
    if (title.isEmpty()) {
        title = info.completeBaseName();
    }

    const QString artist = cache ? cache->artist : QString();
    const QString album = cache ? cache->album : QString();

    QString topLine = title.toHtmlEscaped();
    if (!artist.isEmpty()) {
        topLine += QStringLiteral(" - ") + artist.toHtmlEscaped();
    }

    titleLabel->setText(QStringLiteral("<b>%1</b>").arg(topLine));
    albumLabel->setText(album.toHtmlEscaped());

    setWindowTitle(QStringLiteral("Velion - ") + title +
                   (artist.isEmpty() ? QString() : QStringLiteral(" - ") + artist));
}

void PlayerWindow::updateCoverArt()
{
    const QString artPath = currentCoverArtPath();

    if (artPath.isEmpty()) {
        coverLabel->setPixmap(QPixmap());
        coverLabel->setText(QStringLiteral("♪"));
        return;
    }

    QPixmap pixmap(artPath);
    if (pixmap.isNull()) {
        coverLabel->setPixmap(QPixmap());
        coverLabel->setText(QStringLiteral("♪"));
        return;
    }

    coverLabel->setText(QString());
    coverLabel->setPixmap(
        pixmap.scaled(
            coverLabel->size(),
            Qt::KeepAspectRatioByExpanding,
            Qt::SmoothTransformation
        )
    );
}

void PlayerWindow::clearPlaylist()
{
    playlist.clear();
    metadataCache.clear();
    coverArtManager->clear();
    shuffleHistory.clear();
    suppressShuffleHistoryRecording = false;
    resumeIndex = -1;
    clearCurrentTrackDisplay(false);
    refreshPlaylistWidget();
    updatePlaylistSummary();
    emit mprisMetadataChanged();
    emit mprisPlaybackStateChanged();
    emit mprisPositionChanged();
}

void PlayerWindow::loadSettings()
{
    QSettings settings(QStringLiteral("Nikos"), QStringLiteral("Velion"));

    const int savedVolume = settings.value(QStringLiteral("audio/volume"), 50).toInt();
    volumeSlider->setValue(savedVolume);
    player->setVolume(savedVolume / 100.0);
    updateVolumeLabel();

    const QList<int> columnWidths = fromVariantList(settings.value(QStringLiteral("playlist/columnWidths")));
    if (!columnWidths.isEmpty()) {
        playlistWidget->setColumnWidths(columnWidths);
    }

    const QList<int> splitterSizes = fromVariantList(settings.value(QStringLiteral("lyrics/splitterSizes")));
    if (!splitterSizes.isEmpty() && lyricsSplitter) {
        lyricsSplitter->setSizes(splitterSizes);
    }

    const QStringList savedPlaylist = settings.value(QStringLiteral("playlist/files")).toStringList();
    const int savedIndex = settings.value(QStringLiteral("playlist/currentIndex"), -1).toInt();
    const qint64 savedPosition = settings.value(QStringLiteral("playback/position"), 0).toLongLong();
    const bool wasPlaying = settings.value(QStringLiteral("playback/wasPlaying"), false).toBool();
    repeatMode = static_cast<RepeatMode>(settings.value(QStringLiteral("playback/repeatMode"), static_cast<int>(RepeatNone)).toInt());
    if (repeatMode < RepeatNone || repeatMode > RepeatOne) {
        repeatMode = RepeatNone;
    }
    shuffleOn = settings.value(QStringLiteral("playback/shuffle"), false).toBool();
    const bool lyricsViewEnabled = settings.value(QStringLiteral("ui/lyricsView"), false).toBool();
    lyricsManager->setSyncOffsetMs(settings.value(QStringLiteral("lyrics/syncOffsetMs"), 0).toLongLong());
    shuffleHistory.clear();
    suppressShuffleHistoryRecording = false;

    updateRepeatButton();
    updateShuffleButton();

    QStringList existingFiles;
    for (const QString &filePath : savedPlaylist) {
        if (QFileInfo::exists(filePath) && QFileInfo(filePath).isFile()) {
            existingFiles.append(filePath);
        }
    }

    playlist = existingFiles;

    if (playlist.isEmpty()) {
        currentIndex = -1;
        refreshPlaylistWidget();
        lyricsToggleButton->setChecked(lyricsViewEnabled);
        toggleLyricsPage();
        resetPlayerUi();
        return;
    }

    currentIndex = savedIndex;
    if (currentIndex < 0 || currentIndex >= playlist.size()) {
        currentIndex = 0;
    }

    rebuildMetadataCache();
    refreshPlaylistWidget();
    lyricsToggleButton->setChecked(lyricsViewEnabled);
    toggleLyricsPage();
    loadTrack(currentIndex, wasPlaying, savedPosition, false);
}

void PlayerWindow::saveSettings()
{
    QSettings settings(QStringLiteral("Nikos"), QStringLiteral("Velion"));
    settings.setValue(QStringLiteral("audio/volume"), volumeSlider->value());
    settings.setValue(QStringLiteral("playlist/columnWidths"), toVariantList(playlistWidget->columnWidths()));
    settings.setValue(QStringLiteral("playlist/files"), playlist);
    settings.setValue(QStringLiteral("playlist/currentIndex"), currentIndex);
    settings.setValue(QStringLiteral("playback/position"), player->position());
    settings.setValue(QStringLiteral("playback/wasPlaying"),
                      player->playbackState() == GstPlayerBackend::PlayingState);
    settings.setValue(QStringLiteral("playback/repeatMode"), static_cast<int>(repeatMode));
    settings.setValue(QStringLiteral("playback/shuffle"), shuffleOn);
    settings.setValue(QStringLiteral("ui/lyricsView"), lyricsToggleButton && lyricsToggleButton->isChecked());
    settings.setValue(QStringLiteral("lyrics/syncOffsetMs"), lyricsManager ? lyricsManager->syncOffsetMs() : 0);
    settings.setValue(QStringLiteral("lyrics/splitterSizes"), lyricsSplitter ? toVariantList(lyricsSplitter->sizes()) : QVariantList{});
}

void PlayerWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (!audioFilesFromMimeData(event->mimeData()).isEmpty()) {
        event->acceptProposedAction();
    }
}

void PlayerWindow::dropEvent(QDropEvent *event)
{
    const QStringList files = audioFilesFromMimeData(event->mimeData());
    if (files.isEmpty()) {
        return;
    }

    const bool hadNoCurrentTrack = (currentIndex < 0);
    addFilesToPlaylist(files);

    if (hadNoCurrentTrack && !playlist.isEmpty()) {
        loadTrack(0, true);
    }

    event->acceptProposedAction();
}

void PlayerWindow::handleCommandLineLaunch(const QStringList &files, bool fromRunningInstance)
{
    const QStringList cleanFiles = sanitizedAudioFiles(files);
    if (cleanFiles.isEmpty()) {
        return;
    }

    if (fromRunningInstance) {
        if (playlist.isEmpty() || currentIndex < 0) {
            replacePlaylist(cleanFiles, true);
        } else {
            queueFilesNext(cleanFiles);
        }
    } else {
        replacePlaylist(cleanFiles, true);
    }

    show();
    raise();
    activateWindow();
}

void PlayerWindow::closeEvent(QCloseEvent *event)
{
    saveSettings();

    if (trayIcon) {
        trayIcon->hide();
    }

    QMainWindow::closeEvent(event);
}

QString PlayerWindow::currentFilePath() const
{
    if (currentIndex < 0 || currentIndex >= playlist.size()) {
        return QString();
    }
    return playlist[currentIndex];
}

QString PlayerWindow::currentTrackTitle() const
{
    const QString filePath = currentFilePath();
    if (filePath.isEmpty()) {
        return QString();
    }

    const TrackMetadata::Info *info = cachedMetadata(filePath);
    return info ? info->title : QString();
}

QString PlayerWindow::currentTrackArtist() const
{
    const QString filePath = currentFilePath();
    if (filePath.isEmpty()) {
        return QString();
    }

    const TrackMetadata::Info *info = cachedMetadata(filePath);
    return info ? info->artist : QString();
}

QString PlayerWindow::currentTrackAlbum() const
{
    const QString filePath = currentFilePath();
    if (filePath.isEmpty()) {
        return QString();
    }

    const TrackMetadata::Info *info = cachedMetadata(filePath);
    return info ? info->album : QString();
}

QString PlayerWindow::currentTrackNumber() const
{
    const QString filePath = currentFilePath();
    if (filePath.isEmpty()) {
        return QString();
    }

    const TrackMetadata::Info *info = cachedMetadata(filePath);
    return info ? info->trackNumber : QString();
}

QString PlayerWindow::currentCoverArtPath() const
{
    return coverArtManager->coverArtPath(currentFilePath());
}

qint64 PlayerWindow::currentPosition() const
{
    return player->position();
}

qint64 PlayerWindow::currentDuration() const
{
    return player->duration();
}

double PlayerWindow::currentVolume() const
{
    return player->volume();
}

bool PlayerWindow::canGoNext() const
{
    return nextTrackIndex(true) >= 0;
}

bool PlayerWindow::canGoPrevious() const
{
    return previousTrackIndex() >= 0;
}

bool PlayerWindow::canPlay() const
{
    return !playlist.isEmpty();
}

bool PlayerWindow::canPause() const
{
    return !playlist.isEmpty();
}

bool PlayerWindow::canControl() const
{
    return true;
}

QString PlayerWindow::playbackStatusString() const
{
    switch (player->playbackState()) {
    case GstPlayerBackend::PlayingState:
        return QStringLiteral("Playing");
    case GstPlayerBackend::PausedState:
        return QStringLiteral("Paused");
    case GstPlayerBackend::StoppedState:
    default:
        return QStringLiteral("Stopped");
    }
}

QString PlayerWindow::loopStatusString() const
{
    switch (repeatMode) {
    case RepeatAll:
        return QStringLiteral("Playlist");
    case RepeatOne:
        return QStringLiteral("Track");
    case RepeatNone:
    default:
        return QStringLiteral("None");
    }
}

bool PlayerWindow::shuffleEnabled() const
{
    return shuffleOn;
}

void PlayerWindow::notifyPlaybackStartedIfAvailable()
{
    if (!notificationManager || currentFilePath().isEmpty()) {
        return;
    }

    notificationManager->notifyPlaybackStarted(
        currentTrackTitle(),
        currentTrackArtist(),
        currentTrackAlbum(),
        currentTrackNumber(),
        currentCoverArtPath());
}

void PlayerWindow::notifyPlaybackPausedIfAvailable()
{
    if (!notificationManager || currentFilePath().isEmpty()) {
        return;
    }

    notificationManager->notifyPlaybackPaused(
        currentTrackTitle(),
        currentTrackArtist(),
        currentTrackAlbum(),
        currentTrackNumber(),
        currentCoverArtPath());
}

void PlayerWindow::requestPlay()
{
    if (playlist.isEmpty()) {
        return;
    }

    if (currentIndex < 0) {
        if (resumeIndex >= 0 && resumeIndex < playlist.size()) {
            loadTrack(resumeIndex, true);
        } else {
            loadTrack(0, true);
        }
        return;
    }

    if (player->playbackState() == GstPlayerBackend::PlayingState) {
        return;
    }

    player->play();
    notifyPlaybackStartedIfAvailable();
}

void PlayerWindow::requestPause()
{
    if (player->playbackState() != GstPlayerBackend::PlayingState) {
        return;
    }

    player->pause();
    notifyPlaybackPausedIfAvailable();
}

void PlayerWindow::requestPlayPauseToggle()
{
    if (player->playbackState() == GstPlayerBackend::PlayingState) {
        requestPause();
        return;
    }

    requestPlay();
}

void PlayerWindow::mprisPlay()
{
    requestPlay();
}

void PlayerWindow::mprisPause()
{
    requestPause();
}

void PlayerWindow::mprisPlayPause()
{
    requestPlayPauseToggle();
}

void PlayerWindow::mprisStop()
{
    clearCurrentTrackDisplay(true);
}

void PlayerWindow::mprisNext()
{
    const int newIndex = nextTrackIndex(true);
    if (newIndex < 0) {
        clearCurrentTrackDisplay(false);
        return;
    }
    loadTrack(newIndex, true);
}

void PlayerWindow::mprisPrevious()
{
    if (shuffleOn && !shuffleHistory.isEmpty()) {
        const int newIndex = shuffleHistory.takeLast();
        suppressShuffleHistoryRecording = true;
        loadTrack(newIndex, true);
        return;
    }

    const int newIndex = previousTrackIndex();
    if (newIndex < 0) {
        return;
    }
    loadTrack(newIndex, true);
}

void PlayerWindow::mprisSetPosition(qint64 position)
{
    const qint64 clampedPosition = std::clamp<qint64>(position, 0, player->duration());
    player->setPosition(clampedPosition);
}

void PlayerWindow::mprisSetVolume(double volume)
{
    if (volume < 0.0) {
        volume = 0.0;
    }
    if (volume > 1.0) {
        volume = 1.0;
    }

    player->setVolume(volume);
}


void PlayerWindow::mprisSetLoopStatus(const QString &loopStatus)
{
    RepeatMode newMode = RepeatNone;
    if (loopStatus == QStringLiteral("Playlist")) {
        newMode = RepeatAll;
    } else if (loopStatus == QStringLiteral("Track")) {
        newMode = RepeatOne;
    } else if (loopStatus != QStringLiteral("None")) {
        return;
    }

    if (repeatMode == newMode) {
        updateRepeatButton();
        return;
    }

    repeatMode = newMode;
    updateRepeatButton();
    emit mprisMetadataChanged();
}

void PlayerWindow::mprisSetShuffle(bool enabled)
{
    if (shuffleOn == enabled) {
        updateShuffleButton();
        return;
    }

    shuffleOn = enabled;
    if (!shuffleOn) {
        shuffleHistory.clear();
    }
    suppressShuffleHistoryRecording = false;
    updateShuffleButton();
    emit mprisMetadataChanged();
}

void PlayerWindow::mprisRaise()
{
    show();
    raise();
    activateWindow();
}