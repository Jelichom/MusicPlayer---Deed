#ifndef PLAYLISTWIDGET_H
#define PLAYLISTWIDGET_H

#include <QList>
#include <QPoint>
#include <QTableWidget>
#include <QStringList>

class QTimer;
class QResizeEvent;
class QObject;
class QEvent;
class QDragEnterEvent;
class QDragLeaveEvent;
class QDragMoveEvent;
class QDropEvent;
class QMimeData;
class QPaintEvent;
class QMouseEvent;
class QWheelEvent;
class QTableWidgetItem;
class QRubberBand;

class PlaylistWidget : public QTableWidget
{
    Q_OBJECT

public:
    enum Column {
        ColumnTrackNumber = 0,
        ColumnTitle,
        ColumnArtist,
        ColumnAlbum,
        ColumnLength,
        ColumnBitrate,
        ColumnFileType,
        ColumnCount
    };

    explicit PlaylistWidget(QWidget *parent = nullptr);
    ~PlaylistWidget() override;

signals:
    void itemsReordered();
    void externalFilesDropped(const QStringList &files, int row);
    void rowDoubleClicked(int row);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void startDrag(Qt::DropActions supportedActions) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct RowData {
        QList<QTableWidgetItem *> items;
    };

    int dropRowForPosition(const QPoint &pos) const;
    QStringList audioFilesFromMimeData(const QMimeData *mimeData) const;
    void clearExternalDropIndicator();
    QList<int> selectedRowsSorted() const;
    RowData takeRow(int row);
    void insertRowData(int row, RowData &&rowData);
    void moveSelectedRows(int targetRow);

    void applyManualColumnWidthsToViewport();
    void applyColumnWidths(const QList<int> &widths);
    void rebalanceAfterUserResize(int section, int oldSize, int newSize);
    int availableViewportWidth() const;
    void updateDragAutoScroll(const QPoint &pos);
    void stopDragAutoScroll();
    bool handleDragWheel(QWheelEvent *event);
    void updateRubberBandSelection();

public:
    QList<int> columnWidths() const;
    void setColumnWidths(const QList<int> &widths);

private:
    bool m_hasExternalDropIndicator;
    int m_externalDropRow;
    bool m_resizingColumns;
    bool m_dragAutoScrollActive;
    int m_dragAutoScrollDelta;
    QPoint m_lastDragViewportPos;
    QTimer *m_dragAutoScrollTimer;
    QList<int> m_manualColumnWidths;
    QRubberBand *m_rubberBand;
    bool m_rubberBandSelecting;
    QPoint m_rubberBandOrigin;
};

#endif
