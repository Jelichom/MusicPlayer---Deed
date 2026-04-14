#include "playlistwidget.h"

#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QHeaderView>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QRubberBand>
#include <QTableWidgetItem>
#include <QTimer>
#include <QUrl>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {
constexpr const char *kInternalRowsMimeType = "application/x-deed-playlist-rows";
constexpr int kEdgeScrollZonePx = 39;
constexpr int kAutoScrollTimerMs = 120;
constexpr int kMinAutoScrollStep = 1;
constexpr int kMaxAutoScrollStep = 8;

bool isSupportedAudioFile(const QString &filePath)
{
    const QString lower = filePath.toLower();
    return lower.endsWith(".mp3") ||
           lower.endsWith(".wav") ||
           lower.endsWith(".ogg") ||
           lower.endsWith(".flac");
}

QList<int> defaultColumnWidths()
{
    return {56, 220, 170, 190, 80, 90, 80};
}

QList<int> minimumColumnWidths()
{
    return {48, 120, 100, 110, 64, 72, 64};
}

int signedStepForDistanceToEdge(int distanceToEdge)
{
    if (distanceToEdge < 0 || distanceToEdge > kEdgeScrollZonePx) {
        return 0;
    }

    const double closeness = 1.0 - (static_cast<double>(distanceToEdge) / kEdgeScrollZonePx);
    const double curved = std::pow(closeness, 4.0);
    const int step = kMinAutoScrollStep + static_cast<int>(std::round(curved * (kMaxAutoScrollStep - kMinAutoScrollStep)));
    return std::clamp(step, kMinAutoScrollStep, kMaxAutoScrollStep);
}
}

PlaylistWidget::PlaylistWidget(QWidget *parent)
    : QTableWidget(parent),
      m_hasExternalDropIndicator(false),
      m_externalDropRow(0),
      m_resizingColumns(false),
      m_dragAutoScrollActive(false),
      m_dragAutoScrollDelta(0),
      m_lastDragViewportPos(),
      m_dragAutoScrollTimer(new QTimer(this)),
      m_manualColumnWidths(defaultColumnWidths()),
      m_rubberBand(new QRubberBand(QRubberBand::Rectangle, viewport())),
      m_rubberBandSelecting(false),
      m_rubberBandOrigin()
{
    qApp->installEventFilter(this);

    setColumnCount(ColumnCount);
    setHorizontalHeaderLabels({"#", "Title", "Artist", "Album", "Length", "Bitrate", "Type"});
    setShowGrid(false);
    setAlternatingRowColors(false);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setDragEnabled(true);
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);
    viewport()->setMouseTracking(true);
    setAutoScroll(false);
    setDragDropOverwriteMode(false);
    setDropIndicatorShown(false);
    setSortingEnabled(false);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::MoveAction);
    verticalHeader()->setVisible(false);
    verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    verticalHeader()->setDefaultSectionSize(24);

    QHeaderView *header = horizontalHeader();
    header->setSectionsMovable(false);
    header->setStretchLastSection(false);
    header->setMinimumSectionSize(48);
    for (int i = 0; i < ColumnCount; ++i) {
        header->setSectionResizeMode(i, QHeaderView::Interactive);
    }
    applyManualColumnWidthsToViewport();

    connect(header, &QHeaderView::sectionResized, this,
            [this](int section, int oldSize, int newSize) {
        if (m_resizingColumns || section < 0 || section >= ColumnCount || oldSize == newSize) {
            return;
        }
        rebalanceAfterUserResize(section, oldSize, newSize);
    });

    m_dragAutoScrollTimer->setInterval(kAutoScrollTimerMs);
    connect(m_dragAutoScrollTimer, &QTimer::timeout, this, [this]() {
        if (!m_dragAutoScrollActive || m_dragAutoScrollDelta == 0 || !verticalScrollBar()) {
            return;
        }

        QScrollBar *bar = verticalScrollBar();
        const int oldValue = bar->value();
        bar->setValue(oldValue + m_dragAutoScrollDelta);

        if (bar->value() != oldValue) {
            const int nextRow = dropRowForPosition(m_lastDragViewportPos);
            if (nextRow != m_externalDropRow) {
                m_externalDropRow = nextRow;
                viewport()->update();
            }
        }
    });

    connect(this, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        emit rowDoubleClicked(row);
    });

    m_rubberBand->hide();
}

PlaylistWidget::~PlaylistWidget()
{
    qApp->removeEventFilter(this);
}

void PlaylistWidget::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->source() == this && event->mimeData()->hasFormat(kInternalRowsMimeType)) {
        m_externalDropRow = dropRowForPosition(event->position().toPoint());
        m_hasExternalDropIndicator = true;
        updateDragAutoScroll(event->position().toPoint());
        viewport()->update();
        event->setDropAction(Qt::MoveAction);
        event->accept();
        return;
    }

    if (!audioFilesFromMimeData(event->mimeData()).isEmpty()) {
        m_externalDropRow = dropRowForPosition(event->position().toPoint());
        m_hasExternalDropIndicator = true;
        updateDragAutoScroll(event->position().toPoint());
        viewport()->update();
        event->acceptProposedAction();
        return;
    }

    QTableWidget::dragEnterEvent(event);
}

void PlaylistWidget::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->source() == this && event->mimeData()->hasFormat(kInternalRowsMimeType)) {
        m_externalDropRow = dropRowForPosition(event->position().toPoint());
        m_hasExternalDropIndicator = true;
        updateDragAutoScroll(event->position().toPoint());
        viewport()->update();
        event->setDropAction(Qt::MoveAction);
        event->accept();
        return;
    }

    if (!audioFilesFromMimeData(event->mimeData()).isEmpty()) {
        m_externalDropRow = dropRowForPosition(event->position().toPoint());
        m_hasExternalDropIndicator = true;
        updateDragAutoScroll(event->position().toPoint());
        viewport()->update();
        event->acceptProposedAction();
        return;
    }

    QTableWidget::dragMoveEvent(event);
}

void PlaylistWidget::dragLeaveEvent(QDragLeaveEvent *event)
{
    stopDragAutoScroll();
    clearExternalDropIndicator();
    QTableWidget::dragLeaveEvent(event);
}

void PlaylistWidget::dropEvent(QDropEvent *event)
{
    if (event->source() == this && event->mimeData()->hasFormat(kInternalRowsMimeType)) {
        const int targetRow = dropRowForPosition(event->position().toPoint());
        stopDragAutoScroll();
        clearExternalDropIndicator();
        moveSelectedRows(targetRow);
        event->setDropAction(Qt::MoveAction);
        event->accept();
        return;
    }

    const QStringList files = audioFilesFromMimeData(event->mimeData());
    if (!files.isEmpty()) {
        const int insertRow = dropRowForPosition(event->position().toPoint());
        stopDragAutoScroll();
        clearExternalDropIndicator();
        emit externalFilesDropped(files, insertRow);
        event->acceptProposedAction();
        return;
    }

    stopDragAutoScroll();
    clearExternalDropIndicator();
    QTableWidget::dropEvent(event);
}

void PlaylistWidget::mousePressEvent(QMouseEvent *event)
{
    const QPoint pos = event->position().toPoint();

    if (event->button() == Qt::RightButton) {
        const QModelIndex index = indexAt(pos);
        if (index.isValid() && selectionModel() &&
            selectionModel()->isRowSelected(index.row(), QModelIndex())) {
            event->accept();
            return;
        }
    }

    if (event->button() == Qt::LeftButton && !indexAt(pos).isValid()) {
        m_rubberBandSelecting = true;
        m_rubberBandOrigin = pos;
        m_rubberBand->setGeometry(QRect(m_rubberBandOrigin, QSize()));
        m_rubberBand->show();
        clearSelection();
        setCurrentItem(nullptr);
        event->accept();
        return;
    }

    QTableWidget::mousePressEvent(event);
}

void PlaylistWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_rubberBandSelecting) {
        const QRect rect = QRect(m_rubberBandOrigin, event->position().toPoint()).normalized();
        m_rubberBand->setGeometry(rect);
        updateRubberBandSelection();
        event->accept();
        return;
    }

    QTableWidget::mouseMoveEvent(event);
}

void PlaylistWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_rubberBandSelecting && event->button() == Qt::LeftButton) {
        const QRect rect = QRect(m_rubberBandOrigin, event->position().toPoint()).normalized();
        m_rubberBand->setGeometry(rect);
        updateRubberBandSelection();
        m_rubberBand->hide();
        m_rubberBandSelecting = false;
        event->accept();
        return;
    }

    QTableWidget::mouseReleaseEvent(event);
}

void PlaylistWidget::startDrag(Qt::DropActions)
{
    if (selectedRowsSorted().isEmpty()) {
        return;
    }

    auto *mimeData = new QMimeData();
    mimeData->setData(kInternalRowsMimeType, QByteArray("move"));

    QDrag drag(this);
    drag.setMimeData(mimeData);
    drag.exec(Qt::MoveAction);
}

void PlaylistWidget::resizeEvent(QResizeEvent *event)
{
    QTableWidget::resizeEvent(event);
    applyManualColumnWidthsToViewport();
}

void PlaylistWidget::wheelEvent(QWheelEvent *event)
{
    if (handleDragWheel(event)) {
        return;
    }

    QTableWidget::wheelEvent(event);
}

bool PlaylistWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (m_dragAutoScrollActive && event->type() == QEvent::Wheel) {
        auto *wheelEvent = static_cast<QWheelEvent *>(event);
        if (handleDragWheel(wheelEvent)) {
            return true;
        }
    }

    return QTableWidget::eventFilter(watched, event);
}


void PlaylistWidget::updateRubberBandSelection()
{
    if (!selectionModel()) {
        return;
    }

    selectionModel()->clearSelection();

    const QRect selectionRect = m_rubberBand->geometry().normalized();
    for (int row = 0; row < rowCount(); ++row) {
        QRect rowRect;
        for (int column = 0; column < columnCount(); ++column) {
            const QModelIndex index = model()->index(row, column);
            rowRect = rowRect.united(visualRect(index));
        }

        if (rowRect.isValid() && rowRect.intersects(selectionRect)) {
            const QModelIndex index = model()->index(row, 0);
            selectionModel()->select(index, QItemSelectionModel::Select | QItemSelectionModel::Rows);
        }
    }
}

bool PlaylistWidget::handleDragWheel(QWheelEvent *event)
{
    if (!m_dragAutoScrollActive || !verticalScrollBar() || !event) {
        return false;
    }

    const QPoint globalPos = event->globalPosition().toPoint();
    const QPoint localPos = viewport()->mapFromGlobal(globalPos);
    if (!viewport()->rect().adjusted(-24, -24, 24, 24).contains(localPos)) {
        return false;
    }

    const QPoint delta = event->angleDelta();
    if (delta.isNull()) {
        return false;
    }

    verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
    m_lastDragViewportPos = localPos;
    m_externalDropRow = dropRowForPosition(m_lastDragViewportPos);
    viewport()->update();
    event->accept();
    return true;
}

void PlaylistWidget::paintEvent(QPaintEvent *event)
{
    QTableWidget::paintEvent(event);

    if (!m_hasExternalDropIndicator) {
        return;
    }

    QPainter painter(viewport());
    QPen pen(QColor(235, 235, 235));
    pen.setWidth(3);
    painter.setPen(pen);

    int y = 0;
    if (rowCount() == 0) {
        y = 2;
    } else if (m_externalDropRow >= rowCount()) {
        y = rowViewportPosition(rowCount() - 1) + rowHeight(rowCount() - 1);
    } else {
        y = rowViewportPosition(m_externalDropRow);
    }

    painter.drawLine(4, y, viewport()->width() - 4, y);
}

int PlaylistWidget::dropRowForPosition(const QPoint &pos) const
{
    const int row = rowAt(pos.y());
    if (row < 0) {
        return rowCount();
    }

    const int rowTop = rowViewportPosition(row);
    const int rowMid = rowTop + (rowHeight(row) / 2);
    return pos.y() < rowMid ? row : row + 1;
}

QStringList PlaylistWidget::audioFilesFromMimeData(const QMimeData *mimeData) const
{
    QStringList files;
    if (!mimeData || !mimeData->hasUrls()) {
        return files;
    }

    const QList<QUrl> urls = mimeData->urls();
    for (const QUrl &url : urls) {
        if (!url.isLocalFile()) {
            continue;
        }

        const QString filePath = url.toLocalFile();
        if (isSupportedAudioFile(filePath)) {
            files.append(filePath);
        }
    }

    return files;
}

void PlaylistWidget::clearExternalDropIndicator()
{
    if (!m_hasExternalDropIndicator) {
        return;
    }

    m_hasExternalDropIndicator = false;
    m_externalDropRow = 0;
    viewport()->update();
}

QList<int> PlaylistWidget::selectedRowsSorted() const
{
    QList<int> rows;
    const QModelIndexList selected = selectionModel() ? selectionModel()->selectedRows() : QModelIndexList{};
    rows.reserve(selected.size());

    for (const QModelIndex &index : selected) {
        rows.append(index.row());
    }

    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    return rows;
}

PlaylistWidget::RowData PlaylistWidget::takeRow(int row)
{
    RowData data;
    data.items.reserve(columnCount());

    for (int column = 0; column < columnCount(); ++column) {
        data.items.append(takeItem(row, column));
    }

    removeRow(row);
    return data;
}

void PlaylistWidget::insertRowData(int row, RowData &&rowData)
{
    insertRow(row);
    for (int column = 0; column < rowData.items.size(); ++column) {
        setItem(row, column, rowData.items.at(column));
    }
}

void PlaylistWidget::moveSelectedRows(int targetRow)
{
    QList<int> rows = selectedRowsSorted();
    if (rows.isEmpty()) {
        return;
    }

    const int firstRow = rows.first();
    const int lastRow = rows.last();
    int boundedTargetRow = std::clamp(targetRow, 0, rowCount());

    if (boundedTargetRow >= firstRow && boundedTargetRow <= lastRow + 1) {
        return;
    }

    QList<RowData> movedRows;
    movedRows.reserve(rows.size());

    for (int i = rows.size() - 1; i >= 0; --i) {
        movedRows.prepend(takeRow(rows.at(i)));
    }

    for (const int row : rows) {
        if (row < boundedTargetRow) {
            --boundedTargetRow;
        }
    }
    boundedTargetRow = std::clamp(boundedTargetRow, 0, rowCount());

    for (int i = 0; i < movedRows.size(); ++i) {
        insertRowData(boundedTargetRow + i, std::move(movedRows[i]));
    }

    clearSelection();
    if (selectionModel()) {
        for (int i = 0; i < rows.size(); ++i) {
            const QModelIndex index = model()->index(boundedTargetRow + i, 0);
            selectionModel()->select(index, QItemSelectionModel::Select | QItemSelectionModel::Rows);
        }
        setCurrentCell(boundedTargetRow, 0, QItemSelectionModel::NoUpdate);
    }

    emit itemsReordered();
}

void PlaylistWidget::updateDragAutoScroll(const QPoint &pos)
{
    m_lastDragViewportPos = pos;

    const int viewportHeight = viewport()->height();
    int delta = 0;

    if (pos.y() < kEdgeScrollZonePx) {
        delta = -signedStepForDistanceToEdge(pos.y());
    } else if (pos.y() > viewportHeight - kEdgeScrollZonePx) {
        delta = signedStepForDistanceToEdge(viewportHeight - pos.y());
    }

    m_dragAutoScrollDelta = delta;
    m_dragAutoScrollActive = true;

    if (delta == 0) {
        m_dragAutoScrollTimer->stop();
    } else if (!m_dragAutoScrollTimer->isActive()) {
        m_dragAutoScrollTimer->start();
    }
}

void PlaylistWidget::stopDragAutoScroll()
{
    m_dragAutoScrollActive = false;
    m_dragAutoScrollDelta = 0;
    m_dragAutoScrollTimer->stop();
}

int PlaylistWidget::availableViewportWidth() const
{
    int availableWidth = viewport()->width();
    if (verticalScrollBar() && verticalScrollBar()->isVisible()) {
        availableWidth -= verticalScrollBar()->width();
    }
    return availableWidth;
}

void PlaylistWidget::applyColumnWidths(const QList<int> &widths)
{
    if (widths.size() != ColumnCount) {
        return;
    }

    m_resizingColumns = true;
    for (int i = 0; i < ColumnCount; ++i) {
        QTableWidget::setColumnWidth(i, widths.at(i));
    }
    m_resizingColumns = false;
}

void PlaylistWidget::applyManualColumnWidthsToViewport()
{
    if (m_resizingColumns) {
        return;
    }

    if (m_manualColumnWidths.size() != ColumnCount) {
        m_manualColumnWidths = defaultColumnWidths();
    }

    const QList<int> mins = minimumColumnWidths();
    int availableWidth = availableViewportWidth();
    if (availableWidth <= 0) {
        return;
    }

    int totalManualWidth = 0;
    for (int i = 0; i < ColumnCount; ++i) {
        totalManualWidth += std::max(mins.at(i), m_manualColumnWidths.at(i));
    }

    if (totalManualWidth <= 0) {
        return;
    }

    QList<int> scaledWidths;
    scaledWidths.reserve(ColumnCount);
    int usedWidth = 0;

    for (int i = 0; i < ColumnCount; ++i) {
        int width = static_cast<int>(
            (static_cast<long long>(std::max(mins.at(i), m_manualColumnWidths.at(i))) * availableWidth) / totalManualWidth
        );
        width = std::max(mins.at(i), width);
        scaledWidths.append(width);
        usedWidth += width;
    }

    int delta = availableWidth - usedWidth;
    if (delta != 0) {
        if (delta > 0) {
            scaledWidths[ColumnCount - 1] += delta;
        } else {
            for (int i = ColumnCount - 1; i >= 0 && delta < 0; --i) {
                const int shrink = std::min(scaledWidths.at(i) - mins.at(i), -delta);
                if (shrink > 0) {
                    scaledWidths[i] -= shrink;
                    delta += shrink;
                }
            }
        }
    }

    applyColumnWidths(scaledWidths);
}

void PlaylistWidget::rebalanceAfterUserResize(int section, int oldSize, int newSize)
{
    if (m_manualColumnWidths.size() != ColumnCount) {
        m_manualColumnWidths = defaultColumnWidths();
    }

    const QList<int> mins = minimumColumnWidths();
    QList<int> widths = columnWidths();

    const int neighbor = (section == ColumnCount - 1) ? section - 1 : section + 1;
    if (neighbor < 0 || neighbor >= ColumnCount) {
        m_manualColumnWidths = widths;
        return;
    }

    const int requestedDelta = newSize - oldSize;
    int appliedDelta = requestedDelta;

    if (requestedDelta > 0) {
        const int neighborShrinkRoom = widths.at(neighbor) - mins.at(neighbor);
        appliedDelta = std::min(requestedDelta, neighborShrinkRoom);
    } else if (requestedDelta < 0) {
        const int sectionShrinkRoom = oldSize - mins.at(section);
        appliedDelta = -std::min(-requestedDelta, sectionShrinkRoom);
    }

    if (appliedDelta != requestedDelta) {
        widths[section] = oldSize + appliedDelta;
    } else {
        widths[section] = newSize;
    }
    widths[section] = std::max(mins.at(section), widths.at(section));
    widths[neighbor] = std::max(mins.at(neighbor), widths.at(neighbor) - appliedDelta);

    const int targetTotal = availableViewportWidth();
    if (targetTotal > 0) {
        int total = 0;
        for (int width : widths) {
            total += width;
        }
        int remainder = targetTotal - total;
        if (remainder != 0) {
            const int absorbIndex = (section == ColumnCount - 1) ? neighbor : ColumnCount - 1;
            if (remainder > 0) {
                widths[absorbIndex] += remainder;
            } else {
                for (int i = ColumnCount - 1; i >= 0 && remainder < 0; --i) {
                    const int idx = (i == section) ? absorbIndex : i;
                    if (idx < 0 || idx >= ColumnCount) {
                        continue;
                    }
                    const int shrink = std::min(widths.at(idx) - mins.at(idx), -remainder);
                    if (shrink > 0) {
                        widths[idx] -= shrink;
                        remainder += shrink;
                    }
                }
            }
        }
    }

    applyColumnWidths(widths);
    m_manualColumnWidths = widths;
}

QList<int> PlaylistWidget::columnWidths() const
{
    QList<int> widths;
    widths.reserve(ColumnCount);
    for (int i = 0; i < ColumnCount; ++i) {
        widths.append(horizontalHeader()->sectionSize(i));
    }
    return widths;
}

void PlaylistWidget::setColumnWidths(const QList<int> &widths)
{
    if (widths.size() == ColumnCount) {
        m_manualColumnWidths = widths;
    } else {
        m_manualColumnWidths = defaultColumnWidths();
    }
    applyManualColumnWidthsToViewport();
}
