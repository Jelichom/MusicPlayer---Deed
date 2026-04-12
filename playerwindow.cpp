#include "playerwindow.h"

#include "clickableslider.h"
#include "mprisservice.h"
#include "playlistwidget.h"

#include <QAbstractItemView>
#include <QAudioOutput>
#include <QBrush>
#include <QCloseEvent>
#include <QColor>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMediaMetaData>
#include <QMediaPlayer>
#include <QMenu>
#include <QMimeData>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QSettings>
#include <QSizePolicy>
#include <QStandardPaths>
#include <QStyle>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVariant>
#include <QVBoxLayout>
#include <QWidget>

#include <pulse/context.h>
#include <pulse/error.h>
#include <pulse/introspect.h>
#include <pulse/mainloop-api.h>
#include <pulse/proplist.h>
#include <pulse/subscribe.h>
#include <pulse/thread-mainloop.h>
#include <pulse/volume.h>

#include <taglib/attachedpictureframe.h>
#include <taglib/fileref.h>
#include <taglib/flacfile.h>
#include <taglib/flacpicture.h>
#include <taglib/id3v2tag.h>
#include <taglib/mpegfile.h>
#include <taglib/tag.h>

#include <algorithm>

namespace {
constexpr int kFilePathRole = Qt::UserRole + 1;
constexpr int kHeaderMinHeight = 88;
constexpr int kHeaderMaxHeight = 150;
constexpr int kCoverMinSize = 64;
constexpr int kCoverMaxSize = 124;
constexpr const char *kUserAgent = "Deed/1.0 (Qt6; Linux)";
constexpr uint32_t kInvalidSinkInputIndex = static_cast<uint32_t>(-1);
}

PlayerWindow::PlayerWindow(QWidget *parent)
    : QMainWindow(parent),
      openButton(nullptr),
      previousButton(nullptr),
      playPauseButton(nullptr),
      nextButton(nullptr),
      stopButton(nullptr),
      headerWidget(nullptr),
      coverLabel(nullptr),
      titleLabel(nullptr),
      albumLabel(nullptr),
      timeLabel(nullptr),
      volumePercentLabel(nullptr),
      positionSlider(nullptr),
      volumeSlider(nullptr),
      playlistWidget(nullptr),
      player(new QMediaPlayer(this)),
      audioOutput(new QAudioOutput(this)),
      mprisService(nullptr),
      networkManager(new QNetworkAccessManager(this)),
      pulseMainloop(nullptr),
      pulseContext(nullptr),
      currentSinkInputIndex(kInvalidSinkInputIndex),
      pulseReady(false),
      updatingVolumeFromMixer(false),
      currentIndex(-1),
      pendingRestorePosition(0),
      pendingRestoreAutoPlay(false),
      isUserSeeking(false)
{
    player->setAudioOutput(audioOutput);
    setAcceptDrops(true);

    setupUi();
    setupConnections();
    startPulseSync();
    loadSettings();
    updatePlayPauseButton();
    updateVolumeLabel();
    updateHeaderSizing();

    setWindowTitle("Deed");

    mprisService = new MprisService(this);
}

PlayerWindow::~PlayerWindow()
{
    stopPulseSync();
}

void PlayerWindow::setupUi()
{
    auto *central = new QWidget(this);
    setCentralWidget(central);

    auto *mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(8);

    headerWidget = new QWidget(this);
    headerWidget->setMinimumHeight(kHeaderMinHeight);
    headerWidget->setMaximumHeight(kHeaderMaxHeight);
    headerWidget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto *headerLayout = new QHBoxLayout(headerWidget);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(14);

    coverLabel = new QLabel(this);
    coverLabel->setAlignment(Qt::AlignCenter);
    coverLabel->setMinimumSize(kCoverMinSize, kCoverMinSize);
    coverLabel->setMaximumSize(kCoverMaxSize, kCoverMaxSize);
    coverLabel->setText("♪");
    coverLabel->setStyleSheet(
        "QLabel {"
        "  border-radius: 10px;"
        "  background: rgba(255,255,255,0.06);"
        "  font-size: 24px;"
        "}"
    );

    titleLabel = new QLabel("Drop audio files here or click Open", this);
    titleLabel->setTextFormat(Qt::RichText);
    titleLabel->setWordWrap(true);

    albumLabel = new QLabel(QString(), this);
    albumLabel->setWordWrap(true);
    albumLabel->setStyleSheet("QLabel { color: palette(mid); }");

    auto *infoLayout = new QVBoxLayout();
    infoLayout->setContentsMargins(0, 0, 0, 0);
    infoLayout->setSpacing(2);
    infoLayout->addStretch(1);
    infoLayout->addWidget(titleLabel);
    infoLayout->addWidget(albumLabel);
    infoLayout->addStretch(1);

    headerLayout->addWidget(coverLabel, 0, Qt::AlignLeft | Qt::AlignVCenter);
    headerLayout->addLayout(infoLayout, 1);

    timeLabel = new QLabel("00:00 / 00:00", this);

    positionSlider = new ClickableSlider(Qt::Horizontal, this);
    positionSlider->setRange(0, 0);
    positionSlider->setTracking(false);

    volumeSlider = new ClickableSlider(Qt::Horizontal, this);
    volumeSlider->setRange(0, 100);
    volumeSlider->setValue(50);
    volumeSlider->setFixedWidth(120);

    volumePercentLabel = new QLabel("50%", this);
    volumePercentLabel->setMinimumWidth(40);
    volumePercentLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    playlistWidget = new PlaylistWidget(this);
    playlistWidget->setMinimumHeight(220);
    playlistWidget->setSelectionMode(QAbstractItemView::ExtendedSelection);
    playlistWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    playlistWidget->setDragDropMode(QAbstractItemView::InternalMove);
    playlistWidget->setDefaultDropAction(Qt::MoveAction);
    playlistWidget->setDragEnabled(true);
    playlistWidget->setAcceptDrops(true);
    playlistWidget->setDropIndicatorShown(true);

    openButton = new QPushButton(this);
    previousButton = new QPushButton(this);
    playPauseButton = new QPushButton(this);
    nextButton = new QPushButton(this);
    stopButton = new QPushButton(this);

    openButton->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    previousButton->setIcon(style()->standardIcon(QStyle::SP_MediaSkipBackward));
    playPauseButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    nextButton->setIcon(style()->standardIcon(QStyle::SP_MediaSkipForward));
    stopButton->setIcon(style()->standardIcon(QStyle::SP_MediaStop));

    openButton->setToolTip("Open files");
    previousButton->setToolTip("Previous");
    playPauseButton->setToolTip("Play / Pause");
    nextButton->setToolTip("Next");
    stopButton->setToolTip("Stop");

    auto *bottomLayout = new QHBoxLayout();
    bottomLayout->addWidget(openButton);
    bottomLayout->addWidget(previousButton);
    bottomLayout->addWidget(playPauseButton);
    bottomLayout->addWidget(nextButton);
    bottomLayout->addWidget(stopButton);

    bottomLayout->addStretch(1);

    auto *separator = new QFrame(this);
    separator->setFrameShape(QFrame::VLine);
    separator->setFrameShadow(QFrame::Plain);
    separator->setStyleSheet("color: palette(mid);");
    bottomLayout->addWidget(separator);

    auto *volumeText = new QLabel("Vol", this);
    volumeText->setStyleSheet("QLabel { color: palette(mid); }");

    bottomLayout->addSpacing(6);
    bottomLayout->addWidget(volumeText);
    bottomLayout->addWidget(volumeSlider);
    bottomLayout->addWidget(volumePercentLabel);

    mainLayout->addWidget(headerWidget);
    mainLayout->addWidget(positionSlider);
    mainLayout->addWidget(timeLabel);
    mainLayout->addWidget(playlistWidget, 1);
    mainLayout->addLayout(bottomLayout);
}

void PlayerWindow::setupConnections()
{
    connect(openButton, &QPushButton::clicked, this, [this]() {
        const QStringList files = QFileDialog::getOpenFileNames(
            this,
            "Open Audio Files",
            QString(),
            "Audio Files (*.mp3 *.wav *.ogg *.flac);;All Files (*)"
        );

        if (files.isEmpty()) {
            return;
        }

        const bool hadNoCurrentTrack = (currentIndex < 0);
        addFilesToPlaylist(files);

        if (hadNoCurrentTrack && !playlist.isEmpty()) {
            loadTrack(0, true);
        }
    });

    connect(previousButton, &QPushButton::clicked, this, [this]() {
        if (playlist.isEmpty()) {
            return;
        }

        int newIndex = currentIndex - 1;
        if (newIndex < 0) {
            newIndex = static_cast<int>(playlist.size()) - 1;
        }

        loadTrack(newIndex, true);
    });

    connect(nextButton, &QPushButton::clicked, this, [this]() {
        if (playlist.isEmpty()) {
            return;
        }

        int newIndex = currentIndex + 1;
        if (newIndex >= playlist.size()) {
            newIndex = 0;
        }

        loadTrack(newIndex, true);
    });

    connect(playPauseButton, &QPushButton::clicked, this, [this]() {
        if (playlist.isEmpty()) {
            return;
        }

        if (player->playbackState() == QMediaPlayer::PlayingState) {
            player->pause();
        } else {
            player->play();
        }
    });

    connect(stopButton, &QPushButton::clicked, player, &QMediaPlayer::stop);

    connect(volumeSlider, &ClickableSlider::valueChanged, this, [this](int value) {
        audioOutput->setVolume(value / 100.0);
        updateVolumeLabel();
        emit mprisVolumeChanged();

        if (!updatingVolumeFromMixer) {
            setPulseStreamVolume(value);
        }
    });

    connect(player, &QMediaPlayer::durationChanged, this, [this](qint64 duration) {
        positionSlider->setRange(0, static_cast<int>(duration));

        if (!isUserSeeking) {
            updateTimeLabel(player->position(), duration);
        }

        emit mprisMetadataChanged();
    });

    connect(player, &QMediaPlayer::positionChanged, this, [this](qint64 position) {
        if (!isUserSeeking && !positionSlider->isSliderDown()) {
            positionSlider->setValue(static_cast<int>(position));
            updateTimeLabel(position, player->duration());
        }

        emit mprisPositionChanged();
    });

    connect(positionSlider, &ClickableSlider::sliderPressed, this, [this]() {
        isUserSeeking = true;
    });

    connect(positionSlider, &ClickableSlider::sliderMoved, this, [this](int position) {
        updateTimeLabel(position, player->duration());
    });

    connect(positionSlider, &ClickableSlider::sliderReleased, this, [this]() {
        player->setPosition(positionSlider->value());
        isUserSeeking = false;
        emit mprisPositionChanged();
    });

    connect(player, &QMediaPlayer::playbackStateChanged, this, [this]() {
        updatePlayPauseButton();
        emit mprisPlaybackStateChanged();

        if (player->playbackState() == QMediaPlayer::PlayingState) {
            QTimer::singleShot(300, this, [this]() {
                requestPulseSinkInputRefresh();
            });
            QTimer::singleShot(900, this, [this]() {
                requestPulseSinkInputRefresh();
            });
        }
    });

    connect(player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia) {
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

        if (status == QMediaPlayer::EndOfMedia && !playlist.isEmpty()) {
            int newIndex = currentIndex + 1;
            if (newIndex >= playlist.size()) {
                newIndex = 0;
            }
            loadTrack(newIndex, true);
        }
    });

    connect(player, &QMediaPlayer::metaDataChanged, this, [this]() {
        updateTrackInfoLabels();
        updateCoverArt();
        emit mprisMetadataChanged();
    });

    connect(playlistWidget, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
        const int index = playlistWidget->row(item);
        loadTrack(index, true);
    });

    connect(playlistWidget, &PlaylistWidget::itemsReordered, this, [this]() {
        rebuildPlaylistFromWidgetOrder();
        emit mprisMetadataChanged();
    });

    connect(playlistWidget, &PlaylistWidget::externalFilesDropped,
            this, [this](const QStringList &files, int row) {
        const bool hadNoCurrentTrack = (currentIndex < 0);

        insertFilesIntoPlaylist(files, row);

        if (hadNoCurrentTrack && !playlist.isEmpty()) {
            loadTrack(0, true);
        }
    });

    connect(playlistWidget, &QListWidget::customContextMenuRequested,
            this, &PlayerWindow::showPlaylistContextMenu);
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

    int coverSize = static_cast<int>(headerHeight * 0.78);
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

QString PlayerWindow::formatTime(qint64 ms) const
{
    const int totalSeconds = static_cast<int>(ms / 1000);
    const int minutes = totalSeconds / 60;
    const int seconds = totalSeconds % 60;

    return QString("%1:%2")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'));
}

void PlayerWindow::updateTimeLabel(qint64 position, qint64 duration)
{
    timeLabel->setText(formatTime(position) + " / " + formatTime(duration));
}

void PlayerWindow::updateVolumeLabel()
{
    volumePercentLabel->setText(QString::number(volumeSlider->value()) + "%");
}

QString PlayerWindow::readTitleWithTagLib(const QString &filePath) const
{
    TagLib::FileRef file(filePath.toUtf8().constData());
    if (file.isNull() || !file.tag()) {
        return QFileInfo(filePath).completeBaseName();
    }

    const TagLib::String title = file.tag()->title();
    const QString qTitle = QString::fromStdString(title.to8Bit(true));

    if (!qTitle.trimmed().isEmpty()) {
        return qTitle.trimmed();
    }

    return QFileInfo(filePath).completeBaseName();
}

QString PlayerWindow::readArtistWithTagLib(const QString &filePath) const
{
    TagLib::FileRef file(filePath.toUtf8().constData());
    if (file.isNull() || !file.tag()) {
        return QString();
    }

    const TagLib::String artist = file.tag()->artist();
    const QString qArtist = QString::fromStdString(artist.to8Bit(true));
    return qArtist.trimmed();
}

QString PlayerWindow::readAlbumWithTagLib(const QString &filePath) const
{
    TagLib::FileRef file(filePath.toUtf8().constData());
    if (file.isNull() || !file.tag()) {
        return QString();
    }

    const TagLib::String album = file.tag()->album();
    const QString qAlbum = QString::fromStdString(album.to8Bit(true));
    return qAlbum.trimmed();
}

QString PlayerWindow::cacheCoverArt(const QString &filePath,
                                    const QByteArray &imageData,
                                    const QString &mimeType) const
{
    if (imageData.isEmpty()) {
        return QString();
    }

    const QString baseDir =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/covers";
    QDir().mkpath(baseDir);

    QString ext = "img";
    if (mimeType.contains("png", Qt::CaseInsensitive)) {
        ext = "png";
    } else if (mimeType.contains("jpeg", Qt::CaseInsensitive) ||
               mimeType.contains("jpg", Qt::CaseInsensitive)) {
        ext = "jpg";
    }

    const QByteArray hash = QCryptographicHash::hash(
        QFileInfo(filePath).absoluteFilePath().toUtf8(),
        QCryptographicHash::Sha1
    ).toHex();

    const QString outPath = baseDir + "/" + QString::fromUtf8(hash) + "." + ext;

    QFile f(outPath);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(imageData);
        f.close();
        return outPath;
    }

    return QString();
}

QString PlayerWindow::findFolderCoverArt(const QString &filePath) const
{
    const QDir dir = QFileInfo(filePath).dir();

    const QStringList imageFilters = {
        "*.jpg", "*.jpeg", "*.png", "*.webp", "*.bmp"
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
        if (baseName.contains("cover", Qt::CaseInsensitive) ||
            baseName.contains("front", Qt::CaseInsensitive)) {
            return info.absoluteFilePath();
        }
    }

    return QString();
}

QString PlayerWindow::saveDownloadedCoverArt(const QString &filePath,
                                             const QByteArray &imageData,
                                             const QString &contentType) const
{
    if (imageData.isEmpty()) {
        return QString();
    }

    QString ext = "jpg";
    if (contentType.contains("png", Qt::CaseInsensitive)) {
        ext = "png";
    } else if (contentType.contains("jpeg", Qt::CaseInsensitive) ||
               contentType.contains("jpg", Qt::CaseInsensitive)) {
        ext = "jpg";
    }

    const QDir songDir = QFileInfo(filePath).dir();
    const QString folderPath = songDir.filePath("cover." + ext);

    QFile folderFile(folderPath);
    if (folderFile.open(QIODevice::WriteOnly)) {
        folderFile.write(imageData);
        folderFile.close();
        return folderPath;
    }

    return cacheCoverArt(filePath, imageData, contentType);
}

void PlayerWindow::queueOnlineCoverArtFetch(const QString &filePath)
{
    if (filePath.isEmpty()) {
        return;
    }

    if (!findFolderCoverArt(filePath).isEmpty()) {
        return;
    }

    if (attemptedOnlineCoverFetches.contains(filePath)) {
        return;
    }
    attemptedOnlineCoverFetches.insert(filePath);

    const QString artist = readArtistWithTagLib(filePath).trimmed();
    const QString album = readAlbumWithTagLib(filePath).trimmed();

    if (album.isEmpty()) {
        return;
    }

    QString queryText = QString("release:\"%1\"").arg(album);
    if (!artist.isEmpty()) {
        queryText += QString(" AND artist:\"%1\"").arg(artist);
    }

    QUrl mbUrl("https://musicbrainz.org/ws/2/release/");
    QUrlQuery mbQuery;
    mbQuery.addQueryItem("query", queryText);
    mbQuery.addQueryItem("fmt", "json");
    mbQuery.addQueryItem("limit", "5");
    mbUrl.setQuery(mbQuery);

    QNetworkRequest request(mbUrl);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader("User-Agent", kUserAgent);
    request.setRawHeader("Accept", "application/json");

    QNetworkReply *reply = networkManager->get(request);
    reply->setProperty("filePath", filePath);
    connect(reply, &QNetworkReply::finished, this, &PlayerWindow::onCoverSearchFinished);
}

void PlayerWindow::startCoverDownloadAttempt(const QString &filePath,
                                             const QStringList &mbids,
                                             int index)
{
    if (filePath.isEmpty() || index < 0 || index >= mbids.size()) {
        return;
    }

    const QUrl coverUrl(QString("https://coverartarchive.org/release/%1/front-500").arg(mbids[index]));

    QNetworkRequest request(coverUrl);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader("User-Agent", kUserAgent);

    QNetworkReply *reply = networkManager->get(request);
    reply->setProperty("filePath", filePath);
    reply->setProperty("mbids", mbids);
    reply->setProperty("mbidIndex", index);
    connect(reply, &QNetworkReply::finished, this, &PlayerWindow::onCoverDownloadFinished);
}

void PlayerWindow::onCoverSearchFinished()
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) {
        return;
    }

    const QString filePath = reply->property("filePath").toString();

    if (reply->error() != QNetworkReply::NoError) {
        reply->deleteLater();
        return;
    }

    const QByteArray jsonData = reply->readAll();
    reply->deleteLater();

    const QJsonDocument doc = QJsonDocument::fromJson(jsonData);
    if (!doc.isObject()) {
        return;
    }

    const QJsonArray releases = doc.object().value("releases").toArray();
    if (releases.isEmpty()) {
        return;
    }

    QStringList mbids;
    mbids.reserve(releases.size());

    for (const QJsonValue &value : releases) {
        const QString mbid = value.toObject().value("id").toString();
        if (!mbid.isEmpty()) {
            mbids.append(mbid);
        }
    }

    if (mbids.isEmpty()) {
        return;
    }

    startCoverDownloadAttempt(filePath, mbids, 0);
}

void PlayerWindow::onCoverDownloadFinished()
{
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) {
        return;
    }

    const QString filePath = reply->property("filePath").toString();
    const QStringList mbids = reply->property("mbids").toStringList();
    const int index = reply->property("mbidIndex").toInt();

    if (reply->error() != QNetworkReply::NoError) {
        reply->deleteLater();
        startCoverDownloadAttempt(filePath, mbids, index + 1);
        return;
    }

    const QByteArray imageData = reply->readAll();
    const QString contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
    reply->deleteLater();

    const QString savedPath = saveDownloadedCoverArt(filePath, imageData, contentType);
    if (savedPath.isEmpty()) {
        startCoverDownloadAttempt(filePath, mbids, index + 1);
        return;
    }

    coverArtCache.insert(filePath, savedPath);

    if (currentFilePath() == filePath) {
        updateCoverArt();
        emit mprisMetadataChanged();
    }
}

QString PlayerWindow::extractCoverArtToCache(const QString &filePath) const
{
    const QString suffix = QFileInfo(filePath).suffix().toLower();

    if (suffix == "mp3") {
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
                    return cached;
                }
            }
        }
    } else if (suffix == "flac") {
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
                return cached;
            }
        }
    }

    return findFolderCoverArt(filePath);
}

void PlayerWindow::startPulseSync()
{
    pulseMainloop = pa_threaded_mainloop_new();
    if (!pulseMainloop) {
        return;
    }

    pa_mainloop_api *api = pa_threaded_mainloop_get_api(pulseMainloop);

    // Create proplist
    pa_proplist *proplist = pa_proplist_new();

    // Set application properties
    pa_proplist_sets(proplist, PA_PROP_APPLICATION_NAME, "Deed");
    pa_proplist_sets(proplist, PA_PROP_APPLICATION_ID, "com.nikos.deed");

    // 🔥 These are the ones you asked for
    pa_proplist_sets(proplist, PA_PROP_MEDIA_ROLE, "music");

    // Optional but nice (shows icon in some apps)
    pa_proplist_sets(proplist, PA_PROP_APPLICATION_ICON_NAME, "multimedia-player");

    // Create context with proplist
    pulseContext = pa_context_new_with_proplist(api, "Deed", proplist);

    // Free proplist (context keeps a copy)
    pa_proplist_free(proplist);
    if (!pulseContext) {
        pa_threaded_mainloop_free(pulseMainloop);
        pulseMainloop = nullptr;
        return;
    }

    pa_context_set_state_callback(pulseContext, &PlayerWindow::pulseContextStateCallback, this);

    pa_threaded_mainloop_lock(pulseMainloop);

    if (pa_context_connect(pulseContext, nullptr, PA_CONTEXT_NOFLAGS, nullptr) < 0) {
        pa_threaded_mainloop_unlock(pulseMainloop);
        pa_context_unref(pulseContext);
        pulseContext = nullptr;
        pa_threaded_mainloop_free(pulseMainloop);
        pulseMainloop = nullptr;
        return;
    }

    if (pa_threaded_mainloop_start(pulseMainloop) < 0) {
        pa_threaded_mainloop_unlock(pulseMainloop);
        pa_context_disconnect(pulseContext);
        pa_context_unref(pulseContext);
        pulseContext = nullptr;
        pa_threaded_mainloop_free(pulseMainloop);
        pulseMainloop = nullptr;
        return;
    }

    pa_threaded_mainloop_unlock(pulseMainloop);
}

void PlayerWindow::stopPulseSync()
{
    if (!pulseMainloop) {
        return;
    }

    pa_threaded_mainloop_lock(pulseMainloop);

    if (pulseContext) {
        pa_context_disconnect(pulseContext);
        pa_context_unref(pulseContext);
        pulseContext = nullptr;
    }

    pa_threaded_mainloop_unlock(pulseMainloop);

    pa_threaded_mainloop_stop(pulseMainloop);
    pa_threaded_mainloop_free(pulseMainloop);
    pulseMainloop = nullptr;

    pulseReady = false;
    currentSinkInputIndex = kInvalidSinkInputIndex;
}

void PlayerWindow::requestPulseSinkInputRefresh()
{
    if (!pulseMainloop || !pulseContext || !pulseReady) {
        return;
    }

    pa_threaded_mainloop_lock(pulseMainloop);
    pa_operation *op = pa_context_get_sink_input_info_list(
        pulseContext,
        &PlayerWindow::pulseSinkInputInfoListCallback,
        this
    );
    if (op) {
        pa_operation_unref(op);
    }
    pa_threaded_mainloop_unlock(pulseMainloop);
}

void PlayerWindow::setPulseStreamVolume(int value)
{
    if (!pulseMainloop || !pulseContext || !pulseReady) {
        return;
    }

    if (currentSinkInputIndex == kInvalidSinkInputIndex) {
        requestPulseSinkInputRefresh();
        return;
    }

    pa_cvolume volume;
    pa_cvolume_set(&volume, 2, static_cast<pa_volume_t>((value / 100.0) * PA_VOLUME_NORM));

    pa_threaded_mainloop_lock(pulseMainloop);
    pa_operation *op = pa_context_set_sink_input_volume(
        pulseContext,
        currentSinkInputIndex,
        &volume,
        nullptr,
        nullptr
    );
    if (op) {
        pa_operation_unref(op);
    }
    pa_threaded_mainloop_unlock(pulseMainloop);
}

void PlayerWindow::applyPulseVolumeToUi(int value)
{
    updatingVolumeFromMixer = true;

    if (volumeSlider->value() != value) {
        volumeSlider->blockSignals(true);
        volumeSlider->setValue(value);
        volumeSlider->blockSignals(false);
    }

    audioOutput->setVolume(value / 100.0);
    updateVolumeLabel();
    emit mprisVolumeChanged();

    updatingVolumeFromMixer = false;
}

void PlayerWindow::updatePulseSinkInputFromInfo(const pa_sink_input_info *info)
{
    if (!info || !info->proplist) {
        return;
    }

    const char *pidStr = pa_proplist_gets(info->proplist, "application.process.id");
    if (!pidStr) {
        return;
    }

    const QString myPid = QString::number(QCoreApplication::applicationPid());
    if (QString::fromUtf8(pidStr) != myPid) {
        return;
    }

    currentSinkInputIndex = info->index;

    const pa_volume_t avg = pa_cvolume_avg(&info->volume);
    const int value = std::clamp(
        static_cast<int>((avg * 100.0) / PA_VOLUME_NORM + 0.5),
        0,
        150
    );

    QMetaObject::invokeMethod(this, [this, value]() {
        applyPulseVolumeToUi(value);
    }, Qt::QueuedConnection);
}

void PlayerWindow::pulseContextStateCallback(pa_context *context, void *userdata)
{
    auto *self = static_cast<PlayerWindow *>(userdata);
    if (!self || !self->pulseMainloop) {
        return;
    }

    switch (pa_context_get_state(context)) {
    case PA_CONTEXT_READY: {
        self->pulseReady = true;
        pa_context_set_subscribe_callback(context, &PlayerWindow::pulseSubscribeCallback, self);

        pa_operation *op = pa_context_subscribe(
            context,
            static_cast<pa_subscription_mask_t>(PA_SUBSCRIPTION_MASK_SINK_INPUT),
            nullptr,
            nullptr
        );
        if (op) {
            pa_operation_unref(op);
        }

        QMetaObject::invokeMethod(self, [self]() {
            self->requestPulseSinkInputRefresh();
        }, Qt::QueuedConnection);
        break;
    }
    case PA_CONTEXT_FAILED:
    case PA_CONTEXT_TERMINATED:
        self->pulseReady = false;
        self->currentSinkInputIndex = kInvalidSinkInputIndex;
        break;
    default:
        break;
    }

    pa_threaded_mainloop_signal(self->pulseMainloop, 0);
}

void PlayerWindow::pulseSubscribeCallback(pa_context *,
                                          pa_subscription_event_type_t eventType,
                                          uint32_t,
                                          void *userdata)
{
    auto *self = static_cast<PlayerWindow *>(userdata);
    if (!self) {
        return;
    }

    const pa_subscription_event_type_t facility =
        static_cast<pa_subscription_event_type_t>(eventType & PA_SUBSCRIPTION_EVENT_FACILITY_MASK);

    if (facility != PA_SUBSCRIPTION_EVENT_SINK_INPUT) {
        return;
    }

    QMetaObject::invokeMethod(self, [self]() {
        self->requestPulseSinkInputRefresh();
    }, Qt::QueuedConnection);
}

void PlayerWindow::pulseSinkInputInfoListCallback(pa_context *,
                                                  const pa_sink_input_info *info,
                                                  int eol,
                                                  void *userdata)
{
    auto *self = static_cast<PlayerWindow *>(userdata);
    if (!self) {
        return;
    }

    if (eol > 0) {
        return;
    }

    self->updatePulseSinkInputFromInfo(info);
}

QString PlayerWindow::currentCoverArtPath() const
{
    const QString filePath = currentFilePath();
    if (filePath.isEmpty()) {
        return QString();
    }

    return coverArtCache.value(filePath);
}

QString PlayerWindow::playlistDisplayTextForFile(const QString &filePath) const
{
    const QString cached = titleCache.value(filePath);
    if (!cached.isEmpty()) {
        return cached;
    }

    return QFileInfo(filePath).completeBaseName();
}

void PlayerWindow::rebuildTitleCache()
{
    titleCache.clear();

    for (const QString &filePath : playlist) {
        titleCache[filePath] = readTitleWithTagLib(filePath);
    }
}

void PlayerWindow::refreshPlaylistWidget()
{
    const QList<int> selectedRows = selectedRowsSorted();

    playlistWidget->clear();

    for (const QString &filePath : playlist) {
        auto *item = new QListWidgetItem(playlistDisplayTextForFile(filePath));
        item->setData(kFilePathRole, filePath);
        playlistWidget->addItem(item);
    }

    for (const int row : selectedRows) {
        if (row >= 0 && row < playlistWidget->count()) {
            playlistWidget->item(row)->setSelected(true);
        }
    }

    updatePlaylistSelection();
    updatePlaylistHighlight();
}

void PlayerWindow::addFilesToPlaylist(const QStringList &files)
{
    bool addedAny = false;

    for (const QString &file : files) {
        if (!playlist.contains(file)) {
            playlist.append(file);
            titleCache[file] = readTitleWithTagLib(file);
            addedAny = true;
        }
    }

    if (addedAny) {
        refreshPlaylistWidget();
        emit mprisMetadataChanged();
    }
}

void PlayerWindow::loadTrack(int index, bool autoPlay, qint64 startPosition)
{
    if (index < 0 || index >= playlist.size()) {
        return;
    }

    currentIndex = index;
    currentSinkInputIndex = kInvalidSinkInputIndex;

    const QString &filePath = playlist[currentIndex];
    const QFileInfo info(filePath);

    updatePlaylistSelection();
    updatePlaylistHighlight();

    isUserSeeking = false;

    titleLabel->setText("<b>" + info.completeBaseName().toHtmlEscaped() + "</b>");
    albumLabel->clear();

    if (!coverArtCache.contains(filePath)) {
        coverArtCache.insert(filePath, extractCoverArtToCache(filePath));
    }

    if (coverArtCache.value(filePath).isEmpty()) {
        queueOnlineCoverArtFetch(filePath);
    }

    updateCoverArt();

    player->setSource(QUrl::fromLocalFile(filePath));

    emit mprisMetadataChanged();
    emit mprisPlaybackStateChanged();
    emit mprisPositionChanged();

    QTimer::singleShot(300, this, [this]() {
        requestPulseSinkInputRefresh();
    });

    if (autoPlay && startPosition == 0) {
        pendingRestorePosition = 0;
        pendingRestoreAutoPlay = false;
        player->play();

        QTimer::singleShot(900, this, [this]() {
            requestPulseSinkInputRefresh();
        });

        return;
    }

    pendingRestorePosition = startPosition;
    pendingRestoreAutoPlay = autoPlay;
}

void PlayerWindow::updatePlayPauseButton()
{
    if (player->playbackState() == QMediaPlayer::PlayingState) {
        playPauseButton->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
    } else {
        playPauseButton->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    }
}

QList<int> PlayerWindow::selectedRowsSorted() const
{
    QList<int> rows;

    const QList<QListWidgetItem *> items = playlistWidget->selectedItems();
    for (QListWidgetItem *item : items) {
        rows.append(playlistWidget->row(item));
    }

    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());

    return rows;
}

void PlayerWindow::updatePlaylistSelection()
{
    if (currentIndex >= 0 && currentIndex < playlistWidget->count()) {
        playlistWidget->setCurrentRow(currentIndex, QItemSelectionModel::NoUpdate);
    }
}

void PlayerWindow::updatePlaylistHighlight()
{
    for (int i = 0; i < playlistWidget->count(); ++i) {
        QListWidgetItem *item = playlistWidget->item(i);
        if (!item) {
            continue;
        }

        QFont font = item->font();
        font.setBold(i == currentIndex);
        item->setFont(font);

        if (i == currentIndex) {
            item->setBackground(QBrush(QColor(60, 90, 140)));
            item->setForeground(QBrush(Qt::white));
        } else {
            item->setBackground(QBrush());
            item->setForeground(QBrush());
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
    newPlaylist.reserve(playlistWidget->count());

    for (int i = 0; i < playlistWidget->count(); ++i) {
        QListWidgetItem *item = playlistWidget->item(i);
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

    updatePlaylistSelection();
    updatePlaylistHighlight();
}

void PlayerWindow::showPlaylistContextMenu(const QPoint &pos)
{
    QListWidgetItem *clickedItem = playlistWidget->itemAt(pos);

    if (clickedItem && !clickedItem->isSelected()) {
        playlistWidget->clearSelection();
        clickedItem->setSelected(true);
        playlistWidget->setCurrentItem(clickedItem);
    }

    if (playlistWidget->selectedItems().isEmpty()) {
        return;
    }

    QMenu menu(this);

    QAction *removeAction = menu.addAction("Remove");
    QAction *moveUpAction = menu.addAction("Move Up");
    QAction *moveDownAction = menu.addAction("Move Down");

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
        titleCache.remove(removedFile);
        coverArtCache.remove(removedFile);
        attemptedOnlineCoverFetches.remove(removedFile);

        if (row < currentIndex) {
            --currentIndex;
        }
    }

    if (playlist.isEmpty()) {
        coverArtCache.clear();
        attemptedOnlineCoverFetches.clear();

        currentIndex = -1;
        currentSinkInputIndex = kInvalidSinkInputIndex;
        pendingRestorePosition = 0;
        pendingRestoreAutoPlay = false;
        isUserSeeking = false;
        player->stop();
        player->setSource(QUrl());
        positionSlider->setValue(0);
        titleLabel->setText("Drop audio files here or click Open");
        albumLabel->clear();
        coverLabel->setPixmap(QPixmap());
        coverLabel->setText("♪");
        setWindowTitle("Deed");
        refreshPlaylistWidget();
        emit mprisMetadataChanged();
        emit mprisPlaybackStateChanged();
        emit mprisPositionChanged();
        return;
    }

    rebuildTitleCache();

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

    rebuildTitleCache();
    refreshPlaylistWidget();

    playlistWidget->clearSelection();
    for (const int row : rows) {
        playlistWidget->item(row - 1)->setSelected(true);
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

    rebuildTitleCache();
    refreshPlaylistWidget();

    playlistWidget->clearSelection();
    for (const int row : rows) {
        playlistWidget->item(row + 1)->setSelected(true);
    }

    updatePlaylistHighlight();
    emit mprisMetadataChanged();
}

void PlayerWindow::updateTrackInfoLabels()
{
    if (currentIndex < 0 || currentIndex >= playlist.size()) {
        titleLabel->setText("Drop audio files here or click Open");
        albumLabel->clear();
        setWindowTitle("Deed");
        return;
    }

    const QString filePath = playlist[currentIndex];
    const QFileInfo info(filePath);

    const QMediaMetaData meta = player->metaData();

    QString title = meta.stringValue(QMediaMetaData::Title);
    if (title.isEmpty()) {
        title = titleCache.value(filePath, info.completeBaseName());
    }

    QString artist;
    const QVariant artistValue = meta.value(QMediaMetaData::ContributingArtist);
    if (artistValue.canConvert<QStringList>()) {
        const QStringList artists = artistValue.toStringList();
        if (!artists.isEmpty()) {
            artist = artists.join(", ");
        }
    }
    if (artist.isEmpty()) {
        artist = readArtistWithTagLib(filePath);
    }

    QString album = meta.stringValue(QMediaMetaData::AlbumTitle);
    if (album.isEmpty()) {
        album = readAlbumWithTagLib(filePath);
    }

    QString topLine = title.toHtmlEscaped();
    if (!artist.isEmpty()) {
        topLine += " - " + artist.toHtmlEscaped();
    }

    titleLabel->setText("<b>" + topLine + "</b>");
    albumLabel->setText(album.toHtmlEscaped());

    setWindowTitle("Deed - " + title + (artist.isEmpty() ? QString() : " - " + artist));
}

void PlayerWindow::updateCoverArt()
{
    const QString artPath = currentCoverArtPath();

    if (artPath.isEmpty()) {
        coverLabel->setPixmap(QPixmap());
        coverLabel->setText("♪");
        return;
    }

    QPixmap pixmap(artPath);
    if (pixmap.isNull()) {
        coverLabel->setPixmap(QPixmap());
        coverLabel->setText("♪");
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

void PlayerWindow::loadSettings()
{
    QSettings settings("Nikos", "Deed");

    const int savedVolume = settings.value("audio/volume", 50).toInt();
    volumeSlider->setValue(savedVolume);
    audioOutput->setVolume(savedVolume / 100.0);
    updateVolumeLabel();

    const QStringList savedPlaylist = settings.value("playlist/files").toStringList();
    const int savedIndex = settings.value("playlist/currentIndex", -1).toInt();
    const qint64 savedPosition = settings.value("playback/position", 0).toLongLong();
    const bool wasPlaying = settings.value("playback/wasPlaying", false).toBool();

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
        return;
    }

    currentIndex = savedIndex;
    if (currentIndex < 0 || currentIndex >= playlist.size()) {
        currentIndex = 0;
    }

    rebuildTitleCache();
    refreshPlaylistWidget();
    loadTrack(currentIndex, wasPlaying, savedPosition);
}

void PlayerWindow::saveSettings()
{
    QSettings settings("Nikos", "Deed");
    settings.setValue("audio/volume", volumeSlider->value());
    settings.setValue("playlist/files", playlist);
    settings.setValue("playlist/currentIndex", currentIndex);
    settings.setValue("playback/position", player->position());
    settings.setValue("playback/wasPlaying",
                      player->playbackState() == QMediaPlayer::PlayingState);
}

void PlayerWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void PlayerWindow::dropEvent(QDropEvent *event)
{
    QStringList files;

    const QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl &url : urls) {
        if (url.isLocalFile()) {
            const QString filePath = url.toLocalFile();
            const QFileInfo info(filePath);
            const QString suffix = info.suffix().toLower();

            if (suffix == "mp3" || suffix == "wav" || suffix == "ogg" || suffix == "flac") {
                files.append(filePath);
            }
        }
    }

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

void PlayerWindow::insertFilesIntoPlaylist(const QStringList &files, int row)
{
    row = std::clamp(row, 0, static_cast<int>(playlist.size()));

    QStringList newFiles;
    for (const QString &file : files) {
        if (!playlist.contains(file) && !newFiles.contains(file)) {
            newFiles.append(file);
            titleCache[file] = readTitleWithTagLib(file);
        }
    }

    if (newFiles.isEmpty()) {
        return;
    }

    for (int i = 0; i < newFiles.size(); ++i) {
        playlist.insert(row + i, newFiles[i]);
    }

    if (currentIndex >= row) {
        currentIndex += newFiles.size();
    }

    refreshPlaylistWidget();
    emit mprisMetadataChanged();
}

void PlayerWindow::closeEvent(QCloseEvent *event)
{
    saveSettings();
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

    const QMediaMetaData meta = player->metaData();
    const QString title = meta.stringValue(QMediaMetaData::Title);
    if (!title.isEmpty()) {
        return title;
    }

    return titleCache.value(filePath, QFileInfo(filePath).completeBaseName());
}

QString PlayerWindow::currentTrackArtist() const
{
    const QString filePath = currentFilePath();
    if (filePath.isEmpty()) {
        return QString();
    }

    const QMediaMetaData meta = player->metaData();
    const QVariant artistValue = meta.value(QMediaMetaData::ContributingArtist);
    if (artistValue.canConvert<QStringList>()) {
        const QStringList artists = artistValue.toStringList();
        if (!artists.isEmpty()) {
            return artists.join(", ");
        }
    }

    return readArtistWithTagLib(filePath);
}

QString PlayerWindow::currentTrackAlbum() const
{
    const QString filePath = currentFilePath();
    if (filePath.isEmpty()) {
        return QString();
    }

    const QMediaMetaData meta = player->metaData();
    const QString album = meta.stringValue(QMediaMetaData::AlbumTitle);
    if (!album.isEmpty()) {
        return album;
    }

    return readAlbumWithTagLib(filePath);
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
    return audioOutput->volume();
}

bool PlayerWindow::canGoNext() const
{
    return !playlist.isEmpty();
}

bool PlayerWindow::canGoPrevious() const
{
    return !playlist.isEmpty();
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
    case QMediaPlayer::PlayingState:
        return "Playing";
    case QMediaPlayer::PausedState:
        return "Paused";
    case QMediaPlayer::StoppedState:
    default:
        return "Stopped";
    }
}

void PlayerWindow::mprisPlay()
{
    if (!playlist.isEmpty()) {
        player->play();
    }
}

void PlayerWindow::mprisPause()
{
    player->pause();
}

void PlayerWindow::mprisPlayPause()
{
    if (player->playbackState() == QMediaPlayer::PlayingState) {
        player->pause();
    } else if (!playlist.isEmpty()) {
        player->play();
    }
}

void PlayerWindow::mprisStop()
{
    player->stop();
}

void PlayerWindow::mprisNext()
{
    if (playlist.isEmpty()) {
        return;
    }

    int newIndex = currentIndex + 1;
    if (newIndex >= playlist.size()) {
        newIndex = 0;
    }
    loadTrack(newIndex, true);
}

void PlayerWindow::mprisPrevious()
{
    if (playlist.isEmpty()) {
        return;
    }

    int newIndex = currentIndex - 1;
    if (newIndex < 0) {
        newIndex = static_cast<int>(playlist.size()) - 1;
    }
    loadTrack(newIndex, true);
}

void PlayerWindow::mprisSetPosition(qint64 position)
{
    player->setPosition(position);
}

void PlayerWindow::mprisSetVolume(double volume)
{
    if (volume < 0.0) {
        volume = 0.0;
    }
    if (volume > 1.0) {
        volume = 1.0;
    }

    audioOutput->setVolume(volume);
    volumeSlider->setValue(static_cast<int>(volume * 100.0));
    updateVolumeLabel();
    emit mprisVolumeChanged();

    if (!updatingVolumeFromMixer) {
        setPulseStreamVolume(static_cast<int>(volume * 100.0));
    }
}

void PlayerWindow::mprisRaise()
{
    show();
    raise();
    activateWindow();
}