#pragma once
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QGraphicsEllipseItem>
#include <QGraphicsTextItem>
#include <QGraphicsLineItem>
#include <QTimer>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>
#include <sys/inotify.h>
#include <sys/stat.h>

// ── Forward declarations ──────────────────────────────────────────────────────
class FsNodeItem;
class FsEdgeItem;

// ── Data model for one node ───────────────────────────────────────────────────
struct FsNodeData {
    QString  absPath;       // full path in sandbox
    QString  name;          // basename
    ino_t    inode  = 0;
    nlink_t  nlinks = 0;
    enum Type { File, Dir, Symlink } type = File;
    QString  symlinkTarget; // only when type==Symlink
};

// ── A single node on the canvas ───────────────────────────────────────────────
class FsNodeItem : public QGraphicsItem {
public:
    explicit FsNodeItem(const FsNodeData& d, QGraphicsItem* parent = nullptr);

    void        setData(const FsNodeData& d);
    FsNodeData  data() const { return m_data; }
    QString     absPath() const { return m_data.absPath; }
    QList<FsEdgeItem*> edges() const { return m_edges; }
    void        addEdge(FsEdgeItem* e) { m_edges.append(e); }
    void        removeEdge(FsEdgeItem* e) { m_edges.removeAll(e); }
    void        refreshStat();   // re-stat() and repaint

    // QGraphicsItem
    QRectF      boundingRect() const override;
    void        paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) override;

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* e) override;
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;

private:
    FsNodeData         m_data;
    QList<FsEdgeItem*> m_edges;
    bool               m_selected = false;

    static constexpr int W = 100;
    static constexpr int H = 56;
    QColor bodyColor() const;
    QString typeLabel() const;
};

// ── An edge connecting two nodes ──────────────────────────────────────────────
class FsEdgeItem : public QGraphicsItem {
public:
    enum EdgeKind { Hardlink, Symlink };

    FsEdgeItem(FsNodeItem* src, FsNodeItem* dst, EdgeKind kind,
               QGraphicsItem* parent = nullptr);

    FsNodeItem* source() const { return m_src; }
    FsNodeItem* dest()   const { return m_dst; }
    EdgeKind    kind()   const { return m_kind; }
    bool        dangling() const { return m_dangling; }
    void        setDangling(bool d) { m_dangling = d; update(); }
    void        adjust();   // recalculate endpoints from node positions

    QRectF   boundingRect() const override;
    void     paint(QPainter*, const QStyleOptionGraphicsItem*, QWidget*) override;

private:
    FsNodeItem* m_src;
    FsNodeItem* m_dst;
    EdgeKind    m_kind;
    bool        m_dangling = false;
    QPointF     m_sp, m_ep;  // start/end in scene coords
};

// ── The main canvas widget ────────────────────────────────────────────────────
class FsCanvas : public QGraphicsView {
    Q_OBJECT
public:
    explicit FsCanvas(QWidget* parent = nullptr);
    ~FsCanvas();

    // Sandbox root — everything lives under this path
    QString sandboxRoot() const { return m_sandboxRoot; }
    void    setSandboxRoot(const QString& p);

    // Actions called by toolbar buttons
    void doNewFile();
    void doNewDir();
    void doDelete();
    void doRename();
    void doHardLink();
    void doSymLink();

    // Called by the inotify poller in FilesystemLab to feed events here
    void onInotifyEvent(uint32_t mask, const QString& name);

signals:
    void nodeClicked(const FsNodeData& d);
    void statusMessage(const QString& msg);

protected:
    void dragEnterEvent(QDragEnterEvent*) override;
    void dragMoveEvent(QDragMoveEvent*)   override;
    void dropEvent(QDropEvent*)           override;
    void wheelEvent(QWheelEvent*)         override;
    void mousePressEvent(QMouseEvent*)    override;
    void mouseReleaseEvent(QMouseEvent*)  override;
    void mouseMoveEvent(QMouseEvent*)     override;

private:
    QGraphicsScene* m_scene;
    QString         m_sandboxRoot;

    // All live nodes keyed by absolute path
    QMap<QString, FsNodeItem*> m_nodes;
    // All live edges
    QList<FsEdgeItem*>         m_edges;

    // Edge-drawing drag state
    bool        m_drawingEdge = false;
    FsNodeItem* m_edgeSrc     = nullptr;
    QGraphicsLineItem* m_edgeDraftLine = nullptr;
    enum PendingEdge { None, Hard, Sym } m_pendingEdge = None;

    // Inotify for sandbox
    int     m_inoFd   = -1;
    int     m_inoWd   = -1;
    QTimer* m_inoTimer = nullptr;

    // ── helpers ───────────────────────────────────────────────────────────────
    void        scanSandbox();
    FsNodeItem* nodeAt(const QPointF& scenePos) const;
    FsNodeItem* addNode(const QString& absPath);
    void        removeNode(const QString& absPath);
    void        addEdge(FsNodeItem* src, FsNodeItem* dst, FsEdgeItem::EdgeKind kind);
    void        removeEdgesFor(FsNodeItem* n);
    void        autoLayout();
    void        refreshEdgeDangling();
    QString     uniqueName(const QString& dir, const QString& base, const QString& ext = {});
    void        pollInotify();
    void        startInotify();
    void        stopInotify();
};
