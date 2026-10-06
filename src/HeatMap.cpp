#include "HeatMap.h"
#include "Theme.h"
#include <QMouseEvent>
#include <QPainterPath>
#include <functional>
#include <cmath>

HeatMap::HeatMap(QWidget* parent) : QWidget(parent) {
    setMinimumWidth(240);
    setStyleSheet(QString("background: %1;").arg(Theme::BG_SIDEBAR));

    // Connect to EventBus — react to all OS events visually
    connect(&EventBus::get(), &EventBus::osEvent, this, &HeatMap::onOSEvent);

    // Timer to clear stale highlights
    highlightTimer = new QTimer(this);
    highlightTimer->setSingleShot(false);
}

QColor HeatMap::heatColor(float value) {
    value = std::max(0.0f, std::min(100.0f, value));
    if (value < 30.0f) {
        float t = value / 30.0f;
        return QColor(
            (int)(34  + t * (234-34)),
            (int)(197 + t * (179-197)),
            (int)(94  + t * (8-94))
        );
    } else if (value < 70.0f) {
        float t = (value-30.0f)/40.0f;
        return QColor(
            (int)(234 + t * (249-234)),
            (int)(179 + t * (115-179)),
            (int)(8   + t * (22-8))
        );
    } else {
        float t = (value-70.0f)/30.0f;
        return QColor(
            (int)(249 + t * (239-249)),
            (int)(115 - t * 47),
            (int)(22  - t * 22)
        );
    }
}

void HeatMap::updateCores(const std::vector<CoreStat>& c) { coreList=c; update(); }
void HeatMap::updateMemory(float p, long u, long t) { memUsedPercent=p; memUsedMB=u; memTotalMB=t; update(); }
void HeatMap::updateProcessSlots(const std::vector<ProcessSlot>& p) { procList=p; update(); }

void HeatMap::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(Theme::BG_SIDEBAR));

    int w=width(), h=height(), pad=14;
    int y=12;

    // Title
    p.setPen(QColor(Theme::TEXT_SECONDARY));
    p.setFont(QFont("Segoe UI", 9, QFont::Bold));
    p.drawText(pad, y+12, "SYSTEM MONITOR");
    y += 26;

    int sectionH = (h - y - pad*3) / 3;

    // Helper: draw section card
    auto drawCard = [&](QRect r, const QString& title, const char* titleColor) {
        // White card with shadow effect
        QPainterPath path;
        path.addRoundedRect(r, 10, 10);
        p.fillPath(path, QColor("white"));
        p.setPen(QPen(QColor(Theme::BORDER), 1));
        p.drawPath(path);

        // Section title
        p.setPen(QColor(titleColor));
        p.setFont(QFont("Segoe UI", 9, QFont::Bold));
        p.drawText(r.x()+10, r.y()+16, title);
    };

    // ── CPU CORES ──
    cpuSectionRect = QRect(pad, y, w-pad*2, sectionH);
    drawCard(cpuSectionRect, "CPU CORES", Theme::BLUE);

    if (!coreList.empty()) {
        int n=coreList.size(), cellPad=5;
        int cellW=(cpuSectionRect.width()-cellPad*(n+1))/n;
        int cellH=cpuSectionRect.height()-30;
        int cellY=cpuSectionRect.y()+22;

        for (int i=0;i<n;i++) {
            int cellX=cpuSectionRect.x()+cellPad+i*(cellW+cellPad);
            QRect cell(cellX,cellY,cellW,cellH);

            // Background track
            QPainterPath track; track.addRoundedRect(cell,4,4);
            p.fillPath(track, QColor("#F1F5F9"));

            // Filled bar from bottom
            float usage=coreList[i].usage;
            int fillH=(int)(cellH*usage/100.0f);
            if (fillH>0) {
                QRect fill(cellX,cellY+cellH-fillH,cellW,fillH);
                QPainterPath fillPath; fillPath.addRoundedRect(fill,4,4);
                p.fillPath(fillPath, heatColor(usage));
            }

            // Percentage text
            p.setPen(usage>60?QColor(Theme::TEXT_WHITE):QColor(Theme::TEXT_PRIMARY));
            p.setFont(QFont("Segoe UI",7,QFont::Bold));
            p.drawText(cell, Qt::AlignHCenter|Qt::AlignBottom,
                       QString("%1%").arg((int)usage));
        }
    }
    y += sectionH + pad;

    // ── MEMORY ──
    memSectionRect = QRect(pad, y, w-pad*2, sectionH);
    drawCard(memSectionRect, QString("RAM  %1/%2 MB").arg(memUsedMB).arg(memTotalMB), Theme::PURPLE);

    {
        int blockSize=12, gapX=3, gapY=3;
        int cols=(memSectionRect.width()-12)/(blockSize+gapX);
        int rows=(memSectionRect.height()-26)/(blockSize+gapY);
        int total=cols*rows;
        int used=(int)(total*memUsedPercent/100.0f);
        int startY=memSectionRect.y()+20;

        for (int i=0;i<total;i++) {
            int col=i%cols, row=i/cols;
            int bx=memSectionRect.x()+6+col*(blockSize+gapX);
            int by=startY+row*(blockSize+gapY);
            QRect block(bx,by,blockSize,blockSize);
            QPainterPath bp; bp.addRoundedRect(block,3,3);
            QColor blockColor = i<used ? heatColor(memUsedPercent) : QColor("#E2E8F0");
            if (memFlashing && i<used) {
                // Blend flash color with normal color
                blockColor = QColor(
                    (blockColor.red()   + memFlashColor.red())   / 2,
                    (blockColor.green() + memFlashColor.green()) / 2,
                    (blockColor.blue()  + memFlashColor.blue())  / 2
                );
            }
            p.fillPath(bp, blockColor);
        }
    }
    y += sectionH + pad;

    // ── PROCESS SLOTS ──
    procSectionRect = QRect(pad, y, w-pad*2, h-y-pad);
    drawCard(procSectionRect, QString("PROCESSES  (%1)").arg(procList.size()), Theme::ORANGE);

    {
        int slotSize=9, gap=2;
        int cols=(procSectionRect.width()-10)/(slotSize+gap);
        int startY=procSectionRect.y()+20;

        for (int i=0;i<(int)procList.size();i++) {
            int col=i%cols, row=i/cols;
            int sx=procSectionRect.x()+5+col*(slotSize+gap);
            int sy=startY+row*(slotSize+gap);
            if (sy+slotSize>procSectionRect.bottom()-18) break;

            QRect slot(sx,sy,slotSize,slotSize);
            QPainterPath sp; sp.addRoundedRect(slot,2,2);

            QColor color;
            switch(procList[i].state) {
                case 'R': color=heatColor(procList[i].cpu>0?procList[i].cpu:70); break;
                case 'S': color=QColor("#BFDBFE"); break;
                case 'Z': color=QColor(Theme::RED); break;
                case 'D': color=QColor(Theme::ORANGE); break;
                default:  color=QColor("#E2E8F0"); break;
            }
            auto hit = highlightedPids.find(procList[i].pid);
            if (hit != highlightedPids.end()) {
                color = hit->second;
                QPainterPath glow; glow.addRoundedRect(slot.adjusted(-3,-3,3,3),4,4);
                QColor gc = hit->second; gc.setAlpha(80);
                p.fillPath(glow, gc);
            }
            p.fillPath(sp, color);
        }

        // Legend — fixed offset from section bottom so it never overlaps content
        int lx=procSectionRect.x()+6, ly=procSectionRect.bottom()-16;
        p.setFont(QFont("Segoe UI",8));
        auto legend=[&](QColor c, QString label) {
            QPainterPath lp; lp.addRoundedRect(lx,ly,8,8,2,2);
            p.fillPath(lp,c);
            p.setPen(QColor(Theme::TEXT_MUTED));
            p.drawText(lx+10,ly+8,label);
            lx+=10+p.fontMetrics().horizontalAdvance(label)+6;
        };
        legend(heatColor(70),"Run");
        legend(QColor("#BFDBFE"),"Sleep");
        legend(QColor(Theme::ORANGE),"Wait");
        legend(QColor(Theme::RED),"Zombie");
    }
}

void HeatMap::mousePressEvent(QMouseEvent* event) {
    QPoint pos=event->pos();
    if (cpuSectionRect.contains(pos)) {
        emit explanationNeeded(
            "<b>CPU Cores</b><br><br>"
            "Each bar shows one CPU core's current load. "
            "Color goes from <span style='color:#22C55E'>green (idle)</span> → "
            "<span style='color:#F97316'>orange (busy)</span> → "
            "<span style='color:#EF4444'>red (maxed out)</span>.<br><br>"
            "The kernel scheduler decides which process runs on which core "
            "thousands of times per second.");
    } else if (memSectionRect.contains(pos)) {
        emit explanationNeeded(QString(
            "<b>RAM Usage</b><br><br>"
            "Each block = a chunk of your physical RAM. "
            "Currently <b>%1 MB</b> of <b>%2 MB</b> in use.<br><br>"
            "When RAM fills up, the kernel moves cold pages to swap on disk — "
            "which is 10–100× slower."
        ).arg(memUsedMB).arg(memTotalMB));
    } else if (procSectionRect.contains(pos)) {
        int running=0,sleeping=0,zombie=0;
        for(auto& s:procList){
            if(s.state=='R')running++;
            else if(s.state=='S')sleeping++;
            else if(s.state=='Z')zombie++;
        }
        emit explanationNeeded(QString(
            "<b>Process Slots</b><br><br>"
            "Your system has <b>%1 processes</b> right now:<br>"
            "● <b style='color:#22C55E'>%2 Running</b> — using CPU<br>"
            "● <b style='color:#4F6EF7'>%3 Sleeping</b> — waiting for input/timer<br>"
            "● <b style='color:#EF4444'>%4 Zombie</b> — finished, not yet cleaned up<br><br>"
            "Most processes sleep most of the time. "
            "Only a handful are ever truly running simultaneously."
        ).arg(procList.size()).arg(running).arg(sleeping).arg(zombie));
    }
}

// ── Event reactions ───────────────────────────────────────────────────────

void HeatMap::highlightPid(pid_t pid, QColor color, int durationMs) {
    highlightedPids[pid] = color;
    update();
    QTimer::singleShot(durationMs, this, [this, pid]() {
        highlightedPids.erase(pid);
        update();
    });
}

void HeatMap::flashMemory(QColor color, int durationMs) {
    memFlashColor = color;
    memFlashing   = true;
    update();
    QTimer::singleShot(durationMs, this, [this]() {
        memFlashing = false;
        update();
    });
}

void HeatMap::clearHighlights() {
    highlightedPids.clear();
    memFlashing = false;
    update();
}

void HeatMap::onOSEvent(OSEvent event) {
    switch (event.type) {
        case OSEvent::ProcessSpawned:
            highlightPid(event.pid, QColor(Theme::GREEN), 2000);
            break;
        case OSEvent::ProcessKilled:
            highlightPid(event.pid, QColor(Theme::RED), 1000);
            break;
        case OSEvent::ProcessPaused:
            highlightPid(event.pid, QColor(Theme::ORANGE), 2000);
            break;
        case OSEvent::ProcessResumed:
            highlightPid(event.pid, QColor(Theme::GREEN), 1500);
            break;
        case OSEvent::SignalSent:
            highlightPid(event.pid,
                event.extra == "delivered" ? QColor(Theme::ORANGE) : QColor(Theme::RED),
                1500);
            break;
        case OSEvent::MemoryAllocated:
            flashMemory(QColor(Theme::PURPLE), 600);
            break;
        case OSEvent::MemoryFreed:
            flashMemory(QColor(Theme::GREEN), 400);
            break;
        case OSEvent::MemoryPressureHigh:
            flashMemory(QColor(Theme::RED), 1000);
            break;
        case OSEvent::IPCDataSent:
            // Flash both sender and receiver
            flashMemory(QColor(Theme::TEAL), 400);
            break;
        default: break;
    }
}
