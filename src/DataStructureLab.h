#pragma once
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QLineEdit>
#include <QSpinBox>
#include <QTextEdit>
#include <QPainter>
#include <QVariant>
#include <vector>
#include <utility>
#include <QString>
#include <QMap>
#include <QSet>
#include <unistd.h>

enum class DSType { Stack, Queue, LinkedList, HashMap, BST, ProcessStack };

// Real singly-linked node.  value is QVariant so it holds int/double/string
// without casting.  The hex address shown on screen is whatever new() returned.
struct LLNode {
    QVariant value;
    LLNode*  next = nullptr;
    explicit LLNode(QVariant v) : value(std::move(v)) {}
};

// Real chained hash-table entry — collisions via real ->next pointers.
struct HashEntry {
    QVariant    key;
    QVariant    value;
    HashEntry*  next = nullptr;
    HashEntry(QVariant k, QVariant v) : key(std::move(k)), value(std::move(v)) {}
};

// Real binary search tree node.
// Comparisons use variantCompare (numeric-by-value, string-lexicographic,
// type-mismatch → error) so the BST never silently coerces mixed types.
struct BSTNode {
    QVariant value;
    BSTNode* left  = nullptr;
    BSTNode* right = nullptr;
    explicit BSTNode(QVariant v) : value(std::move(v)) {}
};

// A stack frame entry used for the Process Stack view.
struct StackFrame {
    QString functionName;
    unsigned long address = 0;
    int frameIndex = 0;
};

// Custom-painted visualization.  Reads the real pointers directly off
// DataStructureLab at paint time — no copy.
class DSView : public QWidget {
    Q_OBJECT
public:
    explicit DSView(QWidget* parent = nullptr);

    DSType type = DSType::Stack;

    LLNode* stackTop  = nullptr;
    LLNode* queueHead = nullptr;
    LLNode* queueTail = nullptr;
    LLNode* listHead  = nullptr;

    std::vector<HashEntry*>* buckets = nullptr;
    BSTNode* bstRoot = nullptr;

    std::vector<StackFrame> processFrames;
    pid_t   inspectedPid = -1;

    void*       highlightNode = nullptr;
    QSet<void*> visitedNodes;

protected:
    void paintEvent(QPaintEvent*) override;

private:
    void paintLinear(QPainter& p);
    void paintHashMap(QPainter& p);
    void paintBST(QPainter& p);
    void paintProcessStack(QPainter& p);
    int layoutBST(BSTNode* node, int depth, int& counter, QMap<BSTNode*, QPoint>& pos);
    void drawNodeBox(QPainter& p, const QRect& r, const QString& text, const void* addr,
                      bool highlighted, bool visited);
    void drawFrameBox(QPainter& p, const QRect& r, const StackFrame& frame, bool isTop);
};

// ── DSTinyVM ──────────────────────────────────────────────────────────────
// A tiny recursive-descent interpreter that runs the traversal scripts
// the student writes in the editor.  It operates directly on the real
// LLNode / BSTNode pointers — no FFI, no type coercion, no external engine.
//
// Script vocabulary:
//   var x = <expr>            variable declaration / assignment
//   <expr>.<prop>             .value .address .next .left .right
//   if (<cond>) { } else { }
//   while (<cond>) { }
//   function f(a,b) { }  + recursive calls
//   return <expr>
//   log(<expr>)               append to output log
//   visited(<node>)           mark node orange in DSView
//   highlight(<node>)         mark node blue in DSView
//   clearVisited()
//   null  true  false
//   ==  !=  <  >  <=  >=  &&  ||  !
//   +  -  *  /  (string concat with +)
//
// head  — resolves to stackTop / queueHead / listHead depending on active type
// root  — resolves to bstRoot
#include <QObject>
#include <functional>
#include <unordered_map>
#include <memory>

class DSView;

class DSTinyVM {
public:
    // Output / interaction callbacks set by DataStructureLab before run().
    std::function<void(const QString&)> onLog;
    std::function<void(void*)>          onVisited;
    std::function<void(void*)>          onHighlight;
    std::function<void()>               onClearVisited;

    // Pointer values for the two entry-point identifiers.
    void* headPtr = nullptr;   // stackTop / queueHead / listHead
    void* rootPtr = nullptr;   // bstRoot

    // Run src.  Returns error string or empty string on success.
    // Caps at MAX_STEPS to prevent infinite loops from freezing the GUI.
    QString run(const QString& src);

private:
    // ── Value type ──────────────────────────────────────────────────────
    enum class VT { Null, Number, String, Bool, LLPtr, BSTPtr, HMPtr };
    struct Val {
        VT      type  = VT::Null;
        double  d_num = 0;
        bool    d_flag = false;
        QString d_str;
        void*   d_ptr = nullptr;  // raw node pointer; interpreted per VT

        static Val null()               { return {}; }
        static Val num(double v)        { Val r; r.type=VT::Number; r.d_num=v; return r; }
        static Val str(const QString& s){ Val r; r.type=VT::String; r.d_str=s; return r; }
        static Val boolean(bool b)      { Val r; r.type=VT::Bool;   r.d_flag=b; return r; }
        static Val llptr(void* p)       { Val r; r.type=VT::LLPtr;  r.d_ptr=p; return r; }
        static Val bstptr(void* p)      { Val r; r.type=VT::BSTPtr; r.d_ptr=p; return r; }
        static Val hmptr(void* p)       { Val r; r.type=VT::HMPtr;  r.d_ptr=p; return r; }

        bool  isTruthy() const;
        QString toString() const;
        bool operator==(const Val& o) const;
        bool operator!=(const Val& o) const { return !(*this==o); }
    };

    // ── AST ──────────────────────────────────────────────────────────────
    struct Node;
    using NodePtr = std::shared_ptr<Node>;
    enum class NK {
        Num, Str, Bool, Null, Ident,
        Unary, Binary, Assign,
        PropAccess, Call,
        VarDecl, Block, If, While, Return, FuncDecl, ExprStmt
    };
    struct Node {
        NK kind;
        QString       sval;
        double        nval  = 0;
        bool          bval  = false;
        std::vector<NodePtr> children;  // generic child list
        std::vector<QString> params;    // for FuncDecl
        explicit Node(NK k) : kind(k) {}
    };

    // ── Lexer ─────────────────────────────────────────────────────────────
    enum class TK {
        Num, Str, Ident, Var, Function, Return, If, Else, While, Null, True, False,
        Plus, Minus, Star, Slash,
        Eq, NotEq, Lt, LtEq, Gt, GtEq, And, Or, Not, Assign,
        Dot, Comma, Semi, LParen, RParen, LBrace, RBrace,
        Eof, Bad
    };
    struct Token { TK kind; QString val; double num=0; };

    QStringList m_lines;
    int         m_pos    = 0;     // character position in joined source
    QString     m_src;
    std::vector<Token> m_tokens;
    int         m_tok    = 0;     // current token index

    void     tokenize();
    Token&   cur();
    Token&   peek(int offset=1);
    Token    consume();
    Token    expect(TK kind, const char* what);

    // ── Parser ────────────────────────────────────────────────────────────
    NodePtr parseProgram();
    NodePtr parseStmt();
    NodePtr parseBlock();
    NodePtr parseFuncDecl();
    NodePtr parseVarDecl();
    NodePtr parseIf();
    NodePtr parseWhile();
    NodePtr parseReturn();
    NodePtr parseExprStmt();
    NodePtr parseExpr();
    NodePtr parseAssign();
    NodePtr parseOr();
    NodePtr parseAnd();
    NodePtr parseEq();
    NodePtr parseCmp();
    NodePtr parseAdd();
    NodePtr parseMul();
    NodePtr parseUnary();
    NodePtr parsePostfix();
    NodePtr parsePrimary();

    // ── Evaluator ─────────────────────────────────────────────────────────
    struct Env {
        std::unordered_map<std::string, Val>       vars;
        std::unordered_map<std::string, NodePtr>   funcs;
        Env* parent = nullptr;
        Val* lookup(const std::string& name);
        void set(const std::string& name, const Val& v);
        void defFunc(const std::string& name, NodePtr fn);
        NodePtr getFunc(const std::string& name);
    };

    struct ReturnException { Val val; };

    long m_steps = 0;
    static constexpr long MAX_STEPS = 100'000;

    Val  evalNode(NodePtr n, Env& env);
    Val  evalBlock(NodePtr n, Env& env);
    Val  callBuiltin(const QString& name, const std::vector<Val>& args);
    Val  callFunc(NodePtr fn, const std::vector<Val>& args, Env& env);
    Val  getProp(const Val& obj, const QString& prop);
    bool lt(const Val& a, const Val& b);
    bool eq(const Val& a, const Val& b);

    QString m_error;
};

// ── DataStructureLab ──────────────────────────────────────────────────────

class DataStructureLab : public QWidget {
    Q_OBJECT
public:
    explicit DataStructureLab(QWidget* parent = nullptr);
    ~DataStructureLab();

signals:
    void explanationNeeded(QString text);

public slots:
    void loadProcessStack(pid_t pid);

private slots:
    void onStructureChanged(int index);
    void onPrimaryAction();
    void onSecondaryAction();
    void onSearchAction();
    void onLoadMapsClicked();
    void onRunScript();

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

    QTextEdit*   scriptEdit;
    QComboBox*   scriptLibBox;
    QTextEdit*   scriptLog;
    QPushButton* runScriptBtn;

    LLNode* stackTop  = nullptr;
    LLNode* queueHead = nullptr;
    LLNode* queueTail = nullptr;
    LLNode* listHead  = nullptr;

    static constexpr int BUCKET_COUNT = 7;
    std::vector<HashEntry*> buckets;
    BSTNode* bstRoot = nullptr;

    void updateControlsForType();
    void refreshView();
    void populateScriptLibrary();
    static void freeChain(LLNode* head);
    void destroyBST(BSTNode* node);
    static int variantCompare(const QVariant& a, const QVariant& b, bool& err);
    BSTNode* bstInsert(BSTNode* node, const QVariant& value);
    BSTNode* bstRemove(BSTNode* node, const QVariant& value, bool& removed);
    BSTNode* bstFind(BSTNode* node, const QVariant& value, std::vector<BSTNode*>& path) const;
    int hashKey(const QVariant& key) const;

    pid_t lastSandboxPid = -1;
    std::vector<StackFrame> readProcessStack(pid_t pid);
};

// ── Worker-backed additions ───────────────────────────────────────────────
#include <QTableWidget>
#include <QTimer>
#include <QProgressBar>
#include <QSpinBox>
#include "DSLabDriver.h"

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
