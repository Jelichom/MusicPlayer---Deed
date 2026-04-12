#include "playlistwidget.h"

#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QMimeData>
#include <QModelIndex>
#include <QRect>
#include <QUrl>

PlaylistWidget::PlaylistWidget(QWidget *parent)
    : QListWidget(parent)
{
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);
    setDropIndicatorShown(true);
}

int PlaylistWidget::dropRowFromPosition(const QPoint &pos) const
{
    const QModelIndex idx = indexAt(pos);
    if (!idx.isValid()) {
        return count();
    }

    const QRect rect = visualRect(idx);
    if (pos.y() > rect.center().y()) {
        return idx.row() + 1;
    }

    return idx.row();
}

void PlaylistWidget::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
        return;
    }

    QListWidget::dragEnterEvent(event);
}

void PlaylistWidget::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        setDropIndicatorShown(true);
        event->acceptProposedAction();
        return;
    }

    QListWidget::dragMoveEvent(event);
}

void PlaylistWidget::dropEvent(QDropEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        QStringList files;

        for (const QUrl &url : event->mimeData()->urls()) {
            if (!url.isLocalFile()) {
                continue;
            }

            const QString filePath = url.toLocalFile();
            const QString suffix = QFileInfo(filePath).suffix().toLower();

            if (suffix == "mp3" || suffix == "wav" || suffix == "ogg" || suffix == "flac") {
                files.append(filePath);
            }
        }

        if (!files.isEmpty()) {
            const int row = dropRowFromPosition(event->position().toPoint());
            emit externalFilesDropped(files, row);
            event->acceptProposedAction();
            return;
        }
    }

    QListWidget::dropEvent(event);
    emit itemsReordered();
}
