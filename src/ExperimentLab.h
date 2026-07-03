#pragma once
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QProgressBar>
#include <QTableWidget>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QPainter>
#include <QSlider>
#include <vector>
#include "ExperimentManager.h"

// Live timeline chart showing which process ran each tick
class ExperimentTimeline : public QWidget {
    Q_OBJECT
public:
    explicit ExperimentTimeline(QWidget* parent = nullptr);
    void addTick(int tick, const QVector<ExpProcess>& processes);
    void clear();
protected:
    void paintEvent(QPaintEvent*) override;
private:
    // tick → {processName → running}
    struct TickState {
        int tick;
        QVector<QPair<QString,bool>> states; // name, wasRunning
    };
    QVector<TickState> history;
    QStringList processNames;
    QMap<QString,QColor> colors;
};

// Kernel parameters panel — live read/write to /proc/sys
class KernelParamsPanel : public QWidget {
    Q_OBJECT
public:
    explicit KernelParamsPanel(QWidget* parent = nullptr);

signals:
    void explanationNeeded(QString text);

private slots:
    void onSwappinessChanged(int value);
    void onSchedLatencyChanged(int value);
    void onOvercommitChanged(int index);
    void onParamChanged(QString param, QString value);
    void onWriteFailed(QString param, QString attemptedValue);
    void refreshValues();
private:
    QLabel*  swappinessVal;
    QLabel*  schedLatencyVal;
    QLabel*  overcommitVal;
    QLabel*  permissionStatus;
    QSlider* swappinessSlider;
    QSlider* schedLatencySlider;
    QComboBox* overcommitBox;
    QTimer*  refreshTimer;
};

class ExperimentLab : public QWidget {
    Q_OBJECT
public:
    explicit ExperimentLab(QWidget* parent = nullptr);

signals:
    void explanationNeeded(QString text);

private slots:
    void onExperimentSelected(int index);
    void onRunExperiment();
    void onStopExperiment();
    void onAlgoChanged(int index);
    void onExperimentStarted(Experiment exp);
    void onExperimentTick(int tick, int total, QVector<ExpProcess> processes);
    void onExperimentFinished(ExpResult result);
    void onStageChanged(QString stage, QString desc);

private:
    // Experiment selector
    QComboBox*          experimentBox;
    QComboBox*          algoBox;
    QLabel*             expDescLabel;
    QPushButton*        runBtn;
    QPushButton*        stopBtn;

    // Progress
    QProgressBar*       progressBar;
    QLabel*             stageLabel;
    QLabel*             tickLabel;

    // Live process table during experiment
    QTableWidget*       processTable;

    // Timeline visualization
    ExperimentTimeline* timeline;

    // Results
    QTextEdit*          resultsLog;

    // Kernel params
    KernelParamsPanel*  kernelParams;

    QVector<Experiment> experiments;

    void updateProcessTable(const QVector<ExpProcess>& processes);
};
