#include "MemoryTableWidget.h"
#include "Theme.h"
#include <QVBoxLayout>
#include <QHeaderView>
#include <QDateTime>
#include <QFont>
#include <algorithm>

// Column indices — keep in sync with the header setup below.
enum Col { COL_ID = 0, COL_ADDR, COL_SIZE, COL_PERM, COL_LIFETIME, COL_LABEL, COL_COUNT };

MemoryTableWidget::MemoryTableWidget(QWidget* parent) : QWidget(parent) {
    setStyleSheet(QString("background: %1;").arg(Theme::BG_APP));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* hdr = new QLabel("Memory Table  —  every allocation this session");
    hdr->setStyleSheet(QString(
        "color:%1; font-size:11px; font-weight:600; padding:6px 10px;"
    ).arg(Theme::TEXT_SECONDARY));
    layout->addWidget(hdr);

    table = new QTableWidget(0, COL_COUNT, this);
    table->setHorizontalHeaderLabels({"#", "Address", "Size", "Perm", "Lifetime", "Label"});
    table->horizontalHeader()->setSectionResizeMode(COL_ADDR,     QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(COL_SIZE,     QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(COL_PERM,     QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(COL_LIFETIME, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(COL_LABEL,    QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(COL_ID,       QHeaderView::ResizeToContents);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setAlternatingRowColors(true);
    table->verticalHeader()->hide();
    table->setStyleSheet(QString(
        "QTableWidget { background:%1; color:%2; gridline-color:%3; font-size:12px; }"
        "QTableWidget::item:selected { background:%4; color:white; }"
        "QHeaderView::section { background:%1; color:%5; font-weight:600; "
        "  padding:4px 8px; border:none; border-bottom:1px solid %3; }"
    ).arg(Theme::BG_APP, Theme::TEXT_PRIMARY, Theme::BORDER,
          "#3b82d4", Theme::TEXT_SECONDARY));
    layout->addWidget(table, 1);
}

void MemoryTableWidget::clear() {
    table->setRowCount(0);
    rowForId.clear();
}

// ── helpers ──────────────────────────────────────────────────────────────────

QString MemoryTableWidget::fmtAddr(long addr) {
    return QString("0x%1").arg((unsigned long)addr, 12, 16, QChar('0'));
}

QString MemoryTableWidget::fmtSize(long size) {
    if (size >= 1024 * 1024)
        return QString("%1 MB").arg(size / (1024.0 * 1024.0), 0, 'f', 2);
    if (size >= 1024)
        return QString("%1 KB").arg(size / 1024.0, 0, 'f', 1);
    return QString("%1 B").arg(size);
}

QString MemoryTableWidget::fmtTime(qint64 ms) {
    return QDateTime::fromMSecsSinceEpoch(ms).toString("HH:mm:ss.zzz");
}

// ── slot implementations ──────────────────────────────────────────────────────

void MemoryTableWidget::onArenaUpdated(const std::vector<ArenaBlock>& blocks) {
    for (const auto& b : blocks) {
        if (!b.used) continue;
        ensureRow(b.id, b);
    }
}

void MemoryTableWidget::ensureRow(int id, const ArenaBlock& b) {
    int row;
    if (rowForId.contains(id)) {
        row = rowForId[id];
    } else {
        row = table->rowCount();
        table->insertRow(row);
        rowForId[id] = row;
    }

    auto setCell = [&](int col, const QString& text, Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter) {
        QTableWidgetItem* item = table->item(row, col);
        if (!item) {
            item = new QTableWidgetItem(text);
            item->setTextAlignment(align);
            table->setItem(row, col, item);
        } else {
            item->setText(text);
        }
        item->setForeground(QColor(Theme::TEXT_PRIMARY));
        // Restore live row background
        item->setBackground(QColor(Theme::BG_APP));
    };

    setCell(COL_ID,       QString::number(id),  Qt::AlignRight  | Qt::AlignVCenter);
    setCell(COL_ADDR,     fmtAddr(b.offset),    Qt::AlignLeft   | Qt::AlignVCenter);
    setCell(COL_SIZE,     fmtSize(b.size),       Qt::AlignRight  | Qt::AlignVCenter);
    setCell(COL_PERM,     "RW",                  Qt::AlignCenter | Qt::AlignVCenter);
    setCell(COL_LIFETIME, "Alive",               Qt::AlignCenter | Qt::AlignVCenter);
    setCell(COL_LABEL,    b.label,               Qt::AlignLeft   | Qt::AlignVCenter);

    // All sandbox blocks are RW — colour green
    if (auto* permItem = table->item(row, COL_PERM))
        permItem->setForeground(QColor("#22C55E"));
}

void MemoryTableWidget::onEventLogged(const AllocEvent& ev) {
    if (ev.op == AllocEvent::Op::Free && rowForId.contains(ev.blockId)) {
        markFreed(ev.blockId, ev.timestampMs);
    }
    if (ev.op == AllocEvent::Op::Reset) {
        // Collect keys first — markFreed() removes from rowForId, so we cannot
        // iterate and erase simultaneously (undefined behaviour → crash on Reset).
        const QList<int> liveIds = rowForId.keys();
        for (int id : liveIds) {
            markFreed(id, ev.timestampMs);
        }
    }
}

void MemoryTableWidget::markFreed(int blockId, qint64 timestampMs) {
    if (!rowForId.contains(blockId)) return;
    int row = rowForId[blockId];

    QString freedAt = QString("Freed %1").arg(fmtTime(timestampMs));
    QColor mutedBg(Theme::BG_INPUT);
    QColor mutedFg(Theme::TEXT_MUTED);

    for (int col = 0; col < COL_COUNT; col++) {
        QTableWidgetItem* item = table->item(row, col);
        if (!item) { item = new QTableWidgetItem(); table->setItem(row, col, item); }
        item->setBackground(mutedBg);
        item->setForeground(mutedFg);
        if (col == COL_LIFETIME) item->setText(freedAt);
    }
    // Remove from rowForId so a new alloc with a recycled id gets a fresh row.
    rowForId.remove(blockId);
}
