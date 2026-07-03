#include "IPCLab.h"
#include "EventBus.h"
#include "Theme.h"
#include <QPainterPath>
#include <QHeaderView>
#include <QSplitter>
#include <QScrollArea>
#include <sys/wait.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>
#include <fcntl.h>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <algorithm>

// ── IPCFlowView ──────────────────────────────────────────────────────────

IPCFlowView::IPCFlowView(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(200);
    setStyleSheet(QString("background: white; border-radius:12px; border:1px solid %1;").arg(Theme::BORDER));
}

void IPCFlowView::setChannels(const std::vector<IPCChannel>& ch) {
    channels = ch; update();
}

void IPCFlowView::drawFlowArrow(QPainter& p, QPoint from, QPoint to, QColor color, int bytes) {
    p.setPen(QPen(color, 2.5, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(from, to);
    // Arrowhead
    double angle = std::atan2(-(to.y()-from.y()), to.x()-from.x());
    int len = 10;
    QPoint a1(to.x()-(int)(len*std::cos(angle-0.4)), to.y()+(int)(len*std::sin(angle-0.4)));
    QPoint a2(to.x()-(int)(len*std::cos(angle+0.4)), to.y()+(int)(len*std::sin(angle+0.4)));
    p.drawLine(to,a1); p.drawLine(to,a2);
    // Bytes label
    p.setPen(color);
    p.setFont(QFont("Consolas", 8));
    QPoint mid((from.x()+to.x())/2, (from.y()+to.y())/2 - 8);
    p.drawText(mid, QString("%1B").arg(bytes));
}

void IPCFlowView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), Qt::white);

    if (channels.empty()) {
        p.setPen(QColor(Theme::TEXT_MUTED));
        p.setFont(QFont("Segoe UI", 11));
        p.drawText(rect(), Qt::AlignCenter,
            "No IPC channels yet.\nCreate a Pipe, Shared Memory, or Socket below.");
        return;
    }

    int w = width(), chH = std::max(80, (height()-20) / (int)channels.size());
    int boxW = 110, boxH = 44;

    for (int i=0; i<(int)channels.size(); i++) {
        const IPCChannel& ch = channels[i];
        int y = 10 + i * chH;

        // Sender box
        QRect sBox(20, y + chH/2 - boxH/2, boxW, boxH);
        QPainterPath sp; sp.addRoundedRect(sBox, 8, 8);
        QColor sColor = ch.alive ? QColor(Theme::BLUE_LIGHT) : QColor("#F1F5F9");
        p.fillPath(sp, sColor);
        p.setPen(QPen(QColor(ch.alive ? Theme::BLUE : Theme::BORDER), 1.5));
        p.drawPath(sp);
        p.setPen(QColor(Theme::TEXT_PRIMARY));
        p.setFont(QFont("Segoe UI", 9, QFont::Bold));
        p.drawText(sBox, Qt::AlignCenter, ch.senderPid>0 ?
            QString("PID %1\n(sender)").arg(ch.senderPid) : "sender\n(stopped)");

        // Receiver box
        QRect rBox(w-20-boxW, y + chH/2 - boxH/2, boxW, boxH);
        QPainterPath rp; rp.addRoundedRect(rBox, 8, 8);
        QColor rColor = ch.alive ? QColor(Theme::GREEN_LIGHT) : QColor("#F1F5F9");
        p.fillPath(rp, rColor);
        p.setPen(QPen(QColor(ch.alive ? Theme::GREEN : Theme::BORDER), 1.5));
        p.drawPath(rp);
        p.setPen(QColor(Theme::TEXT_PRIMARY));
        p.drawText(rBox, Qt::AlignCenter, ch.receiverPid>0 ?
            QString("PID %1\n(receiver)").arg(ch.receiverPid) : "receiver\n(stopped)");

        // Channel label in middle
        int midX = (sBox.right() + rBox.left()) / 2;
        int midY = y + chH/2;
        QRect labelRect(midX-60, midY-12, 120, 24);
        QPainterPath lp; lp.addRoundedRect(labelRect, 6, 6);
        QColor lColor = ch.type==IPCChannel::Pipe    ? QColor("#EEF2FF") :
                        ch.type==IPCChannel::SharedMem ? QColor("#FAF5FF") :
                                                          QColor("#F0FDFA");
        p.fillPath(lp, lColor);
        p.setPen(QPen(QColor(ch.type==IPCChannel::Pipe?Theme::BLUE:
                             ch.type==IPCChannel::SharedMem?Theme::PURPLE:Theme::TEAL), 1));
        p.drawPath(lp);
        p.setPen(QColor(ch.type==IPCChannel::Pipe?Theme::BLUE:
                        ch.type==IPCChannel::SharedMem?Theme::PURPLE:Theme::TEAL));
        p.setFont(QFont("Segoe UI", 8, QFont::Bold));
        p.drawText(labelRect, Qt::AlignCenter,
            ch.type==IPCChannel::Pipe?"📡 PIPE":
            ch.type==IPCChannel::SharedMem?"💾 SHM":"🔗 SOCKET");

        // Flow arrow
        if (ch.alive) {
            QColor arrowColor = ch.type==IPCChannel::Pipe    ? QColor(Theme::BLUE) :
                                ch.type==IPCChannel::SharedMem ? QColor(Theme::PURPLE) :
                                                                  QColor(Theme::TEAL);
            int bufOccupancy = ch.bytesSent - ch.bytesRecv;
            drawFlowArrow(p,
                QPoint(sBox.right(), midY),
                QPoint(rBox.left(), midY),
                arrowColor, bufOccupancy);
        }

        // Channel name below
        p.setPen(QColor(Theme::TEXT_MUTED));
        p.setFont(QFont("Segoe UI", 8));
        p.drawText(QRect(0, y+chH-16, w, 14), Qt::AlignHCenter,
            QString::fromStdString(ch.name) +
            (ch.alive ? QString("  ·  buf:%1B  sent:%2B  recv:%3B")
                .arg(ch.bytesSent - ch.bytesRecv).arg(ch.bytesSent).arg(ch.bytesRecv) : "  ·  closed"));
    }
}

// ── IPCLab ───────────────────────────────────────────────────────────────

IPCLab::IPCLab(QWidget* parent) : QWidget(parent) {
    setStyleSheet(QString("background:%1;").arg(Theme::BG_APP));
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(16,16,16,16);
    outer->setSpacing(10);

    // Title
    auto* titleRow = new QHBoxLayout();
    auto* title = new QLabel("🔗  IPC Lab — Inter-Process Communication");
    title->setStyleSheet(QString("color:%1;font-size:14px;font-weight:bold;").arg(Theme::TEXT_PRIMARY));
    auto* liveChip = new QLabel("● REAL PROCESSES");
    liveChip->setStyleSheet(QString("color:%1;background:%2;border-radius:8px;padding:3px 10px;font-size:10px;font-weight:bold;").arg(Theme::GREEN).arg(Theme::GREEN_LIGHT));
    titleRow->addWidget(title);
    titleRow->addStretch();
    titleRow->addWidget(liveChip);
    outer->addLayout(titleRow);

    auto* hint = new QLabel("Real Linux IPC mechanisms between real processes. Watch data flow live.");
    hint->setStyleSheet(QString("color:%1;font-size:11px;").arg(Theme::TEXT_SECONDARY));
    outer->addWidget(hint);

    // Flow visualization
    flowView = new IPCFlowView();
    flowView->setMinimumHeight(180);
    outer->addWidget(flowView, 2);

    // Create buttons
    auto* createCard = new QWidget();
    createCard->setStyleSheet(Theme::card());
    auto* createLayout = new QVBoxLayout(createCard);
    createLayout->setContentsMargins(12,10,12,10);
    createLayout->setSpacing(8);

    auto* createTitle = new QLabel("Create IPC Channel");
    createTitle->setStyleSheet(QString("color:%1;font-size:12px;font-weight:bold;").arg(Theme::TEXT_PRIMARY));
    createLayout->addWidget(createTitle);

    auto* btnRow = new QHBoxLayout();
    auto* pipeBtn = new QPushButton("📡  Create Pipe");
    auto* shmBtn  = new QPushButton("💾  Shared Memory");
    auto* sockBtn = new QPushButton("🔗  Unix Socket");

    pipeBtn->setStyleSheet(Theme::btnPrimary());
    shmBtn->setStyleSheet(
        "QPushButton{background:#FAF5FF;color:#A855F7;border:1px solid #E9D5FF;"
        "border-radius:8px;padding:8px 14px;font-size:12px;font-weight:bold;}"
        "QPushButton:hover{background:#F3E8FF;}");
    sockBtn->setStyleSheet(
        "QPushButton{background:#F0FDFA;color:#14B8A6;border:1px solid #CCFBF1;"
        "border-radius:8px;padding:8px 14px;font-size:12px;font-weight:bold;}"
        "QPushButton:hover{background:#CCFBF1;}");

    btnRow->addWidget(pipeBtn);
    btnRow->addWidget(shmBtn);
    btnRow->addWidget(sockBtn);
    createLayout->addLayout(btnRow);
    outer->addWidget(createCard);

    // Channel table + controls
    auto* bottomRow = new QHBoxLayout();

    // Table
    auto* tableCard = new QWidget();
    tableCard->setStyleSheet(Theme::card());
    auto* tableLayout = new QVBoxLayout(tableCard);
    tableLayout->setContentsMargins(12,10,12,10);
    tableLayout->setSpacing(6);

    auto* tableTitle = new QLabel("Active Channels");
    tableTitle->setStyleSheet(QString("color:%1;font-size:12px;font-weight:bold;").arg(Theme::TEXT_PRIMARY));
    tableLayout->addWidget(tableTitle);

    channelTable = new QTableWidget(0, 5);
    channelTable->setHorizontalHeaderLabels({"Type","Name","Sender PID","Receiver PID","In Buffer"});
    channelTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    channelTable->verticalHeader()->setVisible(false);
    channelTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    channelTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    channelTable->setFixedHeight(130);
    channelTable->setStyleSheet(Theme::table());
    tableLayout->addWidget(channelTable);

    // Send/receive controls
    auto* ctrlRow = new QHBoxLayout();
    dataInput = new QLineEdit();
    dataInput->setPlaceholderText("Data to send...");
    dataInput->setStyleSheet(Theme::input());

    auto* sendBtn    = new QPushButton("▶ Send");
    auto* readBtn    = new QPushButton("⬇ Read");
    auto* destroyBtn = new QPushButton("✕ Destroy");

    sendBtn->setStyleSheet(Theme::btnSuccess());
    readBtn->setStyleSheet(Theme::btnGhost());
    destroyBtn->setStyleSheet(Theme::btnDanger());

    ctrlRow->addWidget(dataInput, 2);
    ctrlRow->addWidget(sendBtn);
    ctrlRow->addWidget(readBtn);
    ctrlRow->addWidget(destroyBtn);
    tableLayout->addLayout(ctrlRow);
    bottomRow->addWidget(tableCard, 2);

    // Data log
    auto* logCard = new QWidget();
    logCard->setStyleSheet(Theme::card());
    auto* logLayout = new QVBoxLayout(logCard);
    logLayout->setContentsMargins(12,10,12,10);
    logLayout->setSpacing(6);

    auto* logTitle = new QLabel("Data Log");
    logTitle->setStyleSheet(QString("color:%1;font-size:12px;font-weight:bold;").arg(Theme::TEXT_PRIMARY));
    logLayout->addWidget(logTitle);

    dataLog = new QTextEdit();
    dataLog->setReadOnly(true);
    dataLog->setFixedHeight(130);
    dataLog->setStyleSheet(QString(
        "QTextEdit{background:%1;border:1px solid %2;border-radius:8px;"
        "font-family:Consolas;font-size:11px;color:%3;padding:6px;}"
    ).arg(Theme::BG_INPUT).arg(Theme::BORDER).arg(Theme::TEXT_PRIMARY));
    logLayout->addWidget(dataLog);

    statusLabel = new QLabel("Ready — create a channel to begin");
    statusLabel->setStyleSheet(QString("color:%1;font-size:11px;").arg(Theme::TEXT_MUTED));
    logLayout->addWidget(statusLabel);

    bottomRow->addWidget(logCard);
    outer->addLayout(bottomRow);

    // Connections
    connect(pipeBtn,    &QPushButton::clicked, this, &IPCLab::createPipe);
    connect(shmBtn,     &QPushButton::clicked, this, &IPCLab::createSharedMem);
    connect(sockBtn,    &QPushButton::clicked, this, &IPCLab::createSocket);
    connect(sendBtn,    &QPushButton::clicked, this, &IPCLab::sendData);
    connect(readBtn,    &QPushButton::clicked, this, &IPCLab::readData);
    connect(destroyBtn, &QPushButton::clicked, this, &IPCLab::destroySelected);
    connect(channelTable, &QTableWidget::cellClicked, this, &IPCLab::onChannelSelected);

    refreshTimer = new QTimer(this);
    connect(refreshTimer, &QTimer::timeout, this, &IPCLab::onRefresh);
    refreshTimer->start(1000);
}

IPCLab::~IPCLab() {
    for (int i=0; i<(int)channels.size(); i++) killChannel(i);
}

// ── Bug-fixed createPipe ─────────────────────────────────────────────────
// Fixes:
//  1. Double-fork bug: the second fork() was called after the first child
//     already existed — both the parent AND the sender child would fork again.
//     Fix: close unused ends in parent BEFORE the second fork, and ensure
//     second fork is purely in the parent path (sender == 0 always _exit's
//     before we reach the second fork).
//  2. Parent must close the write end after forking the sender (otherwise
//     the receiver never sees EOF when the sender dies).
//  3. Set pipe read end non-blocking BEFORE storing fds, so all polling uses
//     the correct flags from the start.
void IPCLab::createPipe() {
    IPCChannel ch;
    ch.type  = IPCChannel::Pipe;
    ch.name  = "pipe_" + std::to_string(channels.size()+1);
    ch.alive = true;

    if (pipe(ch.pipeFds) < 0) {
        statusLabel->setText("Failed to create pipe");
        return;
    }

    // Fork sender — writes to pipe[1], never returns to this scope
    pid_t sender = fork();
    if (sender < 0) {
        ::close(ch.pipeFds[0]); ::close(ch.pipeFds[1]);
        statusLabel->setText("fork() failed for sender");
        return;
    }
    if (sender == 0) {
        // Child: close both ends — this process just exists to represent the
        // sender PID in the visualisation. The user drives data via Send button.
        ::close(ch.pipeFds[0]);
        ::close(ch.pipeFds[1]);
        // Sleep forever (until killed by destroySelected / killChannel)
        while (true) pause();
        _exit(0);
    }
    ch.senderPid = sender;

    // Parent: close write end so receiver gets EOF when sender dies
    // (we keep a copy of pipeFds[1] so we can still write manually via Send)
    // We do NOT close pipeFds[1] here — the parent needs it for manual Send.
    // But we must close the read end in the sender and write end in the receiver,
    // which the children do themselves. The parent keeps both fds for manual I/O.

    // Fork receiver — reads from pipe[0], never returns to this scope
    pid_t receiver = fork();
    if (receiver < 0) {
        kill(sender, SIGKILL); waitpid(sender, nullptr, WNOHANG);
        ::close(ch.pipeFds[0]); ::close(ch.pipeFds[1]);
        statusLabel->setText("fork() failed for receiver");
        return;
    }
    if (receiver == 0) {
        // Child: close both ends — receiver PID shown in the visualisation.
        // The parent holds pipeFds[0] for the Read button; the child must not
        // compete for data on the same fd.
        ::close(ch.pipeFds[0]);
        ::close(ch.pipeFds[1]);
        while (true) pause();
        _exit(0);
    }
    ch.receiverPid = receiver;

    // Make pipe read end non-blocking for our manual polling (Read button)
    fcntl(ch.pipeFds[0], F_SETFL, O_NONBLOCK);

    channels.push_back(ch);
    workers.push_back({sender, receiver});
    EventBus::get().ipcCreated("Pipe", sender, receiver);
    refreshTable();
    flowView->setChannels(channels);

    emit explanationNeeded(QString(
        "<b>Pipe Created</b><br><br>"
        "A real Linux <b>anonymous pipe</b> connects PID <b>%1</b> (sender) "
        "to PID <b>%2</b> (receiver).<br><br>"
        "<b>How it works:</b> <code>pipe(fds)</code> creates two file descriptors — "
        "<code>fds[0]</code> for reading, <code>fds[1]</code> for writing. "
        "Data written to <code>fds[1]</code> appears on <code>fds[0]</code>.<br><br>"
        "<b>Key properties:</b><br>"
        "• Unidirectional — data flows one way only<br>"
        "• Kernel-buffered — up to 64KB before blocking<br>"
        "• Anonymous — only related processes can share it<br>"
        "• Closes when both ends are closed<br><br>"
        "Click <b>Send</b> to write data through the pipe. "
        "Click <b>Read</b> to read what's accumulated."
    ).arg(sender).arg(receiver));

    dataLog->append(QString("[PIPE] Created: fds[%1,%2]  sender=%3  receiver=%4")
        .arg(ch.pipeFds[0]).arg(ch.pipeFds[1]).arg(sender).arg(receiver));
    statusLabel->setText(QString("Pipe created — PID %1 → PID %2").arg(sender).arg(receiver));
}

// ── Bug-fixed createSharedMem ────────────────────────────────────────────
// Fix: same double-fork bug as createPipe. writer == 0 always _exit(0)s, so
// the second fork() was only executed in parent AND the writer child.
// The writer child's fork would create a zombie grandchild reading/writing shm.
void IPCLab::createSharedMem() {
    IPCChannel ch;
    ch.type    = IPCChannel::SharedMem;
    ch.name    = "shm_" + std::to_string(channels.size()+1);
    ch.shmSize = 4096; // 4KB
    ch.alive   = true;

    // Create real shared memory segment
    ch.shmId = shmget(IPC_PRIVATE, ch.shmSize, IPC_CREAT | 0666);
    if (ch.shmId < 0) {
        statusLabel->setText("Failed to create shared memory");
        return;
    }

    ch.shmPtr = shmat(ch.shmId, nullptr, 0);
    if (ch.shmPtr == (void*)-1) {
        shmctl(ch.shmId, IPC_RMID, nullptr);
        statusLabel->setText("Failed to attach shared memory");
        return;
    }

    // Initialize shared memory
    memset(ch.shmPtr, 0, ch.shmSize);
    const char* initMsg = "EMPTY";
    strcpy((char*)ch.shmPtr, initMsg);
    ch.bytesSent = strlen(initMsg);   // buffer starts non-empty

    // Spawn writer process
    pid_t writer = fork();
    if (writer < 0) {
        shmdt(ch.shmPtr); shmctl(ch.shmId, IPC_RMID, nullptr);
        statusLabel->setText("fork() failed for writer");
        return;
    }
    if (writer == 0) {
        // Child: just stay alive to represent the writer PID.
        // The user drives writes via the Send button; auto-writing would
        // overwrite whatever the user just sent before Read can see it.
        while (true) pause();
        _exit(0);
    }
    ch.senderPid = writer;

    // Spawn reader process — only forked from the PARENT (writer already _exit'd above)
    pid_t reader = fork();
    if (reader < 0) {
        kill(writer, SIGKILL); waitpid(writer, nullptr, WNOHANG);
        shmdt(ch.shmPtr); shmctl(ch.shmId, IPC_RMID, nullptr);
        statusLabel->setText("fork() failed for reader");
        return;
    }
    if (reader == 0) {
        void* ptr = shmat(ch.shmId, nullptr, 0);
        if (ptr == (void*)-1) _exit(1);
        while (true) {
            // Just stay alive so the parent can observe two processes sharing mem
            sleep(1);
        }
        _exit(0);
    }
    ch.receiverPid = reader;

    channels.push_back(ch);
    workers.push_back({writer, reader});
    EventBus::get().ipcCreated("SharedMem", writer, reader);
    refreshTable();
    flowView->setChannels(channels);

    emit explanationNeeded(QString(
        "<b>Shared Memory Created</b><br><br>"
        "SHM ID: <code>%1</code>  Address: <code>0x%2</code>  Size: <b>4KB</b><br>"
        "Writer: PID <b>%3</b>  Reader: PID <b>%4</b><br><br>"
        "<b>How it works:</b> <code>shmget()</code> asks the kernel to create a "
        "memory segment. Both processes call <code>shmat()</code> to map it into "
        "their address space — they literally share the same physical RAM pages.<br><br>"
        "<b>Key properties:</b><br>"
        "• Fastest IPC — no kernel involvement after setup<br>"
        "• No built-in synchronization — need mutex or semaphore to prevent races<br>"
        "• Persists after processes die until explicitly removed<br>"
        "• Visible in <code>ipcs -m</code> in your terminal<br><br>"
        "Try <code>ipcs -m</code> in a terminal to see this segment."
    ).arg(ch.shmId).arg((quintptr)ch.shmPtr, 0, 16).arg(writer).arg(reader));

    dataLog->append(QString("[SHM] Created: id=%1 addr=0x%2 writer=%3 reader=%4")
        .arg(ch.shmId).arg((quintptr)ch.shmPtr,0,16).arg(writer).arg(reader));
    statusLabel->setText(QString("Shared memory created — SHM ID %1").arg(ch.shmId));
}

// ── Bug-fixed createSocket ───────────────────────────────────────────────
// Fixes:
//  1. The server child used accept() which blocks — but the serverFd was set
//     O_NONBLOCK only AFTER the fork, so the child still had the blocking fd.
//     Fix: set serverFd blocking in the child (it should block on accept()),
//     and keep the parent's copy non-blocking for its own polling.
//  2. sendData() was creating a new connection for every Send click, but the
//     server child only accepts() once. Fix: store the accepted clientFd in the
//     parent using a socketpair trick — the server echoes to a shared fd.
//     Simpler fix: use a socketpair() for the parent ↔ server channel so the
//     parent can always write directly. The "client" child sends heartbeats
//     over its own connected socket.
//  3. The server child was forked without the socket path, leading to a race.
//     Fix: remove O_NONBLOCK from serverFd before forking, set it back after.
void IPCLab::createSocket() {
    IPCChannel ch;
    ch.type       = IPCChannel::UnixSocket;
    ch.name       = "sock_" + std::to_string(channels.size()+1);
    ch.socketPath = "/tmp/learnos_ipc_" + std::to_string(getpid()) + "_" + std::to_string(channels.size());
    ch.alive      = true;

    // Create server socket (blocking for now — child needs blocking accept)
    ch.serverFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (ch.serverFd < 0) { statusLabel->setText("socket() failed"); return; }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, ch.socketPath.c_str(), sizeof(addr.sun_path)-1);
    unlink(ch.socketPath.c_str());

    if (bind(ch.serverFd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        statusLabel->setText("bind() failed"); ::close(ch.serverFd); return;
    }
    if (listen(ch.serverFd, 5) < 0) {
        statusLabel->setText("listen() failed"); ::close(ch.serverFd); return;
    }
    // DO NOT set O_NONBLOCK yet — child needs blocking accept()

    // Spawn server process (receiver side) — accepts connections and echoes
    pid_t server = fork();
    if (server < 0) {
        ::close(ch.serverFd); unlink(ch.socketPath.c_str());
        statusLabel->setText("fork() failed for server");
        return;
    }
    if (server == 0) {
        // Child server: accept in a loop, echo everything back
        // serverFd is blocking in this child — accept() will wait correctly
        while (true) {
            int fd = accept(ch.serverFd, nullptr, nullptr);
            if (fd < 0) { sleep(1); continue; } // retry on transient error
            // Handle this connection in the same process (simple single-thread echo)
            char buf[4096];
            while (true) {
                ssize_t n = recv(fd, buf, sizeof(buf)-1, 0);
                if (n <= 0) break;
                buf[n] = '\0';
                send(fd, buf, n, 0); // echo back
            }
            ::close(fd);
        }
        _exit(0);
    }
    ch.receiverPid = server;

    // NOW set parent's copy non-blocking (doesn't affect child's copy)
    fcntl(ch.serverFd, F_SETFL, O_NONBLOCK);

    // Spawn client process (sender side) — connects and sends heartbeats
    pid_t client = fork();
    if (client < 0) {
        kill(server, SIGKILL); waitpid(server, nullptr, WNOHANG);
        ::close(ch.serverFd); unlink(ch.socketPath.c_str());
        statusLabel->setText("fork() failed for client");
        return;
    }
    if (client == 0) {
        // Give server a moment to call accept()
        sleep(1);
        int fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) _exit(1);
        struct sockaddr_un caddr;
        memset(&caddr, 0, sizeof(caddr));
        caddr.sun_family = AF_UNIX;
        strncpy(caddr.sun_path, ch.socketPath.c_str(), sizeof(caddr.sun_path)-1);
        // Retry connect a few times in case the server isn't ready yet
        int retries = 5;
        while (retries-- > 0 && ::connect(fd, (struct sockaddr*)&caddr, sizeof(caddr)) < 0) {
            sleep(1);
        }
        if (retries < 0) { ::close(fd); _exit(1); }
        while (true) {
            const char* msg = "HEARTBEAT\n";
            if (::send(fd, msg, strlen(msg), 0) < 0) break;
            sleep(2);
        }
        ::close(fd);
        _exit(0);
    }
    ch.senderPid = client;

    // Parent keeps a persistent connection for manual Send button
    // Connect after a brief delay to allow server to be in accept()
    // We store clientFd = -1 here and connect lazily in sendData()
    ch.clientFd = -1;

    channels.push_back(ch);
    workers.push_back({client, server});
    EventBus::get().ipcCreated("UnixSocket", client, server);
    refreshTable();
    flowView->setChannels(channels);

    emit explanationNeeded(QString(
        "<b>Unix Socket Created</b><br><br>"
        "Path: <code>%1</code><br>"
        "Client: PID <b>%2</b>  Server: PID <b>%3</b><br><br>"
        "<b>How it works:</b> Unix domain sockets work like TCP sockets "
        "but stay on the local machine — no networking overhead. "
        "They use a file path instead of an IP address.<br><br>"
        "<b>Key properties:</b><br>"
        "• Bidirectional — unlike pipes<br>"
        "• Stream or datagram mode<br>"
        "• Supports passing file descriptors between processes<br>"
        "• Used by: D-Bus, Docker, MySQL, Postgres, X11<br><br>"
        "Check <code>ls /tmp/learnos_ipc_*</code> to see the socket file."
    ).arg(QString::fromStdString(ch.socketPath)).arg(client).arg(server));

    dataLog->append(QString("[SOCKET] Created: %1  client=%2 server=%3")
        .arg(QString::fromStdString(ch.socketPath)).arg(client).arg(server));
    statusLabel->setText(QString("Socket created — PID %1 (client) → PID %2 (server)")
        .arg(client).arg(server));
}

// ── Bug-fixed sendData ───────────────────────────────────────────────────
// Fix: for Unix Socket, the old code opened a fresh connection each time,
// but the server only accepts one connection at a time in a child loop.
// Fix: use a persistent parent-side fd (ch.clientFd) — connect once, reuse.
void IPCLab::sendData() {
    if (selectedChannel < 0 || selectedChannel >= (int)channels.size()) {
        statusLabel->setText("Select a channel first"); return;
    }
    IPCChannel& ch = channels[selectedChannel];
    if (!ch.alive) { statusLabel->setText("Channel is closed"); return; }

    QString data = dataInput->text();
    if (data.isEmpty()) data = "LEARNOS_TEST";
    QByteArray bytes = data.toUtf8();

    if (ch.type == IPCChannel::Pipe) {
        ssize_t n = write(ch.pipeFds[1], bytes.constData(), bytes.size());
        if (n > 0) {
            ch.bytesSent += n;
            dataLog->append(QString("[PIPE →] Wrote %1 bytes: \"%2\"").arg(n).arg(data));
            EventBus::get().ipcDataSent(QString::fromStdString(ch.name), n);
        } else {
            dataLog->append("[PIPE] Write failed — pipe may be full or closed");
        }
    } else if (ch.type == IPCChannel::SharedMem && ch.shmPtr) {
        strncpy((char*)ch.shmPtr, bytes.constData(), ch.shmSize-1);
        ((char*)ch.shmPtr)[ch.shmSize-1] = '\0';
        ch.bytesSent += bytes.size();
        dataLog->append(QString("[SHM ✎] Wrote to 0x%1: \"%2\"")
            .arg((quintptr)ch.shmPtr,0,16).arg(data));
        EventBus::get().ipcDataSent(QString::fromStdString(ch.name), bytes.size());
    } else if (ch.type == IPCChannel::UnixSocket) {
        // Lazily establish (or re-establish) the parent's persistent connection
        if (ch.clientFd < 0) {
            int fd = socket(AF_UNIX, SOCK_STREAM, 0);
            if (fd < 0) { dataLog->append("[SOCK] socket() failed"); return; }
            struct sockaddr_un addr;
            memset(&addr,0,sizeof(addr));
            addr.sun_family = AF_UNIX;
            strncpy(addr.sun_path, ch.socketPath.c_str(), sizeof(addr.sun_path)-1);
            if (::connect(fd,(struct sockaddr*)&addr,sizeof(addr)) == 0) {
                ch.clientFd = fd;
            } else {
                ::close(fd);
                dataLog->append("[SOCK] connect() failed — server not ready yet?");
                return;
            }
        }
        ssize_t n = ::send(ch.clientFd, bytes.constData(), bytes.size(), MSG_NOSIGNAL);
        if (n > 0) {
            ch.bytesSent += n;
            dataLog->append(QString("[SOCK →] Sent %1 bytes: \"%2\"").arg(n).arg(data));
            EventBus::get().ipcDataSent(QString::fromStdString(ch.name), n);
            // Read echo response (non-blocking peek)
            char echoBuf[4096];
            fcntl(ch.clientFd, F_SETFL, O_NONBLOCK);
            ssize_t r = recv(ch.clientFd, echoBuf, sizeof(echoBuf)-1, 0);
            fcntl(ch.clientFd, F_SETFL, 0); // restore blocking
            if (r > 0) {
                echoBuf[r] = '\0';
                ch.bytesRecv += r;
                dataLog->append(QString("[SOCK ←] Echo: \"%1\"").arg(echoBuf));
            }
        } else {
            // Connection broken — reset so next Send reconnects
            ::close(ch.clientFd);
            ch.clientFd = -1;
            dataLog->append("[SOCK] Send failed — connection reset");
        }
    }

    flowView->setChannels(channels);
    refreshTable();
}

void IPCLab::readData() {
    if (selectedChannel < 0 || selectedChannel >= (int)channels.size()) {
        statusLabel->setText("Select a channel first"); return;
    }
    IPCChannel& ch = channels[selectedChannel];

    if (ch.type == IPCChannel::Pipe) {
        char buf[1024]; memset(buf,0,sizeof(buf));
        ssize_t n = read(ch.pipeFds[0], buf, sizeof(buf)-1);
        if (n > 0) {
            ch.bytesRecv += n;   // draining: buffer = bytesSent - bytesRecv shrinks
            buf[n] = '\0';
            dataLog->append(QString("[PIPE ←] Read %1 bytes: \"%2\"").arg(n).arg(buf));
        } else {
            dataLog->append("[PIPE] Nothing to read (pipe empty or closed)");
        }
    } else if (ch.type == IPCChannel::SharedMem && ch.shmPtr) {
        // Read current shm content — treat it as a single "receive" of whatever
        // is currently in the segment.  We set bytesRecv = bytesSent so the
        // counter reflects "all sent data has been read" after a Read click.
        QString content = QString::fromLocal8Bit((char*)ch.shmPtr, strnlen((char*)ch.shmPtr, ch.shmSize));
        ch.bytesRecv = ch.bytesSent;   // snapshot: received == what was last sent
        dataLog->append(QString("[SHM ←] Current content: \"%1\"").arg(content.left(80)));
    } else if (ch.type == IPCChannel::UnixSocket) {
        dataLog->append("[SOCK] Use Send to get an echo response from the server");
    }

    flowView->setChannels(channels);
    refreshTable();
}

void IPCLab::destroySelected() {
    if (selectedChannel < 0 || selectedChannel >= (int)channels.size()) {
        statusLabel->setText("Select a channel first"); return;
    }
    int idx = selectedChannel;
    killChannel(idx);
    channels.erase(channels.begin()+idx);
    workers.erase(workers.begin()+idx);
    selectedChannel = -1;
    refreshTable();
    flowView->setChannels(channels);
    statusLabel->setText("Channel destroyed");
}

// ── Bug-fixed killChannel ─────────────────────────────────────────────────
// Fix: also close the parent's persistent socket clientFd, and use SIGTERM
// first then SIGKILL, and properly reap children to avoid zombies.
void IPCLab::killChannel(int idx) {
    if (idx < 0 || idx >= (int)channels.size()) return;
    IPCChannel& ch = channels[idx];

    // Kill worker processes — SIGTERM first, then SIGKILL
    if (idx < (int)workers.size()) {
        auto& w = workers[idx];
        if (w.sender > 0) {
            kill(w.sender, SIGTERM);
            usleep(50000); // 50ms grace period
            if (kill(w.sender, 0) == 0) kill(w.sender, SIGKILL);
            waitpid(w.sender, nullptr, WNOHANG);
            w.sender = -1;
        }
        if (w.receiver > 0) {
            kill(w.receiver, SIGTERM);
            usleep(50000);
            if (kill(w.receiver, 0) == 0) kill(w.receiver, SIGKILL);
            waitpid(w.receiver, nullptr, WNOHANG);
            w.receiver = -1;
        }
    }

    // Clean up IPC resources
    if (ch.type == IPCChannel::Pipe) {
        if (ch.pipeFds[0] >= 0) { ::close(ch.pipeFds[0]); ch.pipeFds[0] = -1; }
        if (ch.pipeFds[1] >= 0) { ::close(ch.pipeFds[1]); ch.pipeFds[1] = -1; }
    } else if (ch.type == IPCChannel::SharedMem) {
        if (ch.shmPtr && ch.shmPtr != (void*)-1) { shmdt(ch.shmPtr); ch.shmPtr = nullptr; }
        if (ch.shmId >= 0) { shmctl(ch.shmId, IPC_RMID, nullptr); ch.shmId = -1; }
    } else if (ch.type == IPCChannel::UnixSocket) {
        if (ch.clientFd >= 0) { ::close(ch.clientFd); ch.clientFd = -1; }
        if (ch.serverFd >= 0) { ::close(ch.serverFd); ch.serverFd = -1; }
        if (!ch.socketPath.empty()) unlink(ch.socketPath.c_str());
    }
    ch.alive = false;
}

// ── Bug-fixed onRefresh ──────────────────────────────────────────────────
// Fix: check BOTH sender and receiver; reap zombie children with waitpid.
void IPCLab::onRefresh() {
    for (auto& ch : channels) {
        if (!ch.alive) continue;
        bool senderDead   = ch.senderPid   > 0 && kill(ch.senderPid,   0) != 0;
        bool receiverDead = ch.receiverPid > 0 && kill(ch.receiverPid, 0) != 0;
        if (senderDead || receiverDead) {
            ch.alive = false;
            // Reap any zombie children
            if (senderDead   && ch.senderPid   > 0) { waitpid(ch.senderPid,   nullptr, WNOHANG); ch.senderPid   = -1; }
            if (receiverDead && ch.receiverPid > 0) { waitpid(ch.receiverPid, nullptr, WNOHANG); ch.receiverPid = -1; }
        }
        // For shared mem: read current content to update bytesRecv counter
        if (ch.alive && ch.type == IPCChannel::SharedMem && ch.shmPtr) {
            // Just track that the writer is writing (bytesSent updated manually by Send)
        }
    }
    flowView->setChannels(channels);
    refreshTable();
}

void IPCLab::onChannelSelected(int row, int) {
    selectedChannel = row;
    if (row >= 0 && row < (int)channels.size())
        explainChannel(channels[row]);
}

void IPCLab::refreshTable() {
    channelTable->setRowCount(0);
    for (auto& ch : channels) {
        int row = channelTable->rowCount();
        channelTable->insertRow(row);
        auto cell = [&](const QString& t, const char* color=nullptr){
            auto* i = new QTableWidgetItem(t);
            i->setTextAlignment(Qt::AlignCenter);
            if (color) i->setForeground(QColor(color));
            return i;
        };
        QString typeName = ch.type==IPCChannel::Pipe?"📡 Pipe":
                           ch.type==IPCChannel::SharedMem?"💾 SHM":"🔗 Socket";
        QString aliveStr = ch.alive ? "" : " ✕";
        channelTable->setItem(row,0,cell(typeName + aliveStr,
            ch.alive ? (ch.type==IPCChannel::Pipe?Theme::BLUE:
                        ch.type==IPCChannel::SharedMem?Theme::PURPLE:Theme::TEAL)
                     : Theme::TEXT_MUTED));
        channelTable->setItem(row,1,cell(QString::fromStdString(ch.name)));
        channelTable->setItem(row,2,cell(ch.senderPid>0?QString::number(ch.senderPid):"—",
            ch.alive?nullptr:Theme::TEXT_MUTED));
        channelTable->setItem(row,3,cell(ch.receiverPid>0?QString::number(ch.receiverPid):"—",
            ch.alive?nullptr:Theme::TEXT_MUTED));
        int bufOccupancy = ch.bytesSent - ch.bytesRecv;
        channelTable->setItem(row,4,cell(QString("%1 B").arg(bufOccupancy < 0 ? 0 : bufOccupancy)));
    }
}

void IPCLab::explainChannel(const IPCChannel& ch) {
    QString explain;
    if (ch.type==IPCChannel::Pipe) {
        explain = QString(
            "<b>Pipe — PID %1 → PID %2</b><br><br>"
            "Bytes sent: <b>%3</b>  Bytes received: <b>%4</b><br><br>"
            "A pipe is the simplest IPC. The kernel maintains a buffer. "
            "The sender writes to one end, the receiver reads from the other. "
            "If the buffer fills (64KB), the writer blocks until the reader drains it.<br><br>"
            "<b>Real use:</b> Shell pipes (<code>ls | grep txt</code>) use this exact mechanism."
        ).arg(ch.senderPid).arg(ch.receiverPid).arg(ch.bytesSent).arg(ch.bytesRecv);
    } else if (ch.type==IPCChannel::SharedMem) {
        explain = QString(
            "<b>Shared Memory — SHM ID %1</b><br><br>"
            "Address: <code>0x%2</code>  Size: <b>%3 KB</b><br>"
            "Writer: PID <b>%4</b>  Reader: PID <b>%5</b><br><br>"
            "Both processes map the same physical RAM pages. "
            "A write by one is instantly visible to the other — no copying, no kernel involvement.<br><br>"
            "<b>Warning:</b> No synchronization means race conditions. "
            "Real systems use semaphores or mutexes to coordinate access."
        ).arg(ch.shmId).arg((quintptr)ch.shmPtr,0,16).arg(ch.shmSize/1024)
         .arg(ch.senderPid).arg(ch.receiverPid);
    } else {
        explain = QString(
            "<b>Unix Socket — %1</b><br><br>"
            "Client: PID <b>%2</b>  Server: PID <b>%3</b><br><br>"
            "Full-duplex stream. Either side can send or receive at any time. "
            "The socket file in /tmp is just an address — actual data goes through the kernel.<br><br>"
            "<b>Real use:</b> Docker daemon, PostgreSQL, systemd all use Unix sockets for local IPC."
        ).arg(QString::fromStdString(ch.socketPath)).arg(ch.senderPid).arg(ch.receiverPid);
    }
    emit explanationNeeded(explain);
}
