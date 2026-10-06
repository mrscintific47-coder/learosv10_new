#include "MemoryLab.h"
#include "EventBus.h"
#include "Theme.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QScrollBar>
#include <QScrollArea>
#include <QFrame>
#include <QPainterPath>
#include <QToolTip>
#include <QHeaderView>
#include <cmath>
#include <algorithm>

// ═══════════════════════════════════════════════════════════════════════════════
// FragBar
// ═══════════════════════════════════════════════════════════════════════════════

FragBar::FragBar(QWidget* parent) : QWidget(parent) {
    setFixedHeight(24);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setToolTip("Memory composition: Used (blue) · Internal waste (orange) · Free (dark)");
}

void FragBar::update(const ArenaSummary& s) {
    summary = s;
    QWidget::update();
}

void FragBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    long total = summary.totalBytes > 0 ? summary.totalBytes : 1;
    double usedFrac  = (double)summary.usedBytes   / total;
    double wasteFrac = (double)summary.internalWaste / total;
    double freeFrac  = 1.0 - usedFrac - wasteFrac;
    freeFrac = std::max(freeFrac, 0.0);

    int W = width(), H = height();
    double x = 0;

    // Used — blue
    double usedW = usedFrac * W;
    p.fillRect(QRectF(x, 0, usedW, H), QColor("#4F6EF7"));
    x += usedW;

    // Internal waste — amber
    double wasteW = wasteFrac * W;
    if (wasteW > 0.5) {
        p.fillRect(QRectF(x, 0, wasteW, H), QColor("#F59E0B"));
        x += wasteW;
    }

    // Free — dark
    double freeW = freeFrac * W;
    p.fillRect(QRectF(x, 0, freeW, H), QColor("#1E2633"));

    // Border
    p.setPen(QPen(QColor("#334155"), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(0, 0, W - 1, H - 1);

    // Labels (only if wide enough)
    p.setFont(QFont("Segoe UI", 8, QFont::Bold));
    if (usedW > 40) {
        p.setPen(Qt::white);
        p.drawText(QRectF(0, 0, usedW, H), Qt::AlignCenter,
                   QString("%1%").arg((int)(usedFrac * 100)));
    }
    if (wasteW > 40) {
        p.setPen(Qt::white);
        p.drawText(QRectF(usedW, 0, wasteW, H), Qt::AlignCenter,
                   QString("~%1%").arg((int)(wasteFrac * 100)));
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// ChallengePanel
// ═══════════════════════════════════════════════════════════════════════════════

ChallengePanel::ChallengePanel(QWidget* parent) : QWidget(parent) {
    // Use the design-system accent card: blue left border, tinted background
    setStyleSheet(
        "background: #EEF2FF;"
        "border-radius: 10px;"
        "border: 1px solid #C7D2FE;"
        "border-left: 4px solid #4F6EF7;"
    );

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(12, 10, 12, 10);
    lay->setSpacing(4);

    auto* header = new QHBoxLayout();
    auto* badge = new QLabel("🎯 CHALLENGE");
    badge->setStyleSheet(
        "color: #3B5BF6; font-size: 9px; font-weight: 800;"
        "letter-spacing: .12em; background: transparent; border: none;");
    progressLabel = new QLabel("1 / 7");
    progressLabel->setStyleSheet(
        "color: #6366F1; font-size: 9px; background: transparent; border: none;");
    header->addWidget(badge);
    header->addStretch();
    header->addWidget(progressLabel);
    lay->addLayout(header);

    titleLabel = new QLabel();
    titleLabel->setStyleSheet(
        "color: #1E1B4B; font-size: 11px; font-weight: 700;"
        "background: transparent; border: none;");
    titleLabel->setWordWrap(true);
    lay->addWidget(titleLabel);

    descLabel = new QLabel();
    descLabel->setStyleSheet(
        "color: #3730A3; font-size: 10px;"
        "background: transparent; border: none;");
    descLabel->setWordWrap(true);
    lay->addWidget(descLabel);

    hintLabel = new QLabel();
    hintLabel->setStyleSheet(
        "color: #4338CA; font-size: 10px; font-style: italic;"
        "background: transparent; border: none;");
    hintLabel->setWordWrap(true);
    lay->addWidget(hintLabel);

    // ── Define challenges ─────────────────────────────────────────────────────
    challenges = {
        {
            "1. Make your first allocation",
            "Set size to anything and click Raw Alloc. Watch a coloured block appear on the canvas.",
            "Hint: Try 32768 B (32 KB) to see a clearly visible block.",
            "<b>✓ First Allocation</b><br><br>"
            "Every call to <code>malloc()</code> eventually asks the OS for memory "
            "via <code>mmap(MAP_ANONYMOUS)</code> or <code>brk()</code>. "
            "Here the 4 MB sandbox is pre-mapped and your allocator carves pieces from it. "
            "The block's colour is tied to its ID — so you can identify it even after "
            "other blocks are added.<br><br>"
            "Notice the <b>address offset</b> in the inspector — that's a real byte offset "
            "inside the <code>mmap</code> region.",
            [](const ArenaSummary& s, int /*bc*/, bool, bool, bool) {
                return s.blockCount >= 1;
            }
        },
        {
            "2. Cause external fragmentation",
            "Allocate 3+ blocks, then free the middle one. "
            "Watch the free space split into two non-adjacent holes.",
            "Hint: Allocate three ~64 KB blocks, click the second one, then hit 🗑 Free.",
            "<b>✓ External Fragmentation Observed</b><br><br>"
            "External fragmentation happens when free memory is split into small non-contiguous "
            "holes. Even though total free space might be large, a big allocation can fail "
            "because no single hole is big enough.<br><br>"
            "The <b>Holes</b> counter in the stats bar now shows more than one hole. "
            "The <b>Largest hole</b> is smaller than the total free space — that gap "
            "is fragmentation in action.<br><br>"
            "Real allocators like <code>jemalloc</code> and <code>tcmalloc</code> fight "
            "this with size classes, coalescing, and compaction.",
            [](const ArenaSummary& s, int /*bc*/, bool, bool, bool) {
                // Fragmented = more than 1 hole and largest hole < free bytes
                return s.holeCount >= 2 && s.largestHole < s.freeBytes;
            }
        },
        {
            "3. Fill the sandbox until allocation fails",
            "Keep allocating until the worker reports an error (status bar turns red).",
            "Hint: Use large sizes like 512 KB repeatedly.",
            "<b>✓ Out-of-Memory Condition</b><br><br>"
            "When a flat allocator can't find a fit — even if total free > 0 — it returns "
            "<code>NULL</code> (or throws <code>std::bad_alloc</code>). "
            "The OS equivalent is <code>ENOMEM</code>.<br><br>"
            "This is why real systems use virtual memory: the kernel can map anonymous pages "
            "on-demand and defer physical frame assignment until a page is actually touched "
            "(demand paging). Your sandbox has no such luxury.",
            [](const ArenaSummary& s, int /*bc*/, bool, bool, bool) {
                return s.usedBytes > 0 && s.freeBytes < 65536;
            }
        },
        {
            "4. Reset and switch to Paged mode",
            "Click ↺ Reset, then change the mode to \"Paged (4 KB pages + page table)\". "
            "Allocate a block and click ⟳ Table to see the page table.",
            "Hint: The page table maps virtual page numbers → physical frames.",
            "<b>✓ Paged Memory</b><br><br>"
            "Paging divides memory into fixed-size chunks (4 KB here, matching the x86-64 "
            "hardware page size). Every allocation rounds up to a whole number of pages.<br><br>"
            "<b>Key insight:</b> paging eliminates <i>external</i> fragmentation — "
            "there are no odd-sized holes — but introduces <i>internal</i> fragmentation "
            "(the last page of an allocation is often partially wasted).<br><br>"
            "The <b>page table</b> is the data structure the MMU walks on every memory "
            "access. In Linux each process has its own page table tree rooted at "
            "<code>CR3</code>.",
            [](const ArenaSummary& /*s*/, int /*bc*/, bool, bool hasPaged, bool) {
                return hasPaged;
            }
        },
        {
            "5. Observe internal fragmentation in Paged mode",
            "Allocate a block that is NOT a multiple of 4096 B (e.g. 5000 B). "
            "Watch the orange waste segment appear in the fragmentation bar.",
            "Hint: 5000 B rounds up to 2 pages = 8192 B → 3192 B wasted.",
            "<b>✓ Internal Fragmentation</b><br><br>"
            "Internal fragmentation is the wasted space <i>inside</i> an allocated region. "
            "When you request 5000 bytes and get 8192 (2 × 4 KB pages), the extra 3192 bytes "
            "are allocated but unused.<br><br>"
            "The amber bar in the fragmentation meter shows this waste. Real OSes minimise "
            "it by using multiple page sizes (huge pages: 2 MB, 1 GB on x86) for large "
            "allocations and slab/SLUB caches for small kernel objects.",
            [](const ArenaSummary& s, int, bool, bool hasPaged, bool) {
                return hasPaged && s.internalWaste > 0;
            }
        },
        {
            "6. Switch to Segmented mode and compare",
            "Reset and switch to \"Segmented (named segments)\". "
            "Allocate to fill several named segments, then click ⟳ Table.",
            "Hint: Each segment maps to a named region: code, data, heap, stack…",
            "<b>✓ Segmented Memory</b><br><br>"
            "Segmentation divides memory into logical regions with meaningful names. "
            "Each segment has a <b>base address</b> and a <b>limit</b> (max size). "
            "The CPU checks every access against the segment's limit — out-of-range "
            "accesses cause a General Protection Fault (#GP).<br><br>"
            "x86-64 Linux uses a flat segmentation model (all segment bases = 0, limit = full) "
            "for user space but keeps <code>FS</code> for thread-local storage (TLS) — "
            "exactly what the <code>tls</code> segment here represents.",
            [](const ArenaSummary& /*s*/, int /*bc*/, bool, bool, bool hasSeg) {
                return hasSeg;
            }
        },
        {
            "7. Allocate a linked list and inspect pointer arrows",
            "Reset to Contiguous, then click \"Linked List\" with n=6. "
            "Zoom in on the canvas to see the arrows connecting nodes.",
            "Hint: Each node is 32 bytes: 8 bytes next-pointer + 24 bytes data.",
            "<b>✓ Pointer-Based Data Structures in Memory</b><br><br>"
            "A linked list stores each node at a <i>real address</i>. The first 8 bytes "
            "of every node hold a pointer to the next node — a sandbox offset here, "
            "a virtual address in real programs.<br><br>"
            "Notice that all nodes are contiguous in this sandbox (they're one big alloc). "
            "In a real <code>malloc</code>-based list, each node is allocated separately "
            "and ends up scattered across the heap — cache-unfriendly and fragmentation-prone. "
            "Arrays beat linked lists on locality, which is why <code>std::vector</code> "
            "is almost always faster in practice.",
            [](const ArenaSummary& s, int /*bc*/, bool hasCont, bool, bool) {
                return hasCont && s.blockCount >= 1;
            }
        },
    };

    refreshDisplay();
}

void ChallengePanel::advance() {
    if (currentIdx < challenges.size() - 1) {
        currentIdx++;
        lastCompleted = false;
        refreshDisplay();
    }
}

void ChallengePanel::refreshDisplay() {
    if (challenges.isEmpty()) return;
    const auto& c = challenges[currentIdx];
    progressLabel->setText(QString("%1 / %2").arg(currentIdx + 1).arg(challenges.size()));
    titleLabel->setText(c.title);
    descLabel->setText(c.desc);
    hintLabel->setText(c.hint);
}

void ChallengePanel::onArenaUpdated(const ArenaSummary& s, int blockCount,
                                     bool hasContiguous, bool hasPaged, bool hasSegmented) {
    if (challenges.isEmpty() || currentIdx >= challenges.size()) return;
    const auto& c = challenges[currentIdx];
    bool done = c.check(s, blockCount, hasContiguous, hasPaged, hasSegmented);
    if (done && !lastCompleted) {
        lastCompleted = true;
        emit challengeExplanationNeeded(c.successExplanation);
        titleLabel->setText("✅ " + c.title);
        hintLabel->setText("✓ Challenge complete! Read the explanation panel →");
        hintLabel->setStyleSheet(
            "color: #15803D; font-size: 10px; font-weight: 700;"
            "background: transparent; border: none;");
        // Auto-advance after a short delay
        QTimer::singleShot(2200, this, [this]() { advance(); });
    }
}

// ═══════════════════════════════════════════════════════════════════════════════
// MemoryCanvas
// ═══════════════════════════════════════════════════════════════════════════════

static const char* BLOCK_PALETTE[] = {
    "#4F6EF7", "#22C55E", "#F97316", "#A855F7",
    "#14B8A6", "#EAB308", "#EC4899", "#0EA5E9",
    "#6366F1", "#84CC16", "#FB923C", "#C084FC",
    "#2DD4BF", "#FCD34D", "#F472B6", "#38BDF8"
};

MemoryCanvas::MemoryCanvas(QWidget* parent) : QWidget(parent) {
    setMinimumSize(400, 300);
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setStyleSheet("background: #0D1117; border-radius: 10px;");
}

// ── layout helpers ─────────────────────────────────────────────────────────

// At zoom==1 the sandbox is laid out as a 2D grid. cellsPerRow() determines
// how many "cells" (each representing CELL_BYTES bytes) fit across the widget.
// With zoom the canvas becomes wider and taller proportionally.
static constexpr long CELL_BYTES = 1024; // each cell = 1 KB at zoom 1

int MemoryCanvas::cellsPerRow() const {
    // Aim for roughly 64 cells per row at zoom==1
    return 64;
}

QRectF MemoryCanvas::offsetToRect(long offset, long size) const {
    long totalCells    = (long)(sandboxSize / CELL_BYTES);
    int  cols          = cellsPerRow();
    int  rows          = (int)std::ceil((double)totalCells / cols);

    double cellW = (double)width()  * zoom / cols;
    double cellH = (double)height() * zoom / rows;

    // Clamp to minimum pixel size
    cellW = std::max(cellW, 1.5);
    cellH = std::max(cellH, 1.5);

    long startCell = offset / CELL_BYTES;
    long endCell   = std::max(startCell + 1, (long)std::ceil((double)(offset + size) / CELL_BYTES));
    endCell = std::min(endCell, totalCells);

    // Multi-row blocks: use the bounding rect from startCell to endCell
    int sc = (int)startCell, ec = (int)(endCell - 1);
    int sc_row = sc / cols, sc_col = sc % cols;
    int ec_row = ec / cols, ec_col = ec % cols;

    double x = sc_col * cellW - panX;
    double y = sc_row * cellH - panY;
    double w, h;

    if (sc_row == ec_row) {
        w = (ec_col - sc_col + 1) * cellW;
        h = cellH;
    } else {
        // Multi-row: use full width × rows
        x = -panX;
        w = cols * cellW;
        h = (ec_row - sc_row + 1) * cellH;
    }
    return QRectF(x, y, w - 1.5, h - 1.5);
}

long MemoryCanvas::pointToOffset(QPoint p) const {
    long totalCells = (long)(sandboxSize / CELL_BYTES);
    int  cols       = cellsPerRow();
    int  rows       = (int)std::ceil((double)totalCells / cols);

    double cellW = (double)width()  * zoom / cols;
    double cellH = (double)height() * zoom / rows;
    cellW = std::max(cellW, 1.5);
    cellH = std::max(cellH, 1.5);

    int col = (int)((p.x() + panX) / cellW);
    int row = (int)((p.y() + panY) / cellH);
    if (col < 0 || col >= cols || row < 0 || row >= rows) return -1;

    long cell = (long)row * cols + col;
    if (cell >= totalCells) return -1;
    return cell * CELL_BYTES;
}

QColor MemoryCanvas::colorForBlock(const ArenaBlock& b) const {
    return QColor(BLOCK_PALETTE[b.id % 16]);
}

void MemoryCanvas::clampPan() {
    int contentW = (int)(width()  * zoom);
    int contentH = (int)(height() * zoom);
    panX = std::max(0, std::min(panX, contentW  - width()));
    panY = std::max(0, std::min(panY, contentH - height()));
}

void MemoryCanvas::drawArrow(QPainter& p, QPointF from, QPointF to, QColor color) {
    if (from.isNull() || to.isNull()) return;
    p.setPen(QPen(color, 1.5, Qt::SolidLine));
    p.setBrush(Qt::NoBrush);
    p.drawLine(from, to);
    // Arrowhead
    QPointF dir = to - from;
    double len = std::sqrt(dir.x()*dir.x() + dir.y()*dir.y());
    if (len < 1) return;
    dir /= len;
    QPointF perp(-dir.y(), dir.x());
    QPolygonF head;
    head << to
         << (to - dir * 8 + perp * 4)
         << (to - dir * 8 - perp * 4);
    p.setBrush(color);
    p.setPen(Qt::NoPen);
    p.drawPolygon(head);
}

// ── setters ───────────────────────────────────────────────────────────────────

void MemoryCanvas::setBlocks(const std::vector<ArenaBlock>& b, long sz) {
    blocks = b;
    sandboxSize = sz > 0 ? sz : 4 * 1024 * 1024;
    // Remove stale struct overlays for freed blocks
    overlays.erase(
        std::remove_if(overlays.begin(), overlays.end(), [&](const StructOverlay& o) {
            for (const auto& blk : blocks) if (blk.id == o.structId) return false;
            return true;
        }),
        overlays.end()
    );
    update();
}

void MemoryCanvas::setStructNodes(int structId, const std::vector<StructNode>& nodes,
                                   const QString& type) {
    for (auto& o : overlays) {
        if (o.structId == structId) { o.nodes = nodes; o.type = type; update(); return; }
    }
    overlays.push_back({structId, type, nodes});
    update();
}

void MemoryCanvas::clearStructOverlays() { overlays.clear(); update(); }

void MemoryCanvas::setSelectedId(int id) { selectedId = id; update(); }

void MemoryCanvas::zoomIn()  { zoom = std::min(zoom * 2.0, 32.0); clampPan(); update(); }
void MemoryCanvas::zoomOut() { zoom = std::max(zoom / 2.0, 0.25); clampPan(); update(); }
void MemoryCanvas::resetZoom() { zoom = 1.0; panX = panY = 0; update(); }

// ── paint ─────────────────────────────────────────────────────────────────────

void MemoryCanvas::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // Dark background grid
    p.fillRect(rect(), QColor("#0D1117"));

    long totalCells = (long)(sandboxSize / CELL_BYTES);
    int  cols       = cellsPerRow();
    int  rows       = (int)std::ceil((double)totalCells / cols);

    double cellW = (double)width()  * zoom / cols;
    double cellH = (double)height() * zoom / rows;
    cellW = std::max(cellW, 1.5);
    cellH = std::max(cellH, 1.5);

    // ── Grid lines (only when zoomed enough to see them) ──────────────────
    if (cellW >= 6 && cellH >= 6) {
        p.setPen(QPen(QColor("#1E2633"), 0.5));
        for (int c = 0; c <= cols; c++) {
            double x = c * cellW - panX;
            if (x >= 0 && x <= width())
                p.drawLine(QPointF(x, 0), QPointF(x, height()));
        }
        for (int r = 0; r <= rows; r++) {
            double y = r * cellH - panY;
            if (y >= 0 && y <= height())
                p.drawLine(QPointF(0, y), QPointF(width(), y));
        }
    }

    // ── Free space hint ────────────────────────────────────────────────────
    // Paint the entire canvas in a very dark color to represent free memory
    // Blocks will paint on top of this
    // (already handled by the dark background — free cells stay dark)

    // ── Blocks ────────────────────────────────────────────────────────────
    for (const auto& blk : blocks) {
        if (!blk.used) continue;

        // Multi-row blocks span many cells — draw each occupied row segment
        long startCell = blk.offset / CELL_BYTES;
        long numCells  = std::max(1L, (long)std::ceil((double)blk.size / CELL_BYTES));
        long endCell   = std::min(startCell + numCells, totalCells);

        QColor fillColor = colorForBlock(blk);
        if (blk.id == selectedId) fillColor = fillColor.lighter(140);

        // Draw each row segment
        long cell = startCell;
        while (cell < endCell) {
            int row = (int)(cell / cols);
            int col = (int)(cell % cols);
            // How many cells this row can take
            long rowEnd = std::min(endCell, (long)(row + 1) * cols);
            int  ncells = (int)(rowEnd - cell);

            double x = col  * cellW - panX;
            double y = row  * cellH - panY;
            double w = ncells * cellW;
            double h = cellH;

            // Clip to visible area (skip if off-screen)
            if (x + w < 0 || x > width() || y + h < 0 || y > height()) {
                cell = rowEnd;
                continue;
            }

            QRectF r(x + 1, y + 1, w - 2, h - 2);
            QPainterPath path;
            path.addRoundedRect(r, 3, 3);
            p.fillPath(path, fillColor);

            // Selection ring
            if (blk.id == selectedId) {
                p.setPen(QPen(Qt::white, 2.0));
                p.setBrush(Qt::NoBrush);
                p.drawPath(path);
            }

            // Label — only if cell is wide enough
            if (w > 32 && h > 10) {
                p.setPen(QColor(0, 0, 0, 180));
                p.setFont(QFont("Consolas", std::max(7.0, std::min(10.0, cellW * 0.35)), QFont::Bold));
                // Show struct type icon + label on first row only
                if (row == (int)(startCell / cols)) {
                    QString lbl = blk.label;
                    if (blk.isStruct) lbl = "[" + blk.structType + "] " + lbl;
                    p.drawText(r, Qt::AlignCenter | Qt::TextWordWrap, lbl);
                }
            }
            cell = rowEnd;
        }
    }

    // ── Struct overlays (arrows between nodes) ────────────────────────────
    for (const auto& ov : overlays) {
        // Find the block to get colour
        QColor arrowColor = QColor("#94A3B8");
        for (const auto& blk : blocks)
            if (blk.id == ov.structId) { arrowColor = colorForBlock(blk).lighter(160); break; }

        if (ov.type == "LL") {
            // Draw next pointers as arrows
            for (const auto& node : ov.nodes) {
                if (node.link1 == 0) continue;
                QRectF fromR = offsetToRect(node.offset, 32);
                QRectF toR   = offsetToRect(node.link1,  32);
                if (fromR.isValid() && toR.isValid())
                    drawArrow(p, fromR.center(), toR.center(), arrowColor);
            }
        } else if (ov.type == "TREE") {
            for (const auto& node : ov.nodes) {
                QRectF fromR = offsetToRect(node.offset, 48);
                if (node.link1 > 0) {
                    QRectF toR = offsetToRect(node.link1, 48);
                    if (fromR.isValid() && toR.isValid())
                        drawArrow(p, fromR.center(), toR.center(), arrowColor);
                }
                if (node.link2 > 0) {
                    QRectF toR = offsetToRect(node.link2, 48);
                    if (fromR.isValid() && toR.isValid())
                        drawArrow(p, fromR.center(), toR.center(),
                                  arrowColor.darker(120));
                }
            }
        } else if (ov.type == "HASH") {
            // Draw bucket chain arrows (all buckets → same color band)
            for (size_t i = 0; i + 1 < ov.nodes.size(); i++) {
                QRectF a = offsetToRect(ov.nodes[i].offset,   16);
                QRectF b = offsetToRect(ov.nodes[i+1].offset, 16);
                if (a.isValid() && b.isValid())
                    drawArrow(p, a.center(), b.center(), arrowColor);
            }
        }
        // ARRAY: no arrows needed — contiguous cells speak for themselves
    }

    // ── Address ruler (left edge, when zoomed enough) ─────────────────────
    if (cellH >= 14) {
        p.setFont(QFont("Consolas", 8));
        for (int row = 0; row < rows; row++) {
            double y = row * cellH - panY;
            if (y < -cellH || y > height()) continue;
            long byteOffset = (long)row * cols * CELL_BYTES;
            QString addr = QString("0x%1").arg(byteOffset, 6, 16, QChar('0'));
            p.setPen(QColor("#475569"));
            p.drawText(QRectF(2, y, 54, cellH), Qt::AlignVCenter | Qt::AlignLeft, addr);
        }
    }

    // ── Legend ─────────────────────────────────────────────────────────────
    int lx = 8, ly = height() - 18;
    p.setFont(QFont("Segoe UI", 8));
    auto legend = [&](QColor c, const QString& lbl) {
        p.fillRect(lx, ly + 3, 10, 10, c);
        p.setPen(QColor("#94A3B8"));
        p.drawText(lx + 13, ly + 12, lbl);
        lx += 14 + p.fontMetrics().horizontalAdvance(lbl) + 6;
    };
    legend(QColor("#4F6EF7"), "LL");
    legend(QColor("#22C55E"), "Array");
    legend(QColor("#F97316"), "Tree");
    legend(QColor("#A855F7"), "Hash");
    legend(QColor("#334155"), "Free");

    // Zoom indicator
    p.setPen(QColor("#64748B"));
    p.setFont(QFont("Segoe UI", 8));
    p.drawText(width() - 50, height() - 6, QString("%1×").arg(zoom, 0, 'f', 1));
}

// ── mouse ─────────────────────────────────────────────────────────────────────

void MemoryCanvas::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        panning      = true;
        panStart     = e->pos();
        panXAtStart  = panX;
        panYAtStart  = panY;
    }
}

void MemoryCanvas::mouseMoveEvent(QMouseEvent* e) {
    if (panning && (e->buttons() & Qt::LeftButton)) {
        panX = panXAtStart - (e->pos().x() - panStart.x());
        panY = panYAtStart - (e->pos().y() - panStart.y());
        clampPan();
        update();
    }
}

void MemoryCanvas::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && panning) {
        panning = false;
        int dx = std::abs(e->pos().x() - panStart.x());
        int dy = std::abs(e->pos().y() - panStart.y());
        if (dx < 4 && dy < 4) {
            // Treat as click
            long off = pointToOffset(e->pos());
            if (off < 0) { emit emptyClicked(); return; }
            for (const auto& blk : blocks) {
                if (blk.used && off >= blk.offset && off < blk.offset + blk.size) {
                    emit blockClicked(blk.id);
                    return;
                }
            }
            emit emptyClicked();
        }
    }
}

void MemoryCanvas::wheelEvent(QWheelEvent* e) {
    int ticks = e->angleDelta().y();
    if (ticks == 0) return;
    double old = zoom;
    zoom = ticks > 0 ? std::min(zoom * 1.25, 32.0) : std::max(zoom / 1.25, 0.25);
    // Zoom toward cursor
    QPoint c = e->position().toPoint();
    panX = (int)((panX + c.x()) * (zoom / old)) - c.x();
    panY = (int)((panY + c.y()) * (zoom / old)) - c.y();
    clampPan();
    update();
    e->accept();
}

void MemoryCanvas::resizeEvent(QResizeEvent*) { clampPan(); }

// ═══════════════════════════════════════════════════════════════════════════════
// TablePanel
// ═══════════════════════════════════════════════════════════════════════════════

TablePanel::TablePanel(QWidget* parent) : QWidget(parent) {
    setStyleSheet(QString("background:%1;").arg(Theme::BG_APP));
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(4);

    titleLabel = new QLabel("No table yet — pick a mode");
    titleLabel->setStyleSheet(QString(
        "color:%1; font-size:11px; font-weight:600; padding:4px 8px;"
    ).arg(Theme::TEXT_SECONDARY));
    lay->addWidget(titleLabel);

    table = new QTableWidget(0, 4, this);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->verticalHeader()->hide();
    table->setAlternatingRowColors(true);
    table->setStyleSheet(Theme::table());
    lay->addWidget(table, 1);
}

void TablePanel::clear() {
    table->setRowCount(0);
    table->setColumnCount(0);
    titleLabel->setText("—");
}

static void setCell(QTableWidget* t, int row, int col, const QString& text,
                    Qt::Alignment align = Qt::AlignLeft | Qt::AlignVCenter) {
    auto* item = new QTableWidgetItem(text);
    item->setTextAlignment(align);
    t->setItem(row, col, item);
}

void TablePanel::showPageTable(const std::vector<PageEntry>& entries) {
    titleLabel->setText(QString("Page Table  (%1 pages  ·  4 KB each)").arg(entries.size()));
    table->setColumnCount(4);
    table->setHorizontalHeaderLabels({"VPN", "Frame", "Present", "Owner Block"});
    // Only show the first 256 entries for performance; user can scroll
    int show = std::min((int)entries.size(), 256);
    table->setRowCount(show);
    for (int i = 0; i < show; i++) {
        const auto& e = entries[i];
        setCell(table, i, 0, QString::number(e.vpn),   Qt::AlignRight | Qt::AlignVCenter);
        setCell(table, i, 1, QString::number(e.frame), Qt::AlignRight | Qt::AlignVCenter);
        setCell(table, i, 2, e.present ? "✓" : "—",   Qt::AlignCenter | Qt::AlignVCenter);
        setCell(table, i, 3, e.blockId >= 0 ? QString("#%1").arg(e.blockId) : "free",
                Qt::AlignCenter | Qt::AlignVCenter);
        if (e.present) {
            for (int c = 0; c < 4; c++)
                if (table->item(i, c))
                    table->item(i, c)->setBackground(QColor("#EFF6FF"));
        }
    }
    table->resizeColumnsToContents();
}

void TablePanel::showSegTable(const std::vector<SegEntry>& entries) {
    titleLabel->setText(QString("Segment Table  (%1 segments)").arg(entries.size()));
    table->setColumnCount(4);
    table->setHorizontalHeaderLabels({"Segment", "Base", "Limit", "Owner Block"});
    table->setRowCount((int)entries.size());
    for (int i = 0; i < (int)entries.size(); i++) {
        const auto& e = entries[i];
        setCell(table, i, 0, e.name);
        setCell(table, i, 1, QString("0x%1").arg(e.base,  6, 16, QChar('0')));
        setCell(table, i, 2, QString("0x%1").arg(e.limit, 6, 16, QChar('0')));
        setCell(table, i, 3, e.blockId >= 0 ? QString("#%1").arg(e.blockId) : "free");
        if (e.blockId >= 0) {
            for (int c = 0; c < 4; c++)
                if (table->item(i, c))
                    table->item(i, c)->setBackground(QColor("#F0FDF4"));
        }
    }
    table->resizeColumnsToContents();
}

void TablePanel::showFrameTable(const std::vector<FrameEntry>& entries) {
    titleLabel->setText(QString("Frame Table  (%1 frames)").arg(entries.size()));
    table->setColumnCount(3);
    table->setHorizontalHeaderLabels({"Frame #", "Status", "Owner Block"});
    int show = std::min((int)entries.size(), 256);
    table->setRowCount(show);
    for (int i = 0; i < show; i++) {
        const auto& e = entries[i];
        setCell(table, i, 0, QString::number(e.frame), Qt::AlignRight | Qt::AlignVCenter);
        setCell(table, i, 1, e.free ? "Free" : "Occupied", Qt::AlignCenter);
        setCell(table, i, 2, e.blockId >= 0 ? QString("#%1").arg(e.blockId) : "—",
                Qt::AlignCenter);
        if (!e.free) {
            for (int c = 0; c < 3; c++)
                if (table->item(i, c))
                    table->item(i, c)->setBackground(QColor("#FFF7ED"));
        }
    }
    table->resizeColumnsToContents();
}

// ═══════════════════════════════════════════════════════════════════════════════
// MemoryLab
// ═══════════════════════════════════════════════════════════════════════════════

MemoryLab::MemoryLab(QWidget* parent) : QWidget(parent) {
    setStyleSheet(QString("background:%1;").arg(Theme::BG_APP));

    // ── Content widget (scrollable) ─────────────────────────────────────────
    auto* content = new QWidget();
    content->setStyleSheet(QString("background:%1;").arg(Theme::BG_APP));

    // ── Outer layout: left panel + right canvas ─────────────────────────────
    auto* root = new QHBoxLayout(content);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(12);

    // ══ LEFT PANEL (controls + inspect + challenge) ═════════════════════════
    auto* left = new QWidget();
    left->setFixedWidth(300);
    left->setStyleSheet(QString("background:%1;").arg(Theme::BG_APP));
    auto* leftLay = new QVBoxLayout(left);
    leftLay->setContentsMargins(0, 0, 0, 0);
    leftLay->setSpacing(8);

    // Title
    auto* title = new QLabel("🗺  Memory Lab");
    title->setStyleSheet(QString("color:%1; font-size:14px; font-weight:700;").arg(Theme::TEXT_PRIMARY));
    leftLay->addWidget(title);

    // Mode card
    auto* modeCard = new QWidget();
    modeCard->setStyleSheet(Theme::card());
    auto* modeCardLay = new QVBoxLayout(modeCard);
    modeCardLay->setContentsMargins(12, 10, 12, 10);
    modeCardLay->setSpacing(6);
    auto* modeLbl = new QLabel("Allocation Technique");
    modeLbl->setStyleSheet(QString("color:%1; font-size:11px; font-weight:700;").arg(Theme::TEXT_SECONDARY));
    modeBox = new QComboBox();
    modeBox->addItem("Contiguous  (free-list, first-fit)", "contiguous");
    modeBox->addItem("Paged  (4 KB pages + page table)",   "paged");
    modeBox->addItem("Segmented  (named segments)",         "segmented");
    modeBox->addItem("Framed  (physical frame table)",      "framed");
    modeBox->setStyleSheet(Theme::input());
    modeCardLay->addWidget(modeLbl);
    modeCardLay->addWidget(modeBox);
    leftLay->addWidget(modeCard);

    // Allocate card
    auto* allocCard = new QWidget();
    allocCard->setStyleSheet(Theme::card());
    auto* allocLay = new QVBoxLayout(allocCard);
    allocLay->setContentsMargins(12, 10, 12, 10);
    allocLay->setSpacing(6);

    auto* allocTitle = new QLabel("Allocate");
    allocTitle->setStyleSheet(QString("color:%1; font-size:11px; font-weight:700;").arg(Theme::TEXT_SECONDARY));
    allocLay->addWidget(allocTitle);

    // Raw alloc
    auto* rawRow = new QHBoxLayout();
    rawSizeSpin = new QSpinBox();
    rawSizeSpin->setRange(16, 512 * 1024);
    rawSizeSpin->setValue(32768);   // default 32 KB — clearly visible on canvas
    rawSizeSpin->setSingleStep(4096);
    rawSizeSpin->setSuffix(" B");
    rawSizeSpin->setStyleSheet(Theme::input());
    auto* rawBtn = new QPushButton("Raw Alloc");
    rawBtn->setStyleSheet(Theme::btnPrimary());
    rawRow->addWidget(rawSizeSpin, 1);
    rawRow->addWidget(rawBtn);
    allocLay->addLayout(rawRow);

    // Struct controls
    auto* nRow = new QHBoxLayout();
    auto* nLbl = new QLabel("n:");
    nLbl->setStyleSheet(QString("color:%1; font-size:11px;").arg(Theme::TEXT_SECONDARY));
    structNSpin = new QSpinBox();
    structNSpin->setRange(1, 128);
    structNSpin->setValue(8);
    structNSpin->setStyleSheet(Theme::input());
    auto* esizeLbl = new QLabel("elem B:");
    esizeLbl->setStyleSheet(nLbl->styleSheet());
    structESizeSpin = new QSpinBox();
    structESizeSpin->setRange(1, 64);
    structESizeSpin->setValue(8);
    structESizeSpin->setStyleSheet(Theme::input());
    nRow->addWidget(nLbl);
    nRow->addWidget(structNSpin, 1);
    nRow->addSpacing(6);
    nRow->addWidget(esizeLbl);
    nRow->addWidget(structESizeSpin, 1);
    allocLay->addLayout(nRow);

    auto* structRow = new QHBoxLayout();
    auto* llBtn    = new QPushButton("Linked List");
    auto* arrBtn   = new QPushButton("Array");
    auto* treeBtn  = new QPushButton("Tree");
    auto* hashBtn  = new QPushButton("Hash Map");
    for (auto* b : {llBtn, arrBtn, treeBtn, hashBtn})
        b->setStyleSheet(Theme::btnGhost());
    structRow->addWidget(llBtn);
    structRow->addWidget(arrBtn);
    allocLay->addLayout(structRow);
    auto* structRow2 = new QHBoxLayout();
    structRow2->addWidget(treeBtn);
    structRow2->addWidget(hashBtn);
    allocLay->addLayout(structRow2);

    leftLay->addWidget(allocCard);

    // Action buttons
    auto* actCard = new QWidget();
    actCard->setStyleSheet(Theme::card());
    auto* actLay = new QHBoxLayout(actCard);
    actLay->setContentsMargins(12, 8, 12, 8);
    actLay->setSpacing(8);
    freeBtn = new QPushButton("🗑 Free");
    freeBtn->setStyleSheet(Theme::btnGhost());
    freeBtn->setEnabled(false);
    auto* resetBtn = new QPushButton("↺ Reset");
    resetBtn->setStyleSheet(Theme::btnDanger());
    auto* refreshBtn = new QPushButton("⟳ Table");
    refreshBtn->setStyleSheet(Theme::btnGhost());
    refreshBtn->setToolTip("Refresh the page/segment/frame table on the right");
    actLay->addWidget(freeBtn);
    actLay->addWidget(resetBtn);
    actLay->addWidget(refreshBtn);
    leftLay->addWidget(actCard);

    // Zoom buttons
    auto* zoomCard = new QWidget();
    zoomCard->setStyleSheet(Theme::card());
    auto* zoomLay = new QHBoxLayout(zoomCard);
    zoomLay->setContentsMargins(12, 6, 12, 6);
    zoomLay->setSpacing(6);
    auto* zIn  = new QPushButton("＋");
    auto* zOut = new QPushButton("－");
    auto* z1   = new QPushButton("1:1");
    for (auto* b : {zIn, zOut, z1}) {
        b->setFixedWidth(42);
        b->setStyleSheet(Theme::btnGhost());
    }
    auto* zLbl = new QLabel("Zoom:");
    zLbl->setStyleSheet(QString("color:%1; font-size:11px;").arg(Theme::TEXT_SECONDARY));
    zoomLay->addWidget(zLbl);
    zoomLay->addWidget(zOut);
    zoomLay->addWidget(zIn);
    zoomLay->addWidget(z1);
    zoomLay->addStretch();
    leftLay->addWidget(zoomCard);

    // Inspect card
    auto* inspCard = new QWidget();
    inspCard->setStyleSheet(Theme::card());
    auto* inspLay = new QVBoxLayout(inspCard);
    inspLay->setContentsMargins(12, 10, 12, 10);
    inspLay->setSpacing(4);
    auto* inspTitle = new QLabel("Block Inspector");
    inspTitle->setStyleSheet(QString("color:%1; font-size:11px; font-weight:700;").arg(Theme::TEXT_SECONDARY));
    inspectLabel = new QLabel("Click a block on the canvas →");
    inspectLabel->setWordWrap(true);
    inspectLabel->setStyleSheet(QString(
        "color:%1; font-size:11px; font-family:'Consolas','Fira Code',monospace;"
        "line-height:1.5;"
    ).arg(Theme::TEXT_PRIMARY));
    inspLay->addWidget(inspTitle);
    inspLay->addWidget(inspectLabel);
    leftLay->addWidget(inspCard);

    // ── Fragmentation stats card ─────────────────────────────────────────────
    auto* fragCard = new QWidget();
    fragCard->setStyleSheet(Theme::card());
    auto* fragLay = new QVBoxLayout(fragCard);
    fragLay->setContentsMargins(12, 10, 12, 10);
    fragLay->setSpacing(6);

    auto* fragTitle = new QLabel("Memory Composition");
    fragTitle->setStyleSheet(QString("color:%1; font-size:11px; font-weight:700;").arg(Theme::TEXT_SECONDARY));
    fragLay->addWidget(fragTitle);

    fragBar = new FragBar();
    fragLay->addWidget(fragBar);

    // Legend row
    auto* fragLegend = new QHBoxLayout();
    auto addLegend = [&](const QString& color, const QString& text) {
        auto* dot = new QLabel("●");
        dot->setStyleSheet(QString("color:%1; font-size:10px;").arg(color));
        auto* lbl = new QLabel(text);
        lbl->setStyleSheet(QString("color:%1; font-size:10px;").arg(Theme::TEXT_SECONDARY));
        fragLegend->addWidget(dot);
        fragLegend->addWidget(lbl);
        fragLegend->addSpacing(8);
    };
    addLegend("#4F6EF7", "Used");
    addLegend("#F59E0B", "Waste");
    addLegend("#475569", "Free");
    fragLegend->addStretch();
    fragLay->addLayout(fragLegend);

    statsLabel = new QLabel("4 MB sandbox — no blocks yet");
    statsLabel->setWordWrap(true);
    statsLabel->setStyleSheet(QString("color:%1; font-size:10px; font-family:Consolas;").arg(Theme::TEXT_SECONDARY));
    fragLay->addWidget(statsLabel);

    leftLay->addWidget(fragCard);

    // Status
    statusLabel = new QLabel("Starting worker…");
    statusLabel->setStyleSheet(QString(
        "color:%1; font-size:10px; padding:2px 0;"
    ).arg(Theme::TEXT_MUTED));
    leftLay->addWidget(statusLabel);

    // ── Guided challenge panel ───────────────────────────────────────────────
    challengePanel = new ChallengePanel();
    leftLay->addWidget(challengePanel);

    leftLay->addStretch();

    // ══ RIGHT SIDE: canvas (top) + table (bottom) ══════════════════════════
    auto* right = new QWidget();
    auto* rightLay = new QVBoxLayout(right);
    rightLay->setContentsMargins(0, 0, 0, 0);
    rightLay->setSpacing(8);

    // Canvas header
    auto* canvasHeader = new QHBoxLayout();
    auto* canvasTitle  = new QLabel("Memory Map  —  4 MB sandbox");
    canvasTitle->setStyleSheet(QString(
        "color:%1; font-size:12px; font-weight:700;"
    ).arg(Theme::TEXT_PRIMARY));
    auto* canvasHint = new QLabel("Each cell = 1 KB  ·  scroll to zoom  ·  drag to pan  ·  click to inspect");
    canvasHint->setStyleSheet(QString("color:%1; font-size:10px;").arg(Theme::TEXT_MUTED));
    canvasHeader->addWidget(canvasTitle);
    canvasHeader->addStretch();
    canvasHeader->addWidget(canvasHint);
    rightLay->addLayout(canvasHeader);

    canvas = new MemoryCanvas();
    canvas->setMinimumHeight(320);
    rightLay->addWidget(canvas, 3);

    // Page/seg/frame table panel below canvas
    tablePanel = new TablePanel();
    tablePanel->setMinimumHeight(150);
    tablePanel->setMaximumHeight(220);
    rightLay->addWidget(tablePanel, 1);

    // ── Allocation history table ─────────────────────────────────────────────
    auto* memTableCard = new QWidget();
    memTableCard->setStyleSheet(Theme::card());
    auto* mtl = new QVBoxLayout(memTableCard);
    mtl->setContentsMargins(0, 0, 0, 0);
    mtl->setSpacing(0);
    memTable = new MemoryTableWidget();
    memTable->setMinimumHeight(120);
    memTable->setMaximumHeight(200);
    mtl->addWidget(memTable);
    rightLay->addWidget(memTableCard, 1);

    // ── Time Scrubber ────────────────────────────────────────────────────────
    auto* scrubCard = new QWidget();
    scrubCard->setStyleSheet(Theme::card());
    auto* sl2 = new QVBoxLayout(scrubCard);
    sl2->setContentsMargins(10, 8, 10, 8);
    timeScrubber = new TimeScrubber();
    sl2->addWidget(timeScrubber);
    rightLay->addWidget(scrubCard);

    root->addWidget(left);
    root->addWidget(right, 1);

    // ── Wrap in QScrollArea so content is never clipped on small windows ─────
    auto* scroll = new QScrollArea(this);
    scroll->setWidget(content);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setStyleSheet("QScrollArea{border:none;background:transparent;}" + Theme::scrollbar());
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->addWidget(scroll);

    // ── Wire signals ────────────────────────────────────────────────────────
    connect(modeBox,     QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MemoryLab::onModeChanged);
    connect(rawBtn,      &QPushButton::clicked, this, &MemoryLab::onAllocRaw);
    connect(llBtn,       &QPushButton::clicked, this, &MemoryLab::onAllocLL);
    connect(arrBtn,      &QPushButton::clicked, this, &MemoryLab::onAllocArray);
    connect(treeBtn,     &QPushButton::clicked, this, &MemoryLab::onAllocTree);
    connect(hashBtn,     &QPushButton::clicked, this, &MemoryLab::onAllocHash);
    connect(freeBtn,     &QPushButton::clicked, this, &MemoryLab::onFreeSelected);
    connect(resetBtn,    &QPushButton::clicked, this, &MemoryLab::onReset);
    connect(refreshBtn,  &QPushButton::clicked, this, &MemoryLab::onRefreshTable);
    connect(zIn,         &QPushButton::clicked, canvas, &MemoryCanvas::zoomIn);
    connect(zOut,        &QPushButton::clicked, canvas, &MemoryCanvas::zoomOut);
    connect(z1,          &QPushButton::clicked, canvas, &MemoryCanvas::resetZoom);
    connect(canvas, &MemoryCanvas::blockClicked, this, &MemoryLab::onBlockClicked);
    connect(canvas, &MemoryCanvas::emptyClicked, this, [this]() {
        selectedId = -1;
        freeBtn->setEnabled(false);
        canvas->setSelectedId(-1);
        inspectLabel->setText("Click a block on the canvas →");
    });

    // Forward challenge completions to the Explainer panel
    connect(challengePanel, &ChallengePanel::challengeExplanationNeeded,
            this, &MemoryLab::explanationNeeded);

    // Driver
    driver = new MemoryLabDriver(this);
    connect(driver, &MemoryLabDriver::workerReady,    this, &MemoryLab::onWorkerReady);
    connect(driver, &MemoryLabDriver::workerPidKnown, this, &MemoryLab::onWorkerPidKnown);
    connect(driver, &MemoryLabDriver::workerDied,     this, &MemoryLab::onWorkerDied);
    connect(driver, &MemoryLabDriver::arenaUpdated,   this, &MemoryLab::onArenaUpdated);
    connect(driver, &MemoryLabDriver::structDefined,  this, &MemoryLab::onStructDefined);
    connect(driver, &MemoryLabDriver::commandFailed,  this, &MemoryLab::onCommandFailed);
    connect(driver, &MemoryLabDriver::pageTableReady, this, &MemoryLab::onPageTableReady);
    connect(driver, &MemoryLabDriver::segTableReady,  this, &MemoryLab::onSegTableReady);
    connect(driver, &MemoryLabDriver::frameTableReady,this, &MemoryLab::onFrameTableReady);

    // ── Wire MemoryTableWidget + TimeScrubber ────────────────────────────────
    connect(driver, &MemoryLabDriver::arenaUpdated,
            this, [this](std::vector<ArenaBlock> blocks, ArenaSummary /*s*/) {
                memTable->onArenaUpdated(blocks);
                timeScrubber->setLog(&driver->eventLog());
            });
    connect(driver, &MemoryLabDriver::eventLogged,
            this, [this](AllocEvent ev) {
                memTable->onEventLogged(ev);
                timeScrubber->setLog(&driver->eventLog());
            });
    connect(driver, &MemoryLabDriver::workerDied, memTable, &MemoryTableWidget::clear);
    connect(timeScrubber, &TimeScrubber::snapshotReady,
            this, [this](std::vector<ArenaBlock> blocks, ArenaSummary s) {
                if (!timeScrubber->isLive()) {
                    canvas->setBlocks(blocks, sandboxSize);
                    onArenaUpdated(blocks, s);
                }
            });
    connect(timeScrubber, &TimeScrubber::resumedLive, this, [this]() {
        // Re-arm live updates when user scrubs back to the live end
        // (next arenaUpdated from driver will repaint the canvas)
    });
}

MemoryLab::~MemoryLab() { driver->stop(); }

void MemoryLab::ensureStarted() {
    if (!driver->isRunning()) {
        statusLabel->setText("Starting worker…");
        driver->start();
    }
}

// ── slots ──────────────────────────────────────────────────────────────────

void MemoryLab::onWorkerReady(long bytes) {
    sandboxSize = bytes;
    statusLabel->setText(QString("Sandbox ready  ·  %1 KB  ·  PID —").arg(bytes / 1024));
}

void MemoryLab::onWorkerPidKnown(pid_t pid) {
    statusLabel->setText(QString("Worker PID %1  ·  sandbox ready  ·  4 MB").arg(pid));
    emit explanationNeeded(
        "<b>Memory Lab — Ready</b><br><br>"
        "A real <code>mmap(MAP_ANONYMOUS)</code> sandbox is running in worker PID "
        + QString::number(pid) + ". Every allocation carves real bytes from it.<br><br>"
        "<b>🎯 Follow the Challenge panel</b> on the left — it guides you through "
        "7 hands-on tasks, each revealing a key OS memory concept:<br><br>"
        "1. First allocation<br>"
        "2. External fragmentation<br>"
        "3. Out-of-memory<br>"
        "4. Paging + page table<br>"
        "5. Internal fragmentation<br>"
        "6. Segmentation<br>"
        "7. Linked list layout in memory<br><br>"
        "Each completed challenge explains the underlying OS concept in the "
        "<b>Explainer panel</b> (here). You can also explore freely — "
        "click any block on the canvas to inspect it."
    );
}

void MemoryLab::onWorkerDied() {
    statusLabel->setText("⚠ Worker exited. Reset or restart.");
    selectedId = -1;
    freeBtn->setEnabled(false);
    canvas->setSelectedId(-1);
}

void MemoryLab::onArenaUpdated(std::vector<ArenaBlock> blocks, ArenaSummary s) {
    canvas->setBlocks(blocks, sandboxSize);

    // Refresh selection validity
    bool live = false;
    for (const auto& b : blocks) if (b.id == selectedId) { live = true; break; }
    if (!live) { selectedId = -1; freeBtn->setEnabled(false); canvas->setSelectedId(-1); }

    // Fragmentation bar
    fragBar->update(s);

    // Stats — show fragmentation-relevant numbers
    long pct = s.totalBytes > 0 ? (s.usedBytes * 100 / s.totalBytes) : 0;
    QString statsText = QString(
        "Used: %1 KB / %2 KB  (%3%)\n"
        "Free: %4 KB  ·  Blocks: %5"
    ).arg(s.usedBytes / 1024).arg(s.totalBytes / 1024).arg(pct)
     .arg(s.freeBytes / 1024).arg(s.blockCount);

    if (s.holeCount > 0) {
        statsText += QString("\nHoles: %1  ·  Largest: %2 KB")
                        .arg(s.holeCount)
                        .arg(s.largestHole / 1024);
    }
    if (s.internalWaste > 0) {
        statsText += QString("\nWaste: %1 KB (page rounding)").arg(s.internalWaste / 1024);
    }
    statsLabel->setText(statsText);

    // Auto-refresh the page/seg/frame table on every alloc
    refreshTableForMode();

    // Update challenge panel
    challengePanel->onArenaUpdated(s, s.blockCount,
        currentMode == "contiguous",
        visitedPaged,
        visitedSegmented);
}

void MemoryLab::onStructDefined(ArenaBlock block, std::vector<StructNode> nodes) {
    canvas->setStructNodes(block.id, nodes, block.structType);
    EventBus::get().memoryAllocated(driver->workerPid(), block.size,
                                    QString("[%1] %2").arg(block.structType, block.label));
}

void MemoryLab::onCommandFailed(QString reason) {
    statusLabel->setText("⚠ " + reason);
}

void MemoryLab::onPageTableReady(std::vector<PageEntry> entries) {
    tablePanel->showPageTable(entries);
}
void MemoryLab::onSegTableReady(std::vector<SegEntry> entries) {
    tablePanel->showSegTable(entries);
}
void MemoryLab::onFrameTableReady(std::vector<FrameEntry> entries) {
    tablePanel->showFrameTable(entries);
}

void MemoryLab::onAllocRaw() {
    long sz = rawSizeSpin->value();
    driver->alloc(sz);
    EventBus::get().memoryAllocated(driver->workerPid(), sz, "raw");
}

void MemoryLab::onAllocLL() {
    driver->allocLL(structNSpin->value());
}
void MemoryLab::onAllocArray() {
    driver->allocArray(structNSpin->value(), structESizeSpin->value());
}
void MemoryLab::onAllocTree() {
    driver->allocTree(structNSpin->value());
}
void MemoryLab::onAllocHash() {
    driver->allocHash(structNSpin->value());
}

void MemoryLab::onFreeSelected() {
    if (selectedId < 0) return;
    driver->freeBlock(selectedId);
    canvas->clearStructOverlays(); // overlays for this block will be removed on next arenaUpdated
    selectedId = -1;
    freeBtn->setEnabled(false);
    canvas->setSelectedId(-1);
    inspectLabel->setText("Block freed.");
}

void MemoryLab::onReset() {
    driver->resetArena();
    canvas->clearStructOverlays();
    selectedId = -1;
    freeBtn->setEnabled(false);
    canvas->setSelectedId(-1);
    tablePanel->clear();
    inspectLabel->setText("Click a block on the canvas →");
}

void MemoryLab::onModeChanged(int) {
    currentMode = modeBox->currentData().toString();
    if (currentMode == "paged" || currentMode == "framed") visitedPaged = true;
    if (currentMode == "segmented") visitedSegmented = true;
    driver->setMode(currentMode);
    canvas->clearStructOverlays();
    selectedId = -1;
    freeBtn->setEnabled(false);
    canvas->setSelectedId(-1);
    tablePanel->clear();
    emitExplanation(currentMode);
    // Auto-refresh table after a short delay (worker processes MODE asynchronously)
    QTimer::singleShot(350, this, &MemoryLab::refreshTableForMode);
}

void MemoryLab::onBlockClicked(int id) {
    selectedId = id;
    freeBtn->setEnabled(true);
    canvas->setSelectedId(id);

    // For immediate feedback, scan the event log for the most recent Alloc:
    for (const auto& ev : driver->eventLog()) {
        if (ev.blockId == id && ev.op == AllocEvent::Op::Alloc) {
            // Build a temporary block for display
            ArenaBlock tmp;
            tmp.id     = id;
            tmp.offset = ev.offset;
            tmp.size   = ev.size;
            tmp.label  = ev.label;
            updateInspect(&tmp);
            return;
        }
    }
    inspectLabel->setText(QString("Block #%1 selected").arg(id));
}

void MemoryLab::onRefreshTable() {
    refreshTableForMode();
}

void MemoryLab::updateInspect(const ArenaBlock* b, const ArenaSummary* /*s*/) {
    if (!b) { inspectLabel->setText("Click a block on the canvas →"); return; }

    // Compute page-rounded waste for this block when in paged mode
    long pageWaste = 0;
    if (currentMode == "paged" || currentMode == "framed") {
        constexpr long PAGE = 4096;
        long rounded = ((b->size + PAGE - 1) / PAGE) * PAGE;
        pageWaste = rounded - b->size;
    }

    QString shortInfo = QString(
        "#%1  %2\n"
        "Offset: 0x%3\n"
        "Size:   %4 KB (%5 B)"
    ).arg(b->id)
     .arg(b->label)
     .arg(b->offset, 6, 16, QChar('0'))
     .arg(b->size / 1024.0, 0, 'f', 1)
     .arg(b->size);

    if (b->isStruct)
        shortInfo += QString("\nType: [%1]  n=%2  elem=%3 B")
                        .arg(b->structType).arg(b->elemCount).arg(b->elemSize);
    if (pageWaste > 0)
        shortInfo += QString("\nWaste: %1 B (page rounding)").arg(pageWaste);

    inspectLabel->setText(shortInfo);

    // Build the rich Explainer panel content
    QString structDetail;
    if (b->isStruct) {
        static const QMap<QString, QString> structExplain = {
            {"LL",
             "A <b>linked list</b> where each node holds an 8-byte next-pointer "
             "and 24 bytes of data (total 32 B/node). The canvas draws arrows "
             "following the <code>next</code> pointers. Traversal is O(n) and "
             "cache-unfriendly because nodes in a real heap would be scattered."},
            {"ARRAY",
             "A <b>contiguous array</b> of equal-sized elements. Index i lives at "
             "<code>base + i × elemSize</code> — O(1) random access, excellent cache "
             "locality. This is why <code>std::vector</code> usually outperforms linked "
             "lists in benchmarks."},
            {"TREE",
             "A <b>complete binary tree</b> stored in BFS order: node i has left child "
             "at 2i+1 and right child at 2i+2. Each node stores 8-byte left/right "
             "pointers plus a 4-byte key. Searching costs O(log n) comparisons. "
             "The canvas shows both child pointers as arrows."},
            {"HASH",
             "A <b>hash map</b> with a 64-byte metadata header (bucket count, load factor) "
             "followed by an array of 16-byte buckets. At 0.75 load factor (default), "
             "¾ of buckets are occupied before resizing — a classic space-vs-speed trade-off."},
        };
        structDetail = structExplain.value(b->structType,
            QString("Structure [%1]: %2 elements × %3 B each.")
                .arg(b->structType).arg(b->elemCount).arg(b->elemSize));
    } else {
        structDetail = "Raw anonymous allocation — <code>memset</code> to zero on creation. "
                       "In a real allocator these bytes would be in a free-list bucket or "
                       "returned from a <code>brk()</code>/<code>mmap()</code> call.";
    }

    QString wasteNote;
    if (pageWaste > 0) {
        wasteNote = QString(
            "<br><br><b>Internal fragmentation:</b> this block needed %1 B but got "
            "%2 B (%3 pages). The extra <b>%4 B</b> are wasted inside the allocation — "
            "they count as yours but contain no useful data. This is the cost of paging."
        ).arg(b->size).arg(b->size + pageWaste)
         .arg((b->size + pageWaste) / 4096).arg(pageWaste);
    }

    QString alignNote;
    if (!b->isStruct && b->size % 16 != 0) {
        alignNote = QString("<br><br><b>Alignment:</b> requested %1 B, stored as %2 B "
                            "(rounded to 16-byte alignment boundary). "
                            "Alignment waste = %3 B.")
                        .arg(b->size).arg(((b->size + 15) / 16) * 16)
                        .arg(((b->size + 15) / 16) * 16 - b->size);
    }

    emit explanationNeeded(QString(
        "<b>Block #%1 — %2</b><br><br>"
        "Offset in sandbox: <code>0x%3</code> &nbsp;(%4 KB from base)<br>"
        "Size: <b>%5 KB</b> &nbsp;(%6 bytes)<br>"
        "Permissions: RW (anonymous, no file backing)<br><br>"
        "%7%8%9"
    ).arg(b->id)
     .arg(b->label.toHtmlEscaped())
     .arg(b->offset, 0, 16)
     .arg(b->offset / 1024)
     .arg(b->size / 1024.0, 0, 'f', 1)
     .arg(b->size)
     .arg(structDetail)
     .arg(wasteNote)
     .arg(alignNote)
    );
}

void MemoryLab::refreshTableForMode() {
    if (!driver->isRunning()) return;
    if (currentMode == "paged")     driver->queryPageTable();
    else if (currentMode == "segmented") driver->querySegTable();
    else if (currentMode == "framed")    driver->queryFrameTable();
    else tablePanel->clear();
}

void MemoryLab::emitExplanation(const QString& mode) {
    static const QMap<QString, QString> explanations = {
        {"contiguous",
         "<b>Contiguous Allocation — First-Fit Free List</b><br><br>"
         "All blocks are carved from a single flat 4 MB region. The allocator keeps a "
         "free list; on each request it scans from the start and picks the first hole "
         "that fits (first-fit policy).<br><br>"
         "<b>What to do:</b> allocate 3–4 blocks of equal size, then free the ones in "
         "the middle. Watch the fragmentation bar show multiple holes. Try to allocate "
         "something larger than any single hole — it will fail even though total free > 0. "
         "That is <b>external fragmentation</b>.<br><br>"
         "<b>Real world:</b> <code>ptmalloc</code> (glibc) uses bins of same-size "
         "free chunks and coalesces adjacent free blocks to fight this."},
        {"paged",
         "<b>Paged Allocation — 4 KB Pages + Page Table</b><br><br>"
         "The 4 MB sandbox is split into <b>1024 pages of 4096 bytes each</b>. "
         "Every allocation claims a whole number of pages; partial pages are wasted.<br><br>"
         "<b>What to do:</b> allocate a block whose size is <i>not</i> a multiple of 4096 "
         "(e.g. 5000 B). The stats bar will show orange waste. Then click ⟳ Table — "
         "the page table updates automatically and you can see which VPNs are "
         "marked Present and which block owns them.<br><br>"
         "<b>Key insight:</b> paging eliminates external fragmentation but introduces "
         "<i>internal</i> fragmentation. The MMU walks this table on every memory "
         "access; in hardware it is cached in the TLB."},
        {"segmented",
         "<b>Segmented Allocation — Named Logical Regions</b><br><br>"
         "The 4 MB sandbox is pre-partitioned into <b>8 equal segments</b>: "
         "<code>code, data, heap, stack, bss, tls, extra1, extra2</code>. "
         "Each segment has a base address and a limit.<br><br>"
         "<b>What to do:</b> make 3–4 allocations. The table (auto-refreshes) shows "
         "which segment each block owns and how much of its partition is used. "
         "Free a block and see the segment reset to its full partition limit.<br><br>"
         "<b>Real world:</b> x86-64 Linux uses flat segmentation (all bases = 0) for "
         "user code but uses the <code>FS</code> segment exclusively for thread-local "
         "storage — exactly what the <code>tls</code> slot here represents."},
        {"framed",
         "<b>Frame-Based Allocation — Physical Frame Table</b><br><br>"
         "Functionally identical to Paged but the primary data structure is the "
         "<b>frame table</b> rather than the page table. Each row tracks one 4 KB "
         "physical frame: Free/Occupied + owning block ID.<br><br>"
         "<b>What to do:</b> allocate several blocks of various sizes, then click "
         "⟳ Table. Notice how each block claims a run of contiguous frames. "
         "Free one and watch those frames return to Free.<br><br>"
         "<b>Key insight:</b> the OS kernel thinks in frames (physical). "
         "When the kernel's <b>page reclaim</b> daemon (<code>kswapd</code>) needs "
         "to evict pages, it scans the frame table looking for frames to steal."}
    };
    emit explanationNeeded(explanations.value(mode,
        "<b>Memory Lab</b><br>Select an allocation technique to learn how it works."));
}
