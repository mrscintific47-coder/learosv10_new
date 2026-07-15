#include "FsCanvas.h"
#include "Theme.h"
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
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <cerrno>

// ═════════════════════════════════════════════════════════════════════════════
// FsNodeItem
// ═════════════════════════════════════════════════════════════════════════════

FsNodeItem::FsNodeItem(const FsNodeData& d, QGraphicsItem* parent)
    : QGraphicsItem(parent), m_data(d)
{
    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
    setZValue(1);
    refreshStat();
}

void FsNodeItem::setData(const FsNodeData& d) {
    m_data = d;
    update();
}

void FsNodeItem::refreshStat() {
    struct stat st{};
    if (::lstat(m_data.absPath.toLocal8Bit().constData(), &st) == 0) {
        m_data.inode  = st.st_ino;
        m_data.nlinks = st.st_nlink;
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

QRectF FsNodeItem::boundingRect() const {
    return QRectF(-W/2, -H/2, W, H);
}

QColor FsNodeItem::bodyColor() const {
    switch (m_data.type) {
        case FsNodeData::Dir:     return QColor("#EEF2FF");
        case FsNodeData::Symlink: return QColor("#FFF7ED");
        default:                  return QColor("#F0FDF4");
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

    QRectF rect(-W/2, -H/2, W, H);

    // Shadow
    p->setPen(Qt::NoPen);
    p->setBrush(QColor(0,0,0,18));
    p->drawRoundedRect(rect.translated(0, 3), 10, 10);

    // Body
    QColor bg = bodyColor();
    if (isSelected() || m_selected) bg = bg.darker(110);
    p->setBrush(bg);
    QColor border = isSelected() ? QColor(Theme::BLUE) : QColor(Theme::BORDER);
    p->setPen(QPen(border, isSelected() ? 2.0 : 1.0));
    p->drawRoundedRect(rect, 10, 10);

    // Hardlink indicator — thick left accent when nlinks > 1
    if (m_data.nlinks > 1 && m_data.type == FsNodeData::File) {
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(Theme::ORANGE));
        p->drawRoundedRect(QRectF(-W/2, -H/2+4, 4, H-8), 2, 2);
    }

    // Type icon
    QFont iconFont("Segoe UI Emoji", 14);
    p->setFont(iconFont);
    p->setPen(QColor(Theme::TEXT_SECONDARY));
    p->drawText(QRectF(-W/2+4, -H/2, 24, H),
                Qt::AlignVCenter | Qt::AlignLeft, typeLabel());

    // Name
    QFont nameFont("Consolas", 8, QFont::Bold);
    p->setFont(nameFont);
    p->setPen(QColor(Theme::TEXT_PRIMARY));
    QString name = m_data.name.length() > 12
                 ? m_data.name.left(10) + "…"
                 : m_data.name;
    p->drawText(QRectF(-W/2+26, -H/2, W-30, H/2+2),
                Qt::AlignBottom | Qt::AlignLeft, name);

    // Inode + nlinks badges
    QFont badgeFont("Consolas", 7);
    p->setFont(badgeFont);
    p->setPen(QColor(Theme::TEXT_MUTED));
    QString badge = QString("ino:%1  lk:%2")
        .arg(m_data.inode).arg(m_data.nlinks);
    p->drawText(QRectF(-W/2+26, 2, W-30, H/2-2),
                Qt::AlignTop | Qt::AlignLeft, badge);

    // Symlink target
    if (m_data.type == FsNodeData::Symlink && !m_data.symlinkTarget.isEmpty()) {
        QFont tf("Consolas", 6);
        p->setFont(tf);
        p->setPen(QColor(Theme::ORANGE));
        QString tgt = m_data.symlinkTarget;
        if (tgt.length() > 14) tgt = tgt.left(12) + "…";
        p->drawText(QRectF(-W/2+26, H/2-12, W-30, 12),
                    Qt::AlignBottom | Qt::AlignLeft, "→ " + tgt);
    }
}

void FsNodeItem::mousePressEvent(QGraphicsSceneMouseEvent* e) {
    QGraphicsItem::mousePressEvent(e);
}

void FsNodeItem::mouseDoubleClickEvent(QGraphicsSceneMouseEvent*) {
    // handled by scene/view
}

QVariant FsNodeItem::itemChange(GraphicsItemChange change, const QVariant& value) {
    if (change == ItemPositionHasChanged) {
        for (auto* e : m_edges) e->adjust();
    }
    return QGraphicsItem::itemChange(change, value);
}

// ═════════════════════════════════════════════════════════════════════════════
// FsEdgeItem
// ═════════════════════════════════════════════════════════════════════════════

FsEdgeItem::FsEdgeItem(FsNodeItem* src, FsNodeItem* dst, EdgeKind kind,
                       QGraphicsItem* parent)
    : QGraphicsItem(parent), m_src(src), m_dst(dst), m_kind(kind)
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
    qreal minX = std::min(m_sp.x(), m_ep.x()) - 10;
    qreal minY = std::min(m_sp.y(), m_ep.y()) - 10;
    qreal maxX = std::max(m_sp.x(), m_ep.x()) + 10;
    qreal maxY = std::max(m_sp.y(), m_ep.y()) + 10;
    return QRectF(minX, minY, maxX - minX, maxY - minY);
}

void FsEdgeItem::paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) {
    p->setRenderHint(QPainter::Antialiasing);

    QColor color = m_kind == Symlink
                 ? QColor(Theme::ORANGE)
                 : QColor(Theme::GREEN);
    if (m_dangling) color = QColor(Theme::RED);

    QPen pen(color, m_kind == Hardlink ? 2.5 : 1.5);
    if (m_kind == Symlink || m_dangling) {
        pen.setStyle(Qt::DashLine);
        pen.setDashPattern({4, 3});
    }
    p->setPen(pen);

    // Draw the line
    QLineF line(m_sp, m_ep);
    p->drawLine(line);

    // Arrow head at destination
    if (line.length() < 1.0) return;
    double angle = std::atan2(-(m_ep.y() - m_sp.y()), m_ep.x() - m_sp.x());
    constexpr double arrowSize = 10.0;
    QPointF p1 = m_ep + QPointF(
        std::cos(angle + M_PI*5/6) * arrowSize,
        -std::sin(angle + M_PI*5/6) * arrowSize);
    QPointF p2 = m_ep + QPointF(
        std::cos(angle - M_PI*5/6) * arrowSize,
        -std::sin(angle - M_PI*5/6) * arrowSize);
    p->setBrush(color);
    p->setPen(Qt::NoPen);
    p->drawPolygon(QPolygonF({m_ep, p1, p2}));

    // Label in middle
    QPointF mid = (m_sp + m_ep) / 2.0;
    QFont f("Consolas", 7);
    p->setFont(f);
    p->setPen(color);
    QString label = m_kind == Hardlink ? "hard" : "sym";
    if (m_dangling) label = "dangling";
    p->drawText(mid + QPointF(4, -4), label);
}

// ═════════════════════════════════════════════════════════════════════════════
// FsCanvas
// ═════════════════════════════════════════════════════════════════════════════

FsCanvas::FsCanvas(QWidget* parent)
    : QGraphicsView(parent)
    , m_scene(new QGraphicsScene(this))
{
    setScene(m_scene);
    setRenderHint(QPainter::Antialiasing);
    setDragMode(QGraphicsView::ScrollHandDrag);
    setBackgroundBrush(QColor("#F8FAFC"));
    setStyleSheet(QString(
        "QGraphicsView { border:1px solid %1; border-radius:10px; }"
    ).arg(Theme::BORDER));
    setTransformationAnchor(AnchorUnderMouse);
    setResizeAnchor(AnchorViewCenter);

    m_inoTimer = new QTimer(this);
    connect(m_inoTimer, &QTimer::timeout, this, &FsCanvas::pollInotify);
}

FsCanvas::~FsCanvas() {
    stopInotify();
}

// ── Sandbox root ──────────────────────────────────────────────────────────────

void FsCanvas::setSandboxRoot(const QString& p) {
    stopInotify();
    m_sandboxRoot = p;

    // Create the sandbox directory if it doesn't exist
    QDir().mkpath(p);

    m_scene->clear();
    m_nodes.clear();
    m_edges.clear();

    scanSandbox();
    startInotify();
}

// ── Scan existing sandbox contents ───────────────────────────────────────────

void FsCanvas::scanSandbox() {
    // Walk one level deep (directories show children on expand — for now flat)
    QDir dir(m_sandboxRoot);
    dir.setFilter(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot |
                  QDir::Hidden | QDir::System);
    for (const QFileInfo& fi : dir.entryInfoList()) {
        addNode(fi.absoluteFilePath());
    }
    // Detect hardlink groups by inode
    // Two nodes sharing the same inode (non-symlink) get a hardlink edge
    QMap<ino_t, QList<FsNodeItem*>> inodeMap;
    for (auto* n : m_nodes) {
        if (n->data().type != FsNodeData::Symlink)
            inodeMap[n->data().inode].append(n);
    }
    for (auto& group : inodeMap) {
        if (group.size() < 2) continue;
        for (int i = 1; i < group.size(); ++i) {
            // Only add an edge if none exists yet
            bool exists = false;
            for (auto* e : m_edges) {
                if ((e->source() == group[0] && e->dest() == group[i]) ||
                    (e->source() == group[i] && e->dest() == group[0])) {
                    exists = true; break;
                }
            }
            if (!exists) addEdge(group[0], group[i], FsEdgeItem::Hardlink);
        }
    }
    // Detect symlink edges
    for (auto* n : m_nodes) {
        if (n->data().type != FsNodeData::Symlink) continue;
        QString target = n->data().symlinkTarget;
        // Resolve relative symlinks
        if (!target.startsWith('/'))
            target = m_sandboxRoot + "/" + target;
        if (m_nodes.contains(target))
            addEdge(n, m_nodes[target], FsEdgeItem::Symlink);
        else {
            // dangling — add with a placeholder if we ever support it
        }
    }
    autoLayout();
}

// ── Node management ───────────────────────────────────────────────────────────

FsNodeItem* FsCanvas::addNode(const QString& absPath) {
    if (m_nodes.contains(absPath)) {
        m_nodes[absPath]->refreshStat();
        return m_nodes[absPath];
    }
    FsNodeData d;
    d.absPath = absPath;
    d.name    = QFileInfo(absPath).fileName();
    auto* item = new FsNodeItem(d);
    m_scene->addItem(item);
    m_nodes[absPath] = item;
    connect(m_scene, &QGraphicsScene::selectionChanged, this, [this](){
        const auto sel = m_scene->selectedItems();
        if (!sel.isEmpty()) {
            if (auto* n = dynamic_cast<FsNodeItem*>(sel.first()))
                emit nodeClicked(n->data());
        }
    });
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
        if (e->source() == n || e->dest() == n)
            toRemove.append(e);
    for (auto* e : toRemove) {
        e->source()->removeEdge(e);
        e->dest()->removeEdge(e);
        m_edges.removeAll(e);
        m_scene->removeItem(e);
        delete e;
    }
}

void FsCanvas::addEdge(FsNodeItem* src, FsNodeItem* dst, FsEdgeItem::EdgeKind kind) {
    auto* e = new FsEdgeItem(src, dst, kind);
    m_scene->addItem(e);
    m_edges.append(e);
}

// ── Auto-layout ───────────────────────────────────────────────────────────────

void FsCanvas::autoLayout() {
    // Simple grid layout — nodes arrange in rows of 5
    constexpr int COLS = 5;
    constexpr int DX   = 140;
    constexpr int DY   = 100;
    int i = 0;
    for (auto* n : m_nodes) {
        n->setPos((i % COLS) * DX - (COLS/2)*DX,
                  (i / COLS) * DY);
        i++;
    }
    for (auto* e : m_edges) e->adjust();
}

// ── Toolbar actions ───────────────────────────────────────────────────────────

void FsCanvas::doNewFile() {
    if (m_sandboxRoot.isEmpty()) return;
    bool ok;
    QString name = QInputDialog::getText(this, "New File", "File name:",
                                         QLineEdit::Normal,
                                         uniqueName(m_sandboxRoot, "file", ".txt"), &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    QString path = m_sandboxRoot + "/" + name.trimmed();
    int fd = ::open(path.toLocal8Bit().constData(),
                    O_CREAT | O_WRONLY | O_EXCL, 0644);
    if (fd < 0) {
        emit statusMessage(QString("open() failed: %1").arg(strerror(errno)));
        return;
    }
    ::close(fd);
    emit statusMessage(QString("Created file: %1").arg(path));
    // inotify will pick it up; fallback: add directly
    if (!m_nodes.contains(path)) {
        auto* n = addNode(path);
        // Place near center
        n->setPos(QRandomGenerator::global()->bounded(-100, 100),
                  QRandomGenerator::global()->bounded(-100, 100));
        for (auto* e : m_edges) e->adjust();
    }
}

void FsCanvas::doNewDir() {
    if (m_sandboxRoot.isEmpty()) return;
    bool ok;
    QString name = QInputDialog::getText(this, "New Directory", "Directory name:",
                                         QLineEdit::Normal,
                                         uniqueName(m_sandboxRoot, "dir"), &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    QString path = m_sandboxRoot + "/" + name.trimmed();
    if (::mkdir(path.toLocal8Bit().constData(), 0755) != 0) {
        emit statusMessage(QString("mkdir() failed: %1").arg(strerror(errno)));
        return;
    }
    emit statusMessage(QString("Created dir: %1").arg(path));
    if (!m_nodes.contains(path)) {
        auto* n = addNode(path);
        n->setPos(QRandomGenerator::global()->bounded(-100, 100),
                  QRandomGenerator::global()->bounded(-100, 100));
        for (auto* e : m_edges) e->adjust();
    }
}

void FsCanvas::doDelete() {
    auto sel = m_scene->selectedItems();
    if (sel.isEmpty()) {
        emit statusMessage("Select a node to delete.");
        return;
    }
    auto* n = dynamic_cast<FsNodeItem*>(sel.first());
    if (!n) return;
    QString path = n->absPath();
    QString name = QFileInfo(path).fileName();

    auto ans = QMessageBox::warning(this, "Delete",
        QString("Delete '%1'?\n\nReal syscall — this cannot be undone.").arg(name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ans != QMessageBox::Yes) return;

    int rc;
    if (n->data().type == FsNodeData::Dir)
        rc = ::rmdir(path.toLocal8Bit().constData());
    else
        rc = ::unlink(path.toLocal8Bit().constData());

    if (rc != 0) {
        emit statusMessage(QString("unlink/rmdir failed: %1").arg(strerror(errno)));
        return;
    }
    emit statusMessage(QString("Deleted: %1").arg(path));
    removeNode(path);
    // Refresh link counts on remaining nodes that might share the same inode
    for (auto* node : m_nodes) node->refreshStat();
    refreshEdgeDangling();
}

void FsCanvas::doRename() {
    auto sel = m_scene->selectedItems();
    if (sel.isEmpty()) { emit statusMessage("Select a node to rename."); return; }
    auto* n = dynamic_cast<FsNodeItem*>(sel.first());
    if (!n) return;
    QString oldPath = n->absPath();
    bool ok;
    QString newName = QInputDialog::getText(this, "Rename",
        "New name:", QLineEdit::Normal,
        QFileInfo(oldPath).fileName(), &ok);
    if (!ok || newName.trimmed().isEmpty()) return;
    QString newPath = QFileInfo(oldPath).dir().absolutePath() + "/" + newName.trimmed();
    if (::rename(oldPath.toLocal8Bit().constData(),
                 newPath.toLocal8Bit().constData()) != 0) {
        emit statusMessage(QString("rename() failed: %1").arg(strerror(errno)));
        return;
    }
    emit statusMessage(QString("Renamed: %1 → %2").arg(QFileInfo(oldPath).fileName())
                                                    .arg(newName.trimmed()));
    // Update node
    FsNodeData d = n->data();
    m_nodes.remove(oldPath);
    d.absPath = newPath;
    d.name    = newName.trimmed();
    n->setData(d);
    n->refreshStat();
    m_nodes[newPath] = n;
}

void FsCanvas::doHardLink() {
    m_pendingEdge = Hard;
    setDragMode(NoDrag);
    setCursor(Qt::CrossCursor);
    emit statusMessage("Click source node, then drag to target to create a hard link.");
}

void FsCanvas::doSymLink() {
    m_pendingEdge = Sym;
    setDragMode(NoDrag);
    setCursor(Qt::CrossCursor);
    emit statusMessage("Click source node, then drag to target to create a symlink.");
}

// ── Edge-drawing mouse logic ──────────────────────────────────────────────────

void FsCanvas::mousePressEvent(QMouseEvent* e) {
    if (m_pendingEdge != None && e->button() == Qt::LeftButton) {
        QPointF scenePos = mapToScene(e->pos());
        FsNodeItem* hit = nodeAt(scenePos);
        if (hit) {
            m_drawingEdge = true;
            m_edgeSrc = hit;
            m_edgeDraftLine = m_scene->addLine(
                QLineF(hit->scenePos(), hit->scenePos()),
                QPen(QColor(m_pendingEdge == Hard ? Theme::GREEN : Theme::ORANGE),
                     1.5, Qt::DashLine));
            return;
        }
    }
    QGraphicsView::mousePressEvent(e);
}

void FsCanvas::mouseMoveEvent(QMouseEvent* e) {
    if (m_drawingEdge && m_edgeDraftLine) {
        QPointF sp = mapToScene(e->pos());
        m_edgeDraftLine->setLine(QLineF(m_edgeSrc->scenePos(), sp));
        return;
    }
    QGraphicsView::mouseMoveEvent(e);
}

void FsCanvas::mouseReleaseEvent(QMouseEvent* e) {
    if (m_drawingEdge && e->button() == Qt::LeftButton) {
        // Clean up draft line
        if (m_edgeDraftLine) {
            m_scene->removeItem(m_edgeDraftLine);
            delete m_edgeDraftLine;
            m_edgeDraftLine = nullptr;
        }
        QPointF scenePos = mapToScene(e->pos());
        FsNodeItem* dst = nodeAt(scenePos);
        if (dst && dst != m_edgeSrc) {
            if (m_pendingEdge == Hard) {
                // hard link: link(src, dst_new_name) — src must be a file
                if (m_edgeSrc->data().type == FsNodeData::Dir) {
                    emit statusMessage("Hard links to directories are not permitted by the kernel.");
                } else {
                    bool ok;
                    QString newName = QInputDialog::getText(this,
                        "Hard Link", "New name for the link:",
                        QLineEdit::Normal,
                        uniqueName(m_sandboxRoot,
                                   QFileInfo(m_edgeSrc->absPath()).baseName(), ".txt"), &ok);
                    if (ok && !newName.trimmed().isEmpty()) {
                        QString newPath = m_sandboxRoot + "/" + newName.trimmed();
                        if (::link(m_edgeSrc->absPath().toLocal8Bit().constData(),
                                   newPath.toLocal8Bit().constData()) != 0) {
                            emit statusMessage(QString("link() failed: %1").arg(strerror(errno)));
                        } else {
                            emit statusMessage(QString("Hard link created: %1 → inode %2")
                                .arg(newName.trimmed())
                                .arg(m_edgeSrc->data().inode));
                            // Add new node (shares same inode)
                            auto* newNode = addNode(newPath);
                            newNode->setPos(dst->scenePos() +
                                QPointF(QRandomGenerator::global()->bounded(-30, 30),
                                        QRandomGenerator::global()->bounded(-30, 30)));
                            // Edge from src to new node
                            addEdge(m_edgeSrc, newNode, FsEdgeItem::Hardlink);
                            // Refresh link counts
                            m_edgeSrc->refreshStat();
                            newNode->refreshStat();
                            for (auto* edge : m_edges) edge->adjust();
                        }
                    }
                }
            } else { // Symlink
                bool ok;
                QString linkName = QInputDialog::getText(this,
                    "Symlink", "Name for the symlink:",
                    QLineEdit::Normal,
                    uniqueName(m_sandboxRoot, QFileInfo(dst->absPath()).baseName() + "_link"), &ok);
                if (ok && !linkName.trimmed().isEmpty()) {
                    QString linkPath = m_sandboxRoot + "/" + linkName.trimmed();
                    // symlink target is relative name
                    QString target = QFileInfo(dst->absPath()).fileName();
                    if (::symlink(target.toLocal8Bit().constData(),
                                  linkPath.toLocal8Bit().constData()) != 0) {
                        emit statusMessage(QString("symlink() failed: %1").arg(strerror(errno)));
                    } else {
                        emit statusMessage(QString("Symlink created: %1 → %2")
                            .arg(linkName.trimmed()).arg(target));
                        auto* newNode = addNode(linkPath);
                        newNode->setPos(m_edgeSrc->scenePos() +
                            QPointF(QRandomGenerator::global()->bounded(-50, 50),
                                    QRandomGenerator::global()->bounded(-50, 50)));
                        addEdge(newNode, dst, FsEdgeItem::Symlink);
                        for (auto* edge : m_edges) edge->adjust();
                    }
                }
            }
        }

        m_drawingEdge = false;
        m_edgeSrc     = nullptr;
        m_pendingEdge = None;
        setDragMode(ScrollHandDrag);
        setCursor(Qt::ArrowCursor);
        return;
    }
    QGraphicsView::mouseReleaseEvent(e);
}

void FsCanvas::wheelEvent(QWheelEvent* e) {
    double factor = e->angleDelta().y() > 0 ? 1.15 : 1.0/1.15;
    scale(factor, factor);
}

void FsCanvas::dragEnterEvent(QDragEnterEvent* e) { e->ignore(); }
void FsCanvas::dragMoveEvent(QDragMoveEvent* e)   { e->ignore(); }
void FsCanvas::dropEvent(QDropEvent* e)            { e->ignore(); }

// ── Helpers ───────────────────────────────────────────────────────────────────

FsNodeItem* FsCanvas::nodeAt(const QPointF& scenePos) const {
    for (auto* item : m_scene->items(scenePos)) {
        if (auto* n = dynamic_cast<FsNodeItem*>(item)) return n;
    }
    return nullptr;
}

void FsCanvas::refreshEdgeDangling() {
    for (auto* e : m_edges) {
        if (e->kind() == FsEdgeItem::Symlink) {
            FsNodeItem* src = e->source();
            bool ok = src->data().type == FsNodeData::Symlink;
            if (ok) {
                QString target = src->data().symlinkTarget;
                if (!target.startsWith('/'))
                    target = m_sandboxRoot + "/" + target;
                e->setDangling(!QFileInfo::exists(target));
            }
        }
    }
}

QString FsCanvas::uniqueName(const QString& dir, const QString& base,
                              const QString& ext) {
    QString path = dir + "/" + base + ext;
    if (!QFileInfo::exists(path)) return base + ext;
    for (int i = 2; i < 100; ++i) {
        QString cand = base + QString::number(i) + ext;
        if (!QFileInfo::exists(dir + "/" + cand)) return cand;
    }
    return base + "_" + QString::number(QDateTime::currentMSecsSinceEpoch() % 10000) + ext;
}

// ── Inotify for sandbox ───────────────────────────────────────────────────────

void FsCanvas::startInotify() {
    if (m_sandboxRoot.isEmpty()) return;
    m_inoFd = ::inotify_init1(IN_NONBLOCK);
    if (m_inoFd < 0) return;
    m_inoWd = ::inotify_add_watch(m_inoFd,
                                   m_sandboxRoot.toLocal8Bit().constData(),
                                   IN_CREATE | IN_DELETE | IN_MOVED_FROM |
                                   IN_MOVED_TO | IN_ATTRIB);
    if (m_inoWd < 0) { ::close(m_inoFd); m_inoFd = -1; return; }
    m_inoTimer->start(200);
}

void FsCanvas::stopInotify() {
    m_inoTimer->stop();
    if (m_inoWd >= 0 && m_inoFd >= 0) ::inotify_rm_watch(m_inoFd, m_inoWd);
    if (m_inoFd >= 0) { ::close(m_inoFd); m_inoFd = -1; }
    m_inoWd = -1;
}

void FsCanvas::pollInotify() {
    if (m_inoFd < 0) return;
    char buf[4096] __attribute__((aligned(__alignof__(struct inotify_event))));
    ssize_t n = ::read(m_inoFd, buf, sizeof(buf));
    if (n <= 0) return;

    char* p = buf;
    while (p < buf + n) {
        auto* ev = reinterpret_cast<struct inotify_event*>(p);
        QString name = ev->len > 0 ? QString::fromLocal8Bit(ev->name) : QString();
        onInotifyEvent(ev->mask, name);
        p += sizeof(struct inotify_event) + ev->len;
    }
}

void FsCanvas::onInotifyEvent(uint32_t mask, const QString& name) {
    if (name.isEmpty()) return;
    QString absPath = m_sandboxRoot + "/" + name;

    if (mask & (IN_CREATE | IN_MOVED_TO)) {
        if (!m_nodes.contains(absPath) && QFileInfo::exists(absPath)) {
            auto* n = addNode(absPath);
            // Place near a random existing node or center
            if (!m_nodes.isEmpty()) {
                // pick a nearby spot
                auto it = m_nodes.begin();
                n->setPos(it.value()->pos() +
                          QPointF(QRandomGenerator::global()->bounded(-120, 120),
                                  QRandomGenerator::global()->bounded(-80, 80)));
            }
            // Check if it's a hardlink (same inode as existing node)
            for (auto* other : m_nodes) {
                if (other == n) continue;
                if (other->data().type != FsNodeData::Symlink &&
                    other->data().inode == n->data().inode &&
                    n->data().inode != 0) {
                    // Same inode — make a hardlink edge
                    bool exists = false;
                    for (auto* e : m_edges)
                        if ((e->source()==other&&e->dest()==n) ||
                            (e->source()==n&&e->dest()==other))
                            { exists=true; break; }
                    if (!exists) addEdge(other, n, FsEdgeItem::Hardlink);
                }
            }
            // Check if it's a symlink — add edge
            if (n->data().type == FsNodeData::Symlink) {
                QString target = n->data().symlinkTarget;
                if (!target.startsWith('/')) target = m_sandboxRoot + "/" + target;
                if (m_nodes.contains(target))
                    addEdge(n, m_nodes[target], FsEdgeItem::Symlink);
            }
            for (auto* e : m_edges) e->adjust();
        }
    } else if (mask & (IN_DELETE | IN_MOVED_FROM)) {
        removeNode(absPath);
        // Refresh link counts
        for (auto* node : m_nodes) node->refreshStat();
        refreshEdgeDangling();
    } else if (mask & IN_ATTRIB) {
        if (m_nodes.contains(absPath))
            m_nodes[absPath]->refreshStat();
        refreshEdgeDangling();
    }
}
