#pragma once
#include <QGraphicsView>
#include <QGraphicsScene>
#include <QGraphicsItem>
#include <QGraphicsTextItem>
#include <QTimer>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QWidget>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

// ── Forward declarations ──────────────────────────────────────────────────────
class FsNodeItem;
class FsEdgeItem;

// ── Lock state for a node (flock advisory lock demo) ─────────────────────────
enum class LockState { None, Trying, Locked, Released };

// ── Data model for one node ───────────────────────────────────────────────────
struct FsNodeData {
    QString  absPath;
    QString  name;
    ino_t    inode   = 0;
    nlink_t  nlinks  = 0;
    mode_t   mode    = 0;
    uid_t    uid     = 0;
    gid_t    gid     = 0;
    off_t    size    = 0;
    blkcnt_t blocks  = 0;
    enum Type { File, Dir, Symlink } type = File;
    QString  symlinkTarget;

    // Live experiment state
    LockState lockState     = LockState::None;
    bool      isExternallyOpen = false;
    bool      isContext     = false;   // this dir is the current "create-into" target
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
    void        refreshStat();

    QRectF      boundingRect() const override;
    void        paint(QPainter* p, const QStyleOptionGraphicsItem*, QWidget*) override;

    QString permString() const;

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* e) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent* e) override;
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;

private:
    FsNodeData         m_data;
    QList<FsEdgeItem*> m_edges;

    static constexpr int W = 120;
    static constexpr int H = 72;
    QColor bodyColor() const;
    QString typeLabel() const;
};

// ── An edge connecting two nodes ──────────────────────────────────────────────
class FsEdgeItem : public QGraphicsItem {
public:
    enum EdgeKind {
        Containment,   // dir → child  (thin grey structural line)
        Hardlink,      // two names share one inode (green)
        Symlink,       // symlink → target (orange dashed)
        ProcFd         // live process fd → file (purple dotted)
    };

    FsEdgeItem(FsNodeItem* src, FsNodeItem* dst, EdgeKind kind,
               const QString& label = {},
               QGraphicsItem* parent = nullptr);

    FsNodeItem* source() const { return m_src; }
    FsNodeItem* dest()   const { return m_dst; }
    EdgeKind    kind()   const { return m_kind; }
    bool        dangling() const { return m_dangling; }
    void        setDangling(bool d) { m_dangling = d; update(); }
    QString     edgeLabel() const { return m_label; }
    void        setEdgeLabel(const QString& l) { m_label = l; update(); }
    void        adjust();

    QRectF   boundingRect() const override;
    void     paint(QPainter*, const QStyleOptionGraphicsItem*, QWidget*) override;

private:
    FsNodeItem* m_src;
    FsNodeItem* m_dst;
    EdgeKind    m_kind;
    QString     m_label;
    bool        m_dangling = false;
    QPointF     m_sp, m_ep;
};

// ── Breadcrumb bar ─────────────────────────────────────────────────────────────
class BreadcrumbBar : public QWidget {
    Q_OBJECT
public:
    explicit BreadcrumbBar(QWidget* parent = nullptr);
    void setPath(const QString& sandboxRoot, const QString& contextDir);

signals:
    void navigateTo(const QString& path);

private:
    QHBoxLayout* m_layout;
};

// ── The main canvas widget ────────────────────────────────────────────────────
class FsCanvas : public QGraphicsView {
    Q_OBJECT
public:
    explicit FsCanvas(QWidget* parent = nullptr);
    ~FsCanvas();

    QString sandboxRoot() const { return m_sandboxRoot; }
    void    setSandboxRoot(const QString& p);

    // The directory that is currently selected as the "create inside" context
    QString contextDir() const { return m_contextDir; }

    // Actions called by toolbar buttons
    void doNewFile();
    void doNewDir();
    void doDelete();
    void doRename();
    void doHardLink();
    void doSymLink();
    void doChmod();
    void doChown();
    void doWriteContent();
    void doReadContent();
    void doLockDemo();
    void doToggleODirect();
    void doMakeHole();

    // Refresh /proc/<pid>/fd edges
    void updateProcFdEdges(const std::vector<pid_t>& pids);

    // Fed from FilesystemLab inotify poller
    void onInotifyEvent(uint32_t mask, const QString& name);

    // Set context directory (where new files/dirs will be created)
    void setContextDir(const QString& absPath);

    // Zoom to / highlight a node
    void focusNode(const QString& absPath);

    // Kept for breadcrumb compatibility
    void enterDirectory(const QString& absPath) { setContextDir(absPath); }
    void goUp();

    BreadcrumbBar* breadcrumb() const { return m_breadcrumb; }

signals:
    void nodeClicked(const FsNodeData& d);
    void statusMessage(const QString& msg);
    void dirChanged(const QString& newDir);

protected:
    bool event(QEvent* ev) override;
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
    QString         m_contextDir;    // where new nodes get created
    BreadcrumbBar*  m_breadcrumb;

    // All live nodes keyed by absolute path
    QMap<QString, FsNodeItem*> m_nodes;
    QList<FsEdgeItem*>         m_edges;

    // ProcFd edges
    QMap<QString, FsEdgeItem*> m_procEdges;
    QMap<pid_t,   FsNodeItem*> m_procNodes;

    // Inotify — one watch descriptor per watched directory
    int     m_inoFd  = -1;
    QMap<int, QString> m_inoWds;   // wd → absolute dir path
    QTimer* m_inoTimer = nullptr;

    QString m_workerBin;
    bool    m_useODirect   = false;
    bool    m_initialLayout = true;   // fitInView only on first layout
    QPointF m_pressPos;               // track press position for drag vs click detection

    // ── helpers ──────────────────────────────────────────────────────────────
    void        scanTree(const QString& dir, FsNodeItem* parentNode);
    FsNodeItem* nodeAt(const QPointF& scenePos) const;
    FsNodeItem* getOrAddNode(const QString& absPath);
    FsNodeItem* addNode(const QString& absPath);
    void        removeNode(const QString& absPath);
    void        addEdge(FsNodeItem* src, FsNodeItem* dst,
                        FsEdgeItem::EdgeKind kind, const QString& label = {});
    void        removeEdgesFor(FsNodeItem* n);
    void        treeLayout();
    void        refreshEdgeDangling();
    QString     uniqueName(const QString& dir, const QString& base, const QString& ext = {});
    void        addInotifyWatch(const QString& dir);
    void        pollInotify();
    void        startInotify();
    void        stopInotify();
    void        clearCanvas();

    // Place a new node as a child of parentNode in the live tree
    void        placeAsChild(FsNodeItem* parentNode, FsNodeItem* child);

    // flock demo
    void        runLockWorker(const QString& path, const QString& role, int pipeWfd);

    // Recompute containment-tree layout without losing node positions for already-placed nodes
    void        relayout();
};
