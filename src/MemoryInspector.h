#pragma once
#include <QWidget>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QTimer>
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QToolTip>
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

// Draws the memory map of one process as colored blocks.
// Supports wheel/pinch zoom and click+drag pan.
// +/- zoom buttons are provided by the containing layout (see MemoryInspector).
// Hover shows a tooltip with region details.
class MemMapWidget : public QWidget {
    Q_OBJECT
public:
    explicit MemMapWidget(QWidget* parent = nullptr);
    void setRegions(const std::vector<MemRegion>& regions, long totalKB);

    // Zoom controls for eglfs / touchscreen fallback
    void zoomIn();
    void zoomOut();
    void resetZoom();

signals:
    void regionClicked(MemRegion region);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void wheelEvent(QWheelEvent*) override;

private:
    std::vector<MemRegion> regions;
    std::vector<QRect>     rects;   // in logical (zoomed+panned) coordinates
    long totalKB  = 0;

    // Zoom / pan state
    double zoomFactor = 1.0;   // 1.0 = fit-to-width; > 1 zooms in
    int    panOffset  = 0;     // horizontal scroll in pixels at zoom==1
    bool   panning    = false;
    int    panStartX  = 0;
    int    panOffsetAtStart = 0;

    QColor regionColor(const MemRegion& r) const;
    // Returns the MemRegion under the given widget position, or nullptr.
    const MemRegion* regionAt(QPoint pos) const;
    // Clamp panOffset so we never scroll past the content.
    void clampPan();
};

class MemoryInspector : public QWidget {
    Q_OBJECT
public:
    explicit MemoryInspector(QWidget* parent = nullptr);
    void inspectPid(pid_t pid);

    // Static so other labs can reuse without owning a MemoryInspector instance.
    static std::vector<MemRegion> readMemMap(pid_t pid);
    static long readRSS(pid_t pid);

signals:
    void explanationNeeded(QString text);
private slots:
    void refresh();
    void onRegionClicked(MemRegion r);
private:
    QLabel*       titleLabel;
    MemMapWidget* mapWidget;
    QLabel*       statsLabel;
    QTimer*       refreshTimer;
    pid_t         currentPid = -1;
};
