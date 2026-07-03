#include "MemoryLab.h"
#include "Theme.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainterPath>
#include <cmath>

// ───────────────────────── ArenaView ─────────────────────────

ArenaView::ArenaView(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(90);
    setStyleSheet(QString("background: white; border-radius: 12px; border: 1px solid %1;")
                  .arg(Theme::BORDER));
}

void ArenaView::setBlocks(const std::vector<ArenaBlock>& b, long total) {
    blocks = b;
    totalBytes = total > 0 ? total : 1;
    update();
}

QColor ArenaView::colorForId(int id) const {
    // Stable, distinct-ish color per allocation id so students can visually
    // track "their" block across the strip.
    static const char* palette[] = {
        "#4F6EF7", "#22C55E", "#F97316", "#A855F7",
        "#14B8A6", "#EAB308", "#EC4899", "#0EA5E9"
    };
    return QColor(palette[id % 8]);
}

void ArenaView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    int pad = 10;
    QRect strip(pad, pad, width() - pad * 2, height() - pad * 2);

    double pxPerByte = (double)strip.width() / (double)totalBytes;
    int x = strip.x();

    for (auto& blk : blocks) {
        int w = std::max(1, (int)std::round(blk.size * pxPerByte));
        QRect r(x, strip.y(), w, strip.height());

        QColor fill = blk.used ? colorForId(blk.id) : QColor(Theme::BG_INPUT);
        QPainterPath path;
        path.addRoundedRect(r.adjusted(1, 0, -1, 0), 4, 4);
        p.fillPath(path, fill);

        if (blk.used && w > 24) {
            p.setPen(Qt::white);
            p.setFont(QFont("Segoe UI", 7, QFont::Bold));
            p.drawText(r, Qt::AlignCenter, QString("#%1").arg(blk.id));
        }

        x += w;
    }

    if (blocks.empty()) {
        p.setPen(QColor(Theme::TEXT_MUTED));
        p.drawText(strip, Qt::AlignCenter, "Waiting for worker…");
    }
}

void ArenaView::mousePressEvent(QMouseEvent* event) {
    int pad = 10;
    QRect strip(pad, pad, width() - pad * 2, height() - pad * 2);
    if (!strip.contains(event->pos())) return;

    double pxPerByte = (double)strip.width() / (double)totalBytes;
    int x = strip.x();
    for (auto& blk : blocks) {
        int w = std::max(1, (int)std::round(blk.size * pxPerByte));
        QRect r(x, strip.y(), w, strip.height());
        if (r.contains(event->pos()) && blk.used) {
            emit blockClicked(blk.id);
            return;
        }
        x += w;
    }
}

// ───────────────────────── MemoryLab ─────────────────────────

MemoryLab::MemoryLab(QWidget* parent) : QWidget(parent) {
    setStyleSheet(QString("background: %1;").arg(Theme::BG_APP));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);

    auto* title = new QLabel("🧱  Memory Lab — Real Allocation Arena");
    title->setStyleSheet(QString("color: %1; font-size: 14px; font-weight: bold;")
                          .arg(Theme::TEXT_PRIMARY));
    layout->addWidget(title);

    auto* warn = new QLabel(
        "This arena is <b>real mmap'd memory</b> with genuine pointer arithmetic — "
        "not a drawing. It runs in its own process, capped at a fixed size, so nothing "
        "you do here can affect the rest of your system. If it ever misbehaves, hit "
        "<b>Restart Worker</b>.");
    warn->setWordWrap(true);
    warn->setStyleSheet(QString(
        "background: %1; color: %2; border: 1px solid #FED7AA; border-radius: 10px; "
        "padding: 8px 12px; font-size: 11px;"
    ).arg(Theme::ORANGE_LIGHT, Theme::TEXT_PRIMARY));
    layout->addWidget(warn);

    arenaView = new ArenaView();
    layout->addWidget(arenaView);

    // Controls card
    auto* controlsCard = new QWidget();
    controlsCard->setStyleSheet(Theme::card());
    auto* controlsLayout = new QHBoxLayout(controlsCard);
    controlsLayout->setContentsMargins(14, 12, 14, 12);
    controlsLayout->setSpacing(10);

    sizeSpin = new QSpinBox();
    sizeSpin->setRange(1, 4 * 1024 * 1024);
    sizeSpin->setValue(64 * 1024);
    sizeSpin->setSuffix(" bytes");
    sizeSpin->setStyleSheet(Theme::input());

    strategyBox = new QComboBox();
    strategyBox->addItem("First Fit", "first");
    strategyBox->addItem("Best Fit", "best");
    strategyBox->addItem("Worst Fit", "worst");
    strategyBox->setStyleSheet(Theme::input());

    auto* allocBtn = new QPushButton("➕  Allocate");
    allocBtn->setStyleSheet(Theme::btnPrimary());

    auto* resetBtn = new QPushButton("↺  Reset Arena");
    resetBtn->setStyleSheet(Theme::btnGhost());

    auto* restartBtn = new QPushButton("⛔  Restart Worker");
    restartBtn->setStyleSheet(Theme::btnDanger());

    controlsLayout->addWidget(new QLabel("Size:"));
    controlsLayout->addWidget(sizeSpin);
    controlsLayout->addWidget(new QLabel("Strategy:"));
    controlsLayout->addWidget(strategyBox);
    controlsLayout->addWidget(allocBtn);
    controlsLayout->addStretch();
    controlsLayout->addWidget(resetBtn);
    controlsLayout->addWidget(restartBtn);

    layout->addWidget(controlsCard);

    auto* hint = new QLabel("Click any colored block in the arena above to free it.");
    hint->setStyleSheet(QString("color: %1; font-size: 11px;").arg(Theme::TEXT_MUTED));
    layout->addWidget(hint);

    statsLabel = new QLabel();
    statsLabel->setStyleSheet(QString("color: %1; font-size: 12px;").arg(Theme::TEXT_SECONDARY));
    layout->addWidget(statsLabel);

    statusLabel = new QLabel("Starting worker…");
    statusLabel->setStyleSheet(QString("color: %1; font-size: 11px;").arg(Theme::TEXT_MUTED));
    layout->addWidget(statusLabel);

    layout->addStretch();

    driver = new MemoryLabDriver(this);
    connect(driver, &MemoryLabDriver::arenaUpdated, this, &MemoryLab::onArenaUpdated);
    connect(driver, &MemoryLabDriver::commandFailed, this, &MemoryLab::onCommandFailed);
    connect(driver, &MemoryLabDriver::workerDied, this, &MemoryLab::onWorkerDied);
    connect(driver, &MemoryLabDriver::workerReady, this, &MemoryLab::onWorkerReady);

    connect(allocBtn, &QPushButton::clicked, this, &MemoryLab::onAllocateClicked);
    connect(resetBtn, &QPushButton::clicked, this, &MemoryLab::onResetClicked);
    connect(restartBtn, &QPushButton::clicked, this, &MemoryLab::onRestartClicked);
    connect(arenaView, &ArenaView::blockClicked, this, &MemoryLab::onBlockClicked);

    driver->start();
}

MemoryLab::~MemoryLab() {
    driver->stop();
}

void MemoryLab::onAllocateClicked() {
    long size = sizeSpin->value();
    QString strategy = strategyBox->currentData().toString();
    driver->alloc(size, strategy);
}

void MemoryLab::onBlockClicked(int id) {
    driver->freeBlock(id);
}

void MemoryLab::onResetClicked() {
    driver->resetArena();
    emit explanationNeeded(
        "<b>Arena Reset</b><br><br>"
        "All allocations were cleared and the arena returned to one large free "
        "block. This is the safe undo button — use it whenever fragmentation "
        "gets confusing or an experiment goes somewhere you didn't expect.");
}

void MemoryLab::onRestartClicked() {
    statusLabel->setText("Restarting worker…");
    driver->stop();
    driver->start();
}

void MemoryLab::onWorkerReady(long cap) {
    capacityBytes = cap;
    statusLabel->setText(QString("Worker ready — arena capacity %1 KB").arg(cap / 1024));
}

void MemoryLab::onWorkerDied() {
    statusLabel->setText("⚠ Worker process exited. Click Restart Worker to bring it back.");
}

void MemoryLab::onCommandFailed(QString reason) {
    statusLabel->setText("⚠ " + reason);
}

void MemoryLab::onArenaUpdated(std::vector<ArenaBlock> blocks, ArenaSummary summary) {
    arenaView->setBlocks(blocks, summary.totalBytes);
    updateStatsLabel(summary);
    statusLabel->setText("OK");
}

void MemoryLab::updateStatsLabel(const ArenaSummary& s) {
    double fragPct = s.freeBytes > 0
        ? 100.0 * (1.0 - (double)s.largestFreeRun / (double)s.freeBytes)
        : 0.0;
    statsLabel->setText(QString(
        "Total: %1 KB   •   Used: %2 KB   •   Free: %3 KB   •   "
        "Blocks: %4   •   External fragmentation: %5%"
    ).arg(s.totalBytes / 1024).arg(s.usedBytes / 1024).arg(s.freeBytes / 1024)
     .arg(s.blockCount).arg(QString::number(fragPct, 'f', 1)));
}
