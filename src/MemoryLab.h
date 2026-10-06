#pragma once
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QSpinBox>
#include <QScrollArea>
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QTimer>
#include <QTabWidget>
#include <QTableWidget>
#include <QVector>
#include <vector>
#include <functional>
#include "MemoryLabDriver.h"
#include "MemoryTableWidget.h"
#include "TimeScrubber.h"

// ── MemoryCanvas ─────────────────────────────────────────────────────────────
// The centrepiece: a 2D grid where every pixel represents a byte range in the
// 4 MB sandbox. Blocks are painted as coloured rectangles at their exact
// byte offset. Supports zoom (wheel / buttons) and click to inspect.
class MemoryCanvas : public QWidget {
    Q_OBJECT
public:
    explicit MemoryCanvas(QWidget* parent = nullptr);

    void setBlocks(const std::vector<ArenaBlock>& blocks, long sandboxSize);
    void setStructNodes(int structId, const std::vector<StructNode>& nodes,
                        const QString& type);
    void clearStructOverlays();
    void setSelectedId(int id);

    void zoomIn();
    void zoomOut();
    void resetZoom();

signals:
    void blockClicked(int blockId);
    void emptyClicked();  // clicked on free space

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void resizeEvent(QResizeEvent*) override;

private:
    std::vector<ArenaBlock> blocks;
    long sandboxSize = 4 * 1024 * 1024;
    int  selectedId  = -1;

    // Struct overlays: connector lines drawn on top of blocks
    struct StructOverlay {
        int     structId;
        QString type;
        std::vector<StructNode> nodes;
    };
    std::vector<StructOverlay> overlays;

    // Zoom / pan
    double zoom      = 1.0;
    int    panX      = 0;
    int    panY      = 0;
    bool   panning   = false;
    QPoint panStart;
    int    panXAtStart = 0;
    int    panYAtStart = 0;

    // Layout: how many "cells" per row at zoom==1
    int cellsPerRow() const;
    // Convert byte offset → canvas rect
    QRectF offsetToRect(long offset, long size) const;
    // Reverse: canvas point → byte offset (-1 if out of range)
    long pointToOffset(QPoint p) const;

    QColor colorForBlock(const ArenaBlock& b) const;
    void   clampPan();
    void   drawArrow(QPainter& p, QPointF from, QPointF to, QColor color);
};

// ── TablePanel ───────────────────────────────────────────────────────────────
// Shows the page table, segment table, or frame table depending on mode.
class TablePanel : public QWidget {
    Q_OBJECT
public:
    explicit TablePanel(QWidget* parent = nullptr);
    void showPageTable(const std::vector<PageEntry>& entries);
    void showSegTable(const std::vector<SegEntry>& entries);
    void showFrameTable(const std::vector<FrameEntry>& entries);
    void clear();
private:
    QLabel*       titleLabel;
    QTableWidget* table;
};

// ── ChallengePanel ────────────────────────────────────────────────────────────
// Displays a small guided challenge card that tells the student what to do next.
class ChallengePanel : public QWidget {
    Q_OBJECT
public:
    explicit ChallengePanel(QWidget* parent = nullptr);

    // Feed live arena stats so the panel can detect completed challenges.
    void onArenaUpdated(const ArenaSummary& s, int blockCount, bool hasContiguous,
                        bool hasPaged, bool hasSegmented);

signals:
    void challengeExplanationNeeded(QString html);  // forward to Explainer

private:
    QLabel* titleLabel;
    QLabel* descLabel;
    QLabel* hintLabel;
    QLabel* progressLabel;

    struct Challenge {
        QString title;
        QString desc;
        QString hint;
        QString successExplanation;
        std::function<bool(const ArenaSummary&, int, bool, bool, bool)> check;
    };
    QVector<Challenge> challenges;
    int  currentIdx     = 0;
    bool lastCompleted  = false;

    void advance();
    void refreshDisplay();
};

// ── FragBar ───────────────────────────────────────────────────────────────────
// A compact visual bar showing used / free / waste proportions.
class FragBar : public QWidget {
    Q_OBJECT
public:
    explicit FragBar(QWidget* parent = nullptr);
    void update(const ArenaSummary& s);
protected:
    void paintEvent(QPaintEvent*) override;
private:
    ArenaSummary summary;
};

// ── MemoryLab ─────────────────────────────────────────────────────────────────
class MemoryLab : public QWidget {
    Q_OBJECT
public:
    explicit MemoryLab(QWidget* parent = nullptr);
    ~MemoryLab();

    void ensureStarted();
    void showMemMapForPid(pid_t) {} // no-op in new design

signals:
    void explanationNeeded(QString text);

private slots:
    void onWorkerReady(long bytes);
    void onWorkerPidKnown(pid_t pid);
    void onWorkerDied();
    void onArenaUpdated(std::vector<ArenaBlock> blocks, ArenaSummary summary);
    void onStructDefined(ArenaBlock block, std::vector<StructNode> nodes);
    void onCommandFailed(QString reason);
    void onPageTableReady(std::vector<PageEntry> entries);
    void onSegTableReady(std::vector<SegEntry> entries);
    void onFrameTableReady(std::vector<FrameEntry> entries);

    void onAllocRaw();
    void onAllocLL();
    void onAllocArray();
    void onAllocTree();
    void onAllocHash();
    void onFreeSelected();
    void onReset();
    void onModeChanged(int index);
    void onBlockClicked(int id);
    void onRefreshTable();

private:
    MemoryLabDriver* driver;

    // Canvas
    MemoryCanvas* canvas;
    QScrollArea*  canvasScroll;

    // Controls
    QComboBox*    modeBox;
    QSpinBox*     rawSizeSpin;
    QSpinBox*     structNSpin;
    QSpinBox*     structESizeSpin;  // element size for array
    QPushButton*  freeBtn;
    QLabel*       statsLabel;
    QLabel*       statusLabel;
    QLabel*       inspectLabel;    // shows details of clicked block

    // Fragmentation bar + challenge panel
    FragBar*            fragBar;
    ChallengePanel*     challengePanel;

    // Table panel (right side, page/seg/frame)
    TablePanel*         tablePanel;

    // Allocation history table + time scrubber (below canvas)
    MemoryTableWidget*  memTable;
    TimeScrubber*       timeScrubber;

    int           selectedId  = -1;
    long          sandboxSize = 4 * 1024 * 1024;
    QString       currentMode = "contiguous";

    // Track which modes have been visited (for challenge detection)
    bool          visitedPaged     = false;
    bool          visitedSegmented = false;

    void updateInspect(const ArenaBlock* b, const ArenaSummary* s = nullptr);
    void refreshTableForMode();
    void emitExplanation(const QString& mode);
};
