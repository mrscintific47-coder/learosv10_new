#pragma once
#include <QWidget>
#include <QTableWidget>
#include <QLabel>
#include <QHeaderView>
#include <vector>
#include "MemoryLabDriver.h"

// Live table: Address | Size | Perm | Lifetime | Label
// All data comes directly from ArenaBlock — no synthetic values.
// Freed blocks stay in the table with a "Freed" lifetime and a muted row color
// so the student can see the full history of the session.
class MemoryTableWidget : public QWidget {
    Q_OBJECT
public:
    explicit MemoryTableWidget(QWidget* parent = nullptr);

public slots:
    // Called by MemoryLabDriver::arenaUpdated — updates/adds rows for live blocks.
    void onArenaUpdated(const std::vector<ArenaBlock>& blocks);
    // Called by MemoryLabDriver::eventLogged — marks freed blocks as dead.
    void onEventLogged(const AllocEvent& ev);
    // Clear everything (worker restart).
    void clear();

private:
    QTableWidget* table;
    // Track which block IDs are in the table so we can update rows in place.
    // Value is the row index.
    QMap<int, int> rowForId;

    void ensureRow(int id, const ArenaBlock& b);
    void markFreed(int blockId, qint64 timestampMs);

    static QString fmtAddr(long addr);
    static QString fmtSize(long size);
    static QString fmtTime(qint64 ms);
};
