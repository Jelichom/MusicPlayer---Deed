#include "playlistwidget.h"

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QMimeData>
#include <QModelIndex>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QRect>
#include <QStyleOptionViewItem>
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
        externalDropIndicatorRow = dropRowFromPosition(event->position().toPoint());
        viewport()->update();
        event->acceptProposedAction();
        return;
    }

    externalDropIndicatorRow = -1;
    QListWidget::dragMoveEvent(event);
}

void PlaylistWidget::dragLeaveEvent(QDragLeaveEvent *event)
{
    externalDropIndicatorRow = -1;
    viewport()->update();
    QListWidget::dragLeaveEvent(event);
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
            externalDropIndicatorRow = -1;
            viewport()->update();
            emit externalFilesDropped(files, row);
            event->acceptProposedAction();
            return;
        }
    }

    externalDropIndicatorRow = -1;
    QListWidget::dropEvent(event);
    emit itemsReordered();
}

void PlaylistWidget::paintEvent(QPaintEvent *event)
{
    QListWidget::paintEvent(event);

    if (externalDropIndicatorRow < 0) {
        return;
    }

    QPainter painter(viewport());
    QPen pen(palette().highlight().color(), 2);
    painter.setPen(pen);

    int y = 0;

    if (count() == 0) {
        y = 2;
    } else if (externalDropIndicatorRow >= count()) {
        y = visualItemRect(item(count() - 1)).bottom() + 1;
    } else {
        y = visualItemRect(item(externalDropIndicatorRow)).top();
    }

    painter.drawLine(2, y, viewport()->width() - 2, y);
}
