#include "TimeScrubber.h"
#include "Theme.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QDateTime>
#include <algorithm>

TimeScrubber::TimeScrubber(QWidget* parent) : QWidget(parent) {
    setStyleSheet(QString("background:%1;").arg(Theme::BG_APP));

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(4);

    auto* titleLbl = new QLabel("⏮  Time Scrubber  —  replay real allocations from this session");
    titleLbl->setStyleSheet(QString(
        "color:%1; font-size:11px; font-weight:600; padding:4px 0;"
    ).arg(Theme::TEXT_SECONDARY));
    outer->addWidget(titleLbl);

    auto* row = new QHBoxLayout();
    row->setSpacing(6);

    stepBackBtn = new QPushButton("◀");
    stepBackBtn->setFixedSize(26, 22);
    stepBackBtn->setToolTip("Step back one event");

    slider = new QSlider(Qt::Horizontal);
    slider->setRange(0, 0);
    slider->setValue(0);
    slider->setToolTip("Drag to replay allocation history");

    stepFwdBtn = new QPushButton("▶");
    stepFwdBtn->setFixedSize(26, 22);
    stepFwdBtn->setToolTip("Step forward one event");

    playPauseBtn = new QPushButton("⏸ Pause");
    playPauseBtn->setFixedWidth(70);
    playPauseBtn->setToolTip("Pause/resume live updates");

    for (auto* btn : {stepBackBtn, stepFwdBtn, playPauseBtn}) {
        btn->setStyleSheet(QString(
            "QPushButton { background:%1; color:%2; border:1px solid %3; "
            "border-radius:4px; font-size:10px; }"
            "QPushButton:hover { background:%3; }"
        ).arg(Theme::BG_INPUT, Theme::TEXT_PRIMARY, Theme::BORDER));
    }

    row->addWidget(stepBackBtn);
    row->addWidget(slider, 1);
    row->addWidget(stepFwdBtn);
    row->addWidget(playPauseBtn);
    outer->addLayout(row);

    posLabel = new QLabel("Live  —  allocating will be recorded here");
    posLabel->setStyleSheet(QString("color:%1; font-size:10px; padding:2px 0;")
        .arg(Theme::TEXT_MUTED));
    outer->addWidget(posLabel);

    hintLabel = new QLabel(
        "Every address shown during replay came from a real BLOCK line "
        "the worker emitted — no synthetic data.");
    hintLabel->setWordWrap(true);
    hintLabel->setStyleSheet(QString(
        "color:%1; font-size:10px; font-style:italic;"
    ).arg(Theme::TEXT_MUTED));
    outer->addWidget(hintLabel);

    connect(slider,       &QSlider::valueChanged,   this, &TimeScrubber::onSliderMoved);
    connect(playPauseBtn, &QPushButton::clicked,     this, &TimeScrubber::onPlayPause);
    connect(stepBackBtn,  &QPushButton::clicked,     this, &TimeScrubber::onStepBack);
    connect(stepFwdBtn,   &QPushButton::clicked,     this, &TimeScrubber::onStepForward);
}

void TimeScrubber::setLog(const std::deque<AllocEvent>* log) {
    log_ = log;
    updateSliderRange();
    if (live_) {
        // Keep slider at the live end so new events scroll it automatically.
        slider->blockSignals(true);
        slider->setValue(slider->maximum());
        slider->blockSignals(false);
        int n = log_ ? (int)log_->size() : 0;
        posLabel->setText(QString("Live  ·  %1 events recorded").arg(n));
    }
}

void TimeScrubber::updateSliderRange() {
    int n = log_ ? (int)log_->size() : 0;
    slider->blockSignals(true);
    slider->setRange(0, n);
    slider->blockSignals(false);
}

// ── slot implementations ──────────────────────────────────────────────────────

void TimeScrubber::onSliderMoved(int value) {
    if (!log_) return;
    int n = (int)log_->size();
    bool wasLive = live_;
    live_ = (value >= n);

    if (live_) {
        posLabel->setText(QString("Live  ·  %1 events recorded").arg(n));
        if (!wasLive) emit resumedLive();
        // Show the most recent state
        emitSnapshot(n);
    } else {
        emitSnapshot(value);
    }
}

void TimeScrubber::onPlayPause() {
    if (live_) {
        // Pause — freeze at current position
        live_ = false;
        playPauseBtn->setText("▶ Live");
        playPauseBtn->setToolTip("Return to live mode");
        int n = log_ ? (int)log_->size() : 0;
        emitSnapshot(n > 0 ? n - 1 : 0);
    } else {
        // Resume — jump to live end
        live_ = true;
        playPauseBtn->setText("⏸ Pause");
        playPauseBtn->setToolTip("Pause live updates");
        int n = log_ ? (int)log_->size() : 0;
        slider->blockSignals(true);
        slider->setValue(n);
        slider->blockSignals(false);
        posLabel->setText(QString("Live  ·  %1 events recorded").arg(n));
        emit resumedLive();
        emitSnapshot(n);
    }
}

void TimeScrubber::onStepBack() {
    int cur = slider->value();
    if (cur > 0) {
        live_ = false;
        playPauseBtn->setText("▶ Live");
        slider->setValue(cur - 1);
    }
}

void TimeScrubber::onStepForward() {
    int cur = slider->value();
    int max = slider->maximum();
    if (cur < max) slider->setValue(cur + 1);
}

// ── replay engine ─────────────────────────────────────────────────────────────

std::vector<ArenaBlock> TimeScrubber::replayTo(int upTo) const {
    if (!log_) return {};
    std::vector<ArenaBlock> state;
    int n = std::min(upTo, (int)log_->size());

    for (int i = 0; i < n; i++) {
        const AllocEvent& ev = (*log_)[i];
        if (ev.op == AllocEvent::Op::Reset) {
            state.clear();
        } else if (ev.op == AllocEvent::Op::Alloc) {
            ArenaBlock b;
            b.id       = ev.blockId;
            b.offset   = ev.offset;
            b.size     = ev.size;
            b.used     = true;
            b.label    = ev.label;
            state.push_back(b);
        } else if (ev.op == AllocEvent::Op::Free) {
            // Remove the freed block from the live set
            state.erase(
                std::remove_if(state.begin(), state.end(),
                    [&ev](const ArenaBlock& b){ return b.id == ev.blockId; }),
                state.end()
            );
        }
    }
    return state;
}

ArenaSummary TimeScrubber::summaryFrom(const std::vector<ArenaBlock>& blocks) {
    ArenaSummary s;
    s.blockCount = (int)blocks.size();
    for (const auto& b : blocks) if (b.used) s.usedBytes += b.size;
    s.totalBytes = s.usedBytes;
    // freeBytes / largestFreeRun not tracked in replay (no arena-mode hole data)
    return s;
}

void TimeScrubber::emitSnapshot(int idx) {
    auto blocks = replayTo(idx);
    auto summary = summaryFrom(blocks);
    emit snapshotReady(blocks, summary);

    if (!live_ && log_ && idx < (int)log_->size()) {
        const AllocEvent& ev = (*log_)[idx];
        QString opName;
        switch (ev.op) {
            case AllocEvent::Op::Alloc: opName = "Alloc"; break;
            case AllocEvent::Op::Free:  opName = "Free";  break;
            case AllocEvent::Op::Reset: opName = "Reset"; break;
        }
        posLabel->setText(QString(
            "Event %1 / %2  ·  %3  ·  %4  ·  %5  ·  id #%6")
            .arg(idx + 1).arg(log_->size())
            .arg(QDateTime::fromMSecsSinceEpoch(ev.timestampMs).toString("HH:mm:ss.zzz"))
            .arg(opName)
            .arg(ev.size > 0 ? QString("%1 KB").arg(ev.size / 1024) : QString("—"))
            .arg(ev.blockId));
    }
}
