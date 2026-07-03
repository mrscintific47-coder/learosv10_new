#pragma once
#include <QObject>
#include <QTimer>
#include "HeatMap.h"
#include <fstream>
#include <sstream>
#include <vector>
#include <map>

// Reads /proc every second and pushes data to the HeatMap widget
class HeatMapDriver : public QObject {
    Q_OBJECT

public:
    explicit HeatMapDriver(HeatMap* map, QObject* parent = nullptr);

private slots:
    void refresh();

private:
    HeatMap* heatMap;
    QTimer*  timer;

    // Per-core CPU tracking (need delta between reads)
    struct CoreTimes { long long idle, total; };
    std::map<int, CoreTimes> prevTimes;

    std::vector<CoreStat>    readCores();
    std::vector<ProcessSlot> readSlots();
    void                     readMem(float& pct, long& usedMB, long& totalMB);
};
