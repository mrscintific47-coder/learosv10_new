#pragma once
#include <QWidget>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QTimer>
#include <QPainter>
#include <vector>
#include <string>
#include <unistd.h>

struct MemRegion {
    unsigned long start;
    unsigned long end;
    std::string   perms;   // rwxp
    std::string   label;   // [heap] [stack] or filename
    unsigned long sizeKB;
};

// Draws the memory map of one process as colored blocks
class MemMapWidget : public QWidget {
    Q_OBJECT
public:
    explicit MemMapWidget(QWidget* parent = nullptr);
    void setRegions(const std::vector<MemRegion>& regions, long totalKB);
signals:
    void regionClicked(MemRegion region);
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
private:
    std::vector<MemRegion> regions;
    std::vector<QRect>     rects;
    long totalKB = 0;
    QColor regionColor(const MemRegion& r);
};

class MemoryInspector : public QWidget {
    Q_OBJECT
public:
    explicit MemoryInspector(QWidget* parent = nullptr);
    void inspectPid(pid_t pid);
signals:
    void explanationNeeded(QString text);
private slots:
    void refresh();
    void onRegionClicked(MemRegion r);
private:
    QLabel*      titleLabel;
    MemMapWidget* mapWidget;
    QLabel*      statsLabel;
    QTimer*      refreshTimer;
    pid_t        currentPid = -1;

    std::vector<MemRegion> readMemMap(pid_t pid);
    long readRSS(pid_t pid);
};
