#pragma once
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QSpinBox>
#include <QPainter>
#include <QMouseEvent>
#include <QTimer>
#include <vector>
#include "MemoryLabDriver.h"
#include "MemoryInspector.h"

// Custom-painted strip showing the arena: a horizontal bar split into
// blocks, used blocks colored by a hash of their id, free space light gray.
// Clicking a used block frees it.
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

    // Call once when the tab becomes visible for the first time.
    void ensureStarted();
    // Show the memory map of an arbitrary PID (e.g. a sandbox process).
    void showMemMapForPid(pid_t pid);

signals:
    void explanationNeeded(QString text);

private slots:
    void onAllocateClicked();
    void onArenaUpdated(std::vector<ArenaBlock> blocks, ArenaSummary summary);
    void onCommandFailed(QString reason);
    void onWorkerDied();
    void onWorkerReady(long capacityBytes);
    void onWorkerPidKnown(pid_t pid);
    void onBlockClicked(int id);
    void onResetClicked();
    void onRestartClicked();
    void onMapRefresh();
    void onMprotectClicked();
    void onMadviseClicked();
    void onCowForkClicked();

private:
    MemoryLabDriver* driver;
    ArenaView*    arenaView;
    MemMapWidget* mapView;
    QTimer*       mapTimer;
    pid_t         displayedSandboxPid = -1; // -1 = show worker map
    QSpinBox*     sizeSpin;
    QComboBox*    strategyBox;
    QComboBox*    mprotectBox;   // RO / RW / NONE
    QComboBox*    madviseBox;    // DONTNEED / WILLNEED
    QLabel*       statsLabel;
    QLabel*       statusLabel;
    QLabel*       smapsLabel;    // shows smaps diff after mprotect/madvise/COW
    long          capacityBytes = 0;

    void updateStatsLabel(const ArenaSummary& s);
    void refreshSmapsDiff();     // reads /proc/pid/smaps_rollup and shows delta
};
