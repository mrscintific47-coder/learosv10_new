#include "MemoryInspector.h"
#include "Theme.h"
#include <QMouseEvent>
#include <QWheelEvent>
#include <QToolTip>
#include <QPushButton>
#include <QPainterPath>
#include <fstream>
#include <sstream>
#include <cmath>
#include <algorithm>

// ── MemMapWidget ──────────────────────────────────────────────────────────────

MemMapWidget::MemMapWidget(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(120);
    setMouseTracking(true); // needed for hover tooltips
    setStyleSheet(QString(
        "background: #0F172A; border-radius: 10px; border: 1px solid %1;"
    ).arg(Theme::BORDER));
}

void MemMapWidget::setRegions(const std::vector<MemRegion>& r, long total) {
    regions = r;
    totalKB = total > 0 ? total : 1;
    clampPan();
    update();
}

void MemMapWidget::zoomIn() {
    zoomFactor = std::min(zoomFactor * 1.5, 64.0);
    clampPan();
    update();
}

void MemMapWidget::zoomOut() {
    zoomFactor = std::max(zoomFactor / 1.5, 1.0);
    clampPan();
    update();
}

void MemMapWidget::resetZoom() {
    zoomFactor = 1.0;
    panOffset  = 0;
    update();
}

void MemMapWidget::clampPan() {
    // Maximum pan = extra content width introduced by zoom
    int contentW = (int)(width() * zoomFactor);
    int maxPan   = std::max(0, contentW - width());
    panOffset    = std::max(0, std::min(panOffset, maxPan));
}

QColor MemMapWidget::regionColor(const MemRegion& r) const {
    if (r.label.find("[heap]")  != std::string::npos) return QColor("#F87171");
    if (r.label.find("[stack]") != std::string::npos) return QColor("#4ADE80");
    if (r.label.find("[vdso]")  != std::string::npos) return QColor("#C084FC");
    if (r.perms.find('x')       != std::string::npos) return QColor("#60A5FA");
    if (r.perms.find('w')       != std::string::npos) return QColor("#FBBF24");
    return QColor("#334155");
}

const MemRegion* MemMapWidget::regionAt(QPoint pos) const {
    for (int i = 0; i < (int)rects.size() && i < (int)regions.size(); i++) {
        if (rects[i].contains(pos)) return &regions[i];
    }
    return nullptr;
}

void MemMapWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QPainterPath bg;
    bg.addRoundedRect(rect(), 10, 10);
    p.fillPath(bg, QColor("#0F172A"));

    if (regions.empty()) {
        p.setPen(QColor("#475569"));
        p.setFont(QFont("Segoe UI", 10));
        p.drawText(rect(), Qt::AlignCenter,
            "Click a sandbox process\nto inspect its memory map");
        return;
    }

    rects.clear();
    const int topPad = 8, botPad = 22, sidePad = 8;
    int barH   = height() - topPad - botPad;
    // Effective width is stretched by zoomFactor; panOffset shifts left.
    int totalW = (int)((width() - sidePad * 2) * zoomFactor);

    long total = 0;
    for (auto& r : regions) total += r.sizeKB > 0 ? r.sizeKB : 1;

    int x = sidePad - panOffset;
    for (auto& r : regions) {
        long sz = r.sizeKB > 0 ? r.sizeKB : 1;
        int  bw = std::max(3, (int)((double)sz / total * totalW));
        QRect block(x, topPad, bw - 1, barH);

        // Only paint if at least partially visible
        if (block.right() >= sidePad && block.left() <= width() - sidePad) {
            QColor color = regionColor(r);
            QPainterPath bp;
            bp.addRoundedRect(block, 3, 3);

            bool isSpecial = r.label.find("[heap]")  != std::string::npos ||
                             r.label.find("[stack]") != std::string::npos;
            if (isSpecial) {
                QColor glow = color; glow.setAlpha(60);
                p.fillPath(bp, glow);
            }
            p.fillPath(bp, color);

            if (bw > 28) {
                p.setPen(QColor(0, 0, 0, 180));
                p.setFont(QFont("Consolas", 7, QFont::Bold));
                QString lbl = QString::fromStdString(r.label).split('/').last();
                if (lbl.isEmpty()) lbl = QString::fromStdString(r.perms.substr(0, 3));
                p.drawText(block, Qt::AlignCenter, lbl.left(7));
            }
        }

        rects.push_back(block);
        x += bw;
    }

    // Legend bar
    int lx = sidePad, ly = height() - botPad + 4;
    p.setFont(QFont("Segoe UI", 7));
    auto legend = [&](QColor c, const QString& label) {
        QPainterPath lp; lp.addRoundedRect(lx, ly, 8, 8, 2, 2);
        p.fillPath(lp, c);
        p.setPen(QColor("#94A3B8"));
        p.drawText(lx + 10, ly + 8, label);
        lx += 10 + p.fontMetrics().horizontalAdvance(label) + 8;
    };
    legend(QColor("#F87171"), "heap");
    legend(QColor("#4ADE80"), "stack");
    legend(QColor("#60A5FA"), "code");
    legend(QColor("#FBBF24"), "data");
    legend(QColor("#334155"), "ro");

    // Zoom indicator
    if (zoomFactor > 1.01) {
        p.setPen(QColor("#94A3B8"));
        p.setFont(QFont("Segoe UI", 7));
        p.drawText(width() - 60, ly + 8, QString("%1×").arg(zoomFactor, 0, 'f', 1));
    }
}

void MemMapWidget::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        // Could be start of a pan — record position; emit click on release if delta is small
        panning          = true;
        panStartX        = e->pos().x();
        panOffsetAtStart = panOffset;
    }
}

void MemMapWidget::mouseMoveEvent(QMouseEvent* e) {
    if (panning && (e->buttons() & Qt::LeftButton)) {
        int delta = panStartX - e->pos().x();
        panOffset = panOffsetAtStart + delta;
        clampPan();
        update();
        return;
    }

    // Hover tooltip
    const MemRegion* r = regionAt(e->pos());
    if (r) {
        QString tip = QString(
            "<b>0x%1 – 0x%2</b>  (%3 KB)<br>"
            "Perms: <code>%4</code><br>"
            "%5"
        ).arg(r->start, 0, 16)
         .arg(r->end,   0, 16)
         .arg(r->sizeKB)
         .arg(QString::fromStdString(r->perms))
         .arg(r->label.empty() ? "(anonymous)" : QString::fromStdString(r->label));
        QToolTip::showText(e->globalPosition().toPoint(), tip, this);
    } else {
        QToolTip::hideText();
    }
}

void MemMapWidget::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && panning) {
        int delta = std::abs(e->pos().x() - panStartX);
        panning = false;
        if (delta < 5) {
            // Treat as a click, not a pan
            const MemRegion* r = regionAt(e->pos());
            if (r) emit regionClicked(*r);
        }
    }
}

void MemMapWidget::wheelEvent(QWheelEvent* e) {
    int ticks = e->angleDelta().y();
    if (ticks == 0) return;

    // Zoom toward the cursor position
    double oldZoom = zoomFactor;
    if (ticks > 0) zoomFactor = std::min(zoomFactor * 1.25, 64.0);
    else           zoomFactor = std::max(zoomFactor / 1.25, 1.0);

    // Adjust pan so the point under the cursor stays fixed
    int cursorX = e->position().toPoint().x();
    panOffset   = (int)((panOffset + cursorX) * (zoomFactor / oldZoom)) - cursorX;
    clampPan();
    update();
    e->accept();
}

// ── MemoryInspector ───────────────────────────────────────────────────────────

MemoryInspector::MemoryInspector(QWidget* parent) : QWidget(parent) {
    setStyleSheet(QString("background:%1;").arg(Theme::BG_APP));
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(16, 16, 16, 16);
    outer->setSpacing(12);

    // ── Title ─────────────────────────────────────────────────────────────────
    auto* titleRow = new QHBoxLayout();
    titleLabel = new QLabel("🧠  Memory Inspector");
    titleLabel->setStyleSheet(QString(
        "color:%1; font-size:14px; font-weight:bold;"
    ).arg(Theme::TEXT_PRIMARY));
    auto* chip = new QLabel("● /proc/maps");
    chip->setStyleSheet(QString(
        "color:%1; background:%2; border-radius:8px; padding:3px 10px;"
        "font-size:10px; font-weight:bold;"
    ).arg(Theme::BLUE).arg(Theme::BLUE_LIGHT));
    titleRow->addWidget(titleLabel);
    titleRow->addStretch();
    titleRow->addWidget(chip);
    outer->addLayout(titleRow);

    // ── Stats card ────────────────────────────────────────────────────────────
    auto* statsCard = new QWidget();
    statsCard->setStyleSheet(Theme::card());
    auto* sl = new QVBoxLayout(statsCard);
    sl->setContentsMargins(14, 10, 14, 10);
    sl->setSpacing(4);

    statsLabel = new QLabel("No process selected — click a sandbox process.");
    statsLabel->setStyleSheet(QString("color:%1; font-size:11px;").arg(Theme::TEXT_SECONDARY));
    sl->addWidget(statsLabel);
    outer->addWidget(statsCard);

    // ── Map card ──────────────────────────────────────────────────────────────
    auto* mapCard = new QWidget();
    mapCard->setStyleSheet(Theme::card());
    auto* ml = new QVBoxLayout(mapCard);
    ml->setContentsMargins(14, 12, 14, 12);
    ml->setSpacing(6);

    auto* mapHeader = new QHBoxLayout();
    auto* mapTitle = new QLabel("Virtual Memory Map");
    mapTitle->setStyleSheet(QString(
        "color:%1; font-size:12px; font-weight:bold;"
    ).arg(Theme::TEXT_PRIMARY));
    auto* mapHint = new QLabel("Scroll to zoom · drag to pan · hover for tooltip");
    mapHint->setStyleSheet(QString("color:%1; font-size:10px;").arg(Theme::TEXT_MUTED));

    // +/− zoom buttons — eglfs / touchscreen fallback when wheel doesn't fire
    auto* zoomInBtn    = new QPushButton("+");
    auto* zoomOutBtn   = new QPushButton("−");
    auto* zoomResetBtn = new QPushButton("1:1");
    for (auto* btn : {zoomInBtn, zoomOutBtn, zoomResetBtn}) {
        btn->setFixedSize(26, 22);
        btn->setStyleSheet(QString(
            "QPushButton { background:%1; color:%2; border:1px solid %3; "
            "border-radius:4px; font-size:11px; font-weight:bold; }"
            "QPushButton:hover { background:%3; }"
        ).arg(Theme::BG_INPUT, Theme::TEXT_PRIMARY, Theme::BORDER));
    }

    mapHeader->addWidget(mapTitle);
    mapHeader->addStretch();
    mapHeader->addWidget(mapHint);
    mapHeader->addSpacing(8);
    mapHeader->addWidget(zoomOutBtn);
    mapHeader->addWidget(zoomInBtn);
    mapHeader->addWidget(zoomResetBtn);
    ml->addLayout(mapHeader);

    mapWidget = new MemMapWidget();
    mapWidget->setMinimumHeight(130);
    ml->addWidget(mapWidget);
    outer->addWidget(mapCard, 1);

    outer->addStretch();

    // Connect zoom buttons now that mapWidget exists
    connect(zoomInBtn,    &QPushButton::clicked, mapWidget, &MemMapWidget::zoomIn);
    connect(zoomOutBtn,   &QPushButton::clicked, mapWidget, &MemMapWidget::zoomOut);
    connect(zoomResetBtn, &QPushButton::clicked, mapWidget, &MemMapWidget::resetZoom);

    refreshTimer = new QTimer(this);
    connect(refreshTimer, &QTimer::timeout, this, &MemoryInspector::refresh);
    connect(mapWidget, &MemMapWidget::regionClicked, this, &MemoryInspector::onRegionClicked);
}

void MemoryInspector::inspectPid(pid_t pid) {
    currentPid = pid;
    refreshTimer->start(1500);
    refresh();
}

void MemoryInspector::refresh() {
    if (currentPid <= 0) return;
    auto regions = readMemMap(currentPid);
    long rss     = readRSS(currentPid);
    if (regions.empty()) {
        titleLabel->setText("Memory Inspector — process ended or no access");
        return;
    }
    titleLabel->setText(QString("🧠  Memory Map — PID %1").arg(currentPid));
    long totalVirt = 0;
    for (auto& r : regions) totalVirt += r.sizeKB;
    statsLabel->setText(QString(
        "Virtual: <b>%1 KB</b>  ·  RSS (actual RAM): <b>%2 KB</b>  ·  Regions: <b>%3</b>"
    ).arg(totalVirt).arg(rss).arg(regions.size()));
    mapWidget->setRegions(regions, totalVirt);
}

void MemoryInspector::onRegionClicked(MemRegion r) {
    QString type, explain;
    if (r.label.find("[heap]") != std::string::npos) {
        type = "Heap";
        explain = "The <b>heap</b> is where dynamic memory lives — everything allocated with "
                  "<code>malloc()</code> or <code>new</code>. It grows upward. "
                  "Your memory-eater sandbox process allocates here.";
    } else if (r.label.find("[stack]") != std::string::npos) {
        type = "Stack";
        explain = "The <b>stack</b> holds local variables and function call frames. "
                  "It grows downward from a high address. Stack overflow = it hits the heap.";
    } else if (r.label.find("[vdso]") != std::string::npos) {
        type = "vDSO";
        explain = "The <b>vDSO</b> (Virtual Dynamic Shared Object) is a kernel-mapped "
                  "region that lets user programs call certain kernel functions "
                  "(like <code>gettimeofday</code>) without a full syscall — much faster.";
    } else if (r.perms.find('x') != std::string::npos) {
        type = "Code (executable)";
        explain = "This region contains <b>executable code</b> — the actual machine instructions. "
                  "Marked executable but not writable (W^X protection) "
                  "to prevent code injection attacks.";
    } else if (r.perms.find('w') != std::string::npos) {
        type = "Data (writable)";
        explain = "A <b>writable data region</b> — global variables, "
                  "memory-mapped files, or shared library data sections.";
    } else {
        type = "Read-only";
        explain = "A <b>read-only region</b> — often shared library code or "
                  "memory-mapped read-only files. Multiple processes can share "
                  "the same physical page here.";
    }
    emit explanationNeeded(QString(
        "<b>Memory Region: %1</b><br><br>"
        "Address: <code>0x%2 → 0x%3</code><br>"
        "Size: <b>%4 KB</b><br>"
        "Permissions: <code>%5</code>  (r=read w=write x=execute p=private)<br><br>"
        "%6"
    ).arg(type).arg(r.start,0,16).arg(r.end,0,16).arg(r.sizeKB)
     .arg(QString::fromStdString(r.perms)).arg(explain));
}

std::vector<MemRegion> MemoryInspector::readMemMap(pid_t pid) {
    std::vector<MemRegion> result;
    std::ifstream f("/proc/" + std::to_string(pid) + "/maps");
    if (!f.is_open()) return result;
    std::string line;
    while (std::getline(f, line)) {
        MemRegion r;
        unsigned long start, end;
        char perms[8], label[256] = "";
        unsigned long offset; unsigned int dev1, dev2; unsigned long inode;
        int parsed = sscanf(line.c_str(), "%lx-%lx %7s %lx %x:%x %lu %255s",
                            &start, &end, perms, &offset, &dev1, &dev2, &inode, label);
        r.start  = start;
        r.end    = end;
        r.perms  = perms;
        r.label  = parsed >= 8 ? label : "";
        r.sizeKB = (end - start) / 1024;
        result.push_back(r);
    }
    return result;
}

long MemoryInspector::readRSS(pid_t pid) {
    std::ifstream f("/proc/" + std::to_string(pid) + "/status");
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            std::istringstream ss(line.substr(6));
            long val; ss >> val;
            return val;
        }
    }
    return 0;
}
