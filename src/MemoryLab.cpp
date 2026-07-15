#include "MemoryLab.h"
#include "EventBus.h"
#include "Theme.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainterPath>
#include <QFile>
#include <cmath>
#include <fstream>
#include <sstream>

// ───────────────────────── ArenaView ─────────────────────────

ArenaView::ArenaView(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(90);
    setStyleSheet(QString("background: white; border-radius: 12px; border: 1px solid %1;")
                  .arg(Theme::BORDER));
}

void ArenaView::setBlocks(const std::vector<ArenaBlock>& b, long total) {
    blocks = b;
    totalBytes = total > 0 ? total : 1;
    // Keep selectedId valid — clear it if the block no longer exists
    bool found = false;
    for (auto& blk : blocks) if (blk.used && blk.id == selectedId) { found = true; break; }
    if (!found) selectedId = -1;
    update();
}

void ArenaView::setSelected(int id) {
    selectedId = id;
    update();
}

QColor ArenaView::colorForId(int id) const {
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

    // With per-mmap allocations the "total" is just the sum of live blocks,
    // so scale each block by its fraction of the total used space.
    double pxPerByte = (double)strip.width() / (double)totalBytes;
    int x = strip.x();

    for (auto& blk : blocks) {
        int w = std::max(4, (int)std::round(blk.size * pxPerByte));
        QRect r(x, strip.y(), w, strip.height());

        QColor fill = blk.used ? colorForId(blk.id) : QColor(Theme::BG_INPUT);
        QPainterPath path;
        path.addRoundedRect(r.adjusted(1, 0, -1, 0), 4, 4);
        p.fillPath(path, fill);

        // Draw selection highlight ring around the selected block
        if (blk.used && blk.id == selectedId) {
            p.setPen(QPen(Qt::white, 2.5));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r.adjusted(2, 2, -2, -2), 4, 4);
            p.setPen(QPen(QColor("#1e3a8a"), 1.5));
            p.drawRoundedRect(r.adjusted(1, 1, -1, -1), 4, 4);
        }

        if (blk.used && w > 24) {
            p.setPen(Qt::white);
            p.setFont(QFont("Segoe UI", 7, QFont::Bold));
            p.drawText(r, Qt::AlignCenter, QString("#%1").arg(blk.id));
        }

        x += w;
        if (x >= strip.right()) break;
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
        int w = std::max(4, (int)std::round(blk.size * pxPerByte));
        QRect r(x, strip.y(), w, strip.height());
        if (r.contains(event->pos()) && blk.used) {
            selectedId = (selectedId == blk.id) ? -1 : blk.id; // toggle
            update();
            emit blockSelected(selectedId);
            return;
        }
        x += w;
        if (x >= strip.right()) break;
    }
    // Click on empty space — deselect
    selectedId = -1;
    update();
    emit blockSelected(-1);
}

// ───────────────────────── MemoryLab ─────────────────────────

MemoryLab::MemoryLab(QWidget* parent) : QWidget(parent) {
    setStyleSheet(QString("background: %1;").arg(Theme::BG_APP));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);

    auto* title = new QLabel("🧱  Memory Lab — Real Allocations + Live Memory Map");
    title->setStyleSheet(QString("color: %1; font-size: 14px; font-weight: bold;")
                          .arg(Theme::TEXT_PRIMARY));
    layout->addWidget(title);

    auto* warn = new QLabel(
        "Each <b>Allocate</b> call issues a real <code>mmap(MAP_ANONYMOUS)</code> "
        "in the worker process — each block is a genuine kernel-visible region. "
        "Click a block to <b>select</b> it (highlighted border), then press "
        "<b>Free Selected</b> to call <code>munmap()</code> — the region vanishes from "
        "the map live.");
    warn->setWordWrap(true);
    warn->setStyleSheet(QString(
        "background: %1; color: %2; border: 1px solid #FED7AA; border-radius: 10px; "
        "padding: 8px 12px; font-size: 11px;"
    ).arg(Theme::ORANGE_LIGHT, Theme::TEXT_PRIMARY));
    layout->addWidget(warn);

    // Arena strip — labeled header
    auto* arenaCard = new QWidget();
    arenaCard->setStyleSheet(Theme::card());
    auto* arenaCardLayout = new QVBoxLayout(arenaCard);
    arenaCardLayout->setContentsMargins(12, 10, 12, 10);
    arenaCardLayout->setSpacing(6);
    auto* arenaTitle = new QLabel(
        "Your Allocations  —  blocks you created with mmap()  "
        "(click to select · does not include stack, libs, or other system regions)");
    arenaTitle->setStyleSheet(QString("color:%1; font-size:11px; font-weight:600;")
        .arg(Theme::TEXT_SECONDARY));
    arenaCardLayout->addWidget(arenaTitle);
    arenaView = new ArenaView();
    arenaCardLayout->addWidget(arenaView);
    layout->addWidget(arenaCard);

    // Controls card
    auto* controlsCard = new QWidget();
    controlsCard->setStyleSheet(Theme::card());
    auto* controlsLayout = new QHBoxLayout(controlsCard);
    controlsLayout->setContentsMargins(14, 12, 14, 12);
    controlsLayout->setSpacing(10);

    sizeSpin = new QSpinBox();
    sizeSpin->setRange(4096, 4 * 1024 * 1024);
    sizeSpin->setValue(64 * 1024);
    sizeSpin->setSingleStep(4096);
    sizeSpin->setSuffix(" bytes");
    sizeSpin->setStyleSheet(Theme::input());

    auto* allocBtn = new QPushButton("➕  Allocate");
    allocBtn->setStyleSheet(Theme::btnPrimary());

    freeBtn = new QPushButton("🗑  Free Selected");
    freeBtn->setStyleSheet(Theme::btnGhost());
    freeBtn->setEnabled(false);
    freeBtn->setToolTip("Click a block in the strip above to select it, then press here to munmap() it");

    auto* resetBtn = new QPushButton("↺  Reset All");
    resetBtn->setStyleSheet(Theme::btnGhost());

    auto* restartBtn = new QPushButton("⛔  Restart Worker");
    restartBtn->setStyleSheet(Theme::btnDanger());

    controlsLayout->addWidget(new QLabel("Size:"));
    controlsLayout->addWidget(sizeSpin);
    controlsLayout->addWidget(allocBtn);
    controlsLayout->addWidget(freeBtn);
    controlsLayout->addStretch();
    controlsLayout->addWidget(resetBtn);
    controlsLayout->addWidget(restartBtn);

    layout->addWidget(controlsCard);

    // ── Advanced ops card: mprotect / madvise / COW fork ─────────────────
    auto* advCard = new QWidget();
    advCard->setStyleSheet(Theme::card());
    auto* advLayout = new QHBoxLayout(advCard);
    advLayout->setContentsMargins(14, 10, 14, 10);
    advLayout->setSpacing(10);

    auto* advTitle = new QLabel("Advanced syscall demos:");
    advTitle->setStyleSheet(QString("color:%1; font-size:11px; font-weight:600;")
        .arg(Theme::TEXT_SECONDARY));
    advLayout->addWidget(advTitle);

    mprotectBox = new QComboBox();
    mprotectBox->addItem("mprotect → RO (read-only, write = SIGSEGV)", "RO");
    mprotectBox->addItem("mprotect → RW (restore read-write)",          "RW");
    mprotectBox->addItem("mprotect → NONE (no access at all)",          "NONE");
    mprotectBox->setStyleSheet(Theme::input());
    mprotectBox->setToolTip("Applied to the block you selected in the strip above");

    auto* mprotBtn = new QPushButton("🔒  mprotect");
    mprotBtn->setStyleSheet(Theme::btnGhost());
    mprotBtn->setToolTip("Select a block in the strip above, then click here to change its protection");

    madviseBox = new QComboBox();
    madviseBox->addItem("madvise MADV_DONTNEED (discard pages, free physical RAM)", "DONTNEED");
    madviseBox->addItem("madvise MADV_WILLNEED (prefetch pages)",                   "WILLNEED");
    madviseBox->setStyleSheet(Theme::input());
    madviseBox->setToolTip("Applied to the block you selected in the strip above");

    auto* madvBtn = new QPushButton("💡  madvise");
    madvBtn->setStyleSheet(Theme::btnGhost());
    madvBtn->setToolTip("Select a block in the strip above, then click here to send the advice to the kernel");

    auto* cowBtn = new QPushButton("🍴  fork+COW demo");
    cowBtn->setStyleSheet(Theme::btnGhost());
    cowBtn->setToolTip("Forks a child that writes to the same pages — triggers COW divergence");

    advLayout->addWidget(mprotectBox);
    advLayout->addWidget(mprotBtn);
    advLayout->addSpacing(8);
    advLayout->addWidget(madviseBox);
    advLayout->addWidget(madvBtn);
    advLayout->addSpacing(8);
    advLayout->addWidget(cowBtn);
    layout->addWidget(advCard);

    statsLabel = new QLabel();
    statsLabel->setStyleSheet(QString("color: %1; font-size: 12px;").arg(Theme::TEXT_SECONDARY));
    layout->addWidget(statsLabel);

    smapsLabel = new QLabel();
    smapsLabel->setWordWrap(true);
    smapsLabel->setStyleSheet(QString(
        "background:%1; color:%2; border:1px solid %3; border-radius:8px; padding:6px 10px; font-size:11px;"
    ).arg(Theme::BG_INPUT).arg(Theme::TEXT_PRIMARY).arg(Theme::BORDER));
    smapsLabel->hide();
    layout->addWidget(smapsLabel);

    connect(mprotBtn, &QPushButton::clicked, this, &MemoryLab::onMprotectClicked);
    connect(madvBtn,  &QPushButton::clicked, this, &MemoryLab::onMadviseClicked);
    connect(cowBtn,   &QPushButton::clicked, this, &MemoryLab::onCowForkClicked);
    connect(freeBtn,  &QPushButton::clicked, this, &MemoryLab::onFreeSelectedClicked);

    // Live memory map card — shows the worker's /proc/pid/maps
    auto* mapCard = new QWidget();
    mapCard->setStyleSheet(Theme::card());
    auto* mapCardLayout = new QVBoxLayout(mapCard);
    mapCardLayout->setContentsMargins(12, 10, 12, 10);
    mapCardLayout->setSpacing(6);

    auto* mapHeader = new QHBoxLayout();
    auto* mapTitle = new QLabel("Full Process Memory Map  (/proc/pid/maps)");
    mapTitle->setStyleSheet(QString("color:%1; font-size:12px; font-weight:bold;")
        .arg(Theme::TEXT_PRIMARY));
    auto* mapHint = new QLabel(
        "Includes your mmap blocks plus stack, shared libs, vdso — "
        "everything the kernel maps into the worker process");
    mapHint->setWordWrap(true);
    mapHint->setStyleSheet(QString("color:%1; font-size:10px;").arg(Theme::TEXT_MUTED));
    mapHeader->addWidget(mapTitle);
    mapHeader->addStretch();
    mapHeader->addWidget(mapHint);
    mapCardLayout->addLayout(mapHeader);

    mapView = new MemMapWidget();
    mapView->setMinimumHeight(130);
    mapCardLayout->addWidget(mapView);
    layout->addWidget(mapCard, 1);

    statusLabel = new QLabel("Starting worker…");
    statusLabel->setStyleSheet(QString("color: %1; font-size: 11px;").arg(Theme::TEXT_MUTED));
    layout->addWidget(statusLabel);

    // Map refresh timer — polls the worker's /proc/pid/maps every 1s
    mapTimer = new QTimer(this);
    connect(mapTimer, &QTimer::timeout, this, &MemoryLab::onMapRefresh);

    driver = new MemoryLabDriver(this);
    connect(driver, &MemoryLabDriver::arenaUpdated,   this, &MemoryLab::onArenaUpdated);
    connect(driver, &MemoryLabDriver::commandFailed,  this, &MemoryLab::onCommandFailed);
    connect(driver, &MemoryLabDriver::workerDied,     this, &MemoryLab::onWorkerDied);
    connect(driver, &MemoryLabDriver::workerReady,    this, &MemoryLab::onWorkerReady);
    connect(driver, &MemoryLabDriver::workerPidKnown, this, &MemoryLab::onWorkerPidKnown);

    connect(allocBtn,   &QPushButton::clicked, this, &MemoryLab::onAllocateClicked);
    connect(resetBtn,   &QPushButton::clicked, this, &MemoryLab::onResetClicked);
    connect(restartBtn, &QPushButton::clicked, this, &MemoryLab::onRestartClicked);
    connect(arenaView,  &ArenaView::blockSelected, this, &MemoryLab::onBlockSelected);

    // Worker is NOT started here — only when the Mem Lab tab is first shown.
    // See ensureStarted() called from MainWindow::connectSignals().
}

void MemoryLab::ensureStarted() {
    if (!driver->isRunning()) {
        statusLabel->setText("Starting worker…");
        driver->start();
    }
}

void MemoryLab::showMemMapForPid(pid_t pid) {
    // Show the memory map of an external PID (sandbox process) directly.
    // Pin displayedSandboxPid so the periodic worker-map refresh doesn't overwrite it.
    if (pid <= 0) return;
    auto regions = MemoryInspector::readMemMap(pid);
    if (regions.empty()) {
        statusLabel->setText(QString("No memory map available for PID %1 (process may have ended)").arg(pid));
        return;
    }
    displayedSandboxPid = pid;          // pin: suppress worker-map refresh
    long totalKB = 0;
    for (auto& r : regions) totalKB += r.sizeKB;
    mapView->setRegions(regions, totalKB);
    statusLabel->setText(QString("Showing memory map of sandbox PID %1 — %2 regions, %3 KB total  [click Refresh to return to worker map]")
        .arg(pid).arg(regions.size()).arg(totalKB));

    emit explanationNeeded(QString(
        "<b>Memory Map — Sandbox PID %1</b><br><br>"
        "Showing <b>%2</b> virtual memory regions from <code>/proc/%1/maps</code>.<br>"
        "Total mapped: <b>%3 KB</b><br><br>"
        "<b>Regions:</b><br>"
        "• <span style='color:#4F6EF7;'>■</span> Anonymous (heap / mmap)<br>"
        "• <span style='color:#22C55E;'>■</span> Stack<br>"
        "• <span style='color:#F97316;'>■</span> Shared libs (.so)<br>"
        "• <span style='color:#A855F7;'>■</span> [vdso] / special<br><br>"
        "This is the real kernel view of the process's virtual address space. "
        "Click a region in the strip to see its permissions and label.<br><br>"
        "Switch to the <b>Mem Lab</b> worker to experiment with <code>mmap()</code>, "
        "<code>mprotect()</code> and <code>madvise()</code> on dedicated test blocks."
    ).arg(pid).arg(regions.size()).arg(totalKB));
}

MemoryLab::~MemoryLab() {
    mapTimer->stop();
    driver->stop();
}

void MemoryLab::onAllocateClicked() {
    displayedSandboxPid = -1;   // user is interacting with worker — unpin sandbox view
    long size = sizeSpin->value();
    driver->alloc(size, "first");
    EventBus::get().memoryAllocated(driver->workerPid(), size,
        QString("MemLab mmap"));
}

void MemoryLab::onBlockSelected(int id) {
    selectedBlockId = id;
    freeBtn->setEnabled(id != -1);
    if (id != -1)
        statusLabel->setText(QString("Block #%1 selected — press Free Selected to munmap() it, "
                                     "or use mprotect/madvise to experiment with it").arg(id));
    else
        statusLabel->setText(QString("Worker PID %1 — map live below").arg(driver->workerPid()));
}

void MemoryLab::onFreeSelectedClicked() {
    if (selectedBlockId == -1) return;
    displayedSandboxPid = -1;   // unpin sandbox view
    int id = selectedBlockId;
    selectedBlockId = -1;
    freeBtn->setEnabled(false);
    arenaView->setSelected(-1);
    EventBus::get().memoryFreed(driver->workerPid(), 0);
    driver->freeBlock(id);
}

void MemoryLab::onResetClicked() {
    // All blocks are about to be destroyed — clear selection before the
    // arena update arrives so the Free/mprotect/madvise buttons can't
    // reference a block ID that no longer exists.
    selectedBlockId = -1;
    freeBtn->setEnabled(false);
    arenaView->setSelected(-1);
    driver->resetArena();
    emit explanationNeeded(
        "<b>Arena Reset</b><br><br>"
        "All allocations were cleared — each block was <code>munmap()</code>'d "
        "and the regions vanished from the map. Use this as a clean-slate whenever "
        "an experiment gets confusing.");
}

void MemoryLab::onRestartClicked() {
    displayedSandboxPid = -1;   // return to worker map view
    selectedBlockId = -1;
    freeBtn->setEnabled(false);
    arenaView->setSelected(-1);
    mapTimer->stop();
    statusLabel->setText("Restarting worker…");
    driver->stop();
    driver->start();
}

void MemoryLab::onWorkerReady(long cap) {
    capacityBytes = cap;
    statusLabel->setText(QString("Worker ready — up to %1 allocations of up to 4 MB each").arg(cap / (4*1024*1024)));
}

void MemoryLab::onWorkerPidKnown(pid_t pid) {
    statusLabel->setText(QString("Worker PID %1 — map live below").arg(pid));
    mapTimer->start(1000); // refresh every second
    onMapRefresh();        // immediate first read

    emit explanationNeeded(QString(
        "<b>Memory Lab — Real mmap() Allocations</b><br><br>"
        "Worker PID: <b>%1</b><br><br>"
        "Each time you click <b>Allocate</b>, the worker calls:<br>"
        "<code>mmap(nullptr, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0)</code><br><br>"
        "This creates a real entry in the kernel's virtual memory table. You can verify it "
        "yourself:<br><code>cat /proc/%1/maps</code><br><br>"
        "To <b>free</b> a block, click it in the strip to select it, then press "
        "<b>Free Selected</b>. The worker calls "
        "<code>munmap(ptr, size)</code> — the region disappears from the map immediately. "
        "This is exactly what <code>malloc()</code>/<code>free()</code> do internally, "
        "just one level lower."
    ).arg(pid));
}

void MemoryLab::onWorkerDied() {
    mapTimer->stop();
    // All block IDs from the dead worker are meaningless — force the UI
    // back to "nothing selected" so Free/mprotect/madvise can't fire against
    // IDs that may be reused by a new worker after a restart.
    selectedBlockId = -1;
    freeBtn->setEnabled(false);
    arenaView->setSelected(-1);
    statusLabel->setText("⚠ Worker process exited. Click Restart Worker to bring it back.");
}

void MemoryLab::onCommandFailed(QString reason) {
    statusLabel->setText("⚠ " + reason);
}

void MemoryLab::onArenaUpdated(std::vector<ArenaBlock> blocks, ArenaSummary summary) {
    // totalBytes = sum of live allocations; avoid divide-by-zero
    long total = summary.usedBytes > 0 ? summary.usedBytes : 1;
    arenaView->setBlocks(blocks, total);
    // ArenaView::setBlocks() already clears its internal selectedId if the
    // block is gone.  Mirror that decision back into MemoryLab's own state so
    // the Free button and mprotect/madvise are consistent with what's drawn.
    bool stillLive = false;
    for (auto& blk : blocks)
        if (blk.used && blk.id == selectedBlockId) { stillLive = true; break; }
    if (!stillLive && selectedBlockId != -1) {
        selectedBlockId = -1;
        freeBtn->setEnabled(false);
    }
    updateStatsLabel(summary);
}

void MemoryLab::onMapRefresh() {
    // If a sandbox PID is pinned, refresh that instead of the worker.
    pid_t pid = (displayedSandboxPid > 0) ? displayedSandboxPid : driver->workerPid();
    if (pid <= 0) return;

    auto regions = MemoryInspector::readMemMap(pid);
    if (regions.empty()) return;

    long totalKB = 0;
    for (auto& r : regions) totalKB += r.sizeKB;
    mapView->setRegions(regions, totalKB);
}

void MemoryLab::onMprotectClicked() {
    pid_t pid = driver->workerPid();
    if (pid <= 0) { statusLabel->setText("⚠ Worker not running"); return; }
    if (selectedBlockId == -1) {
        statusLabel->setText("⚠ Select a block first — click a block in the strip above");
        return;
    }
    QString perm = mprotectBox->currentData().toString();
    driver->mprotect(selectedBlockId, perm);
    refreshSmapsDiff();

    emit explanationNeeded(QString(
        "<b>mprotect() — Page Permission Fault Demo</b><br><br>"
        "Block #%1 protection changed to <b>%2</b> via:<br>"
        "<code>mprotect(ptr, size, PROT_%3)</code><br><br>"
        "If set to RO or NONE, the worker immediately tries to write to the page. "
        "The CPU raises a <b>hardware page fault</b> — the kernel delivers "
        "<b>SIGSEGV</b> to the process. We catch it with <code>sigaction()</code> "
        "and report <code>SIGSEGV_TRIGGERED</code> so you can see the fault live.<br><br>"
        "This is exactly how stack overflow detection works — the OS puts a NONE "
        "guard page below every thread stack."
    ).arg(selectedBlockId).arg(perm).arg(perm == "RO" ? "READ" : perm == "NONE" ? "NONE" : "READ|WRITE"));
}

void MemoryLab::onMadviseClicked() {
    pid_t pid = driver->workerPid();
    if (pid <= 0) { statusLabel->setText("⚠ Worker not running"); return; }
    if (selectedBlockId == -1) {
        statusLabel->setText("⚠ Select a block first — click a block in the strip above");
        return;
    }
    QString advice = madviseBox->currentData().toString();
    driver->madvise(selectedBlockId, advice);
    refreshSmapsDiff();

    emit explanationNeeded(QString(
        "<b>madvise() — %1</b><br><br>"
        "Applied to block #%2 via:<br>"
        "<code>madvise(ptr, size, MADV_%1)</code><br><br>"
        "%3<br><br>"
        "The smaps_rollup below shows whether RSS changed — DONTNEED should "
        "reduce the resident set immediately on a cold page."
    ).arg(advice).arg(selectedBlockId).arg(
        advice == "DONTNEED" ?
            "Tells the kernel: I don't need the contents of these pages. "
            "The kernel is free to discard them and reclaim the physical RAM. "
            "The pages remain mapped — the next access causes a minor page fault "
            "and the kernel gives you a fresh zero page." :
            "Tells the kernel: I will need these pages soon. "
            "The kernel starts prefetching them into physical RAM. "
            "Useful before large sequential reads."
    ));
}

void MemoryLab::onCowForkClicked() {
    pid_t pid = driver->workerPid();
    if (pid <= 0) { statusLabel->setText("⚠ Worker not running"); return; }
    driver->cowFork();
    refreshSmapsDiff();

    emit explanationNeeded(
        "<b>fork() + Copy-on-Write (COW)</b><br><br>"
        "The worker forked a child process. Immediately after fork(), "
        "both parent and child point to the <b>same physical pages</b> — "
        "no copying occurs. The MMU marks all shared pages as read-only.<br><br>"
        "When the child writes to a page, the CPU triggers a page fault. "
        "The kernel allocates a new physical page, copies the old content, "
        "and maps the new page into the child's address space. This is COW.<br><br>"
        "<b>What you see:</b> RSS in smaps_rollup diverges — the child's "
        "RSS grows as it writes, while the parent's RSS stays lower.<br><br>"
        "This is exactly how <code>fork()</code> is fast for read-heavy workloads.");
}

void MemoryLab::refreshSmapsDiff() {
    pid_t pid = driver->workerPid();
    if (pid <= 0) return;

    std::string path = std::string("/proc/") + std::to_string(pid) + "/smaps_rollup";
    std::ifstream f(path);
    if (!f.is_open()) {
        // Fall back to /proc/pid/status
        std::string spath = std::string("/proc/") + std::to_string(pid) + "/status";
        std::ifstream sf(spath);
        std::string line; long vmRss = 0;
        while (std::getline(sf, line))
            if (line.rfind("VmRSS:", 0) == 0) { std::istringstream ss(line.substr(6)); ss >> vmRss; break; }
        smapsLabel->setText(QString("RSS: %1 KB  (from /proc/%2/status)")
            .arg(vmRss).arg(pid));
        smapsLabel->show(); return;
    }

    std::string line;
    long rss = 0, pss = 0, shared_clean = 0, private_clean = 0, private_dirty = 0;
    while (std::getline(f, line)) {
        auto ex = [&](const char* pfx, long& v) {
            if (line.rfind(pfx, 0) == 0) {
                std::istringstream ss(line.substr(strlen(pfx))); ss >> v;
            }
        };
        ex("Rss:",           rss);
        ex("Pss:",           pss);
        ex("Shared_Clean:",  shared_clean);
        ex("Private_Clean:", private_clean);
        ex("Private_Dirty:", private_dirty);
    }
    smapsLabel->setText(QString(
        "smaps_rollup PID %1:  RSS=%2 KB  ·  PSS=%3 KB  ·  Shared=%4 KB  "
        "·  Private_Clean=%5 KB  ·  Private_Dirty=%6 KB")
        .arg(pid).arg(rss).arg(pss).arg(shared_clean)
        .arg(private_clean).arg(private_dirty));
    smapsLabel->show();
}

void MemoryLab::updateStatsLabel(const ArenaSummary& s) {
    statsLabel->setText(QString(
        "Live allocations: %1   •   Total mapped: %2 KB"
    ).arg(s.blockCount).arg(s.usedBytes / 1024));
}
