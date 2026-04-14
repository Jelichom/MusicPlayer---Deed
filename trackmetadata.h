#ifndef TRACKMETADATA_H
#define TRACKMETADATA_H

#include <QString>
#include <QtGlobal>

namespace TrackMetadata {

struct Info {
    QString title;
    QString artist;
    QString album;
    QString length;
    QString bitrate;
    QString fileType;
    QString trackNumber;
    qint64 durationMs = 0;
};

Info read(const QString &filePath);

QString title(const QString &filePath);
QString artist(const QString &filePath);
QString album(const QString &filePath);
QString length(const QString &filePath);
QString bitrate(const QString &filePath);
QString fileType(const QString &filePath);
QString trackNumber(const QString &filePath);

}

#endif
