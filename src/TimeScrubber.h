#pragma once
#include <QWidget>
#include <QSlider>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <deque>
#include <vector>
#include "MemoryLabDriver.h"

// Time Scrubber — replays the AllocEvent log to reconstruct the allocator
// state at any past moment in the session.
//
// The scrubber does NOT call any worker commands. It reconstructs a synthetic
// snapshot purely from the event log that was already accumulated while the
// student was allocating/freeing. Every address and size shown here came from
// a real BLOCK line that the worker emitted — no synthetic data.
//
// Usage:
//   1. Feed it the driver's eventLog() via setLog() whenever events are added.
//   2. Connect its snapshotReady() signal to ArenaView::setBlocks() and
//      MemoryTableWidget::onArenaUpdated() to show the replay.
//   3. While scrubbing the scrubber takes ownership of rendering; resume()
//      re-arms live updates.
class TimeScrubber : public QWidget {
    Q_OBJECT
public:
    explicit TimeScrubber(QWidget* parent = nullptr);

    // Update the log reference — call this when new events arrive.
    void setLog(const std::deque<AllocEvent>* log);

    // Returns true while the scrubber is "live" (slider at the right end).
    bool isLive() const { return live_; }

signals:
    // Emitted when the scrubber position changes.
    // blocks = reconstructed block state at the chosen timestamp.
    // summary.usedBytes is derived; arenaMode is preserved from original events.
    void snapshotReady(std::vector<ArenaBlock> blocks, ArenaSummary summary);

    // Emitted when the user returns to the live end (scrubbing paused).
    void resumedLive();

private slots:
    void onSliderMoved(int value);
    void onPlayPause();
    void onStepBack();
    void onStepForward();

private:
    const std::deque<AllocEvent>* log_ = nullptr;
    bool live_ = true;

    QSlider*     slider;
    QLabel*      posLabel;    // "Event 42 / 87  ·  HH:mm:ss.zzz"
    QPushButton* playPauseBtn;
    QPushButton* stepBackBtn;
    QPushButton* stepFwdBtn;
    QLabel*      hintLabel;

    // Reconstruct the block state at log index `upTo` (exclusive).
    // Returns the block list as it was after event[upTo-1].
    std::vector<ArenaBlock> replayTo(int upTo) const;

    // Convert the block list to an ArenaSummary.
    static ArenaSummary summaryFrom(const std::vector<ArenaBlock>& blocks);

    void emitSnapshot(int idx);
    void updateSliderRange();
};
