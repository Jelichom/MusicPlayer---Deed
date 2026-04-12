#ifndef PLAYLISTWIDGET_H
#define PLAYLISTWIDGET_H

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QListWidget>
#include <QPoint>
#include <QStringList>

class PlaylistWidget : public QListWidget
{
    Q_OBJECT

public:
    explicit PlaylistWidget(QWidget *parent = nullptr);

signals:
    void itemsReordered();
    void externalFilesDropped(const QStringList &files, int row);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    int dropRowFromPosition(const QPoint &pos) const;

    int externalDropIndicatorRow = -1;
};

#endif
