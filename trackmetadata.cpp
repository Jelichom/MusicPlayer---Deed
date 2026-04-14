#include "trackmetadata.h"

#include <QFileInfo>

#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/audioproperties.h>

namespace {

QString fromTagLibString(const TagLib::String &value)
{
    return QString::fromStdString(value.to8Bit(true)).trimmed();
}

QString formatTimeSeconds(int totalSeconds)
{
    if (totalSeconds <= 0) {
        return QString();
    }

    const int minutes = totalSeconds / 60;
    const int seconds = totalSeconds % 60;

    return QStringLiteral("%1:%2")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'));
}

QString detectFileType(const QString &filePath)
{
    const QString suffix = QFileInfo(filePath).suffix().toUpper();
    return suffix.isEmpty() ? QStringLiteral("?") : suffix;
}

}

namespace TrackMetadata {

Info read(const QString &filePath)
{
    Info info;
    info.title = QFileInfo(filePath).completeBaseName();
    info.fileType = detectFileType(filePath);

    TagLib::FileRef file(filePath.toUtf8().constData());
    if (file.isNull()) {
        return info;
    }

    if (file.tag()) {
        const QString titleText = fromTagLibString(file.tag()->title());
        if (!titleText.isEmpty()) {
            info.title = titleText;
        }

        info.artist = fromTagLibString(file.tag()->artist());
        info.album = fromTagLibString(file.tag()->album());

        const unsigned int track = file.tag()->track();
        if (track > 0) {
            info.trackNumber = QString::number(track);
        }
    }

    if (file.audioProperties()) {
        const int seconds = file.audioProperties()->lengthInSeconds();
        info.length = formatTimeSeconds(seconds);
        if (seconds > 0) {
            info.durationMs = static_cast<qint64>(seconds) * 1000;
        }

        const int kbps = file.audioProperties()->bitrate();
        if (kbps > 0) {
            info.bitrate = QStringLiteral("%1 kbps").arg(kbps);
        }
    }

    return info;
}

QString title(const QString &filePath) { return read(filePath).title; }
QString artist(const QString &filePath) { return read(filePath).artist; }
QString album(const QString &filePath) { return read(filePath).album; }
QString length(const QString &filePath) { return read(filePath).length; }
QString bitrate(const QString &filePath) { return read(filePath).bitrate; }
QString fileType(const QString &filePath) { return read(filePath).fileType; }
QString trackNumber(const QString &filePath) { return read(filePath).trackNumber; }

}
