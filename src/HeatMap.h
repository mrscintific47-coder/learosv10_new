#pragma once
#include <QWidget>
#include <QPainter>
#include <QTimer>
#include <vector>
#include <map>
#include "EventBus.h"

struct CoreStat {
    int   coreId;
    float usage;
};

struct ProcessSlot {
    int   pid;
    char  state;
    float cpu;
};

class HeatMap : public QWidget {
    Q_OBJECT

public:
    explicit HeatMap(QWidget* parent = nullptr);

    void updateCores(const std::vector<CoreStat>& cores);
    void updateMemory(float usedPercent, long usedMB, long totalMB);
    void updateProcessSlots(const std::vector<ProcessSlot>& procs);

    // React to events — flash/highlight specific processes
    void highlightPid(pid_t pid, QColor color, int durationMs = 1500);
    void flashMemory(QColor color, int durationMs = 800);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

signals:
    void explanationNeeded(QString text);

private slots:
    void onOSEvent(OSEvent event);
    void clearHighlights();

private:
    std::vector<CoreStat>    coreList;
    std::vector<ProcessSlot> procList;
    float memUsedPercent = 0;
    long  memUsedMB  = 0;
    long  memTotalMB = 0;

    QRect cpuSectionRect;
    QRect memSectionRect;
    QRect procSectionRect;

    // Highlight state — pid → color to flash
    std::map<pid_t, QColor> highlightedPids;
    QColor memFlashColor;
    bool   memFlashing = false;
    QTimer* highlightTimer;

    QColor heatColor(float value);
};
