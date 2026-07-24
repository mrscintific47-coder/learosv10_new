#include "FsCanvas.h"
#include "Theme.h"
#include <QMenu>
#include <QEvent>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionGraphicsItem>
#include <QGraphicsSceneMouseEvent>
#include <QInputDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QDir>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QApplication>
#include <QLineEdit>
#include <QRandomGenerator>
#include <QDateTime>
#include <QCoreApplication>
#include <QTextEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QScrollArea>
#include <QProcess>
#include <QTimer>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <sys/file.h>
#include <cerrno>
#include <pwd.h>
#include <grp.h>
#include <dirent.h>
#include <cstdio>
#include <algorithm>
#include <chrono>
#include <functional>

// ─────────────────────────────────────────────────────────────────────────────
// BreadcrumbBar
// ─────────────────────────────────────────────────────────────────────────────

BreadcrumbBar::BreadcrumbBar(QWidget* parent) : QWidget(parent) {
    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(0,0,0,0);
    m_layout->setSpacing(2);
    setStyleSheet("background:transparent;");
}

void BreadcrumbBar::setPath(const QString& sandboxRoot, const QString& contextDir) {
    while (m_layout->count()) {
        auto* item = m_layout->takeAt(0);
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }

    QString rel = contextDir;
    if (rel.startsWith(sandboxRoot))
        rel = rel.mid(sandboxRoot.length());
    if (rel.startsWith('/')) rel = rel.mid(1);

    auto makeBtn = [this](const QString& label, const QString& path) {
        auto* btn = new QPushButton(label);
        btn->setStyleSheet(
            "QPushButton{background:transparent;color:#4F6EF7;border:none;"
            "font-size:11px;font-weight:600;padding:2px 4px;}"
            "QPushButton:hover{text-decoration:underline;}");
        connect(btn, &QPushButton::clicked, this, [this, path]{ emit navigateTo(path); });
        return btn;
    };

    m_layout->addWidget(makeBtn("sandbox", sandboxRoot));

    if (!rel.isEmpty()) {
        QStringList parts = rel.split('/', Qt::SkipEmptyParts);
        QString build = sandboxRoot;
        for (const QString& part : parts) {
            build += "/" + part;
            auto* sep = new QLabel("/");
            sep->setStyleSheet("color:#94A3B8;font-size:11px;");
            m_layout->addWidget(sep);
            m_layout->addWidget(makeBtn(part, build));
        }
    }
    m_layout->addStretch();
}

// ─────────────────────────────────────────────────────────────────────────────
// FsNodeItem
// ─────────────────────────────────────────────────────────────────────────────

FsNodeItem::FsNodeItem(const FsNodeData& d, QGraphicsItem* parent)
    : QGraphicsItem(parent), m_data(d)
{
    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
    setZValue(1);
    refreshStat();
}

void FsNodeItem::setData(const FsNodeData& d) { m_data = d; update(); }

void FsNodeItem::refreshStat() {
    struct stat st{};
    if (::lstat(m_data.absPath.toLocal8Bit().constData(), &st) == 0) {
        m_data.inode  = st.st_ino;
        m_data.nlinks = st.st_nlink;
        m_data.mode   = st.st_mode;
        m_data.uid    = st.st_uid;
        m_data.gid    = st.st_gid;
        m_data.size   = st.st_size;
        m_data.blocks = st.st_blocks;
        if (S_ISDIR(st.st_mode))
            m_data.type = FsNodeData::Dir;
        else if (S_ISLNK(st.st_mode)) {
            m_data.type = FsNodeData::Symlink;
            char buf[PATH_MAX]{};
            ssize_t r = ::readlink(m_data.absPath.toLocal8Bit().constData(), buf, sizeof(buf)-1);
            if (r > 0) m_data.symlinkTarget = QString::fromLocal8Bit(buf, r);
        } else {
            m_data.type = FsNodeData::File;
        }
    }
    update();
}

QString FsNodeItem::permString() const {
    mode_t m = m_data.mode & 0777;
    char s[10];
    s[0]=(m&S_IRUSR)?'r':'-'; s[1]=(m&S_IWUSR)?'w':'-'; s[2]=(m&S_IXUSR)?'x':'-';
    s[3]=(m&S_IRGRP)?'r':'-'; s[4]=(m&S_IWGRP)?'w':'-'; s[5]=(m&S_IXGRP)?'x':'-';
    s[6]=(m&S_IROTH)?'r':'-'; s[7]=(m&S_IWOTH)?'w':'-'; s[8]=(m&S_IXOTH)?'x':'-';
    s[9]=0;
    return QString::fromLatin1(s);
}

QRectF FsNodeItem::boundingRect() const { return QRectF(-W/2,-H/2,W,H); }

QColor FsNodeItem::bodyColor() const {
    if (m_data.isContext) return QColor("#EEF2FF");   // context dir = blue tint
    bool readOnly = m_data.type==FsNodeData::File && (m_data.mode&0222)==0;
    if (readOnly) return QColor("#FEF2F2");
    switch (m_data.type) {
        case FsNodeData::Dir:     return QColor("#F0FDF4");
        case FsNodeData::Symlink: return QColor("#FFF7ED");
        default:                  return QColor("#FAFAFA");
    }
}

QString FsNodeItem::typeLabel() const {
    switch (m_data.type) {
        case FsNodeData::Dir:     return "📁";
        case FsNodeData::Symlink: return "↪";
        default:                  return "📄";
    }
}

void FsNodeItem::paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) {
    p->setRenderHint(QPainter::Antialiasing);

    QRectF rect(-W/2,-H/2,W,H);

    // Shadow
    p->setPen(Qt::NoPen);
    p->setBrush(QColor(0,0,0,15));
    p->drawRoundedRect(rect.translated(0,3),10,10);

    // Body
    QColor bg = bodyColor();
    if (isSelected()) bg = bg.darker(108);
    p->setBrush(bg);

    QColor border;
    if (m_data.isContext)
        border = QColor("#4F6EF7");
    else if (m_data.lockState == LockState::Locked)
        border = QColor("#DC2626");
    else if (m_data.lockState == LockState::Trying)
        border = QColor("#EA580C");
    else if (m_data.isExternallyOpen)
        border = QColor("#7C3AED");
    else if (isSelected())
        border = QColor(Theme::BLUE);
    else
        border = QColor(Theme::BORDER);

    qreal bw = (m_data.isContext || m_data.lockState != LockState::None
                || m_data.isExternallyOpen || isSelected()) ? 2.0 : 1.0;
    p->setPen(QPen(border, bw));
    p->drawRoundedRect(rect, 10, 10);

    // Context indicator: pulsing blue left accent
    if (m_data.isContext) {
        p->setPen(Qt::NoPen);
        p->setBrush(QColor("#4F6EF7"));
        p->drawRoundedRect(QRectF(-W/2,-H/2+4,4,H-8),2,2);
    }

    // Hardlink left accent (orange) when nlinks > 1
    if (!m_data.isContext && m_data.nlinks > 1 && m_data.type==FsNodeData::File) {
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(Theme::ORANGE));
        p->drawRoundedRect(QRectF(-W/2,-H/2+4,4,H-8),2,2);
    }

    // Lock badge top-right
    if (m_data.lockState==LockState::Locked||m_data.lockState==LockState::Trying) {
        QFont lf("Segoe UI Emoji",10); p->setFont(lf);
        p->setPen(m_data.lockState==LockState::Locked?QColor("#DC2626"):QColor("#EA580C"));
        p->drawText(QRectF(W/2-22,-H/2+2,20,18),Qt::AlignRight|Qt::AlignTop,
                    m_data.lockState==LockState::Locked?"🔒":"⏳");
    } else if (m_data.lockState==LockState::Released) {
        QFont lf("Segoe UI Emoji",10); p->setFont(lf);
        p->setPen(QColor(Theme::GREEN));
        p->drawText(QRectF(W/2-22,-H/2+2,20,18),Qt::AlignRight|Qt::AlignTop,"🔓");
    }

    // Externally-open badge
    if (m_data.isExternallyOpen) {
        QFont lf("Segoe UI Emoji",9); p->setFont(lf);
        p->setPen(QColor("#7C3AED"));
        p->drawText(QRectF(-W/2+2,-H/2+2,20,14),Qt::AlignLeft|Qt::AlignTop,"⚙");
    }

    // Type icon
    QFont iconFont("Segoe UI Emoji",13); p->setFont(iconFont);
    p->setPen(QColor(Theme::TEXT_SECONDARY));
    p->drawText(QRectF(-W/2+4,-H/2,24,H/2+2),Qt::AlignVCenter|Qt::AlignLeft,typeLabel());

    // Name
    QFont nameFont("Consolas",8,QFont::Bold); p->setFont(nameFont);
    p->setPen(QColor(m_data.isContext ? "#3B5BF6" : Theme::TEXT_PRIMARY));
    QString name = m_data.name.length()>14 ? m_data.name.left(12)+"…" : m_data.name;
    p->drawText(QRectF(-W/2+28,-H/2,W-32,H/2+2),Qt::AlignBottom|Qt::AlignLeft,name);

    // Inode + link count
    QFont badgeFont("Consolas",7); p->setFont(badgeFont);
    p->setPen(QColor(Theme::TEXT_MUTED));
    p->drawText(QRectF(-W/2+28,2,W-32,H/2-2),Qt::AlignTop|Qt::AlignLeft,
                QString("ino:%1  lk:%2").arg(m_data.inode).arg(m_data.nlinks));

    // Permissions + disk usage row
    if (m_data.mode != 0) {
        blkcnt_t diskBlocks = m_data.blocks;
        off_t    apparent   = m_data.size;
        off_t    diskBytes  = (off_t)diskBlocks * 512;
        bool sparse = (apparent>0 && diskBytes<apparent && m_data.type==FsNodeData::File);

        auto fmtSz = [](off_t b) {
            return b < 1024 ? QString("%1B").arg(b) : QString("%1K").arg(b/1024);
        };

        p->setFont(badgeFont);
        if (sparse) {
            p->setPen(QColor("#7C3AED"));
            p->drawText(QRectF(-W/2+28,H/2-22,W-32,10),Qt::AlignLeft,
                        permString()+"  sparse!");
        } else {
            p->setPen(QColor(Theme::TEXT_MUTED));
            p->drawText(QRectF(-W/2+28,H/2-22,W-32,10),Qt::AlignLeft,permString());
        }
        p->setPen(QColor(Theme::TEXT_MUTED));
        p->drawText(QRectF(-W/2+28,H/2-12,W-32,10),Qt::AlignLeft,
                    QString("disk:%1 sz:%2").arg(fmtSz(diskBytes)).arg(fmtSz(apparent)));
    }

    // Symlink target hint
    if (m_data.type==FsNodeData::Symlink && !m_data.symlinkTarget.isEmpty()) {
        QFont tf("Consolas",6); p->setFont(tf);
        p->setPen(QColor(Theme::ORANGE));
        QString tgt = m_data.symlinkTarget;
        if (tgt.length()>16) tgt=tgt.left(14)+"…";
        p->drawText(QRectF(-W/2+28,H/2-12,W-32,12),Qt::AlignBottom|Qt::AlignLeft,"→ "+tgt);
    }

    // Click-to-enter hint for dirs
    if (m_data.type==FsNodeData::Dir) {
        QFont tf("Consolas",6); p->setFont(tf);
        p->setPen(QColor("#4F6EF7").lighter(150));
        QString hint = m_data.isContext ? "● creating here" : "click→select  dbl→enter";
        p->drawText(QRectF(-W/2+28,H/2-12,W-32,12),Qt::AlignBottom|Qt::AlignLeft,hint);
    }
}

void FsNodeItem::mousePressEvent(QGraphicsSceneMouseEvent* e) {
    QGraphicsItem::mousePressEvent(e);
}

void FsNodeItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent*) {
    // handled by FsCanvas::event()
}

QVariant FsNodeItem::itemChange(GraphicsItemChange change, const QVariant& value) {
    if (change == ItemPositionHasChanged)
        for (auto* e : m_edges) e->adjust();
    return QGraphicsItem::itemChange(change, value);
}

// ─────────────────────────────────────────────────────────────────────────────
// FsEdgeItem
// ─────────────────────────────────────────────────────────────────────────────

FsEdgeItem::FsEdgeItem(FsNodeItem* src, FsNodeItem* dst, EdgeKind kind,
                       const QString& label, QGraphicsItem* parent)
    : QGraphicsItem(parent), m_src(src), m_dst(dst), m_kind(kind), m_label(label)
{
    setZValue(0);
    setAcceptedMouseButtons(Qt::NoButton);
    m_src->addEdge(this);
    m_dst->addEdge(this);
    adjust();
}

void FsEdgeItem::adjust() {
    if (!m_src || !m_dst) return;
    prepareGeometryChange();
    m_sp = m_src->scenePos();
    m_ep = m_dst->scenePos();
    update();
}

QRectF FsEdgeItem::boundingRect() const {
    return QRectF(std::min(m_sp.x(),m_ep.x())-10, std::min(m_sp.y(),m_ep.y())-10,
                  std::abs(m_ep.x()-m_sp.x())+20, std::abs(m_ep.y()-m_sp.y())+20);
}

void FsEdgeItem::paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) {
    p->setRenderHint(QPainter::Antialiasing);

    if (m_kind == Containment) {
        p->setPen(QPen(QColor("#CBD5E1"), 1.0, Qt::SolidLine));
        p->drawLine(m_sp, m_ep);
        return;
    }

    QColor color;
    switch (m_kind) {
        case Hardlink:    color = QColor(Theme::GREEN);   break;
        case Symlink:     color = QColor(Theme::ORANGE);  break;
        case ProcFd:      color = QColor("#7C3AED");      break;
        case Containment: color = QColor("#CBD5E1");      break;
    }
    if (m_dangling) color = QColor(Theme::RED);

    QPen pen(color, m_kind==Hardlink ? 2.5 : 1.5);
    if (m_kind==Symlink || m_dangling) { pen.setStyle(Qt::DashLine); pen.setDashPattern({4,3}); }
    else if (m_kind==ProcFd)           { pen.setStyle(Qt::DotLine); }
    p->setPen(pen);

    QLineF line(m_sp, m_ep);
    p->drawLine(line);
    if (line.length() < 1.0) return;

    double angle = std::atan2(-(m_ep.y()-m_sp.y()), m_ep.x()-m_sp.x());
    constexpr double AS = 10.0;
    QPointF p1 = m_ep + QPointF(std::cos(angle+M_PI*5/6)*AS, -std::sin(angle+M_PI*5/6)*AS);
    QPointF p2 = m_ep + QPointF(std::cos(angle-M_PI*5/6)*AS, -std::sin(angle-M_PI*5/6)*AS);
    p->setBrush(color); p->setPen(Qt::NoPen);
    p->drawPolygon(QPolygonF({m_ep,p1,p2}));

    QPointF mid = (m_sp+m_ep)/2.0;
    QFont f("Consolas",7); p->setFont(f); p->setPen(color);
    QString lbl = m_label.isEmpty()
        ? (m_kind==Hardlink?"hard":m_kind==Symlink?"sym":"fd") : m_label;
    if (m_dangling) lbl = "dangling";
    p->drawText(mid+QPointF(4,-4), lbl);
}

// ─────────────────────────────────────────────────────────────────────────────
// FsCanvas
// ─────────────────────────────────────────────────────────────────────────────

FsCanvas::FsCanvas(QWidget* parent)
    : QGraphicsView(parent)
    , m_scene(new QGraphicsScene(this))
    , m_breadcrumb(new BreadcrumbBar(nullptr))
{
    setScene(m_scene);
    setRenderHint(QPainter::Antialiasing);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setBackgroundBrush(QColor("#F8FAFC"));
    setStyleSheet(QString("QGraphicsView{border:1px solid %1;border-radius:10px;}").arg(Theme::BORDER));
    setTransformationAnchor(AnchorUnderMouse);
    setResizeAnchor(AnchorViewCenter);

    m_inoTimer = new QTimer(this);
    connect(m_inoTimer, &QTimer::timeout, this, &FsCanvas::pollInotify);

    m_workerBin = QCoreApplication::applicationDirPath() + "/fslab_worker";

    connect(m_breadcrumb, &BreadcrumbBar::navigateTo,
            this,          &FsCanvas::setContextDir);

    // Bug fix: connect selectionChanged once here, not once per addNode call
    connect(m_scene, &QGraphicsScene::selectionChanged, this, [this]() {
        const auto sel = m_scene->selectedItems();
        if (!sel.isEmpty())
            if (auto* n = dynamic_cast<FsNodeItem*>(sel.first()))
                emit nodeClicked(n->data());
    });
}

FsCanvas::~FsCanvas() { stopInotify(); }

// ── Sandbox root setup ────────────────────────────────────────────────────────

void FsCanvas::setSandboxRoot(const QString& p) {
    stopInotify();
    m_sandboxRoot   = p;
    m_contextDir    = p;
    m_initialLayout = true;
    QDir().mkpath(p);
    clearCanvas();
    // Recursively scan and display the full tree (hardlink/symlink passes run after full scan)
    FsNodeItem* root = addNode(p);
    scanTree(p, root);

    // Bug fix: run hardlink + symlink detection once after the full recursive scan,
    // not at every recursion level inside scanTree.
    QMap<ino_t, QList<FsNodeItem*>> inodeMap;
    for (auto* n : m_nodes) {
        if (n->data().type != FsNodeData::Symlink)
            inodeMap[n->data().inode].append(n);
    }
    for (auto& group : inodeMap) {
        if (group.size() < 2) continue;
        for (int i = 1; i < group.size(); ++i)
            addEdge(group[0], group[i], FsEdgeItem::Hardlink);
    }
    for (auto* n : m_nodes) {
        if (n->data().type != FsNodeData::Symlink) continue;
        QString target = n->data().symlinkTarget;
        if (!target.startsWith('/'))
            target = QFileInfo(n->absPath()).dir().absolutePath() + "/" + target;
        if (m_nodes.contains(target))
            addEdge(n, m_nodes[target], FsEdgeItem::Symlink);
    }

    treeLayout();
    startInotify();
    m_breadcrumb->setPath(m_sandboxRoot, m_contextDir);
}

// ── Set context directory ─────────────────────────────────────────────────────

void FsCanvas::setContextDir(const QString& absPath) {
    if (!QFileInfo(absPath).isDir()) return;
    if (!absPath.startsWith(m_sandboxRoot) && absPath != m_sandboxRoot) return;

    // Clear old context highlight
    if (m_nodes.contains(m_contextDir)) {
        FsNodeData d = m_nodes[m_contextDir]->data();
        d.isContext = false;
        m_nodes[m_contextDir]->setData(d);
    }

    m_contextDir = absPath;

    // Set new context highlight
    if (m_nodes.contains(m_contextDir)) {
        FsNodeData d = m_nodes[m_contextDir]->data();
        d.isContext = true;
        m_nodes[m_contextDir]->setData(d);
    }

    m_breadcrumb->setPath(m_sandboxRoot, m_contextDir);
    emit dirChanged(m_contextDir);
    emit statusMessage(QString("Context: %1  — new files/dirs go here").arg(m_contextDir));
}

void FsCanvas::focusNode(const QString& absPath) {
    if (!m_nodes.contains(absPath)) return;
    auto* n = m_nodes[absPath];
    centerOn(n);
    m_scene->clearSelection();
    n->setSelected(true);
}

void FsCanvas::goUp() {
    if (m_contextDir == m_sandboxRoot) {
        emit statusMessage("Already at sandbox root.");
        return;
    }
    QString parent = QFileInfo(m_contextDir).dir().absolutePath();
    if (parent.length() < m_sandboxRoot.length()) parent = m_sandboxRoot;
    setContextDir(parent);
}

// ── Clear ─────────────────────────────────────────────────────────────────────

void FsCanvas::clearCanvas() {
    m_procEdges.clear();
    m_procNodes.clear();
    m_scene->clear();
    m_nodes.clear();
    m_edges.clear();
}

// ── Recursive tree scan ───────────────────────────────────────────────────────

void FsCanvas::scanTree(const QString& dir, FsNodeItem* parentNode) {
    // Bug fix: hardlink and symlink detection moved to setSandboxRoot, run once after full scan.
    QDir qdir(dir);
    qdir.setFilter(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot |
                   QDir::Hidden | QDir::System);
    for (const QFileInfo& fi : qdir.entryInfoList()) {
        FsNodeItem* child = addNode(fi.absoluteFilePath());
        addEdge(parentNode, child, FsEdgeItem::Containment);
        // Recurse into subdirectories
        if (fi.isDir() && !fi.isSymLink())
            scanTree(fi.absoluteFilePath(), child);
    }
}

// ── Node management ───────────────────────────────────────────────────────────

FsNodeItem* FsCanvas::getOrAddNode(const QString& absPath) {
    if (m_nodes.contains(absPath)) {
        m_nodes[absPath]->refreshStat();
        return m_nodes[absPath];
    }
    return addNode(absPath);
}

FsNodeItem* FsCanvas::addNode(const QString& absPath) {
    if (m_nodes.contains(absPath)) {
        m_nodes[absPath]->refreshStat();
        return m_nodes[absPath];
    }
    FsNodeData d;
    d.absPath = absPath;
    d.name    = QFileInfo(absPath).fileName().isEmpty()
                ? QFileInfo(absPath).absoluteFilePath()  // root dir
                : QFileInfo(absPath).fileName();
    if (d.name.isEmpty()) d.name = absPath;

    // Mark the sandbox root itself distinctly
    if (absPath == m_sandboxRoot) {
        d.name = "sandbox/";
        d.isContext = (m_contextDir == absPath);
    } else {
        d.isContext = (m_contextDir == absPath);
    }

    auto* item = new FsNodeItem(d);
    m_scene->addItem(item);
    m_nodes[absPath] = item;
    // Note: selectionChanged is connected once in the constructor, not here.

    item->refreshStat();
    return item;
}

void FsCanvas::removeNode(const QString& absPath) {
    if (!m_nodes.contains(absPath)) return;
    FsNodeItem* n = m_nodes.take(absPath);
    removeEdgesFor(n);
    m_scene->removeItem(n);
    delete n;
}

void FsCanvas::removeEdgesFor(FsNodeItem* n) {
    QList<FsEdgeItem*> toRemove;
    for (auto* e : m_edges)
        if (e->source()==n || e->dest()==n)
            toRemove.append(e);
    for (auto* e : toRemove) {
        e->source()->removeEdge(e);
        e->dest()->removeEdge(e);
        m_edges.removeAll(e);
        m_scene->removeItem(e);
        delete e;
    }
}

void FsCanvas::addEdge(FsNodeItem* src, FsNodeItem* dst,
                       FsEdgeItem::EdgeKind kind, const QString& label) {
    auto* e = new FsEdgeItem(src, dst, kind, label);
    m_scene->addItem(e);
    m_edges.append(e);
}

// ── Place a new node as a visual child of parentNode ─────────────────────────

void FsCanvas::placeAsChild(FsNodeItem* parentNode, FsNodeItem* child) {
    // Count existing containment children of this parent
    int childCount = 0;
    for (auto* e : m_edges)
        if (e->kind()==FsEdgeItem::Containment && e->source()==parentNode)
            childCount++;
    // Spread children horizontally below the parent
    // Use childCount-1 because the containment edge was just added before calling this
    qreal spread = (childCount - 1) * 145.0;
    child->setPos(parentNode->pos() + QPointF(spread - (childCount-1)*145.0/2.0, 130));
    // Re-run a light re-layout for this subtree only
    relayout();
}

// ── Tree layout ───────────────────────────────────────────────────────────────

void FsCanvas::treeLayout() {
    // Build child map from containment edges
    QMap<FsNodeItem*, QList<FsNodeItem*>> children;
    FsNodeItem* root = nullptr;

    for (auto* e : m_edges) {
        if (e->kind() == FsEdgeItem::Containment) {
            children[e->source()].append(e->dest());
            if (!root) root = e->source();
        }
    }

    // Find the actual root: the node that appears as source but never as dest
    QSet<FsNodeItem*> hasDest;
    for (auto* e : m_edges)
        if (e->kind()==FsEdgeItem::Containment) hasDest.insert(e->dest());
    for (auto* e : m_edges)
        if (e->kind()==FsEdgeItem::Containment && !hasDest.contains(e->source()))
            { root = e->source(); break; }

    if (!root) {
        // No containment edges — single node or empty
        int i = 0;
        for (auto* n : m_nodes) n->setPos(i++ * 150, 0);
        for (auto* e : m_edges) e->adjust();
        return;
    }

    // Bug fix: use the node's actual rendered width to avoid overlap with many siblings
    auto nodeWidth = [](FsNodeItem* n) -> int {
        return std::max(145, (int)n->boundingRect().width() + 20);
    };
    constexpr int LEVEL_H = 155;

    std::function<int(FsNodeItem*)> subtreeW = [&](FsNodeItem* n) -> int {
        auto& ch = children[n];
        if (ch.isEmpty()) return nodeWidth(n);
        int w = 0;
        for (auto* c : ch) w += subtreeW(c);
        return std::max(w, nodeWidth(n));
    };

    std::function<void(FsNodeItem*, qreal, int)> assign =
        [&](FsNodeItem* n, qreal cx, int depth) {
            n->setPos(cx, depth * LEVEL_H);
            auto& ch = children[n];
            if (ch.isEmpty()) return;
            int totalW = 0;
            for (auto* c : ch) totalW += subtreeW(c);
            qreal x = cx - totalW / 2.0 + subtreeW(ch[0]) / 2.0;
            for (auto* c : ch) {
                assign(c, x, depth + 1);
                x += subtreeW(c);
            }
        };

    assign(root, 0, 0);

    // Position hard-link partners beside their containment-placed node
    QSet<FsNodeItem*> placed;
    for (auto* e : m_edges)
        if (e->kind()==FsEdgeItem::Containment)
            { placed.insert(e->source()); placed.insert(e->dest()); }

    for (auto* e : m_edges) {
        if (e->kind() != FsEdgeItem::Hardlink) continue;
        FsNodeItem* a = e->source(), *b = e->dest();
        if (placed.contains(a) && !placed.contains(b))
            b->setPos(a->pos() + QPointF(nodeWidth(a), 0));
        else if (!placed.contains(a) && placed.contains(b))
            a->setPos(b->pos() + QPointF(nodeWidth(b), 0));
    }

    for (auto* e : m_edges) e->adjust();
    // Bug fix: only fitInView on the initial load, not on every incremental relayout
    if (m_initialLayout) {
        fitInView(m_scene->itemsBoundingRect().adjusted(-30,-30,30,30), Qt::KeepAspectRatio);
        m_initialLayout = false;
    }
}

void FsCanvas::relayout() {
    treeLayout();
}

// ── Real chmod ───────────────────────────────────────────────────────────────

void FsCanvas::doChmod() {
    auto sel = m_scene->selectedItems();
    if (sel.isEmpty()) { emit statusMessage("Select a node first."); return; }
    auto* n = dynamic_cast<FsNodeItem*>(sel.first());
    if (!n) return;

    mode_t cur = n->data().mode & 0777;
    bool ok;
    QString octal = QInputDialog::getText(this, "chmod",
        QString("Current: %1 (%2 octal)\n\nNew octal (e.g. 644, 000, 755, 777):")
            .arg(n->permString()).arg(QString::number(cur,8).rightJustified(3,'0')),
        QLineEdit::Normal, QString::number(cur,8).rightJustified(3,'0'), &ok);
    if (!ok || octal.trimmed().isEmpty()) return;

    bool conv;
    mode_t newMode = octal.trimmed().toUInt(&conv, 8);
    if (!conv || newMode > 0777) { emit statusMessage("Invalid octal."); return; }

    if (::chmod(n->absPath().toLocal8Bit().constData(), newMode) != 0) {
        emit statusMessage(QString("chmod() failed: %1").arg(strerror(errno)));
        return;
    }
    n->refreshStat();
    emit statusMessage(QString("chmod %1 → %2 — node updated")
        .arg(QString::number(newMode,8).rightJustified(3,'0')).arg(n->permString()));

    if (newMode == 0) {
        int fd = ::open(n->absPath().toLocal8Bit().constData(), O_RDONLY);
        if (fd < 0)
            emit statusMessage(QString("open(\"%1\",O_RDONLY) → EACCES — chmod 000 is real!")
                               .arg(n->data().name));
        else ::close(fd);
    }
    emit nodeClicked(n->data());
}

// ── Real chown ───────────────────────────────────────────────────────────────

void FsCanvas::doChown() {
    auto sel = m_scene->selectedItems();
    if (sel.isEmpty()) { emit statusMessage("Select a node first."); return; }
    auto* n = dynamic_cast<FsNodeItem*>(sel.first());
    if (!n) return;

    struct passwd* pw = getpwuid(n->data().uid);
    QString curOwner = pw ? QString::fromLocal8Bit(pw->pw_name) : QString::number(n->data().uid);
    bool ok;
    QString newOwner = QInputDialog::getText(this, "chown",
        QString("Current owner: %1\nNew owner (username or uid):").arg(curOwner),
        QLineEdit::Normal, curOwner, &ok);
    if (!ok || newOwner.trimmed().isEmpty()) return;

    uid_t newUid;
    struct passwd* npw = getpwnam(newOwner.trimmed().toLocal8Bit().constData());
    if (npw) { newUid = npw->pw_uid; }
    else {
        bool numOk;
        newUid = (uid_t)newOwner.trimmed().toUInt(&numOk);
        if (!numOk) { emit statusMessage("Unknown user: "+newOwner.trimmed()); return; }
    }

    if (::lchown(n->absPath().toLocal8Bit().constData(), newUid, (gid_t)-1) != 0) {
        emit statusMessage(QString("chown() failed: %1").arg(strerror(errno)));
        return;
    }
    n->refreshStat();
    emit statusMessage(QString("chown %1 %2").arg(newOwner.trimmed()).arg(n->data().name));
    emit nodeClicked(n->data());
}

// ── Write/Read content ────────────────────────────────────────────────────────

void FsCanvas::doWriteContent() {
    auto sel = m_scene->selectedItems();
    if (sel.isEmpty()) { emit statusMessage("Select a file node first."); return; }
    auto* n = dynamic_cast<FsNodeItem*>(sel.first());
    if (!n || n->data().type != FsNodeData::File) { emit statusMessage("Select a regular file."); return; }

    QDialog dlg(this);
    dlg.setWindowTitle("Write to " + n->data().name);
    auto* vl = new QVBoxLayout(&dlg);
    auto* hint = new QLabel("Content written with write() + fsync(). Node updates disk-block count live.");
    hint->setWordWrap(true);
    hint->setStyleSheet(QString("color:%1;font-size:11px;").arg(Theme::TEXT_SECONDARY));
    vl->addWidget(hint);
    auto* te = new QTextEdit();
    te->setPlaceholderText("Enter content to write…");
    te->setStyleSheet(Theme::termLog());
    te->setMinimumHeight(120);
    vl->addWidget(te);
    auto* btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    vl->addWidget(btns);
    if (dlg.exec() != QDialog::Accepted) return;

    QByteArray content = te->toPlainText().toUtf8();
    int fd = ::open(n->absPath().toLocal8Bit().constData(), O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if (fd < 0) { emit statusMessage(QString("open() failed: %1").arg(strerror(errno))); return; }
    ssize_t written = ::write(fd, content.constData(), content.size());
    ::fsync(fd); ::close(fd);

    n->refreshStat();
    for (auto* e : m_edges) e->adjust();
    emit statusMessage(QString("Wrote %1 bytes → disk blocks: %2×512").arg(written).arg(n->data().blocks));
    emit nodeClicked(n->data());
}

void FsCanvas::doReadContent() {
    auto sel = m_scene->selectedItems();
    if (sel.isEmpty()) { emit statusMessage("Select a file node first."); return; }
    auto* n = dynamic_cast<FsNodeItem*>(sel.first());
    if (!n || n->data().type != FsNodeData::File) { emit statusMessage("Select a regular file."); return; }

    int flags = O_RDONLY;
#ifdef O_DIRECT
    if (m_useODirect) flags |= O_DIRECT;
#endif
    int fd = ::open(n->absPath().toLocal8Bit().constData(), flags);
    bool fellBack = false;
    if (fd < 0 && m_useODirect) { flags=O_RDONLY; fd=::open(n->absPath().toLocal8Bit().constData(),flags); fellBack=true; }
    if (fd < 0) {
        emit statusMessage(QString("open() → %1")
            .arg(errno==EACCES?"EACCES (permission denied!)" : strerror(errno)));
        return;
    }

    auto t0 = std::chrono::steady_clock::now();
    QByteArray buf(65536, 0), total;
    ssize_t nr;
    while ((nr=::read(fd,buf.data(),buf.size()))>0) total.append(buf.constData(),nr);
    auto usec = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now()-t0).count();
    ::close(fd);

    QString modeStr = (m_useODirect&&!fellBack)?"O_DIRECT":"buffered";
    if (fellBack) modeStr += " (O_DIRECT fell back)";

    QDialog dlg(this);
    dlg.setWindowTitle("Read: " + n->data().name);
    auto* vl = new QVBoxLayout(&dlg);
    auto* stats = new QLabel(QString(
        "<b>%1</b> bytes in <b>%2 µs</b> via <b>%3</b><br>"
        "Apparent: %4 B | Disk: %5×512 = %6 B")
        .arg(total.size()).arg(usec).arg(modeStr)
        .arg(n->data().size).arg(n->data().blocks).arg((off_t)n->data().blocks*512));
    stats->setWordWrap(true);
    stats->setStyleSheet(QString("color:%1;font-size:11px;padding:4px;").arg(Theme::TEXT_PRIMARY));
    vl->addWidget(stats);
    auto* te = new QTextEdit(); te->setReadOnly(true); te->setStyleSheet(Theme::termLog());
    te->setMinimumHeight(150);
    te->setPlainText(QString::fromUtf8(total.left(4096)));
    if (total.size()>4096) te->append(QString("\n…(%1 more bytes)").arg(total.size()-4096));
    vl->addWidget(te);
    auto* btns = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::accept);
    vl->addWidget(btns);
    dlg.exec();
    emit statusMessage(QString("Read %1 B in %2µs (%3)").arg(total.size()).arg(usec).arg(modeStr));
}

void FsCanvas::doToggleODirect() {
    m_useODirect = !m_useODirect;
    emit statusMessage(m_useODirect
        ? "O_DIRECT active — next Read bypasses page cache"
        : "Buffered I/O active");
}

// ── Sparse file ───────────────────────────────────────────────────────────────

void FsCanvas::doMakeHole() {
    if (m_contextDir.isEmpty()) return;
    bool ok;
    QString name = QInputDialog::getText(this, "Create Sparse File", "File name:",
        QLineEdit::Normal, uniqueName(m_contextDir,"sparse",".dat"), &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    QString path = m_contextDir + "/" + name.trimmed();
    int fd = ::open(path.toLocal8Bit().constData(), O_CREAT|O_WRONLY|O_TRUNC, 0644);
    if (fd<0) { emit statusMessage(QString("open() failed: %1").arg(strerror(errno))); return; }
    ::write(fd, "HEAD", 4);
    ::lseek(fd, 1024*1024, SEEK_SET);
    ::write(fd, "TAIL", 4);
    ::close(fd);

    emit statusMessage("Sparse file created inside " +
                       QFileInfo(m_contextDir).fileName() +
                       " — apparent=1M+8B, disk blocks much smaller");
    // inotify will add it; direct fallback:
    if (!m_nodes.contains(path)) {
        FsNodeItem* parentNode = m_nodes.value(m_contextDir);
        FsNodeItem* child = addNode(path);
        if (parentNode) {
            addEdge(parentNode, child, FsEdgeItem::Containment);
            placeAsChild(parentNode, child);
        }
    }
}

// ── flock demo ────────────────────────────────────────────────────────────────

void FsCanvas::runLockWorker(const QString& path, const QString& role, int pipeWfd) {
    QProcess* proc = new QProcess(this);
    proc->setProgram(m_workerBin);
    proc->setArguments({"lock", path, role, QString::number(pipeWfd)});
    proc->start();
    ::close(pipeWfd);
}

void FsCanvas::doLockDemo() {
    auto sel = m_scene->selectedItems();
    if (sel.isEmpty()) { emit statusMessage("Select a file node first."); return; }
    auto* n = dynamic_cast<FsNodeItem*>(sel.first());
    if (!n || n->data().type != FsNodeData::File) {
        emit statusMessage("Select a regular file for the lock demo."); return;
    }

    int pipeA[2], pipeB[2];
    if (::pipe(pipeA)!=0 || ::pipe(pipeB)!=0) { emit statusMessage("pipe() failed"); return; }
    ::fcntl(pipeA[0], F_SETFL, O_NONBLOCK);
    ::fcntl(pipeB[0], F_SETFL, O_NONBLOCK);

    emit statusMessage("Lock demo: two processes racing flock(LOCK_EX) on " + n->data().name);
    runLockWorker(n->absPath(), "A", pipeA[1]);
    runLockWorker(n->absPath(), "B", pipeB[1]);

    struct Ctx { int fdA,fdB; QString bufA,bufB; int ticksLeft; FsNodeItem* node; };
    auto* ctx = new Ctx{pipeA[0],pipeB[0],{},{},60,n};
    auto* pollTimer = new QTimer(this);
    pollTimer->setInterval(250);
    connect(pollTimer, &QTimer::timeout, this, [this,ctx,pollTimer]() mutable {
        if (!ctx->node) { pollTimer->stop(); delete ctx; return; }
        auto readLine = [](int fd, QString& buf) -> QString {
            char c;
            while (::read(fd,&c,1)==1) { buf+=c; if(c=='\n'){QString l=buf.trimmed();buf.clear();return l;} }
            return {};
        };
        QString lA = readLine(ctx->fdA,ctx->bufA);
        QString lB = readLine(ctx->fdB,ctx->bufB);
        FsNodeData d = ctx->node->data();
        QString status;
        auto applyLine = [&](const QString& line, const QString& who) {
            if (line=="TRYING")   { status += who+": trying flock()… "; d.lockState=LockState::Trying; }
            if (line=="LOCKED")   { status += who+": GOT LOCK! ";       d.lockState=LockState::Locked; }
            if (line=="RELEASED") { status += who+": released. ";       d.lockState=LockState::Released; }
        };
        applyLine(lA,"A"); applyLine(lB,"B");
        if (!status.isEmpty()) { ctx->node->setData(d); ctx->node->update(); emit statusMessage(status.trimmed()); }
        if (--ctx->ticksLeft <= 0) {
            ::close(ctx->fdA); ::close(ctx->fdB);
            d = ctx->node->data(); d.lockState=LockState::None;
            ctx->node->setData(d); ctx->node->update();
            pollTimer->stop(); delete ctx;
            emit statusMessage("Lock demo done.");
        }
    });
    pollTimer->start();
}

// ── /proc/<pid>/fd edges ──────────────────────────────────────────────────────

void FsCanvas::updateProcFdEdges(const std::vector<pid_t>& pids) {
    for (auto* n : m_nodes) {
        FsNodeData d = n->data();
        if (d.isExternallyOpen) { d.isExternallyOpen=false; n->setData(d); }
    }
    QSet<QString> activeKeys;
    for (pid_t pid : pids) {
        QString fdDir = QString("/proc/%1/fd").arg(pid);
        QDir dir(fdDir);
        if (!dir.exists()) continue;
        dir.setFilter(QDir::Files|QDir::System|QDir::NoDotAndDotDot);
        for (const QFileInfo& fi : dir.entryInfoList()) {
            char tgt[PATH_MAX]{};
            ssize_t r = ::readlink((fdDir+"/"+fi.fileName()).toLocal8Bit().constData(),tgt,sizeof(tgt)-1);
            if (r<=0) continue;
            QString resolved = QString::fromLocal8Bit(tgt,r);
            if (!m_nodes.contains(resolved)) continue;
            FsNodeItem* fileNode = m_nodes[resolved];
            FsNodeData d = fileNode->data(); d.isExternallyOpen=true; fileNode->setData(d);
            QString key = QString("%1:%2").arg(pid).arg(resolved);
            activeKeys.insert(key);
            if (m_procEdges.contains(key)) continue;

            FsNodeItem* procNode = m_procNodes.value(pid);
            if (!procNode) {
                FsNodeData pd;
                pd.absPath = QString("/proc/%1").arg(pid);
                pd.name    = QString("pid:%1").arg(pid);
                pd.type    = FsNodeData::File;
                procNode = new FsNodeItem(pd);
                m_scene->addItem(procNode);
                procNode->setPos(fileNode->pos()+QPointF(QRandomGenerator::global()->bounded(-60,60),-140));
                m_procNodes[pid] = procNode;
            }
            auto* e = new FsEdgeItem(procNode,fileNode,FsEdgeItem::ProcFd,
                                     QString("fd/%1").arg(fi.fileName()));
            m_scene->addItem(e);
            m_procEdges[key] = e;
        }
    }
    for (auto it = m_procEdges.begin(); it != m_procEdges.end();) {
        if (!activeKeys.contains(it.key())) {
            FsEdgeItem* e = it.value();
            if (e->source()) e->source()->removeEdge(e);
            if (e->dest())   e->dest()->removeEdge(e);
            m_scene->removeItem(e); delete e;
            it = m_procEdges.erase(it);
        } else { (*it)->adjust(); ++it; }
    }
    for (auto it = m_procNodes.begin(); it != m_procNodes.end();) {
        bool hasEdge = std::any_of(m_procEdges.begin(),m_procEdges.end(),
                                   [&](FsEdgeItem* e){ return e->source()==it.value(); });
        if (!hasEdge) { m_scene->removeItem(it.value()); delete it.value(); it=m_procNodes.erase(it); }
        else ++it;
    }
}

// ── Toolbar: new file ─────────────────────────────────────────────────────────

void FsCanvas::doNewFile() {
    if (m_contextDir.isEmpty()) { emit statusMessage("Click a directory node to select it first."); return; }
    bool ok;
    QString name = QInputDialog::getText(this, "New File",
        QString("Create file inside  %1\n\nFile name:")
            .arg(QFileInfo(m_contextDir).fileName().isEmpty()
                 ? "sandbox" : QFileInfo(m_contextDir).fileName()),
        QLineEdit::Normal, uniqueName(m_contextDir,"file",".txt"), &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    QString path = m_contextDir + "/" + name.trimmed();
    int fd = ::open(path.toLocal8Bit().constData(), O_CREAT|O_WRONLY|O_EXCL, 0644);
    if (fd < 0) { emit statusMessage(QString("open() failed: %1").arg(strerror(errno))); return; }
    ::close(fd);

    emit statusMessage(QString("Created %1 inside %2")
        .arg(name.trimmed()).arg(QFileInfo(m_contextDir).fileName()));

    if (!m_nodes.contains(path)) {
        FsNodeItem* parentNode = m_nodes.value(m_contextDir);
        FsNodeItem* child = addNode(path);
        if (parentNode) {
            addEdge(parentNode, child, FsEdgeItem::Containment);
            placeAsChild(parentNode, child);
        }
        for (auto* e : m_edges) e->adjust();
    }
}

// ── Toolbar: new directory ────────────────────────────────────────────────────

void FsCanvas::doNewDir() {
    if (m_contextDir.isEmpty()) { emit statusMessage("Click a directory node to select it first."); return; }
    bool ok;
    QString name = QInputDialog::getText(this, "New Directory",
        QString("Create directory inside  %1\n\nDirectory name:")
            .arg(QFileInfo(m_contextDir).fileName().isEmpty()
                 ? "sandbox" : QFileInfo(m_contextDir).fileName()),
        QLineEdit::Normal, uniqueName(m_contextDir,"dir"), &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    QString path = m_contextDir + "/" + name.trimmed();
    if (::mkdir(path.toLocal8Bit().constData(), 0755) != 0) {
        emit statusMessage(QString("mkdir() failed: %1").arg(strerror(errno))); return;
    }

    emit statusMessage(QString("Created dir %1 inside %2")
        .arg(name.trimmed()).arg(QFileInfo(m_contextDir).fileName()));

    if (!m_nodes.contains(path)) {
        FsNodeItem* parentNode = m_nodes.value(m_contextDir);
        FsNodeItem* child = addNode(path);
        if (parentNode) {
            addEdge(parentNode, child, FsEdgeItem::Containment);
            placeAsChild(parentNode, child);
        }
        // Add inotify watch for the new subdir so events inside it are tracked
        addInotifyWatch(path);
        for (auto* e : m_edges) e->adjust();
    }
}

// ── Toolbar: delete ───────────────────────────────────────────────────────────

void FsCanvas::doDelete() {
    auto sel = m_scene->selectedItems();
    if (sel.isEmpty()) { emit statusMessage("Select a node to delete."); return; }
    auto* n = dynamic_cast<FsNodeItem*>(sel.first());
    if (!n) return;
    if (n->absPath() == m_sandboxRoot) { emit statusMessage("Cannot delete sandbox root."); return; }

    auto ans = QMessageBox::warning(this, "Delete",
        QString("Delete '%1'?\n\nReal syscall — cannot be undone.").arg(n->data().name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ans != QMessageBox::Yes) return;

    int rc = (n->data().type==FsNodeData::Dir)
             ? ::rmdir(n->absPath().toLocal8Bit().constData())
             : ::unlink(n->absPath().toLocal8Bit().constData());
    if (rc != 0) { emit statusMessage(QString("unlink/rmdir failed: %1").arg(strerror(errno))); return; }

    // If we deleted the context dir, move context up
    if (m_contextDir == n->absPath()) setContextDir(m_sandboxRoot);

    emit statusMessage("Deleted: " + n->data().name);
    removeNode(n->absPath());
    for (auto* node : m_nodes) node->refreshStat();
    refreshEdgeDangling();
}

// ── Toolbar: rename ───────────────────────────────────────────────────────────

void FsCanvas::doRename() {
    auto sel = m_scene->selectedItems();
    if (sel.isEmpty()) { emit statusMessage("Select a node to rename."); return; }
    auto* n = dynamic_cast<FsNodeItem*>(sel.first());
    if (!n) return;
    QString oldPath = n->absPath();
    bool ok;
    QString newName = QInputDialog::getText(this,"Rename","New name:",
                                            QLineEdit::Normal,QFileInfo(oldPath).fileName(),&ok);
    if (!ok || newName.trimmed().isEmpty()) return;
    QString newPath = QFileInfo(oldPath).dir().absolutePath() + "/" + newName.trimmed();
    if (::rename(oldPath.toLocal8Bit().constData(), newPath.toLocal8Bit().constData()) != 0) {
        emit statusMessage(QString("rename() failed: %1").arg(strerror(errno))); return;
    }

    // Bug fix: rekey the renamed node AND all descendants that had absPath under oldPath
    // Collect all paths that need rekeying (sorted so parent comes before children)
    QList<QPair<QString,FsNodeItem*>> toRekey;
    for (auto it = m_nodes.begin(); it != m_nodes.end(); ++it) {
        if (it.key() == oldPath || it.key().startsWith(oldPath + "/"))
            toRekey.append({it.key(), it.value()});
    }
    for (const auto& kv : toRekey)
        m_nodes.remove(kv.first);

    for (auto& kv : toRekey) {
        QString oldKey = kv.first;
        FsNodeItem* node = kv.second;
        QString newKey = newPath + oldKey.mid(oldPath.length());
        FsNodeData d = node->data();
        d.absPath = newKey;
        if (oldKey == oldPath) d.name = newName.trimmed();
        node->setData(d);
        node->refreshStat();
        m_nodes[newKey] = node;
    }

    if (m_contextDir == oldPath || m_contextDir.startsWith(oldPath + "/")) {
        m_contextDir = newPath + m_contextDir.mid(oldPath.length());
        m_breadcrumb->setPath(m_sandboxRoot, m_contextDir);
    }
    emit statusMessage("Renamed: " + QFileInfo(oldPath).fileName() + " → " + newName.trimmed());
}

// ── Toolbar: hard link ────────────────────────────────────────────────────────

void FsCanvas::doHardLink() {
    auto sel = m_scene->selectedItems();
    if (sel.isEmpty()) { emit statusMessage("Select a file node first, then click Hard Link."); return; }
    auto* srcNode = dynamic_cast<FsNodeItem*>(sel.first());
    if (!srcNode) return;
    if (srcNode->data().type == FsNodeData::Dir) {
        emit statusMessage("Hard links to directories are not allowed by the kernel."); return;
    }

    QString base = srcNode->data().name;
    int dot = base.lastIndexOf('.');
    bool ok;
    QString newName = QInputDialog::getText(this, "Create Hard Link",
        QString("Hard link to '%1' (inode %2, same data)\n\nNew name in %3:")
            .arg(srcNode->data().name).arg(srcNode->data().inode)
            .arg(QFileInfo(m_contextDir).fileName().isEmpty()?"sandbox":QFileInfo(m_contextDir).fileName()),
        QLineEdit::Normal,
        uniqueName(m_contextDir,
                   (dot>0?base.left(dot):base)+"_link",
                   dot>0?"."+base.section('.',-1):""),
        &ok);
    if (!ok || newName.trimmed().isEmpty()) return;

    QString newPath = m_contextDir + "/" + newName.trimmed();
    if (::link(srcNode->absPath().toLocal8Bit().constData(),
               newPath.toLocal8Bit().constData()) != 0) {
        emit statusMessage(QString("link() failed: %1").arg(strerror(errno))); return;
    }

    FsNodeItem* parentNode = m_nodes.value(m_contextDir);
    FsNodeItem* newNode = addNode(newPath);
    newNode->setPos(srcNode->pos() + QPointF(155, 0));
    if (parentNode && parentNode != newNode)
        addEdge(parentNode, newNode, FsEdgeItem::Containment);
    addEdge(srcNode, newNode, FsEdgeItem::Hardlink);
    srcNode->refreshStat(); newNode->refreshStat();
    for (auto* e : m_edges) e->adjust();

    emit statusMessage(QString("'%1' and '%2' share inode %3 (link count: %4)")
        .arg(srcNode->data().name).arg(newName.trimmed())
        .arg(newNode->data().inode).arg(newNode->data().nlinks));
}

// ── Toolbar: symlink ──────────────────────────────────────────────────────────

void FsCanvas::doSymLink() {
    auto sel = m_scene->selectedItems();
    if (sel.isEmpty()) { emit statusMessage("Select the target node first, then click Sym Link."); return; }
    auto* targetNode = dynamic_cast<FsNodeItem*>(sel.first());
    if (!targetNode) return;

    bool ok;
    QString linkName = QInputDialog::getText(this, "Create Symbolic Link",
        QString("New symlink → '%1'\nWill be created in %2\n\nSymlink name:")
            .arg(targetNode->data().name)
            .arg(QFileInfo(m_contextDir).fileName().isEmpty()?"sandbox":QFileInfo(m_contextDir).fileName()),
        QLineEdit::Normal, uniqueName(m_contextDir, targetNode->data().name+"_link"), &ok);
    if (!ok || linkName.trimmed().isEmpty()) return;

    QString linkPath   = m_contextDir + "/" + linkName.trimmed();
    // Build a relative path from contextDir to target
    QString targetRel  = QFileInfo(targetNode->absPath()).fileName();
    // If target is in a different directory, use absolute path
    if (QFileInfo(targetNode->absPath()).dir().absolutePath() != m_contextDir)
        targetRel = targetNode->absPath();

    if (::symlink(targetRel.toLocal8Bit().constData(),
                  linkPath.toLocal8Bit().constData()) != 0) {
        emit statusMessage(QString("symlink() failed: %1").arg(strerror(errno))); return;
    }

    FsNodeItem* parentNode = m_nodes.value(m_contextDir);
    FsNodeItem* linkNode = addNode(linkPath);
    linkNode->setPos(targetNode->pos() + QPointF(0, 155));
    if (parentNode && parentNode != linkNode)
        addEdge(parentNode, linkNode, FsEdgeItem::Containment);
    addEdge(linkNode, targetNode, FsEdgeItem::Symlink);
    for (auto* e : m_edges) e->adjust();

    emit statusMessage(QString("Symlink '%1' → '%2'. Delete target to see dangling arrow.")
        .arg(linkName.trimmed()).arg(targetRel));
}

// ── Mouse events ──────────────────────────────────────────────────────────────

void FsCanvas::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton)
        m_pressPos = mapToScene(e->pos());

    // Right-click context menu on a node
    if (e->button() == Qt::RightButton) {
        QPointF sp = mapToScene(e->pos());
        FsNodeItem* hit = nodeAt(sp);
        if (hit) {
            // Select the node first
            m_scene->clearSelection();
            hit->setSelected(true);

            auto* menu = new QMenu(this);
            menu->setStyleSheet(
                "QMenu{background:#fff;border:1px solid #E5E7EB;border-radius:6px;padding:4px;}"
                "QMenu::item{padding:5px 20px;font-size:12px;color:#1f2328;border-radius:4px;}"
                "QMenu::item:selected{background:#EEF2FF;color:#3B5BF6;}"
                "QMenu::separator{height:1px;background:#E5E7EB;margin:3px 8px;}");

            bool isDir  = hit->data().type == FsNodeData::Dir;
            bool isFile = hit->data().type == FsNodeData::File;
            bool isRoot = (hit->absPath() == m_sandboxRoot);

            if (isDir) {
                menu->addAction("📁 Enter directory", this, [this, hit]{ setContextDir(hit->absPath()); });
                menu->addAction("📄 New File here",   this, [this, hit]{ setContextDir(hit->absPath()); doNewFile(); });
                menu->addAction("📁 New Dir here",    this, [this, hit]{ setContextDir(hit->absPath()); doNewDir(); });
                menu->addSeparator();
            }
            if (isFile) {
                menu->addAction("✍ Write content",   this, &FsCanvas::doWriteContent);
                menu->addAction("📖 Read content",   this, &FsCanvas::doReadContent);
                menu->addAction("🔗 Hard Link…",     this, &FsCanvas::doHardLink);
                menu->addSeparator();
            }
            menu->addAction("↪ Sym Link…",       this, &FsCanvas::doSymLink);
            menu->addAction("✏ Rename…",          this, &FsCanvas::doRename);
            menu->addAction("🔑 chmod…",          this, &FsCanvas::doChmod);
            menu->addAction("👤 chown…",          this, &FsCanvas::doChown);
            if (!isRoot)
                menu->addAction("🗑 Delete",       this, &FsCanvas::doDelete);

            menu->popup(e->globalPos());
            return;  // do not pass right-click to the scene's scroll-drag handler
        }
    }

    QGraphicsView::mousePressEvent(e);
}

void FsCanvas::mouseMoveEvent(QMouseEvent* e) { QGraphicsView::mouseMoveEvent(e); }

void FsCanvas::mouseReleaseEvent(QMouseEvent* e) {
    // Bug fix: set context only on a clean click (no drag).  QGraphicsView uses a 4px threshold
    // internally; we use 6px so small wobble during press never changes context.
    if (e->button() == Qt::LeftButton) {
        QPointF sp = mapToScene(e->pos());
        bool wasDrag = (sp - m_pressPos).manhattanLength() > 6.0;
        if (!wasDrag) {
            FsNodeItem* hit = nodeAt(sp);
            if (hit && hit->data().type == FsNodeData::Dir)
                setContextDir(hit->absPath());
        }
    }
    QGraphicsView::mouseReleaseEvent(e);
}

void FsCanvas::wheelEvent(QWheelEvent* e) {
    scale(e->angleDelta().y() > 0 ? 1.15 : 1.0/1.15, e->angleDelta().y() > 0 ? 1.15 : 1.0/1.15);
}

void FsCanvas::dragEnterEvent(QDragEnterEvent* e) { e->ignore(); }
void FsCanvas::dragMoveEvent(QDragMoveEvent* e)   { e->ignore(); }
void FsCanvas::dropEvent(QDropEvent* e)            { e->ignore(); }

// ── Double-click → zoom into dir without clearing canvas ─────────────────────

bool FsCanvas::event(QEvent* ev) {
    if (ev->type() == QEvent::MouseButtonDblClick) {
        auto* me = static_cast<QMouseEvent*>(ev);
        QPointF sp = mapToScene(me->pos());
        for (auto* item : m_scene->items(sp)) {
            if (auto* n = dynamic_cast<FsNodeItem*>(item)) {
                if (n->data().type == FsNodeData::Dir) {
                    // Just zoom/center on this node and set as context; tree stays visible
                    setContextDir(n->absPath());
                    QRectF r = n->boundingRect().translated(n->pos());
                    r.adjust(-200,-200,200,200);
                    fitInView(r, Qt::KeepAspectRatio);
                    return true;
                }
            }
        }
    }
    return QGraphicsView::event(ev);
}

// ── Helpers ───────────────────────────────────────────────────────────────────

FsNodeItem* FsCanvas::nodeAt(const QPointF& scenePos) const {
    for (auto* item : m_scene->items(scenePos))
        if (auto* n = dynamic_cast<FsNodeItem*>(item)) return n;
    return nullptr;
}

void FsCanvas::refreshEdgeDangling() {
    for (auto* e : m_edges) {
        if (e->kind() != FsEdgeItem::Symlink) continue;
        FsNodeItem* src = e->source();
        if (src->data().type != FsNodeData::Symlink) continue;
        QString target = src->data().symlinkTarget;
        if (!target.startsWith('/'))
            target = QFileInfo(src->absPath()).dir().absolutePath() + "/" + target;
        e->setDangling(!QFileInfo::exists(target));
    }
}

QString FsCanvas::uniqueName(const QString& dir, const QString& base, const QString& ext) {
    if (!QFileInfo::exists(dir+"/"+base+ext)) return base+ext;
    for (int i=2; i<100; ++i) {
        QString c = base+QString::number(i)+ext;
        if (!QFileInfo::exists(dir+"/"+c)) return c;
    }
    return base+"_"+QString::number(QDateTime::currentMSecsSinceEpoch()%10000)+ext;
}

// ── Inotify — multi-watch (one per directory in tree) ────────────────────────

void FsCanvas::addInotifyWatch(const QString& dir) {
    if (m_inoFd < 0) return;
    int wd = ::inotify_add_watch(m_inoFd, dir.toLocal8Bit().constData(),
                                 IN_CREATE|IN_DELETE|IN_MOVED_FROM|IN_MOVED_TO|
                                 IN_ATTRIB|IN_CLOSE_WRITE);
    if (wd >= 0) m_inoWds[wd] = dir;
}

void FsCanvas::startInotify() {
    m_inoFd = ::inotify_init1(IN_NONBLOCK);
    if (m_inoFd < 0) return;

    // Watch the root and every subdir already known
    std::function<void(const QString&)> watchAll = [&](const QString& dir) {
        addInotifyWatch(dir);
        QDir qd(dir);
        qd.setFilter(QDir::Dirs|QDir::NoDotAndDotDot|QDir::Hidden|QDir::System);
        for (const QFileInfo& fi : qd.entryInfoList())
            if (!fi.isSymLink()) watchAll(fi.absoluteFilePath());
    };
    watchAll(m_sandboxRoot);
    m_inoTimer->start(200);
}

void FsCanvas::stopInotify() {
    m_inoTimer->stop();
    if (m_inoFd >= 0) { ::close(m_inoFd); m_inoFd=-1; }
    m_inoWds.clear();
}

void FsCanvas::pollInotify() {
    if (m_inoFd < 0) return;
    char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    ssize_t n = ::read(m_inoFd, buf, sizeof(buf));
    if (n <= 0) return;
    char* p = buf;
    while (p < buf+n) {
        auto* ev = reinterpret_cast<struct inotify_event*>(p);
        QString watchDir = m_inoWds.value(ev->wd);
        QString name = ev->len > 0 ? QString::fromLocal8Bit(ev->name) : QString();
        if (!watchDir.isEmpty() && !name.isEmpty())
            onInotifyEvent(ev->mask, watchDir + "/" + name);
        p += sizeof(struct inotify_event) + ev->len;
    }
}

// onInotifyEvent now receives the full absolute path directly from pollInotify
void FsCanvas::onInotifyEvent(uint32_t mask, const QString& absPath) {
    if (absPath.isEmpty()) return;

    if (mask & (IN_CREATE | IN_MOVED_TO)) {
        if (m_nodes.contains(absPath)) return;
        if (!QFileInfo::exists(absPath)) return;

        FsNodeItem* child = addNode(absPath);

        // Find the parent: it's the directory containing this path
        QString parentPath = QFileInfo(absPath).dir().absolutePath();
        FsNodeItem* parentNode = m_nodes.value(parentPath);

        if (parentNode) {
            bool hasContainment = false;
            for (auto* e : m_edges)
                if (e->kind()==FsEdgeItem::Containment && e->source()==parentNode && e->dest()==child)
                    { hasContainment=true; break; }
            if (!hasContainment) {
                addEdge(parentNode, child, FsEdgeItem::Containment);
                placeAsChild(parentNode, child);
            }
            // If it's a new dir, start watching it
            if (QFileInfo(absPath).isDir() && !QFileInfo(absPath).isSymLink())
                addInotifyWatch(absPath);
        }

        // Hardlink check
        for (auto* other : m_nodes) {
            if (other==child || other==parentNode) continue;
            if (other->data().type==FsNodeData::Symlink) continue;
            if (other->data().inode==child->data().inode && child->data().inode!=0) {
                bool exists = false;
                for (auto* e : m_edges)
                    if (e->kind()==FsEdgeItem::Hardlink &&
                        ((e->source()==other&&e->dest()==child)||(e->source()==child&&e->dest()==other)))
                        { exists=true; break; }
                if (!exists) {
                    addEdge(other, child, FsEdgeItem::Hardlink);
                    child->setPos(other->pos() + QPointF(155, 0));
                }
            }
        }
        // Symlink check
        if (child->data().type == FsNodeData::Symlink) {
            QString target = child->data().symlinkTarget;
            if (!target.startsWith('/'))
                target = QFileInfo(absPath).dir().absolutePath() + "/" + target;
            if (m_nodes.contains(target))
                addEdge(child, m_nodes[target], FsEdgeItem::Symlink);
        }
        for (auto* e : m_edges) e->adjust();

    } else if (mask & (IN_DELETE | IN_MOVED_FROM)) {
        if (m_contextDir == absPath) setContextDir(m_sandboxRoot);
        removeNode(absPath);
        for (auto* node : m_nodes) node->refreshStat();
        refreshEdgeDangling();

    } else if (mask & (IN_ATTRIB | IN_CLOSE_WRITE)) {
        if (m_nodes.contains(absPath))
            m_nodes[absPath]->refreshStat();
        refreshEdgeDangling();
    }
}
