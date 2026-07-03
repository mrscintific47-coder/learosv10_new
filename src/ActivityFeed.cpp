#include "ActivityFeed.h"
#include "Theme.h"
#include <QDateTime>

ActivityFeed::ActivityFeed(QWidget* parent) : QWidget(parent) {
    setStyleSheet("background: transparent;");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8,8,8,4);
    layout->setSpacing(4);

    auto* header = new QHBoxLayout();
    auto* title = new QLabel("⚡ Live Events");
    title->setStyleSheet("color:#F1F5F9;font-size:11px;font-weight:bold;");
    countLabel = new QLabel("0 events");
    countLabel->setStyleSheet("color:#64748B;font-size:10px;");
    header->addWidget(title);
    header->addStretch();
    header->addWidget(countLabel);
    layout->addLayout(header);

    list = new QListWidget();
    list->setStyleSheet(
        "QListWidget{background:#1E293B;border:1px solid #334155;border-radius:8px;"
        "font-size:10px;font-family:Consolas;color:#CBD5E1;}"
        "QListWidget::item{padding:3px 6px;border-bottom:1px solid #1E293B;}"
        "QListWidget::item:selected{background:#334155;color:#F1F5F9;}"
        "QScrollBar:vertical{background:#1E293B;width:4px;border-radius:2px;}"
        "QScrollBar::handle:vertical{background:#475569;border-radius:2px;}"
        "QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;}");
    list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    layout->addWidget(list, 1);

    // Connect to EventBus
    connect(&EventBus::get(), &EventBus::osEvent, this, &ActivityFeed::onOSEvent);
}

void ActivityFeed::onOSEvent(OSEvent event) {
    totalEvents++;
    countLabel->setText(QString("%1 events").arg(totalEvents));

    QString time = QDateTime::currentDateTime().toString("hh:mm:ss");
    QString icon = eventIcon(event.type);
    QString text = QString("%1 %2  %3").arg(icon).arg(time).arg(event.detail);

    auto* item = new QListWidgetItem(text);
    item->setForeground(eventColor(event.type));
    item->setFont(QFont("Consolas", 9));

    list->insertItem(0, item); // newest at top

    // Keep max 100 events
    while (list->count() > 100) delete list->takeItem(list->count()-1);
}

QString ActivityFeed::eventIcon(OSEvent::Type t) {
    switch(t) {
        case OSEvent::ProcessSpawned:     return "🟢";
        case OSEvent::ProcessKilled:      return "🔴";
        case OSEvent::ProcessPaused:      return "🟡";
        case OSEvent::ProcessResumed:     return "🟢";
        case OSEvent::SignalSent:         return "⚡";
        case OSEvent::MemoryAllocated:    return "💾";
        case OSEvent::MemoryFreed:        return "🗑";
        case OSEvent::MemoryPressureHigh: return "🔥";
        case OSEvent::IPCChannelCreated:  return "🔗";
        case OSEvent::IPCDataSent:        return "📡";
        case OSEvent::IPCChannelDestroyed:return "💥";
        case OSEvent::SchedAlgoChanged:   return "⚙";
        case OSEvent::SchedTick:          return "⏱";
        case OSEvent::SchedProcessDone:   return "✓";
        case OSEvent::DSOperation:        return "🧩";
        case OSEvent::CPUHighLoad:        return "🔥";
        case OSEvent::SwapActive:         return "💿";
        default:                          return "•";
    }
}

QColor ActivityFeed::eventColor(OSEvent::Type t) {
    switch(t) {
        case OSEvent::ProcessSpawned:
        case OSEvent::ProcessResumed:
        case OSEvent::SchedProcessDone:   return QColor(Theme::GREEN);
        case OSEvent::ProcessKilled:
        case OSEvent::CPUHighLoad:
        case OSEvent::MemoryPressureHigh: return QColor(Theme::RED);
        case OSEvent::SignalSent:
        case OSEvent::ProcessPaused:      return QColor(Theme::ORANGE);
        case OSEvent::IPCChannelCreated:
        case OSEvent::IPCDataSent:        return QColor(Theme::TEAL);
        case OSEvent::MemoryAllocated:
        case OSEvent::DSOperation:        return QColor(Theme::PURPLE);
        case OSEvent::SchedTick:          return QColor(Theme::BLUE);
        default:                          return QColor(Theme::TEXT_SECONDARY);
    }
}
