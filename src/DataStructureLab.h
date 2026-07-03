#pragma once
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QPainter>
#include <vector>
#include <utility>
#include <QString>
#include <QMap>
#include <QSet>
#include <unistd.h>

enum class DSType { Stack, Queue, LinkedList, HashMap, BST, ProcessStack };

// Real singly-linked node. Every Stack/Queue/Linked-List operation in this
// lab allocates and frees ACTUAL heap memory through these nodes — the
// hex address shown on screen is whatever new() really returned, not a
// generated label. Three different "shapes" of linkage are built from the
// same node: LIFO (stack, link at head), FIFO (queue, head+tail), and a
// general list (head, searchable, removable from the middle).
struct LLNode {
    int value;
    LLNode* next = nullptr;
    explicit LLNode(int v) : value(v) {}
};

// Real chained hash-table entry. Each bucket is a genuine linked chain of
// these — collisions are resolved by walking real ->next pointers, exactly
// like a textbook separate-chaining hash table.
struct HashEntry {
    QString key;
    QString value;
    HashEntry* next = nullptr;
    HashEntry(QString k, QString v) : key(std::move(k)), value(std::move(v)) {}
};

// Real binary search tree — actual node pointers, actual recursive
// insert/remove/search, not a drawn approximation.
struct BSTNode {
    int value;
    BSTNode* left  = nullptr;
    BSTNode* right = nullptr;
    explicit BSTNode(int v) : value(v) {}
};

// A stack frame entry used for the Process Stack view.
struct StackFrame {
    QString functionName; // from /proc/pid/wchan or symbol approximation
    unsigned long address = 0;
    int frameIndex = 0;   // 0 = top (most recent)
};

// Custom-painted visualization. Reads the real pointers directly off
// DataStructureLab rather than keeping its own copy, and renders by
// WALKING those pointers (->next, ->left, ->right) — so the picture on
// screen is a literal traversal of the live structure, addresses included.
class DSView : public QWidget {
    Q_OBJECT
public:
    explicit DSView(QWidget* parent = nullptr);

    DSType type = DSType::Stack;

    // Linear structures — real linked nodes, walked at paint time.
    LLNode* stackTop  = nullptr;
    LLNode* queueHead = nullptr;
    LLNode* queueTail = nullptr;
    LLNode* listHead  = nullptr;

    std::vector<HashEntry*>* buckets = nullptr; // one real chain per bucket
    BSTNode* bstRoot = nullptr;

    // Process Stack view data
    std::vector<StackFrame> processFrames;
    pid_t   inspectedPid = -1;

    void*       highlightNode = nullptr; // node most directly affected by the last op
    QSet<void*> visitedNodes;            // nodes touched while searching/traversing

protected:
    void paintEvent(QPaintEvent*) override;

private:
    void paintLinear(QPainter& p);
    void paintHashMap(QPainter& p);
    void paintBST(QPainter& p);
    void paintProcessStack(QPainter& p);
    int layoutBST(BSTNode* node, int depth, int& counter, QMap<BSTNode*, QPoint>& pos);
    void drawNodeBox(QPainter& p, const QRect& r, int value, const void* addr,
                      bool highlighted, bool visited);
    void drawFrameBox(QPainter& p, const QRect& r, const StackFrame& frame,
                       bool isTop);
};

class DataStructureLab : public QWidget {
    Q_OBJECT
public:
    explicit DataStructureLab(QWidget* parent = nullptr);
    ~DataStructureLab();

signals:
    void explanationNeeded(QString text);

public slots:
    // Called from MainWindow when a sandbox process is selected.
    // Switches to the Process Stack view and loads that PID's stack info.
    void loadProcessStack(pid_t pid);

private slots:
    void onStructureChanged(int index);
    void onPrimaryAction();    // push / enqueue / insert at head / put     / insert
    void onSecondaryAction();  // pop  / dequeue  / remove value  / remove key / remove
    void onSearchAction();     // peek / peek      / contains      / get        / search

private:
    QComboBox*   structureBox;
    QLineEdit*   valueInput;
    QLineEdit*   keyInput;
    QPushButton* primaryBtn;
    QPushButton* secondaryBtn;
    QPushButton* searchBtn;
    QLabel*      keyLabel;
    QLabel*      valueLabel;
    QLabel*      statusLabel;
    DSView*      view;

    // Real backing memory — see LLNode comment above.
    LLNode* stackTop  = nullptr;
    LLNode* queueHead = nullptr;
    LLNode* queueTail = nullptr;
    LLNode* listHead  = nullptr;

    static constexpr int BUCKET_COUNT = 7;
    std::vector<HashEntry*> buckets;
    BSTNode* bstRoot = nullptr;

    void updateControlsForType();
    void refreshView();
    static void freeChain(LLNode* head);
    void destroyBST(BSTNode* node);
    BSTNode* bstInsert(BSTNode* node, int value);
    BSTNode* bstRemove(BSTNode* node, int value, bool& removed);
    BSTNode* bstFind(BSTNode* node, int value, std::vector<BSTNode*>& path) const;
    int hashKey(const QString& key) const;

    // Process stack helpers
    std::vector<StackFrame> readProcessStack(pid_t pid);
};

// ── Worker-backed additions ───────────────────────────────────────────────
// These are added below the existing class to avoid breaking anything.

#include <QTableWidget>
#include <QTimer>
#include <QProgressBar>
#include <QSpinBox>
#include "DSLabDriver.h"

// A small RSS progress bar widget
class DSRSSBar : public QWidget {
    Q_OBJECT
public:
    explicit DSRSSBar(QWidget* parent=nullptr);
    void setValues(long cur, long peak);
protected:
    void paintEvent(QPaintEvent*) override;
private:
    long cur=0, peak=1;
};
