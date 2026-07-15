#pragma once
#include <QWidget>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include "EventBus.h"

// Live feed of every OS event — visible at all times in the sidebar
// This is what makes the system feel alive and connected
class ActivityFeed : public QWidget {
    Q_OBJECT
public:
    explicit ActivityFeed(QWidget* parent = nullptr);

public slots:
    void onOSEvent(OSEvent event);

private slots:
    void onExport();

private:
    QListWidget* list;
    QLabel*      countLabel;
    int          totalEvents = 0;

    QString eventIcon(OSEvent::Type t);
    QColor  eventColor(OSEvent::Type t);
};
