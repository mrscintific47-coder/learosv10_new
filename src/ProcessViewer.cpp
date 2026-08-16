#include "ProcessViewer.h"
#include "Theme.h"
#include <QHeaderView>
#include <QScrollArea>
#include <QFrame>
#include <dirent.h>
#include <vector>
#include <string>
#include <sys/types.h>
#include <fstream>
#include <sstream>
#include <unistd.h>
#include <pwd.h>
#include <map>
#include <chrono>

static std::map<int, std::pair<long long, long long>> prevCpuTimes;

ProcessViewer::ProcessViewer(QWidget* parent) : QWidget(parent) {
    setStyleSheet(QString("background: %1;").arg(Theme::BG_APP));
    auto* content = new QWidget();
    content->setStyleSheet(QString("background: %1;").arg(Theme::BG_APP));
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(16, 16, 16, 8);
    layout->setSpacing(10);

    // Top bar
    auto* topBar = new QHBoxLayout();

    // Stats chips
    auto makeChip = [](const QString& label, const char* color, const char* bg) {
        auto* chip = new QLabel(label);
        chip->setStyleSheet(QString(
            "color: %1; background: %2; border-radius: 8px;"
            "padding: 4px 12px; font-size: 11px; font-weight: bold;"
        ).arg(color).arg(bg));
        return chip;
    };

    runningChip = makeChip("● 0 Running", Theme::GREEN, Theme::GREEN_LIGHT);
    sleepingChip = makeChip("◌ 0 Sleeping", Theme::BLUE, Theme::BLUE_LIGHT);
    totalChip = makeChip("∑ 0 Total", Theme::TEXT_SECONDARY, Theme::BG_INPUT);

    filterInput = new QLineEdit();
    filterInput->setPlaceholderText("🔍  Search by name or PID...");
    filterInput->setStyleSheet(Theme::input());
    filterInput->setFixedWidth(220);

    topBar->addWidget(runningChip);
    topBar->addWidget(sleepingChip);
    topBar->addWidget(totalChip);
    topBar->addStretch();
    topBar->addWidget(filterInput);
    layout->addLayout(topBar);

    // Table
    table = new QTableWidget(this);
    table->setColumnCount(7);
    table->setHorizontalHeaderLabels({"PID","Name","State","CPU %","Memory","User","Type"});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSortingEnabled(true);
    table->verticalHeader()->setVisible(false);
    table->setShowGrid(false);
    table->setAlternatingRowColors(true);
    table->setStyleSheet(Theme::table() +
        "QTableWidget { alternate-background-color: #F8F9FE; }"
    );
    table->verticalHeader()->setDefaultSectionSize(32);
    layout->addWidget(table);

    // Status
    statusLabel = new QLabel("Loading...");
    statusLabel->setStyleSheet(QString("color: %1; font-size: 11px;").arg(Theme::TEXT_MUTED));
    layout->addWidget(statusLabel);

    connect(table, &QTableWidget::cellClicked, this, &ProcessViewer::onRowClicked);
    connect(filterInput, &QLineEdit::textChanged, this, &ProcessViewer::onFilterChanged);

    refreshTimer = new QTimer(this);
    connect(refreshTimer, &QTimer::timeout, this, &ProcessViewer::refresh);
    refreshTimer->start(2000);
    refresh();

    auto* scroll = new QScrollArea(this);
    scroll->setWidget(content);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet("QScrollArea{border:none;background:transparent;}" + Theme::scrollbar());
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->addWidget(scroll);
}

void ProcessViewer::refresh() {
    allProcesses = readAllProcesses();
    QString filter = filterInput->text();
    if (filter.isEmpty()) populateTable(allProcesses);
    else onFilterChanged(filter);

    int running = 0, sleeping = 0;
    for (auto& p : allProcesses) {
        if (p.state == "R") running++;
        else if (p.state == "S") sleeping++;
    }
    runningChip->setText(QString("● %1 Running").arg(running));
    sleepingChip->setText(QString("◌ %1 Sleeping").arg(sleeping));
    totalChip->setText(QString("∑ %1 Total").arg(allProcesses.size()));
    statusLabel->setText(QString("Refreshing every 2s  —  %1 processes tracked").arg(allProcesses.size()));
}

std::vector<ProcessInfo> ProcessViewer::readAllProcesses() {
    std::vector<ProcessInfo> result;
    DIR* procDir = opendir("/proc");
    if (!procDir) return result;
    struct dirent* entry;
    while ((entry = readdir(procDir)) != nullptr) {
        std::string name(entry->d_name);
        bool isNum = !name.empty() && std::all_of(name.begin(), name.end(), ::isdigit);
        if (!isNum) continue;
        int pid = std::stoi(name);
        ProcessInfo p = readProcess(pid);
        if (p.pid != -1) result.push_back(p);
    }
    closedir(procDir);
    return result;
}

ProcessInfo ProcessViewer::readProcess(int pid) {
    ProcessInfo p;
    p.pid = -1;
    p.isSandbox = false;
    std::ifstream statusFile("/proc/" + std::to_string(pid) + "/status");
    if (!statusFile.is_open()) return p;
    p.pid = pid; p.name = "unknown"; p.memKB = 0; p.state = "?";
    std::string line;
    while (std::getline(statusFile, line)) {
        if (line.rfind("Name:",0)==0)  p.name  = line.substr(6);
        if (line.rfind("State:",0)==0) p.state = line.substr(7,1);
        if (line.rfind("VmRSS:",0)==0) { std::istringstream ss(line.substr(6)); ss >> p.memKB; }
        if (line.rfind("Uid:",0)==0) {
            int uid; std::istringstream ss(line.substr(4)); ss >> uid;
            struct passwd* pw = getpwuid(uid);
            p.user = pw ? pw->pw_name : std::to_string(uid);
        }
    }
    p.isSystem = (p.user == "root" || pid < 300);
    p.cpuPercent = computeCpuUsage(pid);
    return p;
}

float ProcessViewer::computeCpuUsage(int pid) {
    std::ifstream statFile("/proc/" + std::to_string(pid) + "/stat");
    if (!statFile.is_open()) return 0.0f;
    std::string content; std::getline(statFile, content);
    std::istringstream ss(content); std::string token;
    std::vector<std::string> fields;
    while (ss >> token) fields.push_back(token);
    if (fields.size() < 15) return 0.0f;
    long long utime = std::stoll(fields[13]);
    long long stime = std::stoll(fields[14]);
    long long totalTime = utime + stime;
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    auto it = prevCpuTimes.find(pid);
    if (it == prevCpuTimes.end()) { prevCpuTimes[pid]={totalTime,now}; return 0.0f; }
    long long prevTotal=it->second.first, prevWall=it->second.second;
    prevCpuTimes[pid]={totalTime,now};
    long long deltaCpu=totalTime-prevTotal, deltaWall=now-prevWall;
    if (deltaWall<=0) return 0.0f;
    long clkTck=sysconf(_SC_CLK_TCK);
    float cpuSec=(float)deltaCpu/clkTck, wallSec=(float)deltaWall/1e9f;
    return std::min((cpuSec/wallSec)*100.0f,100.0f);
}

void ProcessViewer::populateTable(const std::vector<ProcessInfo>& processes) {
    table->setSortingEnabled(false);
    table->setRowCount(0);
    for (const auto& p : processes) {
        int row = table->rowCount();
        table->insertRow(row);
        auto item = [](const QString& text) {
            auto* i = new QTableWidgetItem(text);
            i->setTextAlignment(Qt::AlignCenter);
            return i;
        };
        QString stateLabel = QString::fromStdString(p.state);
        if (p.state=="R") stateLabel="● Running";
        else if (p.state=="S") stateLabel="◌ Sleeping";
        else if (p.state=="Z") stateLabel="✕ Zombie";
        else if (p.state=="D") stateLabel="⧖ Waiting";
        QString typeLabel = p.isSandbox?"🧪 Sandbox":p.isSystem?"⚙ System":"👤 User";
        table->setItem(row,0,item(QString::number(p.pid)));
        table->setItem(row,1,item(QString::fromStdString(p.name)));
        table->setItem(row,2,item(stateLabel));
        table->setItem(row,3,item(QString::number(p.cpuPercent,'f',1)+"%"));
        table->setItem(row,4,item(QString::number(p.memKB/1024.0,'f',1)+" MB"));
        table->setItem(row,5,item(QString::fromStdString(p.user)));
        table->setItem(row,6,item(typeLabel));
        table->item(row,0)->setData(Qt::UserRole, p.pid);
        colorRow(row, p);
    }
    table->setSortingEnabled(true);
}

void ProcessViewer::colorRow(int row, const ProcessInfo& p) {
    QColor fg = QColor(Theme::TEXT_PRIMARY);
    QColor stateFg = fg;

    if (p.state=="R")       stateFg = QColor(Theme::GREEN);
    else if (p.state=="Z")  stateFg = QColor(Theme::RED);
    else if (p.state=="D")  stateFg = QColor(Theme::ORANGE);
    else                     stateFg = QColor(Theme::TEXT_SECONDARY);

    if (p.cpuPercent > 50.0f) fg = QColor(Theme::RED);
    else if (p.cpuPercent > 10.0f) fg = QColor(Theme::ORANGE);
    else if (p.isSystem) fg = QColor(Theme::TEXT_MUTED);

    for (int col = 0; col < table->columnCount(); ++col) {
        if (auto* item = table->item(row, col)) {
            item->setForeground(col==2 ? stateFg : fg);
        }
    }
}

void ProcessViewer::onRowClicked(int row, int) {
    int pid = table->item(row,0)->data(Qt::UserRole).toInt();
    for (const auto& p : allProcesses) {
        if (p.pid != pid) continue;
        emit processSelected(p);
        emit pidSelected(p.pid);  // triggers memory inspector

        // Read extended info from /proc
        std::string ppidStr="?", threads="?", fdCount="?";
        std::string vmPeak="?", vmSize="?", vmRSS="?", vmSwap="?";
        std::string cpuUser="?", cpuSys="?", startTime="?";

        std::ifstream status("/proc/"+std::to_string(pid)+"/status");
        std::string line;
        while(std::getline(status,line)){
            if(line.rfind("PPid:",0)==0){std::istringstream ss(line.substr(5));int v;ss>>v;ppidStr=std::to_string(v);}
            if(line.rfind("Threads:",0)==0){std::istringstream ss(line.substr(8));int v;ss>>v;threads=std::to_string(v);}
            if(line.rfind("VmPeak:",0)==0){std::istringstream ss(line.substr(7));long v;ss>>v;vmPeak=std::to_string(v/1024)+" MB";}
            if(line.rfind("VmSize:",0)==0){std::istringstream ss(line.substr(7));long v;ss>>v;vmSize=std::to_string(v/1024)+" MB";}
            if(line.rfind("VmRSS:",0)==0){std::istringstream ss(line.substr(6));long v;ss>>v;vmRSS=std::to_string(v/1024)+" MB";}
            if(line.rfind("VmSwap:",0)==0){std::istringstream ss(line.substr(7));long v;ss>>v;vmSwap=std::to_string(v/1024)+" MB";}
        }

        // Count open file descriptors
        std::string fdPath="/proc/"+std::to_string(pid)+"/fd";
        DIR* fdDir=opendir(fdPath.c_str());
        if(fdDir){
            int count=0;
            struct dirent* e;
            while((e=readdir(fdDir))!=nullptr)
                if(e->d_name[0]!='.') count++;
            closedir(fdDir);
            fdCount=std::to_string(count);
        }

        // Read CPU times from /proc/[pid]/stat
        std::ifstream statf("/proc/"+std::to_string(pid)+"/stat");
        std::string statLine; std::getline(statf,statLine);
        std::istringstream statss(statLine);
        std::string tok; std::vector<std::string> fields;
        while(statss>>tok) fields.push_back(tok);
        if(fields.size()>=15){
            long utime=std::stol(fields[13]);
            long stime=std::stol(fields[14]);
            long clk=sysconf(_SC_CLK_TCK);
            cpuUser=QString::number((double)utime/clk,'f',1).toStdString()+"s";
            cpuSys =QString::number((double)stime/clk,'f',1).toStdString()+"s";
        }

        QString stateDesc =
            p.state=="S"?"Sleeping — waiting for input or timer":
            p.state=="R"?"Running — on CPU right now":
            p.state=="Z"?"Zombie — finished, parent not yet cleaned up":
            p.state=="D"?"Disk wait — blocked on I/O":
            p.state=="T"?"Stopped — frozen by SIGSTOP":"Unknown";

        QString explanation = QString(
            "<b>%1</b>&nbsp;"            "<span style='color:%2;background:%3;border-radius:6px;padding:2px 8px;font-size:10px;'>PID %4</span>"            "<br><br>"            "<table style='font-size:12px;width:100%%;'>"            "<tr><td><b>State</b></td><td>%5 — %6</td></tr>"            "<tr><td><b>Parent PID</b></td><td>%7</td></tr>"            "<tr><td><b>Threads</b></td><td>%8</td></tr>"            "<tr><td><b>Owner</b></td><td>%9</td></tr>"            "<tr><td><b>CPU user</b></td><td>%10</td></tr>"            "<tr><td><b>CPU sys</b></td><td>%11</td></tr>"            "<tr><td><b>RSS</b></td><td>%12</td></tr>"            "<tr><td><b>Virtual</b></td><td>%13</td></tr>"            "<tr><td><b>Peak VM</b></td><td>%14</td></tr>"            "<tr><td><b>Swap</b></td><td>%15</td></tr>"            "<tr><td><b>Open FDs</b></td><td>%16</td></tr>"            "</table><br>"            "<i style='color:#64748B;'>Memory map loaded in 🧠 Memory tab →</i>"
        )
        .arg(QString::fromStdString(p.name))
        .arg(Theme::BLUE).arg(Theme::BLUE_LIGHT)
        .arg(p.pid)
        .arg(QString::fromStdString(p.state)).arg(stateDesc)
        .arg(QString::fromStdString(ppidStr))
        .arg(QString::fromStdString(threads))
        .arg(QString::fromStdString(p.user))
        .arg(QString::fromStdString(cpuUser))
        .arg(QString::fromStdString(cpuSys))
        .arg(QString::fromStdString(vmRSS))
        .arg(QString::fromStdString(vmSize))
        .arg(QString::fromStdString(vmPeak))
        .arg(QString::fromStdString(vmSwap))
        .arg(QString::fromStdString(fdCount));

        emit explanationNeeded(explanation);
        break;
    }
}

void ProcessViewer::onFilterChanged(const QString& text) {
    if (text.isEmpty()) { populateTable(allProcesses); return; }
    std::vector<ProcessInfo> filtered;
    for (const auto& p : allProcesses) {
        QString name = QString::fromStdString(p.name).toLower();
        if (name.contains(text.toLower()) || QString::number(p.pid).contains(text))
            filtered.push_back(p);
    }
    populateTable(filtered);
}
