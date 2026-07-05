#include "SandboxManager.h"
#include "EventBus.h"
#include "Theme.h"
#include <sys/wait.h>
#include <sys/resource.h>
#include <unistd.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <QTimer>

SandboxManager::SandboxManager(QWidget* parent) : QWidget(parent) {
    setStyleSheet(QString("background: %1;").arg(Theme::BG_APP));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16,16,16,16);
    layout->setSpacing(12);

    // Title
    auto* title = new QLabel("🧪  Experimental Sandbox");
    title->setStyleSheet(QString("color: %1; font-size: 14px; font-weight: bold;").arg(Theme::TEXT_PRIMARY));
    layout->addWidget(title);

    auto* hint = new QLabel("Spawn real Linux processes and watch the system react. Click a process to inspect its memory.");
    hint->setStyleSheet(QString("color: %1; font-size: 11px;").arg(Theme::TEXT_SECONDARY));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    // Spawn buttons
    auto* spawnCard = new QWidget();
    spawnCard->setStyleSheet(QString(
        "background: white; border-radius: 12px; border: 1px solid %1;"
    ).arg(Theme::BORDER));
    auto* spawnLayout = new QVBoxLayout(spawnCard);
    spawnLayout->setContentsMargins(14,12,14,12);
    spawnLayout->setSpacing(8);

    auto* spawnTitle = new QLabel("Spawn a Process");
    spawnTitle->setStyleSheet(QString("color: %1; font-size: 12px; font-weight: bold;").arg(Theme::TEXT_PRIMARY));
    spawnLayout->addWidget(spawnTitle);

    auto* spawnRow = new QHBoxLayout();
    auto* cpuBtn = new QPushButton("🔥  CPU Burner");
    auto* memBtn = new QPushButton("💾  Memory Eater");
    auto* ioBtn  = new QPushButton("💿  I/O Worker");

    cpuBtn->setStyleSheet(
        "QPushButton { background: #FEF2F2; color: #EF4444; border: 1px solid #FECACA;"
        "  border-radius: 8px; padding: 8px 14px; font-size: 12px; font-weight: bold; }"
        "QPushButton:hover { background: #FEE2E2; }");
    memBtn->setStyleSheet(
        "QPushButton { background: #EFF6FF; color: #3B82F6; border: 1px solid #BFDBFE;"
        "  border-radius: 8px; padding: 8px 14px; font-size: 12px; font-weight: bold; }"
        "QPushButton:hover { background: #DBEAFE; }");
    ioBtn->setStyleSheet(
        "QPushButton { background: #F0FDF4; color: #22C55E; border: 1px solid #BBF7D0;"
        "  border-radius: 8px; padding: 8px 14px; font-size: 12px; font-weight: bold; }"
        "QPushButton:hover { background: #DCFCE7; }");

    spawnRow->addWidget(cpuBtn);
    spawnRow->addWidget(memBtn);
    spawnRow->addWidget(ioBtn);
    spawnLayout->addLayout(spawnRow);
    layout->addWidget(spawnCard);

    // Process list card
    auto* listCard = new QWidget();
    listCard->setStyleSheet(QString(
        "background: white; border-radius: 12px; border: 1px solid %1;"
    ).arg(Theme::BORDER));
    auto* listLayout = new QVBoxLayout(listCard);
    listLayout->setContentsMargins(14,12,14,12);
    listLayout->setSpacing(8);

    auto* listTitle = new QLabel("Active Sandbox Processes");
    listTitle->setStyleSheet(QString("color: %1; font-size: 12px; font-weight: bold;").arg(Theme::TEXT_PRIMARY));
    listLayout->addWidget(listTitle);

    processList = new QListWidget();
    processList->setStyleSheet(QString(
        "QListWidget { background: %1; border: 1px solid %2; border-radius: 8px;"
        "  font-size: 12px; color: %3; }"
        "QListWidget::item { padding: 6px 10px; border-radius: 6px; margin: 1px; }"
        "QListWidget::item:selected { background: %4; color: %5; }"
        "QListWidget::item:hover { background: %6; }"
    ).arg(Theme::BG_INPUT).arg(Theme::BORDER).arg(Theme::TEXT_PRIMARY)
     .arg(Theme::BLUE_LIGHT).arg(Theme::BLUE).arg(Theme::BG_APP));
    processList->setFixedHeight(140);
    listLayout->addWidget(processList);

    // Control buttons
    auto* ctrlRow = new QHBoxLayout();
    auto* pauseBtn  = new QPushButton("⏸  Pause");
    auto* resumeBtn = new QPushButton("▶  Resume");
    auto* killBtn   = new QPushButton("✕  Kill");
    pauseBtn->setStyleSheet(Theme::btnGhost());
    resumeBtn->setStyleSheet(Theme::btnSuccess());
    killBtn->setStyleSheet(Theme::btnDanger());
    ctrlRow->addWidget(pauseBtn);
    ctrlRow->addWidget(resumeBtn);
    ctrlRow->addWidget(killBtn);
    listLayout->addLayout(ctrlRow);
    layout->addWidget(listCard);

    layout->addStretch();

    connect(cpuBtn,   &QPushButton::clicked, this, &SandboxManager::spawnCPU);
    connect(memBtn,   &QPushButton::clicked, this, &SandboxManager::spawnMemory);
    connect(ioBtn,    &QPushButton::clicked, this, &SandboxManager::spawnIO);
    connect(killBtn,  &QPushButton::clicked, this, &SandboxManager::killSelected);
    connect(pauseBtn, &QPushButton::clicked, this, &SandboxManager::pauseSelected);
    connect(resumeBtn,&QPushButton::clicked, this, &SandboxManager::resumeSelected);
    connect(processList, &QListWidget::itemClicked, this, &SandboxManager::onRowClicked);

    // Periodically reap any zombies from signal-killed processes
    auto* zombieTimer = new QTimer(this);
    connect(zombieTimer, &QTimer::timeout, this, &SandboxManager::onZombieReap);
    zombieTimer->start(1500);
}

SandboxManager::~SandboxManager() {
    for (auto& p : processes) { kill(p.pid, SIGKILL); waitpid(p.pid, nullptr, WNOHANG); }
}

void SandboxManager::spawnCPU()    { spawn(WorkloadType::CPU); }
void SandboxManager::spawnMemory() { spawn(WorkloadType::MEMORY); }
void SandboxManager::spawnIO()     { spawn(WorkloadType::IO); }

void SandboxManager::spawn(WorkloadType type) {
    static int counter = 1;
    QString typeName = type==WorkloadType::CPU?"cpu_burn":type==WorkloadType::MEMORY?"mem_eat":"io_work";
    QString name = typeName + "_" + QString::number(counter++);

    pid_t pid = fork();
    if (pid == 0) {
        nice(19); // lowest priority — can't freeze system
        if (type == WorkloadType::CPU) {
            volatile double x = 1.0;
            while (true) x = std::sin(x)*std::cos(x)+1.0001;
        } else if (type == WorkloadType::MEMORY) {
            const size_t chunk = 50*1024*1024;
            while (true) {
                char* mem = (char*)malloc(chunk);
                if (mem) for (size_t i=0;i<chunk;i+=4096) mem[i]=(char)(i&0xFF);
                sleep(3); free(mem);
            }
        } else {
            char path[] = "/tmp/learnos_io_XXXXXX";
            int fd = mkstemp(path);
            if (fd<0) _exit(1);
            char buf[4096]; memset(buf,0xAB,sizeof(buf));
            while (true) {
                for (int i=0;i<256;i++) write(fd,buf,sizeof(buf));
                lseek(fd,0,SEEK_SET);
                while (read(fd,buf,sizeof(buf))>0) {}
                lseek(fd,0,SEEK_SET); fsync(fd);
            }
        }
        _exit(0);
    } else if (pid > 0) {
        SandboxProcess sp; sp.pid=pid; sp.name=name.toStdString();
        sp.type=type; sp.priority=0; sp.paused=false;
        processes.push_back(sp);
        refreshList();
        emitPids();
        EventBus::get().processSpawned(pid, QString::fromStdString(name.toStdString()), typeName);

        QString typeDesc = type==WorkloadType::CPU?"🔥 CPU Burner":
                           type==WorkloadType::MEMORY?"💾 Memory Eater":"💿 I/O Worker";
        QString impact   = type==WorkloadType::CPU?"running a tight math loop (sin/cos) forever":
                           type==WorkloadType::MEMORY?"allocating and touching 50MB of real RAM every 3s":
                           "writing and reading 1MB to disk in a loop";
        QString watch    = type==WorkloadType::CPU?"Watch a CPU core bar turn orange/red on the heatmap.":
                           type==WorkloadType::MEMORY?"Watch memory blocks fill up on the heatmap.":
                           "Watch this process flicker to disk-wait state in the process slots.";

        emit explanationNeeded(QString(
            "<b>%1 spawned!</b> &nbsp;"
            "<span style='background:%2;color:%3;border-radius:6px;padding:2px 8px;font-size:10px;'>PID %4</span>"
            "<br><br>This is a real Linux process %5.<br><br>%6<br><br>"
            "Pause it with ⏸ and watch the heatmap cool down immediately."
        ).arg(typeDesc).arg(Theme::GREEN_LIGHT).arg(Theme::GREEN).arg(pid)
         .arg(impact).arg(watch));
    }
}

void SandboxManager::killSelected() {
    auto* item = processList->currentItem();
    if (!item) return;
    pid_t pid = item->data(Qt::UserRole).toInt();
    kill(pid, SIGKILL); waitpid(pid, nullptr, WNOHANG);
    EventBus::get().processKilled(pid, QString::fromStdString(
        [&]{ for(auto&p:processes) if(p.pid==pid) return p.name; return std::string("?"); }()
    ));
    processes.erase(std::remove_if(processes.begin(), processes.end(),
        [pid](const SandboxProcess& p){ return p.pid==pid; }), processes.end());
    refreshList(); emitPids();
    emit explanationNeeded(QString(
        "<b>Process killed</b> (PID %1)<br><br>"
        "SIGKILL sent — the kernel forcibly removed it. "
        "All memory returned to the OS immediately.<br><br>"
        "Watch the heatmap — CPU load or memory usage should drop within one refresh."
    ).arg(pid));
}

void SandboxManager::pauseSelected() {
    auto* item = processList->currentItem();
    if (!item) return;
    pid_t pid = item->data(Qt::UserRole).toInt();
    kill(pid, SIGSTOP);
    EventBus::get().processPaused(pid);
    for (auto& p : processes) if (p.pid==pid) p.paused=true;
    refreshList(); emitPids();
    emit explanationNeeded(QString(
        "<b>Process paused</b> (PID %1)<br><br>"
        "SIGSTOP sent. The kernel freezes this process — zero CPU, holds memory.<br><br>"
        "<i>Watch a CPU core cool down on the heatmap if this was a CPU burner.</i>"
    ).arg(pid));
}

void SandboxManager::resumeSelected() {
    auto* item = processList->currentItem();
    if (!item) return;
    pid_t pid = item->data(Qt::UserRole).toInt();
    kill(pid, SIGCONT);
    EventBus::get().processResumed(pid);
    for (auto& p : processes) if (p.pid==pid) p.paused=false;
    refreshList(); emitPids();
    emit explanationNeeded(QString(
        "<b>Process resumed</b> (PID %1)<br><br>"
        "SIGCONT sent. Back on the run queue.<br><br>"
        "<i>Watch the heatmap heat up again.</i>"
    ).arg(pid));
}

void SandboxManager::refreshList() {
    processList->clear();
    for (const auto& p : processes) {
        QString icon = p.type==WorkloadType::CPU?"🔥":p.type==WorkloadType::MEMORY?"💾":"💿";
        QString label = QString("%1  [%2]  %3  %4")
            .arg(icon).arg(p.pid)
            .arg(QString::fromStdString(p.name))
            .arg(p.paused?"⏸ paused":"▶ running");
        auto* item = new QListWidgetItem(label);
        item->setData(Qt::UserRole, p.pid);
        if (p.paused) item->setForeground(QColor(Theme::ORANGE));
        else item->setForeground(QColor(Theme::TEXT_PRIMARY));
        processList->addItem(item);
    }
}

void SandboxManager::emitPids() {
    std::vector<pid_t> pids;
    for (auto& p : processes) pids.push_back(p.pid);
    emit processesChanged(pids);
}

void SandboxManager::onRowClicked(QListWidgetItem* item) {
    pid_t pid = item->data(Qt::UserRole).toInt();
    emit processSelected(pid);
    emit explanationNeeded(QString(
        "<b>Selected PID %1</b><br><br>"
        "Switch to 🧠 <b>Memory</b> tab to see this process's full memory map live.<br>"
        "Switch to ⚙ <b>Scheduler</b> tab to step through scheduling algorithms with this process."
    ).arg(pid));
}

std::vector<pid_t> SandboxManager::sandboxPids() const {
    std::vector<pid_t> pids;
    for (const auto& p : processes) pids.push_back(p.pid);
    return pids;
}

// Called by MainWindow when SignalPanel fires a signal at a PID.
// If that PID belongs to us and it's now dead, remove it from our list.
void SandboxManager::checkProcessAlive(pid_t pid) {
    auto it = std::find_if(processes.begin(), processes.end(),
        [pid](const SandboxProcess& p){ return p.pid == pid; });
    if (it == processes.end()) return; // not our process

    // Give the signal a short moment to be delivered, then check
    QTimer::singleShot(400, this, [this, pid]() {
        bool dead = (kill(pid, 0) != 0);
        if (dead) {
            waitpid(pid, nullptr, WNOHANG); // reap zombie
            processes.erase(std::remove_if(processes.begin(), processes.end(),
                [pid](const SandboxProcess& p){ return p.pid == pid; }), processes.end());
            refreshList();
            emitPids();
        }
    });
}

// Periodic zombie reaper: removes any sandbox processes that died from
// external signals (e.g. sent from SignalPanel or the OS).
void SandboxManager::onZombieReap() {
    bool changed = false;
    processes.erase(std::remove_if(processes.begin(), processes.end(),
        [&changed](const SandboxProcess& p) {
            if (kill(p.pid, 0) != 0) {
                waitpid(p.pid, nullptr, WNOHANG);
                changed = true;
                return true;
            }
            return false;
        }), processes.end());
    if (changed) {
        refreshList();
        emitPids();
    }
}
