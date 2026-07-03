#pragma once
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QSpinBox>
#include <QPainter>
#include <QMouseEvent>
#include <vector>
#include "MemoryLabDriver.h"

// Custom-painted strip showing the real arena: a horizontal bar split into
// blocks, used blocks colored by a hash of their id, free space light gray.
// Clicking a used block frees it (with a confirm-by-color flash, no popup —
// keeps the "experiment freely" feel while the Reset button is the real
// safety net for anything destructive).
class ArenaView : public QWidget {
    Q_OBJECT
public:
    explicit ArenaView(QWidget* parent = nullptr);
    void setBlocks(const std::vector<ArenaBlock>& blocks, long totalBytes);

signals:
    void blockClicked(int id);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;

private:
    std::vector<ArenaBlock> blocks;
    long totalBytes = 1;
    QColor colorForId(int id) const;
};

class MemoryLab : public QWidget {
    Q_OBJECT
public:
    explicit MemoryLab(QWidget* parent = nullptr);
    ~MemoryLab();

signals:
    void explanationNeeded(QString text);

private slots:
    void onAllocateClicked();
    void onArenaUpdated(std::vector<ArenaBlock> blocks, ArenaSummary summary);
    void onCommandFailed(QString reason);
    void onWorkerDied();
    void onWorkerReady(long capacityBytes);
    void onBlockClicked(int id);
    void onResetClicked();
    void onRestartClicked();

private:
    MemoryLabDriver* driver;
    ArenaView*  arenaView;
    QSpinBox*   sizeSpin;
    QComboBox*  strategyBox;
    QLabel*     statsLabel;
    QLabel*     statusLabel;
    long        capacityBytes = 0;

    void updateStatsLabel(const ArenaSummary& s);
};
