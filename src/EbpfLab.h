#pragma once
// eBPF Lab — uses the bpf() syscall via the kernel's perf_event / tracepoint
// infrastructure. Does NOT require libbpf — we use raw syscall + perf_event_open
// for perf counters, and /sys/kernel/debug/tracing/trace_pipe for ftrace events.
// This works on any kernel >= 4.1 with CONFIG_FTRACE=y (Kali has this by default).

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QCheckBox>
#include <QPainter>
#include <QSocketNotifier>
#include <vector>
#include <unistd.h>

struct PerfCounter {
    QString name;
    long    value;
    long    delta;
    int     fd;
};

struct TraceEvent {
    QString timestamp;
    QString comm;
    int     pid;
    QString function;
    QString detail;
};

// Mini chart for perf counter deltas
class PerfChart : public QWidget {
    Q_OBJECT
public:
    explicit PerfChart(QWidget* parent = nullptr);
    void addSample(const QString& name, long delta);
    void clear();
protected:
    void paintEvent(QPaintEvent*) override;
private:
    struct Series { QString name; QVector<long> samples; QColor color; };
    QVector<Series> series;
    long maxVal = 1;
};

class EbpfLab : public QWidget {
    Q_OBJECT
public:
    explicit EbpfLab(QWidget* parent = nullptr);
    ~EbpfLab();

signals:
    void explanationNeeded(QString text);

private slots:
    void onStartPerfCounters();
    void onStopPerfCounters();
    void onStartFtrace();
    void onStopFtrace();
    void onPerfTick();
    void onFtraceReady();
    void onProbeChanged(int index);

private:
    // Perf counters via perf_event_open
    QVector<PerfCounter> counters;
    pid_t tracePid = -1;   // PID to attach counters to (-1 = system-wide)
    QTimer* perfTimer = nullptr;

    // Ftrace via /sys/kernel/debug/tracing
    int tracePipeFd = -1;
    QSocketNotifier* traceNotifier = nullptr;
    bool ftraceActive = false;

    // UI
    QComboBox*    probeBox;
    QPushButton*  startPerfBtn;
    QPushButton*  stopPerfBtn;
    QPushButton*  startFtraceBtn;
    QPushButton*  stopFtraceBtn;
    QTableWidget* counterTable;
    PerfChart*    perfChart;
    QTextEdit*    traceLog;
    QLabel*       statusLabel;

    bool openPerfCounters();
    void closePerfCounters();
    bool enableFtrace(const QString& probe);
    void disableFtrace();
    void readCounters();
    void refreshCounterTable();
};
