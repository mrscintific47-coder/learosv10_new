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

    // ══ LEFT PANEL (controls + inspect + table) ════════════════════════════
    auto* left = new QWidget();
    left->setFixedWidth(280);
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
    rawSizeSpin->setValue(4096);
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
    inspectLabel->setStyleSheet(QString("color:%1; font-size:11px; font-family:Consolas;").arg(Theme::TEXT_PRIMARY));
    inspLay->addWidget(inspTitle);
    inspLay->addWidget(inspectLabel);
    leftLay->addWidget(inspCard);

    // Stats
    statsLabel = new QLabel("4 MB sandbox — no blocks yet");
    statsLabel->setWordWrap(true);
    statsLabel->setStyleSheet(QString("color:%1; font-size:11px;").arg(Theme::TEXT_SECONDARY));
    leftLay->addWidget(statsLabel);

    // Status
    statusLabel = new QLabel("Starting worker…");
    statusLabel->setStyleSheet(QString("color:%1; font-size:10px;").arg(Theme::TEXT_MUTED));
    leftLay->addWidget(statusLabel);

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
    statusLabel->setText(QString("Worker PID %1  ·  sandbox = 4 MB").arg(pid));
    emit explanationNeeded(
        "<b>Memory Lab — 4 MB Sandbox</b><br><br>"
        "You have a real 4 MB <code>mmap(MAP_ANONYMOUS)</code> region. "
        "Every allocation you make carves a piece from this sandbox at a real address.<br><br>"
        "<b>Canvas:</b> each cell = 1 KB. Coloured blocks show live allocations. "
        "Free cells stay dark. Scroll to zoom in to byte level.<br><br>"
        "<b>Techniques:</b><br>"
        "• <b>Contiguous</b> — first-fit free list, holes form on free<br>"
        "• <b>Paged</b> — sandbox split into 1024 × 4 KB pages, page table shown<br>"
        "• <b>Segmented</b> — 8 named segments (code/data/heap/…), segment table shown<br>"
        "• <b>Framed</b> — explicit physical frame table, same as paged but frame-centric<br><br>"
        "<b>Data structures:</b> Linked List, Array, Binary Tree, Hash Map — each allocates "
        "real bytes and draws pointer arrows between nodes on the canvas."
    );
}

void MemoryLab::onWorkerDied() {
    statusLabel->setText("⚠ Worker exited. Reset or restart.");
    selectedId = -1;
    freeBtn->setEnabled(false);
    canvas->setSelectedId(-1);
}

void MemoryLab::onArenaUpdated(std::vector<ArenaBlock> blocks, ArenaSummary s) {
    // Preserve struct metadata from previous snapshot
    for (auto& nb : blocks) {
        // Canvas will have the overlay; the driver doesn't re-emit STRUCTDEF on STATUS
        // so we just keep whatever the driver has in pendingStructBlock if a prior
        // structDefined was emitted. Nothing to do here.
        (void)nb;
    }

    canvas->setBlocks(blocks, sandboxSize);

    // Refresh selection validity
    bool live = false;
    for (const auto& b : blocks) if (b.id == selectedId) { live = true; break; }
    if (!live) { selectedId = -1; freeBtn->setEnabled(false); canvas->setSelectedId(-1); }

    // Stats
    long pct = s.totalBytes > 0 ? (s.usedBytes * 100 / s.totalBytes) : 0;
    statsLabel->setText(QString(
        "Used: %1 KB / %2 KB  (%3%)  ·  Free: %4 KB  ·  Blocks: %5  ·  Mode: %6"
    ).arg(s.usedBytes / 1024).arg(s.totalBytes / 1024).arg(pct)
     .arg(s.freeBytes / 1024).arg(s.blockCount).arg(s.modeName));
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
    driver->setMode(currentMode);
    canvas->clearStructOverlays();
    selectedId = -1;
    freeBtn->setEnabled(false);
    canvas->setSelectedId(-1);
    tablePanel->clear();
    emitExplanation(currentMode);
    // Auto-refresh table after a short delay (worker processes MODE asynchronously)
    QTimer::singleShot(300, this, &MemoryLab::refreshTableForMode);
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

void MemoryLab::updateInspect(const ArenaBlock* b) {
    if (!b) { inspectLabel->setText("Click a block on the canvas →"); return; }
    inspectLabel->setText(QString(
        "#%1  %2\n"
        "Offset: 0x%3\n"
        "Size:   %4 KB (%5 B)\n"
        "%6"
    ).arg(b->id)
     .arg(b->label)
     .arg(b->offset, 6, 16, QChar('0'))
     .arg(b->size / 1024.0, 0, 'f', 1)
     .arg(b->size)
     .arg(b->isStruct ? QString("Type: [%1]  n=%2  elem=%3 B")
                           .arg(b->structType).arg(b->elemCount).arg(b->elemSize)
                      : QString("(raw allocation)")));

    emit explanationNeeded(QString(
        "<b>Block #%1 — %2</b><br><br>"
        "Offset in sandbox: <code>0x%3</code> (%4 KB from start)<br>"
        "Size: <b>%5 KB</b> (%6 bytes)<br><br>"
        "%7"
    ).arg(b->id)
     .arg(b->label.toHtmlEscaped())
     .arg(b->offset, 0, 16)
     .arg(b->offset / 1024)
     .arg(b->size / 1024.0, 0, 'f', 1)
     .arg(b->size)
     .arg(b->isStruct
         ? QString("Structure type: <b>[%1]</b><br>%2 elements × %3 bytes each<br><br>"
                   "Each element sits at a real address inside the sandbox. "
                   "The canvas shows pointer arrows between nodes.")
               .arg(b->structType).arg(b->elemCount).arg(b->elemSize)
         : "Raw allocation — bytes are zeroed and ready to use.")
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
         "<b>Contiguous Allocation</b><br><br>"
         "All blocks come from a single flat region using a first-fit free list. "
         "Freed blocks leave holes. When a new allocation can't find a large enough "
         "hole, you get fragmentation even if total free space exceeds the request.<br><br>"
         "This is the simplest model — exactly what a basic malloc() does inside a "
         "pre-mapped region."},
        {"paged",
         "<b>Paged Allocation</b><br><br>"
         "The 4 MB sandbox is divided into <b>1024 pages of 4 KB each</b>. "
         "Every allocation rounds up to a whole number of pages. "
         "The <b>page table</b> (visible below the canvas) maps virtual page numbers "
         "→ physical frame numbers.<br><br>"
         "Paging eliminates external fragmentation (holes between blocks) at the cost "
         "of internal fragmentation (unused bytes at the end of the last page)."},
        {"segmented",
         "<b>Segmented Allocation</b><br><br>"
         "The sandbox is divided into <b>8 named segments</b>: code, data, heap, stack, "
         "bss, tls, extra1, extra2 — each with a base address and a limit.<br><br>"
         "Allocations fill segment slots round-robin. The <b>segment table</b> shows "
         "each segment's base + limit + owning block.<br><br>"
         "Segmentation maps naturally to program structure (separate code vs data) "
         "but can waste space if a segment is much larger than the allocation."},
        {"framed",
         "<b>Frame-based Allocation</b><br><br>"
         "Same physical-frame semantics as paged, but the <b>frame table</b> "
         "is the primary data structure — it tracks which physical frames are "
         "free or occupied and by which block.<br><br>"
         "This is the OS's perspective: it cares about physical frames, not virtual "
         "pages. The frame table is what the kernel scans when it needs to reclaim "
         "memory (page eviction)."}
    };
    emit explanationNeeded(explanations.value(mode,
        "<b>Memory Lab</b><br>Select an allocation technique to learn how it works."));
}
