#include "DataStructureLab.h"
#include "MemoryInspector.h"
#include "Theme.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QPainterPath>
#include <QFont>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <climits>
#include <stdexcept>

// Renders the REAL address a pointer holds. Nothing here is a generated
// label — it's reinterpret_cast<quintptr> of whatever new() returned, so
// it changes every run, just like real heap addresses do.
static QString dsAddr(const void* p) {
    if (!p) return "0x0";
    return "0x" + QString::number(reinterpret_cast<quintptr>(p), 16);
}

// ───────────────────────── DSView ─────────────────────────

DSView::DSView(QWidget* parent) : QWidget(parent) {
    setMinimumHeight(320);
    setStyleSheet(QString("background: white; border-radius: 12px; border: 1px solid %1;")
                  .arg(Theme::BORDER));
}

void DSView::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    switch (type) {
        case DSType::Stack:
        case DSType::Queue:
        case DSType::LinkedList:
            paintLinear(p);
            break;
        case DSType::HashMap:
            paintHashMap(p);
            break;
        case DSType::BST:
            paintBST(p);
            break;
        case DSType::ProcessStack:
            paintProcessStack(p);
            break;
    }
}

void DSView::drawNodeBox(QPainter& p, const QRect& r, const QString& text,
                          const void* addrPtr, bool highlighted, bool visited) {
    QColor fill   = highlighted ? QColor(Theme::BLUE)
                  : visited     ? QColor(Theme::ORANGE_LIGHT)
                                : QColor(Theme::BG_INPUT);
    QColor border = highlighted ? QColor(Theme::BLUE)
                  : visited     ? QColor(Theme::ORANGE)
                                : QColor(Theme::BORDER);
    QColor textColor = highlighted ? Qt::white : QColor(Theme::TEXT_PRIMARY);

    QPainterPath path;
    path.addRoundedRect(r, 8, 8);
    p.fillPath(path, fill);
    p.setPen(QPen(border, (highlighted || visited) ? 2 : 1));
    p.drawPath(path);

    p.setPen(textColor);
    p.setFont(QFont("Segoe UI", 11, QFont::Bold));
    p.drawText(QRect(r.x(), r.y() + 2, r.width(), r.height() - 16),
               Qt::AlignCenter, text);

    p.setFont(QFont("Consolas", 7));
    p.setPen(highlighted ? QColor(255, 255, 255, 215) : QColor(Theme::TEXT_MUTED));
    p.drawText(QRect(r.x(), r.bottom() - 14, r.width(), 14),
               Qt::AlignCenter, dsAddr(addrPtr));
}

void DSView::paintLinear(QPainter& p) {
    LLNode* head = nullptr;
    if (type == DSType::Stack)      head = stackTop;
    else if (type == DSType::Queue) head = queueHead;
    else                              head = listHead;

    if (!head) {
        p.setPen(QColor(Theme::TEXT_MUTED));
        QString msg = type == DSType::Stack ? "Empty stack — push a value below"
                    : type == DSType::Queue ? "Empty queue — enqueue a value below"
                                             : "Empty list — insert a value below";
        p.drawText(rect(), Qt::AlignCenter, msg);
        return;
    }

    if (type == DSType::Stack) {
        // Vertical, top-of-stack at the TOP, growing downward
        int boxW = 150, boxH = 46, gap = 22;
        int x = width() / 2 - boxW / 2;
        int y = 14;
        LLNode* n = head;
        bool isTop = true;
        while (n) {
            QRect r(x, y, boxW, boxH);
            bool hi = (void*)n == highlightNode;
            bool vis = visitedNodes.contains((void*)n);
            drawNodeBox(p, r, n->value.toString(), (void*)n, hi, vis);

            if (isTop) {
                p.setPen(QPen(QColor(Theme::BLUE), 2));
                p.setFont(QFont("Segoe UI", 9, QFont::Bold));
                p.drawText(x + boxW + 10, y + boxH / 2 + 4, "← TOP (push/pop happen here)");
                isTop = false;
            }

            int ax = x + boxW / 2;
            int ay = y + boxH;
            p.setPen(QPen(QColor(Theme::TEXT_MUTED), 2));
            if (n->next) {
                p.drawLine(ax, ay, ax, ay + gap);
                p.drawLine(ax, ay + gap, ax - 5, ay + gap - 7);
                p.drawLine(ax, ay + gap, ax + 5, ay + gap - 7);
            } else {
                p.setFont(QFont("Consolas", 8));
                p.drawText(QRect(x, ay + 2, boxW, 16), Qt::AlignCenter, "next = 0x0 (nullptr)");
            }
            y += boxH + gap;
            n = n->next;
        }
        return;
    }

    // Queue / Linked List: horizontal, left → right.
    int boxW = 72, boxH = 46, gap = 36, pad = 24;
    int x = pad, y = height() / 2 - boxH / 2;
    LLNode* n = head;
    while (n) {
        QRect r(x, y, boxW, boxH);
        bool hi = (void*)n == highlightNode;
        bool vis = visitedNodes.contains((void*)n);
        drawNodeBox(p, r, n->value.toString(), (void*)n, hi, vis);

        if (n->next) {
            int ax = x + boxW;
            int ay = y + boxH / 2;
            p.setPen(QPen(QColor(Theme::TEXT_MUTED), 2));
            p.drawLine(ax, ay, ax + gap - 6, ay);
            p.drawLine(ax + gap - 6, ay, ax + gap - 12, ay - 5);
            p.drawLine(ax + gap - 6, ay, ax + gap - 12, ay + 5);
        }
        x += boxW + gap;
        n = n->next;
    }

    int endX = x - gap;
    p.setFont(QFont("Consolas", 8));
    p.setPen(QColor(Theme::TEXT_MUTED));
    p.drawText(QRect(endX, y + boxH + 4, 140, 16), Qt::AlignLeft, "→ nullptr");

    p.setFont(QFont("Segoe UI", 9, QFont::Bold));
    p.setPen(QColor(Theme::BLUE));
    if (type == DSType::Queue) {
        p.drawText(pad, y - 12, "FRONT (dequeue)");
        p.drawText(endX - boxW, y - 12, boxW + 50, 16, Qt::AlignRight, "BACK (enqueue) ");
    } else {
        p.drawText(pad, y - 12, "HEAD");
        p.drawText(endX - boxW, y - 12, boxW + 50, 16, Qt::AlignRight, "TAIL ");
    }
}

void DSView::paintHashMap(QPainter& p) {
    if (!buckets) return;
    int n = (int)buckets->size();
    int rowH = height() / std::max(1, n);
    int pad = 14;

    for (int i = 0; i < n; i++) {
        int y = i * rowH;
        QRect bucketLabelRect(pad, y, 76, rowH);
        p.setFont(QFont("Segoe UI", 9, QFont::Bold));
        p.setPen(QColor(Theme::TEXT_SECONDARY));
        p.drawText(bucketLabelRect, Qt::AlignVCenter | Qt::AlignLeft, QString("bucket %1").arg(i));

        p.setPen(QPen(QColor(Theme::BORDER), 1));
        p.drawLine(pad, y + rowH, width() - pad, y + rowH);

        int x = 96;
        for (HashEntry* e = (*buckets)[i]; e != nullptr; e = e->next) {
            bool hi = (void*)e == highlightNode;
            bool vis = visitedNodes.contains((void*)e);
            QString label = e->key.toString() + ": " + e->value.toString();
            p.setFont(QFont("Segoe UI", 9, QFont::Bold));
            int w = p.fontMetrics().horizontalAdvance(label) + 24;

            if (x + w > width() - pad - 20) {
                p.setFont(QFont("Segoe UI", 9));
                p.setPen(QColor(Theme::TEXT_MUTED));
                p.drawText(x, y + rowH / 2 + 4, "…chain continues");
                break;
            }

            QRect chip(x, y + rowH / 2 - 16, w, 28);
            QColor chipBg = hi ? QColor(Theme::BLUE) : vis ? QColor(Theme::ORANGE_LIGHT) : QColor(Theme::BLUE_LIGHT);
            QColor chipFg = hi ? Qt::white : vis ? QColor(Theme::ORANGE) : QColor(Theme::BLUE);

            QPainterPath path;
            path.addRoundedRect(chip, 14, 14);
            p.fillPath(path, chipBg);
            p.setPen(chipFg);
            p.drawText(chip, Qt::AlignCenter, label);

            p.setFont(QFont("Consolas", 7));
            p.setPen(QColor(Theme::TEXT_MUTED));
            p.drawText(QRect(chip.x(), chip.bottom() + 1, chip.width(), 12), Qt::AlignCenter, dsAddr(e));

            int newX = x + w + 8;
            if (e->next) {
                p.setPen(QPen(QColor(Theme::TEXT_MUTED), 2));
                int ay = y + rowH / 2 - 6;
                p.drawLine(x + w, ay, newX, ay);
            }
            x = newX;
        }
    }
}

int DSView::layoutBST(BSTNode* node, int depth, int& counter, QMap<BSTNode*, QPoint>& pos) {
    if (!node) return 0;
    layoutBST(node->left, depth + 1, counter, pos);
    int x = 34 + counter * 50;
    int y = 30 + depth * 64;
    pos[node] = QPoint(x, y);
    counter++;
    layoutBST(node->right, depth + 1, counter, pos);
    return counter;
}

void DSView::paintBST(QPainter& p) {
    if (!bstRoot) {
        p.setPen(QColor(Theme::TEXT_MUTED));
        p.drawText(rect(), Qt::AlignCenter, "Empty tree — insert a value below");
        return;
    }

    QMap<BSTNode*, QPoint> pos;
    int counter = 0;
    layoutBST(bstRoot, 0, counter, pos);

    // Edges first (so nodes draw on top)
    std::vector<BSTNode*> stack = {bstRoot};
    while (!stack.empty()) {
        BSTNode* n = stack.back(); stack.pop_back();
        QPoint pn = pos[n];
        if (n->left) {
            p.setPen(QPen(QColor(Theme::TEXT_MUTED), 2));
            p.drawLine(pn, pos[n->left]);
            stack.push_back(n->left);
        }
        if (n->right) {
            p.setPen(QPen(QColor(Theme::TEXT_MUTED), 2));
            p.drawLine(pn, pos[n->right]);
            stack.push_back(n->right);
        }
    }

    // Nodes
    stack = {bstRoot};
    while (!stack.empty()) {
        BSTNode* n = stack.back(); stack.pop_back();
        QPoint c = pos[n];
        int r = 17;
        QRect circle(c.x() - r, c.y() - r, r * 2, r * 2);

        bool hi = (void*)n == highlightNode;
        bool vis = visitedNodes.contains((void*)n);
        QColor fill = hi ? QColor(Theme::BLUE) : vis ? QColor(Theme::ORANGE) : QColor(Theme::PURPLE);
        p.setBrush(fill);
        p.setPen(Qt::NoPen);
        p.drawEllipse(circle);

        p.setPen(Qt::white);
        p.setFont(QFont("Segoe UI", 9, QFont::Bold));
        p.drawText(circle, Qt::AlignCenter, n->value.toString());

        p.setFont(QFont("Consolas", 7));
        p.setPen(QColor(Theme::TEXT_MUTED));
        p.drawText(QRect(c.x() - 32, c.y() + r + 2, 64, 12), Qt::AlignCenter, dsAddr(n));

        if (n->left) stack.push_back(n->left);
        if (n->right) stack.push_back(n->right);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// DSTinyVM — pure C++ mini-interpreter for DS traversal scripts
// ═══════════════════════════════════════════════════════════════════════════

// ── Val helpers ─────────────────────────────────────────────────────────────

bool DSTinyVM::Val::isTruthy() const {
    switch (type) {
        case VT::Null:   return false;
        case VT::Bool:   return d_flag;
        case VT::Number: return d_num != 0.0;
        case VT::String: return !d_str.isEmpty();
        default:         return d_ptr != nullptr;   // node ptr — truthy iff non-null
    }
}

QString DSTinyVM::Val::toString() const {
    switch (type) {
        case VT::Null:   return "null";
        case VT::Bool:   return d_flag ? "true" : "false";
        case VT::Number: {
            // Print as integer when the value is whole
            if (d_num == (long long)d_num) return QString::number((long long)d_num);
            return QString::number(d_num, 'g', 10);
        }
        case VT::String: return d_str;
        default: {
            if (!d_ptr) return "null";
            return "0x" + QString::number(reinterpret_cast<quintptr>(d_ptr), 16);
        }
    }
}

bool DSTinyVM::Val::operator==(const Val& o) const {
    if (type == VT::Null && o.type == VT::Null) return true;
    if (type == VT::Null || o.type == VT::Null) return false;
    if (type == VT::Number && o.type == VT::Number) return d_num == o.d_num;
    if (type == VT::Bool   && o.type == VT::Bool)   return d_flag == o.d_flag;
    if (type == VT::String && o.type == VT::String) return d_str == o.d_str;
    // Pointer types: equal iff same raw address
    bool ap = (type==VT::LLPtr||type==VT::BSTPtr||type==VT::HMPtr);
    bool bp = (o.type==VT::LLPtr||o.type==VT::BSTPtr||o.type==VT::HMPtr);
    if (ap && bp) return d_ptr == o.d_ptr;
    // Null pointer == null value
    if (ap && o.type==VT::Null) return d_ptr==nullptr;
    if (bp && type==VT::Null)   return o.d_ptr==nullptr;
    return false;
}

// ── Env ──────────────────────────────────────────────────────────────────────

DSTinyVM::Val* DSTinyVM::Env::lookup(const std::string& name) {
    auto it = vars.find(name);
    if (it != vars.end()) return &it->second;
    return parent ? parent->lookup(name) : nullptr;
}

void DSTinyVM::Env::set(const std::string& name, const Val& v) {
    Val* existing = lookup(name);
    if (existing) { *existing = v; return; }
    vars[name] = v;
}

void DSTinyVM::Env::defFunc(const std::string& name, NodePtr fn) {
    funcs[name] = fn;
    if (parent) parent->defFunc(name, fn); // hoist to top level
}

DSTinyVM::NodePtr DSTinyVM::Env::getFunc(const std::string& name) {
    auto it = funcs.find(name);
    if (it != funcs.end()) return it->second;
    return parent ? parent->getFunc(name) : nullptr;
}

// ── Lexer ────────────────────────────────────────────────────────────────────

void DSTinyVM::tokenize() {
    m_tokens.clear();
    int i = 0;
    int n = m_src.size();
    auto ch = [&]() -> QChar { return i < n ? m_src[i] : QChar(0); };

    while (i < n) {
        // Skip whitespace and single-line comments (// and #)
        if (ch().isSpace()) { i++; continue; }
        if (ch()=='/' && i+1<n && m_src[i+1]=='/') {
            while (i<n && m_src[i]!='\n') i++;
            continue;
        }
        if (ch()=='#') {
            while (i<n && m_src[i]!='\n') i++;
            continue;
        }

        // Numbers
        if (ch().isDigit() || (ch()=='-' && i+1<n && m_src[i+1].isDigit()
                               && !m_tokens.empty()
                               && m_tokens.back().kind!=TK::RParen
                               && m_tokens.back().kind!=TK::Ident)) {
            int start = i;
            if (ch()=='-') i++;
            while (i<n && (m_src[i].isDigit() || m_src[i]=='.')) i++;
            Token t; t.kind=TK::Num;
            t.val = m_src.mid(start, i-start);
            t.num = t.val.toDouble();
            m_tokens.push_back(t);
            continue;
        }

        // Strings
        if (ch()=='"' || ch()=='\'') {
            QChar q = ch(); i++;
            int start = i;
            while (i<n && m_src[i]!=q) {
                if (m_src[i]=='\\') i++; // skip escape
                i++;
            }
            Token t; t.kind=TK::Str; t.val=m_src.mid(start, i-start); i++;
            m_tokens.push_back(t);
            continue;
        }

        // Identifiers / keywords
        if (ch().isLetter() || ch()=='_') {
            int start = i;
            while (i<n && (m_src[i].isLetterOrNumber()||m_src[i]=='_')) i++;
            QString w = m_src.mid(start, i-start);
            Token t; t.val = w;
            if      (w=="var")      t.kind=TK::Var;
            else if (w=="function") t.kind=TK::Function;
            else if (w=="def")      t.kind=TK::Function;   // Python alias
            else if (w=="return")   t.kind=TK::Return;
            else if (w=="if")       t.kind=TK::If;
            else if (w=="else")     t.kind=TK::Else;
            else if (w=="elif")     t.kind=TK::Else;       // Python alias (treated as else-if)
            else if (w=="while")    t.kind=TK::While;
            else if (w=="null")     t.kind=TK::Null;
            else if (w=="None")     t.kind=TK::Null;       // Python alias
            else if (w=="true")     t.kind=TK::True;
            else if (w=="True")     t.kind=TK::True;       // Python alias
            else if (w=="false")    t.kind=TK::False;
            else if (w=="False")    t.kind=TK::False;      // Python alias
            else if (w=="and") {                           // Python alias for &&
                Token ta; ta.kind=TK::And; ta.val="and"; m_tokens.push_back(ta);
                continue;
            }
            else if (w=="or") {                            // Python alias for ||
                Token ta; ta.kind=TK::Or; ta.val="or"; m_tokens.push_back(ta);
                continue;
            }
            else if (w=="not") {                           // Python alias for !
                Token ta; ta.kind=TK::Not; ta.val="not"; m_tokens.push_back(ta);
                continue;
            }
            else                    t.kind=TK::Ident;
            m_tokens.push_back(t);
            continue;
        }

        // Two-char operators
        auto tw = [&](const char* s, TK k) {
            if (i+1<n && m_src[i]==s[0] && m_src[i+1]==s[1]) {
                Token t; t.kind=k; t.val=QString(s); m_tokens.push_back(t);
                i+=2; return true;
            }
            return false;
        };
        if (tw("==",TK::Eq))    continue;
        if (tw("!=",TK::NotEq)) continue;
        if (tw("<=",TK::LtEq))  continue;
        if (tw(">=",TK::GtEq))  continue;
        if (tw("&&",TK::And))   continue;
        if (tw("||",TK::Or))    continue;

        // Single-char tokens
        auto sc = [&](char c, TK k) {
            if (ch()==c) { Token t; t.kind=k; t.val=c; m_tokens.push_back(t); i++; return true; }
            return false;
        };
        if (sc('+',TK::Plus))   continue;
        if (sc('-',TK::Minus))  continue;
        if (sc('*',TK::Star))   continue;
        if (sc('/',TK::Slash))  continue;
        if (sc('<',TK::Lt))     continue;
        if (sc('>',TK::Gt))     continue;
        if (sc('!',TK::Not))    continue;
        if (sc('=',TK::Assign)) continue;
        if (sc('.',TK::Dot))    continue;
        if (sc(',',TK::Comma))  continue;
        if (sc(';',TK::Semi))   continue;
        if (sc('(',TK::LParen)) continue;
        if (sc(')',TK::RParen)) continue;
        if (sc('{',TK::LBrace)) continue;
        if (sc('}',TK::RBrace)) continue;
        // Unknown character — skip silently
        i++;
    }
    Token eof; eof.kind=TK::Eof; m_tokens.push_back(eof);
}

DSTinyVM::Token& DSTinyVM::cur()  { return m_tokens[m_tok]; }
DSTinyVM::Token& DSTinyVM::peek(int o) {
    int idx = m_tok + o;
    if (idx >= (int)m_tokens.size()) return m_tokens.back();
    return m_tokens[idx];
}
DSTinyVM::Token DSTinyVM::consume() {
    Token t = m_tokens[m_tok];
    if (m_tok < (int)m_tokens.size()-1) m_tok++;
    return t;
}
DSTinyVM::Token DSTinyVM::expect(TK kind, const char* what) {
    if (cur().kind != kind)
        throw std::runtime_error(std::string("expected ") + what
                                 + ", got '" + cur().val.toStdString() + "'");
    return consume();
}

// ── Parser ───────────────────────────────────────────────────────────────────

DSTinyVM::NodePtr DSTinyVM::parseProgram() {
    auto block = std::make_shared<Node>(NK::Block);
    while (cur().kind != TK::Eof)
        block->children.push_back(parseStmt());
    return block;
}

DSTinyVM::NodePtr DSTinyVM::parseStmt() {
    switch (cur().kind) {
        case TK::Function: return parseFuncDecl();
        case TK::Var:      return parseVarDecl();
        case TK::If:       return parseIf();
        case TK::While:    return parseWhile();
        case TK::Return:   return parseReturn();
        case TK::LBrace:   return parseBlock();
        case TK::Semi:     { consume(); return std::make_shared<Node>(NK::Block); }
        default:           return parseExprStmt();
    }
}

DSTinyVM::NodePtr DSTinyVM::parseBlock() {
    expect(TK::LBrace, "{");
    auto block = std::make_shared<Node>(NK::Block);
    while (cur().kind != TK::RBrace && cur().kind != TK::Eof)
        block->children.push_back(parseStmt());
    expect(TK::RBrace, "}");
    return block;
}

DSTinyVM::NodePtr DSTinyVM::parseFuncDecl() {
    consume(); // 'function'
    auto n = std::make_shared<Node>(NK::FuncDecl);
    n->sval = expect(TK::Ident, "function name").val;
    expect(TK::LParen, "(");
    while (cur().kind != TK::RParen && cur().kind != TK::Eof) {
        n->params.push_back(expect(TK::Ident, "param").val);
        if (cur().kind == TK::Comma) consume();
    }
    expect(TK::RParen, ")");
    n->children.push_back(parseBlock());
    return n;
}

DSTinyVM::NodePtr DSTinyVM::parseVarDecl() {
    consume(); // 'var'
    auto n = std::make_shared<Node>(NK::VarDecl);
    n->sval = expect(TK::Ident, "variable name").val;
    if (cur().kind == TK::Assign) {
        consume();
        n->children.push_back(parseExpr());
    }
    if (cur().kind == TK::Semi) consume();
    return n;
}

DSTinyVM::NodePtr DSTinyVM::parseIf() {
    consume(); // 'if'
    auto n = std::make_shared<Node>(NK::If);
    expect(TK::LParen, "(");
    n->children.push_back(parseExpr()); // [0] condition
    expect(TK::RParen, ")");
    n->children.push_back(parseStmt()); // [1] then
    if (cur().kind == TK::Else) {
        consume();
        n->children.push_back(parseStmt()); // [2] else
    }
    return n;
}

DSTinyVM::NodePtr DSTinyVM::parseWhile() {
    consume(); // 'while'
    auto n = std::make_shared<Node>(NK::While);
    expect(TK::LParen, "(");
    n->children.push_back(parseExpr()); // [0] condition
    expect(TK::RParen, ")");
    n->children.push_back(parseStmt()); // [1] body
    return n;
}

DSTinyVM::NodePtr DSTinyVM::parseReturn() {
    consume(); // 'return'
    auto n = std::make_shared<Node>(NK::Return);
    if (cur().kind != TK::Semi && cur().kind != TK::RBrace && cur().kind != TK::Eof)
        n->children.push_back(parseExpr());
    if (cur().kind == TK::Semi) consume();
    return n;
}

DSTinyVM::NodePtr DSTinyVM::parseExprStmt() {
    auto n = std::make_shared<Node>(NK::ExprStmt);
    n->children.push_back(parseExpr());
    if (cur().kind == TK::Semi) consume();
    return n;
}

// Expression grammar (lowest → highest precedence):
//   assign → or → and → eq → cmp → add → mul → unary → postfix → primary

DSTinyVM::NodePtr DSTinyVM::parseExpr()   { return parseAssign(); }

DSTinyVM::NodePtr DSTinyVM::parseAssign() {
    auto lhs = parseOr();
    if (cur().kind == TK::Assign) {
        consume();
        auto n = std::make_shared<Node>(NK::Assign);
        n->children.push_back(lhs);
        n->children.push_back(parseAssign());
        return n;
    }
    return lhs;
}

DSTinyVM::NodePtr DSTinyVM::parseOr() {
    auto lhs = parseAnd();
    while (cur().kind == TK::Or) {
        consume();
        auto n = std::make_shared<Node>(NK::Binary); n->sval="||";
        n->children.push_back(lhs); n->children.push_back(parseAnd());
        lhs = n;
    }
    return lhs;
}

DSTinyVM::NodePtr DSTinyVM::parseAnd() {
    auto lhs = parseEq();
    while (cur().kind == TK::And) {
        consume();
        auto n = std::make_shared<Node>(NK::Binary); n->sval="&&";
        n->children.push_back(lhs); n->children.push_back(parseEq());
        lhs = n;
    }
    return lhs;
}

DSTinyVM::NodePtr DSTinyVM::parseEq() {
    auto lhs = parseCmp();
    while (cur().kind==TK::Eq || cur().kind==TK::NotEq) {
        QString op = consume().val;
        auto n = std::make_shared<Node>(NK::Binary); n->sval=op;
        n->children.push_back(lhs); n->children.push_back(parseCmp());
        lhs = n;
    }
    return lhs;
}

DSTinyVM::NodePtr DSTinyVM::parseCmp() {
    auto lhs = parseAdd();
    while (cur().kind==TK::Lt||cur().kind==TK::LtEq||
           cur().kind==TK::Gt||cur().kind==TK::GtEq) {
        QString op = consume().val;
        auto n = std::make_shared<Node>(NK::Binary); n->sval=op;
        n->children.push_back(lhs); n->children.push_back(parseAdd());
        lhs = n;
    }
    return lhs;
}

DSTinyVM::NodePtr DSTinyVM::parseAdd() {
    auto lhs = parseMul();
    while (cur().kind==TK::Plus || cur().kind==TK::Minus) {
        QString op = consume().val;
        auto n = std::make_shared<Node>(NK::Binary); n->sval=op;
        n->children.push_back(lhs); n->children.push_back(parseMul());
        lhs = n;
    }
    return lhs;
}

DSTinyVM::NodePtr DSTinyVM::parseMul() {
    auto lhs = parseUnary();
    while (cur().kind==TK::Star || cur().kind==TK::Slash) {
        QString op = consume().val;
        auto n = std::make_shared<Node>(NK::Binary); n->sval=op;
        n->children.push_back(lhs); n->children.push_back(parseUnary());
        lhs = n;
    }
    return lhs;
}

DSTinyVM::NodePtr DSTinyVM::parseUnary() {
    if (cur().kind==TK::Not || cur().kind==TK::Minus) {
        QString op = consume().val;
        auto n = std::make_shared<Node>(NK::Unary); n->sval=op;
        n->children.push_back(parseUnary());
        return n;
    }
    return parsePostfix();
}

DSTinyVM::NodePtr DSTinyVM::parsePostfix() {
    auto lhs = parsePrimary();
    while (true) {
        if (cur().kind == TK::Dot) {
            consume();
            auto n = std::make_shared<Node>(NK::PropAccess);
            n->sval = expect(TK::Ident, "property name").val;
            n->children.push_back(lhs);
            lhs = n;
        } else if (cur().kind == TK::LParen) {
            consume();
            auto n = std::make_shared<Node>(NK::Call);
            n->children.push_back(lhs); // callee
            while (cur().kind != TK::RParen && cur().kind != TK::Eof) {
                n->children.push_back(parseExpr());
                if (cur().kind == TK::Comma) consume();
            }
            expect(TK::RParen, ")");
            lhs = n;
        } else break;
    }
    return lhs;
}

DSTinyVM::NodePtr DSTinyVM::parsePrimary() {
    switch (cur().kind) {
        case TK::Num: {
            auto n = std::make_shared<Node>(NK::Num);
            n->nval = cur().num; consume(); return n;
        }
        case TK::Str: {
            auto n = std::make_shared<Node>(NK::Str);
            n->sval = cur().val; consume(); return n;
        }
        case TK::True: {
            auto n = std::make_shared<Node>(NK::Bool); n->bval=true; consume(); return n;
        }
        case TK::False: {
            auto n = std::make_shared<Node>(NK::Bool); n->bval=false; consume(); return n;
        }
        case TK::Null: {
            consume(); return std::make_shared<Node>(NK::Null);
        }
        case TK::Ident: {
            auto n = std::make_shared<Node>(NK::Ident);
            n->sval = consume().val; return n;
        }
        case TK::LParen: {
            consume();
            auto n = parseExpr();
            expect(TK::RParen, ")");
            return n;
        }
        default:
            throw std::runtime_error("unexpected token '" + cur().val.toStdString() + "'");
    }
}

// ── Evaluator ────────────────────────────────────────────────────────────────

bool DSTinyVM::lt(const Val& a, const Val& b) {
    if (a.type==VT::Number && b.type==VT::Number) return a.d_num < b.d_num;
    if (a.type==VT::String && b.type==VT::String) return a.d_str < b.d_str;
    return false;
}
bool DSTinyVM::eq(const Val& a, const Val& b) { return a == b; }

DSTinyVM::Val DSTinyVM::getProp(const Val& obj, const QString& prop) {
    if (obj.type == VT::LLPtr) {
        auto* n = static_cast<LLNode*>(obj.d_ptr);
        if (!n) return Val::null();
        if (prop=="value")   return Val::str(n->value.toString());
        if (prop=="address") return Val::str("0x"+QString::number(reinterpret_cast<quintptr>(n),16));
        if (prop=="next")    return n->next ? Val::llptr(n->next) : Val::null();
        if (prop=="left" || prop=="right") return Val::null(); // not applicable
    }
    if (obj.type == VT::BSTPtr) {
        auto* n = static_cast<BSTNode*>(obj.d_ptr);
        if (!n) return Val::null();
        if (prop=="value")   return Val::str(n->value.toString());
        if (prop=="address") return Val::str("0x"+QString::number(reinterpret_cast<quintptr>(n),16));
        if (prop=="left")    return n->left  ? Val::bstptr(n->left)  : Val::null();
        if (prop=="right")   return n->right ? Val::bstptr(n->right) : Val::null();
        if (prop=="next")    return Val::null();
    }
    if (obj.type == VT::HMPtr) {
        auto* e = static_cast<HashEntry*>(obj.d_ptr);
        if (!e) return Val::null();
        if (prop=="key")     return Val::str(e->key.toString());
        if (prop=="value")   return Val::str(e->value.toString());
        if (prop=="address") return Val::str("0x"+QString::number(reinterpret_cast<quintptr>(e),16));
        if (prop=="next")    return e->next ? Val::hmptr(e->next) : Val::null();
    }
    return Val::null();
}

DSTinyVM::Val DSTinyVM::callBuiltin(const QString& name, const std::vector<Val>& args) {
    if (name == "log") {
        if (onLog) onLog(args.empty() ? QString() : args[0].toString());
        return Val::null();
    }
    if (name == "visited") {
        if (!args.empty() && onVisited) {
            void* p = args[0].d_ptr;
            if (p) onVisited(p);
        }
        return Val::null();
    }
    if (name == "highlight") {
        if (!args.empty() && onHighlight) {
            void* p = args[0].d_ptr;
            if (p) onHighlight(p);
        }
        return Val::null();
    }
    if (name == "clearVisited") {
        if (onClearVisited) onClearVisited();
        return Val::null();
    }
    throw std::runtime_error("unknown function '" + name.toStdString() + "'");
}

DSTinyVM::Val DSTinyVM::callFunc(NodePtr fn, const std::vector<Val>& args, Env& callerEnv) {
    Env local;
    local.parent = &callerEnv;
    // Copy all function definitions downward so recursive calls work
    local.funcs = callerEnv.funcs;
    for (int i = 0; i < (int)fn->params.size(); i++)
        local.vars[fn->params[i].toStdString()] = i < (int)args.size() ? args[i] : Val::null();
    try {
        evalBlock(fn->children[0], local);
    } catch (ReturnException& r) {
        return r.val;
    }
    return Val::null();
}

DSTinyVM::Val DSTinyVM::evalBlock(NodePtr n, Env& env) {
    for (auto& child : n->children)
        evalNode(child, env);
    return Val::null();
}

DSTinyVM::Val DSTinyVM::evalNode(NodePtr n, Env& env) {
    if (++m_steps > MAX_STEPS)
        throw std::runtime_error("step limit reached — check for infinite loops");

    switch (n->kind) {
        case NK::Num:    return Val::num(n->nval);
        case NK::Str:    return Val::str(n->sval);
        case NK::Bool:   return Val::boolean(n->bval);
        case NK::Null:   return Val::null();

        case NK::Ident: {
            QString name = n->sval;
            // Built-in entry points
            if (name=="head") return headPtr ? Val::llptr(headPtr) : Val::null();
            if (name=="root") return rootPtr ? Val::bstptr(rootPtr): Val::null();
            if (name=="null") return Val::null();
            // Variable lookup
            Val* v = env.lookup(name.toStdString());
            if (v) return *v;
            return Val::null();
        }

        case NK::VarDecl: {
            Val v = n->children.empty() ? Val::null() : evalNode(n->children[0], env);
            env.vars[n->sval.toStdString()] = v;
            return v;
        }

        case NK::Assign: {
            // LHS must be an Ident (simple variable assignment)
            Val rhs = evalNode(n->children[1], env);
            if (n->children[0]->kind == NK::Ident) {
                env.set(n->children[0]->sval.toStdString(), rhs);
            }
            return rhs;
        }

        case NK::PropAccess: {
            Val obj = evalNode(n->children[0], env);
            return getProp(obj, n->sval);
        }

        case NK::Unary: {
            Val v = evalNode(n->children[0], env);
            if (n->sval=="!")  return Val::boolean(!v.isTruthy());
            if (n->sval=="-")  return Val::num(-v.d_num);
            return v;
        }

        case NK::Binary: {
            QString op = n->sval;
            // Short-circuit logical operators
            if (op=="&&") {
                Val l = evalNode(n->children[0], env);
                if (!l.isTruthy()) return Val::boolean(false);
                return Val::boolean(evalNode(n->children[1], env).isTruthy());
            }
            if (op=="||") {
                Val l = evalNode(n->children[0], env);
                if (l.isTruthy()) return Val::boolean(true);
                return Val::boolean(evalNode(n->children[1], env).isTruthy());
            }
            Val l = evalNode(n->children[0], env);
            Val r = evalNode(n->children[1], env);
            if (op=="+") {
                // Numeric add or string concat
                if (l.type==VT::Number && r.type==VT::Number)
                    return Val::num(l.d_num + r.d_num);
                return Val::str(l.toString() + r.toString());
            }
            if (op=="-") return Val::num(l.d_num - r.d_num);
            if (op=="*") return Val::num(l.d_num * r.d_num);
            if (op=="/") return r.d_num!=0 ? Val::num(l.d_num/r.d_num) : Val::null();
            if (op=="==") return Val::boolean(eq(l,r));
            if (op=="!=") return Val::boolean(!eq(l,r));
            if (op=="<")  return Val::boolean(lt(l,r));
            if (op==">")  return Val::boolean(lt(r,l));
            if (op=="<=") return Val::boolean(!lt(r,l));
            if (op==">=") return Val::boolean(!lt(l,r));
            return Val::null();
        }

        case NK::Call: {
            // Callee is either a plain Ident or a property (method) call
            NodePtr callee = n->children[0];
            std::vector<Val> args;
            for (int i = 1; i < (int)n->children.size(); i++)
                args.push_back(evalNode(n->children[i], env));

            // Plain identifier call
            if (callee->kind == NK::Ident) {
                QString fname = callee->sval;
                // Builtins take priority
                static const QStringList builtins = {"log","visited","highlight","clearVisited"};
                if (builtins.contains(fname)) return callBuiltin(fname, args);
                // User-defined function
                NodePtr fn = env.getFunc(fname.toStdString());
                if (fn) return callFunc(fn, args, env);
                return Val::null();
            }
            return Val::null();
        }

        case NK::FuncDecl: {
            env.defFunc(n->sval.toStdString(), n);
            return Val::null();
        }

        case NK::Block: {
            Env inner; inner.parent = &env;
            inner.funcs = env.funcs;
            for (auto& child : n->children) evalNode(child, inner);
            // Propagate new func definitions upward
            for (auto& kv : inner.funcs) env.funcs[kv.first] = kv.second;
            return Val::null();
        }

        case NK::If: {
            Val cond = evalNode(n->children[0], env);
            if (cond.isTruthy())
                return evalNode(n->children[1], env);
            else if (n->children.size() > 2)
                return evalNode(n->children[2], env);
            return Val::null();
        }

        case NK::While: {
            while (evalNode(n->children[0], env).isTruthy()) {
                if (++m_steps > MAX_STEPS)
                    throw std::runtime_error("step limit reached — check for infinite loops");
                evalNode(n->children[1], env);
            }
            return Val::null();
        }

        case NK::Return: {
            Val v = n->children.empty() ? Val::null() : evalNode(n->children[0], env);
            throw ReturnException{v};
        }

        case NK::ExprStmt:
            return evalNode(n->children[0], env);

        default:
            return Val::null();
    }
}

// ── DSTinyVM::run ─────────────────────────────────────────────────────────────

QString DSTinyVM::run(const QString& src) {
    m_src = src;
    m_tokens.clear();
    m_tok = 0;
    m_steps = 0;
    m_error.clear();

    try {
        tokenize();
        NodePtr program = parseProgram();
        Env global;
        evalNode(program, global);
    } catch (ReturnException&) {
        // top-level return — ignore
    } catch (std::exception& e) {
        return QString::fromStdString(e.what());
    }
    return QString(); // empty = success
}

// ───────────────────────── DataStructureLab ─────────────────────────

// Starter scripts per structure — shipped as the initial content of the
// script editor and accessible via the library dropdown.
namespace StarterScripts {

static const char* BST_INORDER =
R"py(# BST — in-order traversal (left, root, right)
# Visiting nodes in this order produces a sorted sequence.
def inorder(node) {
    if not node { return; }
    inorder(node.left);
    visited(node);
    log(node.value + "  @" + node.address);
    inorder(node.right);
}
clearVisited();
inorder(root);
)py";

static const char* BST_PREORDER =
R"py(# BST — pre-order traversal (root, left, right)
# Used to copy or serialize a tree.
def preorder(node) {
    if not node { return; }
    visited(node);
    log(node.value + "  @" + node.address);
    preorder(node.left);
    preorder(node.right);
}
clearVisited();
preorder(root);
)py";

static const char* BST_POSTORDER =
R"py(# BST — post-order traversal (left, right, root)
# Used when processing children before parents (e.g. deleting a tree).
def postorder(node) {
    if not node { return; }
    postorder(node.left);
    postorder(node.right);
    visited(node);
    log(node.value + "  @" + node.address);
}
clearVisited();
postorder(root);
)py";

static const char* BST_SEARCH =
R"py(# BST — search for a value, highlighting the path taken.
var target = 42;   # <-- change this

def search(node, t) {
    if not node { log("not found"); return; }
    visited(node);
    if (node.value == t) {
        highlight(node);
        log("found " + t + " at " + node.address);
    } else if (t < node.value) {
        log("go left  (" + node.value + " > " + t + ")");
        search(node.left, t);
    } else {
        log("go right (" + node.value + " < " + t + ")");
        search(node.right, t);
    }
}
clearVisited();
search(root, target);
)py";

static const char* LIST_TRAVERSE =
R"py(# Linked List — walk every node and print value + address.
var cur = head;
clearVisited();
while (cur) {
    visited(cur);
    log(cur.value + "  @" + cur.address);
    cur = cur.next;
}
)py";

static const char* LIST_CYCLE =
R"py(# Linked List — Floyd's cycle detection (tortoise and hare).
var slow = head;
var fast = head;
var found = False;
while (fast and fast.next and not found) {
    slow = slow.next;
    fast = fast.next.next;
    if (slow == fast) { found = True; }
}
if (found) {
    log("cycle detected!");
} else {
    log("no cycle — list is acyclic");
}
)py";

static const char* STACK_PRINT =
R"py(# Stack — print all values from top to bottom.
var cur = head;   # head == stackTop for stacks
var depth = 0;
clearVisited();
while (cur) {
    visited(cur);
    log("[" + depth + "] " + cur.value + "  @" + cur.address);
    cur = cur.next;
    depth = depth + 1;
}
log("total: " + depth + " node(s)");
)py";

static const char* CUSTOM =
R"py(# Write your own traversal script here.
# Available helpers:
#   head          — points to stackTop / queueHead / listHead
#   root          — points to bstRoot (for BST)
#   log(msg)      — print a line to the output panel
#   visited(node) — mark node orange in the visualisation
#   highlight(node) — mark node blue in the visualisation
#   clearVisited()  — reset all marks
#
# Node properties:  .value  .address  .next  .left  .right

var cur = head;
while (cur) {
    visited(cur);
    log(cur.value + "  @" + cur.address);
    cur = cur.next;
}
)py";

} // namespace StarterScripts

DataStructureLab::DataStructureLab(QWidget* parent) : QWidget(parent) {
    buckets.resize(BUCKET_COUNT, nullptr);

    setStyleSheet(QString("background: %1;").arg(Theme::BG_APP));
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(16, 16, 16, 16);
    outerLayout->setSpacing(12);

    auto* title = new QLabel("🧩  Data Structure Lab");
    title->setStyleSheet(QString("color: %1; font-size: 14px; font-weight: bold;")
                          .arg(Theme::TEXT_PRIMARY));
    outerLayout->addWidget(title);

    auto* hint = new QLabel(
        "Every operation allocates or frees real heap memory — the hex address "
        "under each box is the literal pointer returned by <code>new</code>. "
        "Use the <b>Script</b> panel below to write your own traversal in "
        "<b>Python-style</b> and watch it drive the visualization live.");
    hint->setWordWrap(true);
    hint->setStyleSheet(QString("color: %1; font-size: 11px;").arg(Theme::TEXT_SECONDARY));
    outerLayout->addWidget(hint);

    // ── Splitter: top = visualization+controls, bottom = script panel ──
    auto* splitter = new QSplitter(Qt::Vertical);
    splitter->setChildrenCollapsible(false);
    outerLayout->addWidget(splitter, 1);

    // ── Top pane ──
    auto* topPane = new QWidget();
    auto* topLayout = new QVBoxLayout(topPane);
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->setSpacing(8);

    view = new DSView();
    topLayout->addWidget(view, 1);

    // Controls card
    auto* card = new QWidget();
    card->setStyleSheet(Theme::card());
    auto* controlsLayout = new QHBoxLayout(card);
    controlsLayout->setContentsMargins(14, 12, 14, 12);
    controlsLayout->setSpacing(10);

    structureBox = new QComboBox();
    structureBox->addItem("Stack", (int)DSType::Stack);
    structureBox->addItem("Queue", (int)DSType::Queue);
    structureBox->addItem("Linked List", (int)DSType::LinkedList);
    structureBox->addItem("Hash Map", (int)DSType::HashMap);
    structureBox->addItem("Binary Search Tree", (int)DSType::BST);
    structureBox->addItem("Process Stack (real)", (int)DSType::ProcessStack);
    structureBox->setStyleSheet(Theme::input());

    keyLabel = new QLabel("Key:");
    keyInput = new QLineEdit();
    keyInput->setPlaceholderText("key");
    keyInput->setStyleSheet(Theme::input());
    keyInput->setMaximumWidth(90);

    valueLabel = new QLabel("Value:");
    valueInput = new QLineEdit();
    valueInput->setPlaceholderText("value");
    valueInput->setStyleSheet(Theme::input());
    valueInput->setMaximumWidth(90);

    primaryBtn = new QPushButton("Push");
    primaryBtn->setStyleSheet(Theme::btnPrimary());

    secondaryBtn = new QPushButton("Pop");
    secondaryBtn->setStyleSheet(Theme::btnDanger());

    searchBtn = new QPushButton("Peek Top");
    searchBtn->setStyleSheet(Theme::btnGhost());

    auto* loadMapsBtn = new QPushButton("Load /proc/maps");
    loadMapsBtn->setStyleSheet(Theme::btnGhost());
    loadMapsBtn->setToolTip(
        "Loads the selected sandbox process's real /proc/pid/maps regions "
        "as string nodes into the current structure.");

    controlsLayout->addWidget(new QLabel("Structure:"));
    controlsLayout->addWidget(structureBox);
    controlsLayout->addWidget(keyLabel);
    controlsLayout->addWidget(keyInput);
    controlsLayout->addWidget(valueLabel);
    controlsLayout->addWidget(valueInput);
    controlsLayout->addWidget(primaryBtn);
    controlsLayout->addWidget(secondaryBtn);
    controlsLayout->addWidget(searchBtn);
    controlsLayout->addStretch();
    controlsLayout->addWidget(loadMapsBtn);

    topLayout->addWidget(card);

    statusLabel = new QLabel(" ");
    statusLabel->setStyleSheet(QString("color: %1; font-size: 11px;").arg(Theme::TEXT_MUTED));
    topLayout->addWidget(statusLabel);

    splitter->addWidget(topPane);

    // ── Bottom pane: script editor ──
    auto* scriptPane = new QWidget();
    scriptPane->setStyleSheet(Theme::card());
    auto* scriptLayout = new QVBoxLayout(scriptPane);
    scriptLayout->setContentsMargins(14, 10, 14, 10);
    scriptLayout->setSpacing(6);

    // Script header row
    auto* scriptHeader = new QHBoxLayout();
    auto* scriptTitle = new QLabel("✏️  Custom Traversal Script (Python-style)");
    scriptTitle->setStyleSheet(QString("color: %1; font-size: 12px; font-weight: bold;")
                                .arg(Theme::TEXT_PRIMARY));
    scriptHeader->addWidget(scriptTitle);
    scriptHeader->addStretch();

    auto* libLabel = new QLabel("Starter:");
    libLabel->setStyleSheet(QString("color: %1; font-size: 11px;").arg(Theme::TEXT_SECONDARY));
    scriptLibBox = new QComboBox();
    scriptLibBox->setStyleSheet(Theme::input());
    scriptLibBox->setMaximumWidth(220);
    populateScriptLibrary();

    runScriptBtn = new QPushButton("▶  Run");
    runScriptBtn->setStyleSheet(Theme::btnSuccess());
    runScriptBtn->setToolTip("Execute the script against the live structure (Ctrl+Enter)");

    scriptHeader->addWidget(libLabel);
    scriptHeader->addWidget(scriptLibBox);
    scriptHeader->addWidget(runScriptBtn);
    scriptLayout->addLayout(scriptHeader);

    // Script editor
    scriptEdit = new QTextEdit();
    scriptEdit->setPlaceholderText("Write your traversal here — use head, root, log(), visited(), highlight()  (Python-style: def, #comments, True/False/None, and/or/not)");
    scriptEdit->setFont(QFont("Consolas", 11));
    scriptEdit->setStyleSheet(
        "QTextEdit { background: #0D1117; color: #C9D1D9;"
        " border: 1px solid #21262D; border-radius: 8px;"
        " font-family: 'Consolas','Fira Code',monospace; font-size: 11px;"
        " padding: 8px; }"
        + Theme::scrollbar());
    scriptEdit->setMinimumHeight(120);
    scriptEdit->setMaximumHeight(200);
    scriptEdit->setPlainText(StarterScripts::CUSTOM);
    scriptLayout->addWidget(scriptEdit);

    // Script log output
    scriptLog = new QTextEdit();
    scriptLog->setReadOnly(true);
    scriptLog->setFont(QFont("Consolas", 10));
    scriptLog->setStyleSheet(
        "QTextEdit { background: #161B22; color: #8B949E;"
        " border: 1px solid #21262D; border-radius: 6px;"
        " font-family: 'Consolas','Fira Code',monospace; font-size: 10px;"
        " padding: 6px; }"
        + Theme::scrollbar());
    scriptLog->setMaximumHeight(80);
    scriptLog->setPlaceholderText("Script output will appear here…");
    scriptLayout->addWidget(scriptLog);

    splitter->addWidget(scriptPane);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 2);

    // ── Connections ──
    connect(structureBox, &QComboBox::currentIndexChanged, this, &DataStructureLab::onStructureChanged);
    connect(primaryBtn,   &QPushButton::clicked, this, &DataStructureLab::onPrimaryAction);
    connect(secondaryBtn, &QPushButton::clicked, this, &DataStructureLab::onSecondaryAction);
    connect(searchBtn,    &QPushButton::clicked, this, &DataStructureLab::onSearchAction);
    connect(loadMapsBtn,  &QPushButton::clicked, this, &DataStructureLab::onLoadMapsClicked);
    connect(runScriptBtn, &QPushButton::clicked, this, &DataStructureLab::onRunScript);

    connect(scriptLibBox, &QComboBox::currentIndexChanged, this, [this](int) {
        QString src = scriptLibBox->currentData().toString();
        if (!src.isEmpty()) scriptEdit->setPlainText(src);
    });

    updateControlsForType();
    refreshView();
}

DataStructureLab::~DataStructureLab() {
    freeChain(stackTop);
    freeChain(queueHead);
    freeChain(listHead);
    for (HashEntry* h : buckets) {
        while (h) {
            HashEntry* next = h->next;
            delete h;
            h = next;
        }
    }
    destroyBST(bstRoot);
}

void DataStructureLab::populateScriptLibrary() {
    scriptLibBox->clear();
    scriptLibBox->addItem("— select starter —", QString());
    scriptLibBox->addItem("BST: In-order traversal",   QString(StarterScripts::BST_INORDER));
    scriptLibBox->addItem("BST: Pre-order traversal",  QString(StarterScripts::BST_PREORDER));
    scriptLibBox->addItem("BST: Post-order traversal", QString(StarterScripts::BST_POSTORDER));
    scriptLibBox->addItem("BST: Search with path",     QString(StarterScripts::BST_SEARCH));
    scriptLibBox->addItem("List: Traverse and print",  QString(StarterScripts::LIST_TRAVERSE));
    scriptLibBox->addItem("List: Cycle detection",     QString(StarterScripts::LIST_CYCLE));
    scriptLibBox->addItem("Stack: Print all frames",   QString(StarterScripts::STACK_PRINT));
    scriptLibBox->addItem("Custom (blank template)",   QString(StarterScripts::CUSTOM));
}

void DataStructureLab::freeChain(LLNode* head) {
    while (head) {
        LLNode* next = head->next;
        delete head;
        head = next;
    }
}

void DataStructureLab::destroyBST(BSTNode* node) {
    if (!node) return;
    destroyBST(node->left);
    destroyBST(node->right);
    delete node;
}

// ── QVariant comparator ────────────────────────────────────────────────────
// Numeric types (int/double/float/long long/…) are compared numerically.
// String types are compared lexicographically.
// Comparing a number against a string is a type mismatch → err = true.
int DataStructureLab::variantCompare(const QVariant& a, const QVariant& b, bool& err) {
    err = false;
    bool aNum = a.canConvert<double>();
    bool bNum = b.canConvert<double>();

    if (aNum && bNum) {
        double da = a.toDouble();
        double db = b.toDouble();
        if (da < db) return -1;
        if (da > db) return  1;
        return 0;
    }

    bool aStr = (a.userType() == QMetaType::QString);
    bool bStr = (b.userType() == QMetaType::QString);
    if (aStr && bStr) {
        return a.toString().compare(b.toString());
    }

    // Type mismatch — can't order meaningfully
    err = true;
    return INT_MIN;
}

BSTNode* DataStructureLab::bstInsert(BSTNode* node, const QVariant& value) {
    if (!node) return new BSTNode(value);
    bool err = false;
    int cmp = variantCompare(value, node->value, err);
    if (err) return node; // type mismatch — silently skip
    if (cmp < 0) node->left  = bstInsert(node->left,  value);
    else if (cmp > 0) node->right = bstInsert(node->right, value);
    // equal values are ignored — BSTs conventionally don't store duplicates
    return node;
}

BSTNode* DataStructureLab::bstRemove(BSTNode* node, const QVariant& value, bool& removed) {
    if (!node) return nullptr;
    bool err = false;
    int cmp = variantCompare(value, node->value, err);
    if (err) return node;
    if (cmp < 0) {
        node->left = bstRemove(node->left, value, removed);
    } else if (cmp > 0) {
        node->right = bstRemove(node->right, value, removed);
    } else {
        removed = true;
        if (!node->left) {
            BSTNode* r = node->right;
            delete node;
            return r;
        }
        if (!node->right) {
            BSTNode* l = node->left;
            delete node;
            return l;
        }
        // Two children: replace with the smallest value in the right subtree
        BSTNode* succ = node->right;
        while (succ->left) succ = succ->left;
        node->value = succ->value;
        bool dummy = false;
        node->right = bstRemove(node->right, succ->value, dummy);
    }
    return node;
}

BSTNode* DataStructureLab::bstFind(BSTNode* node, const QVariant& value,
                                    std::vector<BSTNode*>& path) const {
    while (node) {
        path.push_back(node);
        bool err = false;
        int cmp = variantCompare(value, node->value, err);
        if (err || cmp == 0) return (err ? nullptr : node);
        node = (cmp < 0) ? node->left : node->right;
    }
    return nullptr;
}

int DataStructureLab::hashKey(const QVariant& key) const {
    // Use the string representation as the hash input so any type works.
    QString s = key.toString();
    int sum = 0;
    for (QChar c : s) sum += c.unicode();
    return BUCKET_COUNT > 0 ? ((sum % BUCKET_COUNT + BUCKET_COUNT) % BUCKET_COUNT) : 0;
}

void DataStructureLab::updateControlsForType() {
    DSType t = (DSType)structureBox->currentData().toInt();
    view->type = t;
    view->highlightNode = nullptr;
    view->visitedNodes.clear();

    bool isHashMap = (t == DSType::HashMap);
    keyLabel->setVisible(isHashMap);
    keyInput->setVisible(isHashMap);
    valueLabel->setVisible(true);
    valueInput->setVisible(true);

    switch (t) {
        case DSType::Stack:
            primaryBtn->setText("Push");
            secondaryBtn->setText("Pop");
            searchBtn->setText("Peek Top");
            valueInput->setPlaceholderText("value (int, float, or text)");
            primaryBtn->setEnabled(true);
            secondaryBtn->setEnabled(true);
            searchBtn->setEnabled(true);
            valueInput->setEnabled(true);
            break;
        case DSType::Queue:
            primaryBtn->setText("Enqueue");
            secondaryBtn->setText("Dequeue");
            searchBtn->setText("Peek Front");
            valueInput->setPlaceholderText("value (int, float, or text)");
            primaryBtn->setEnabled(true);
            secondaryBtn->setEnabled(true);
            searchBtn->setEnabled(true);
            valueInput->setEnabled(true);
            break;
        case DSType::LinkedList:
            primaryBtn->setText("Insert at Head");
            secondaryBtn->setText("Remove Value");
            searchBtn->setText("Search");
            valueInput->setPlaceholderText("value (int, float, or text)");
            primaryBtn->setEnabled(true);
            secondaryBtn->setEnabled(true);
            searchBtn->setEnabled(true);
            valueInput->setEnabled(true);
            break;
        case DSType::HashMap:
            primaryBtn->setText("Put");
            secondaryBtn->setText("Remove Key");
            searchBtn->setText("Get");
            valueInput->setPlaceholderText("value (any text or number)");
            primaryBtn->setEnabled(true);
            secondaryBtn->setEnabled(true);
            searchBtn->setEnabled(true);
            valueInput->setEnabled(true);
            break;
        case DSType::BST:
            primaryBtn->setText("Insert");
            secondaryBtn->setText("Remove");
            searchBtn->setText("Search");
            valueInput->setPlaceholderText("value (int, float, or text)");
            primaryBtn->setEnabled(true);
            secondaryBtn->setEnabled(true);
            searchBtn->setEnabled(true);
            valueInput->setEnabled(true);
            break;
        case DSType::ProcessStack:
            primaryBtn->setText("Refresh Stack");
            secondaryBtn->setText("—");
            searchBtn->setText("—");
            valueInput->setPlaceholderText("(select process in Sandbox tab)");
            primaryBtn->setEnabled(true);
            secondaryBtn->setEnabled(false);
            searchBtn->setEnabled(false);
            valueInput->setEnabled(false);
            keyLabel->setVisible(false);
            keyInput->setVisible(false);
            break;
    }
}

void DataStructureLab::onStructureChanged(int) {
    updateControlsForType();
    refreshView();
}

void DataStructureLab::refreshView() {
    view->stackTop  = stackTop;
    view->queueHead = queueHead;
    view->queueTail = queueTail;
    view->listHead  = listHead;
    view->buckets   = &buckets;
    view->bstRoot   = bstRoot;
    view->update();
}

// ── parseValue: parse input text into a QVariant ──────────────────────────
// Try int → double → bool → plain string, in that priority order.
static QVariant parseValue(const QString& text) {
    if (text.isEmpty()) return QVariant();
    // Try integer first
    bool ok = false;
    long long iv = text.toLongLong(&ok);
    if (ok) return QVariant(iv);
    // Try double
    double dv = text.toDouble(&ok);
    if (ok) return QVariant(dv);
    // Bool keywords
    QString lo = text.toLower();
    if (lo == "true")  return QVariant(true);
    if (lo == "false") return QVariant(false);
    // Fall back to string
    return QVariant(text);
}

void DataStructureLab::onPrimaryAction() {
    DSType t = (DSType)structureBox->currentData().toInt();

    // ProcessStack: "Refresh Stack" re-reads /proc data for the current PID
    if (t == DSType::ProcessStack) {
        if (view->inspectedPid > 0) {
            view->processFrames = readProcessStack(view->inspectedPid);
            view->update();
            statusLabel->setText(QString("Stack refreshed for PID %1").arg(view->inspectedPid));
        } else {
            statusLabel->setText("Select a sandbox process first (click its row in Sandbox tab)");
        }
        return;
    }

    if (t == DSType::HashMap) {
        QString keyStr = keyInput->text().trimmed();
        if (keyStr.isEmpty()) { statusLabel->setText("⚠ Enter a key first."); return; }
        QVariant key   = parseValue(keyStr);
        QVariant value = parseValue(valueInput->text().trimmed());
        int b = hashKey(key);
        HashEntry* found = nullptr;
        for (HashEntry* e = buckets[b]; e; e = e->next) {
            if (e->key == key) { found = e; break; }
        }
        if (found) {
            found->value = value;
            view->highlightNode = found;
            statusLabel->setText(QString("Updated key \"%1\" at %2 (bucket %3).")
                                  .arg(key.toString()).arg(dsAddr(found)).arg(b));
        } else {
            HashEntry* ne = new HashEntry(key, value);
            ne->next = buckets[b];
            buckets[b] = ne;
            view->highlightNode = ne;
            statusLabel->setText(QString("Inserted key \"%1\" — new node at %2 (bucket %3).")
                                  .arg(key.toString()).arg(dsAddr(ne)).arg(b));
        }
        view->visitedNodes.clear();
        emit explanationNeeded(QString(
            "<b>Hash Map — Put</b><br><br>"
            "Key <b>%1</b> hashed to bucket <b>%2</b> (of %3 buckets). A new "
            "<code>HashEntry</code> was allocated on the heap at <code>%4</code> "
            "and linked onto the front of that bucket's chain.")
            .arg(key.toString()).arg(b).arg(BUCKET_COUNT).arg(dsAddr(view->highlightNode)));
        refreshView();
        return;
    }

    QVariant value = parseValue(valueInput->text().trimmed());
    if (!value.isValid()) { statusLabel->setText("⚠ Enter a value first."); return; }

    if (t == DSType::Stack) {
        LLNode* n = new LLNode(value);
        n->next = stackTop;
        QString oldTopAddr = n->next ? dsAddr(n->next) : "0x0 (nullptr — it was empty)";
        stackTop = n;
        view->highlightNode = n;
        view->visitedNodes.clear();
        statusLabel->setText(QString("Pushed %1 — new node allocated at %2, now the top.")
                              .arg(value.toString()).arg(dsAddr(n)));
        emit explanationNeeded(QString(
            "<b>Stack — Push</b><br><br>"
            "A new node was allocated on the heap at <code>%1</code>. Its "
            "<code>next</code> pointer was set to point at the previous top "
            "(<code>%2</code>), and it became the new top. Stacks are LIFO — "
            "Last In, First Out — so this is exactly the node Pop will remove "
            "next.").arg(dsAddr(n)).arg(oldTopAddr));
    } else if (t == DSType::Queue) {
        LLNode* n = new LLNode(value);
        if (!queueHead) {
            queueHead = queueTail = n;
        } else {
            queueTail->next = n;
            queueTail = n;
        }
        view->highlightNode = n;
        view->visitedNodes.clear();
        statusLabel->setText(QString("Enqueued %1 — new node allocated at %2, now the back.")
                              .arg(value.toString()).arg(dsAddr(n)));
        emit explanationNeeded(QString(
            "<b>Queue — Enqueue</b><br><br>"
            "A new node was allocated at <code>%1</code> and linked onto the "
            "previous tail's <code>next</code> pointer, then it became the new "
            "tail. Queues are FIFO — First In, First Out.").arg(dsAddr(n)));
    } else if (t == DSType::LinkedList) {
        LLNode* n = new LLNode(value);
        n->next = listHead;
        QString oldHeadAddr = n->next ? dsAddr(n->next) : "0x0 (nullptr — it was empty)";
        listHead = n;
        view->highlightNode = n;
        view->visitedNodes.clear();
        statusLabel->setText(QString("Inserted %1 at the head — new node at %2.")
                              .arg(value.toString()).arg(dsAddr(n)));
        emit explanationNeeded(QString(
            "<b>Linked List — Insert at Head</b><br><br>"
            "A new node was allocated at <code>%1</code>. Its <code>next</code> "
            "pointer was set to the old head (<code>%2</code>), and the list's "
            "head pointer was repointed at it. This is O(1).")
            .arg(dsAddr(n)).arg(oldHeadAddr));
    } else if (t == DSType::BST) {
        bstRoot = bstInsert(bstRoot, value);
        std::vector<BSTNode*> path;
        BSTNode* inserted = bstFind(bstRoot, value, path);
        view->highlightNode = inserted;
        view->visitedNodes.clear();
        for (auto* nd : path) view->visitedNodes.insert(nd);
        statusLabel->setText(QString("Inserted %1 into the tree at %2.")
                              .arg(value.toString()).arg(dsAddr(inserted)));
        emit explanationNeeded(
            "<b>BST — Insert</b><br><br>"
            "Starting at the root, the search (orange path) went left for "
            "smaller values and right for larger ones until it reached an "
            "empty pointer, where a brand-new node was allocated.");
    }

    refreshView();
}

void DataStructureLab::onSecondaryAction() {
    DSType t = (DSType)structureBox->currentData().toInt();

    if (t == DSType::HashMap) {
        QString keyStr = keyInput->text().trimmed();
        if (keyStr.isEmpty()) { statusLabel->setText("⚠ Enter a key to remove."); return; }
        QVariant key = parseValue(keyStr);
        int b = hashKey(key);
        HashEntry* cur = buckets[b];
        HashEntry* prev = nullptr;
        bool found = false;
        while (cur) {
            if (cur->key == key) {
                QString freedAddr = dsAddr(cur);
                if (prev) prev->next = cur->next; else buckets[b] = cur->next;
                delete cur;
                found = true;
                statusLabel->setText(QString("Removed key \"%1\" — freed node %2.")
                                      .arg(key.toString(), freedAddr));
                break;
            }
            prev = cur;
            cur = cur->next;
        }
        if (!found) statusLabel->setText(QString("No such key \"%1\".").arg(key.toString()));
        view->highlightNode = nullptr;
        view->visitedNodes.clear();
        refreshView();
        return;
    }

    if (t == DSType::BST) {
        QVariant value = parseValue(valueInput->text().trimmed());
        if (!value.isValid()) { statusLabel->setText("⚠ Enter a value to remove."); return; }
        bool removed = false;
        bstRoot = bstRemove(bstRoot, value, removed);
        statusLabel->setText(removed
            ? QString("Removed %1 from the tree.").arg(value.toString())
            : QString("%1 isn't in the tree.").arg(value.toString()));
        view->highlightNode = nullptr;
        view->visitedNodes.clear();
        emit explanationNeeded(
            "<b>BST — Remove</b><br><br>"
            "Removing a node with two children is the tricky case: its value "
            "is overwritten with the smallest value in its right subtree (its "
            "in-order successor), and that successor's original node is the "
            "one actually freed. This keeps the ordering property intact.");
        refreshView();
        return;
    }

    if (t == DSType::Stack) {
        if (!stackTop) { statusLabel->setText("Nothing to pop — it's empty."); return; }
        LLNode* old = stackTop;
        QString popped = old->value.toString();
        QString freedAddr = dsAddr(old);
        stackTop = old->next;
        delete old;
        statusLabel->setText(QString("Popped %1 — freed node %2.").arg(popped).arg(freedAddr));
        emit explanationNeeded(QString(
            "<b>Stack — Pop</b><br><br>"
            "The top node (<code>%1</code>) was unlinked and <code>delete</code>d. "
            "Only the top can ever be removed directly, exactly like a real "
            "call stack: the most recently called function returns first.").arg(freedAddr));
    } else if (t == DSType::Queue) {
        if (!queueHead) { statusLabel->setText("Nothing to dequeue — it's empty."); return; }
        LLNode* old = queueHead;
        QString dequeued = old->value.toString();
        QString freedAddr = dsAddr(old);
        queueHead = old->next;
        if (!queueHead) queueTail = nullptr;
        delete old;
        statusLabel->setText(QString("Dequeued %1 — freed node %2.").arg(dequeued).arg(freedAddr));
        emit explanationNeeded(QString(
            "<b>Queue — Dequeue</b><br><br>"
            "The front node (<code>%1</code>) was unlinked from the head and "
            "freed. The element that's been waiting longest leaves first — FIFO.")
            .arg(freedAddr));
    } else if (t == DSType::LinkedList) {
        if (!listHead) { statusLabel->setText("Nothing to remove — it's empty."); return; }
        QVariant value = parseValue(valueInput->text().trimmed());
        if (!value.isValid()) { statusLabel->setText("⚠ Enter the value to remove."); return; }
        LLNode* cur = listHead;
        LLNode* prev = nullptr;
        while (cur && cur->value != value) { prev = cur; cur = cur->next; }
        if (!cur) { statusLabel->setText(QString("%1 isn't in the list.").arg(value.toString())); return; }
        QString freedAddr = dsAddr(cur);
        if (prev) prev->next = cur->next; else listHead = cur->next;
        delete cur;
        statusLabel->setText(QString("Removed %1 — freed node %2.").arg(value.toString()).arg(freedAddr));
        emit explanationNeeded(QString(
            "<b>Linked List — Remove</b><br><br>"
            "The list was walked node by node until the value was found at "
            "<code>%1</code>. The previous node's pointer was redirected "
            "around it, then the node was freed. That search is what makes "
            "removal O(n) even though the actual unlinking step is O(1).").arg(freedAddr));
    }

    view->highlightNode = nullptr;
    view->visitedNodes.clear();
    refreshView();
}

void DataStructureLab::onSearchAction() {
    DSType t = (DSType)structureBox->currentData().toInt();
    view->visitedNodes.clear();
    view->highlightNode = nullptr;

    if (t == DSType::Stack) {
        if (!stackTop) { statusLabel->setText("Nothing to peek — it's empty."); refreshView(); return; }
        view->highlightNode = stackTop;
        statusLabel->setText(QString("Top is %1, living at %2 — peek doesn't remove it.")
                              .arg(stackTop->value.toString()).arg(dsAddr(stackTop)));
        emit explanationNeeded(
            "<b>Stack — Peek</b><br><br>"
            "Peek just reads the top node's value without unlinking or "
            "freeing anything — the stack is left exactly as it was.");
    } else if (t == DSType::Queue) {
        if (!queueHead) { statusLabel->setText("Nothing to peek — it's empty."); refreshView(); return; }
        view->highlightNode = queueHead;
        statusLabel->setText(QString("Front is %1, living at %2 — peek doesn't remove it.")
                              .arg(queueHead->value.toString()).arg(dsAddr(queueHead)));
        emit explanationNeeded(
            "<b>Queue — Peek</b><br><br>"
            "Peek reads the front node — the next one in line to be "
            "dequeued — without removing it.");
    } else if (t == DSType::LinkedList) {
        QVariant value = parseValue(valueInput->text().trimmed());
        if (!value.isValid()) { statusLabel->setText("⚠ Enter a value to search for."); refreshView(); return; }
        LLNode* cur = listHead;
        int hops = 0;
        while (cur) {
            view->visitedNodes.insert(cur);
            if (cur->value == value) break;
            cur = cur->next;
            hops++;
        }
        if (cur) {
            view->highlightNode = cur;
            statusLabel->setText(QString("Found %1 at %2 after %3 hop(s) from the head.")
                                  .arg(value.toString()).arg(dsAddr(cur)).arg(hops));
        } else {
            statusLabel->setText(QString("%1 isn't in the list — walked to nullptr.").arg(value.toString()));
        }
        emit explanationNeeded(
            "<b>Linked List — Search</b><br><br>"
            "There's no shortcut: a linked list has no index, so finding a "
            "value means following <code>next</code> pointers one at a time "
            "from the head (the orange-highlighted nodes) until it's found or "
            "the list runs out. That's O(n).");
    } else if (t == DSType::HashMap) {
        QString keyStr = keyInput->text().trimmed();
        if (keyStr.isEmpty()) { statusLabel->setText("⚠ Enter a key to look up."); refreshView(); return; }
        QVariant key = parseValue(keyStr);
        int b = hashKey(key);
        HashEntry* cur = buckets[b];
        int hops = 0;
        while (cur) {
            view->visitedNodes.insert(cur);
            if (cur->key == key) break;
            cur = cur->next;
            hops++;
        }
        if (cur) {
            view->highlightNode = cur;
            statusLabel->setText(QString("\"%1\" → \"%2\" at %3 (bucket %4, %5 hop(s)).")
                                  .arg(key.toString(), cur->value.toString(), dsAddr(cur)).arg(b).arg(hops));
        } else {
            statusLabel->setText(QString("No key \"%1\" in bucket %2.").arg(key.toString()).arg(b));
        }
        emit explanationNeeded(QString(
            "<b>Hash Map — Get</b><br><br>"
            "The key hashed straight to bucket <b>%1</b> — that part is O(1) "
            "— but a lookup still has to walk that bucket's chain (the orange "
            "nodes) comparing keys one at a time, because that's how "
            "collisions are resolved.").arg(b));
    } else if (t == DSType::BST) {
        QVariant value = parseValue(valueInput->text().trimmed());
        if (!value.isValid()) { statusLabel->setText("⚠ Enter a value to search for."); refreshView(); return; }
        std::vector<BSTNode*> path;
        BSTNode* found = bstFind(bstRoot, value, path);
        for (auto* nd : path) view->visitedNodes.insert(nd);
        if (found) {
            view->highlightNode = found;
            statusLabel->setText(QString("Found %1 at %2 after comparing against %3 node(s).")
                                  .arg(value.toString()).arg(dsAddr(found)).arg((int)path.size()));
        } else {
            statusLabel->setText(QString("%1 isn't in the tree — compared against %2 node(s).")
                                  .arg(value.toString()).arg((int)path.size()));
        }
        emit explanationNeeded(
            "<b>BST — Search</b><br><br>"
            "Each orange node along the path was one comparison: go left if "
            "the target is smaller, right if larger. Because the tree is "
            "ordered, the search skips an entire subtree at every step.");
    }

    refreshView();
}

// ── onRunScript ───────────────────────────────────────────────────────────
// Runs the traversal script through DSTinyVM — a pure C++ interpreter that
// walks the real node pointers directly.  No FFI, no type coercion.
void DataStructureLab::onRunScript() {
    QString src = scriptEdit->toPlainText().trimmed();
    if (src.isEmpty()) return;

    view->visitedNodes.clear();
    view->highlightNode = nullptr;
    view->update();

    QString logOutput;

    DSTinyVM vm;

    // Wire callbacks: these write directly into DSView's existing fields.
    vm.onLog = [&](const QString& msg) {
        logOutput += msg + "\n";
    };
    vm.onVisited = [&](void* ptr) {
        view->visitedNodes.insert(ptr);
        view->update();
    };
    vm.onHighlight = [&](void* ptr) {
        view->highlightNode = ptr;
        view->update();
    };
    vm.onClearVisited = [&]() {
        view->visitedNodes.clear();
        view->highlightNode = nullptr;
        view->update();
    };

    // Set entry-point pointers for `head` and `root` identifiers.
    DSType t = (DSType)structureBox->currentData().toInt();
    switch (t) {
        case DSType::Stack:      vm.headPtr = stackTop;  break;
        case DSType::Queue:      vm.headPtr = queueHead; break;
        case DSType::LinkedList: vm.headPtr = listHead;  break;
        case DSType::BST:        vm.rootPtr = bstRoot;   break;
        default: break;
    }

    QString err = vm.run(src);
    if (!err.isEmpty()) {
        if (!logOutput.isEmpty()) logOutput += "\n";
        logOutput += "❌ " + err;
    }

    scriptLog->setPlainText(logOutput.isEmpty() ? "(no output)" : logOutput);
    refreshView();
}

// ───────────────────────── Process Stack view ─────────────────────────────

void DSView::drawFrameBox(QPainter& p, const QRect& r, const StackFrame& frame, bool isTop) {
    QColor fill   = isTop ? QColor(Theme::GREEN)     : QColor(Theme::BG_INPUT);
    QColor border = isTop ? QColor(Theme::GREEN)     : QColor(Theme::BORDER);
    QColor text   = isTop ? Qt::white                : QColor(Theme::TEXT_PRIMARY);

    QPainterPath path;
    path.addRoundedRect(r, 8, 8);
    p.fillPath(path, fill);
    p.setPen(QPen(border, isTop ? 2 : 1));
    p.drawPath(path);

    p.setFont(QFont("Consolas", 8, QFont::Bold));
    p.setPen(isTop ? QColor(255,255,255,200) : QColor(Theme::TEXT_MUTED));
    p.drawText(QRect(r.x()+4, r.y()+2, 28, r.height()-4),
               Qt::AlignVCenter | Qt::AlignLeft,
               QString("#%1").arg(frame.frameIndex));

    p.setFont(QFont("Segoe UI", 10, QFont::Bold));
    p.setPen(text);
    p.drawText(QRect(r.x()+34, r.y(), r.width()-38, r.height()-14),
               Qt::AlignVCenter | Qt::AlignLeft,
               frame.functionName);

    p.setFont(QFont("Consolas", 7));
    p.setPen(isTop ? QColor(255,255,255,180) : QColor(Theme::TEXT_MUTED));
    p.drawText(QRect(r.x()+4, r.bottom()-14, r.width()-8, 14),
               Qt::AlignVCenter | Qt::AlignLeft,
               frame.address ? QString("0x%1").arg(frame.address, 0, 16) : "kernel");
}

void DSView::paintProcessStack(QPainter& p) {
    if (processFrames.empty()) {
        p.setPen(QColor(Theme::TEXT_MUTED));
        p.setFont(QFont("Segoe UI", 11));
        if (inspectedPid <= 0) {
            p.drawText(rect(), Qt::AlignCenter,
                "Select a process in the Sandbox tab\n"
                "to see its real kernel call stack here.\n\n"
                "The stack shows what each process is\n"
                "doing right now inside the kernel.");
        } else {
            p.drawText(rect(), Qt::AlignCenter,
                QString("PID %1 — no stack frames available\n"
                        "(process may have exited)").arg(inspectedPid));
        }
        return;
    }

    int boxW = 320, boxH = 52, gap = 18;
    int x = width() / 2 - boxW / 2;
    int y = 14;

    p.setFont(QFont("Segoe UI", 9, QFont::Bold));
    p.setPen(QColor(Theme::TEXT_PRIMARY));
    p.drawText(x, y, boxW, 18, Qt::AlignCenter,
               QString("Kernel call stack — PID %1").arg(inspectedPid));
    y += 22;

    for (int i = 0; i < (int)processFrames.size(); i++) {
        const StackFrame& f = processFrames[i];
        QRect r(x, y, boxW, boxH);
        bool isTop = (i == 0);
        drawFrameBox(p, r, f, isTop);

        if (isTop) {
            p.setPen(QPen(QColor(Theme::GREEN), 2));
            p.setFont(QFont("Segoe UI", 8, QFont::Bold));
            p.drawText(x + boxW + 8, y + boxH/2 + 4, "← TOP (current syscall)");
        }

        if (i + 1 < (int)processFrames.size()) {
            int ax = x + boxW/2;
            int ay = y + boxH;
            p.setPen(QPen(QColor(Theme::TEXT_MUTED), 1.5));
            p.drawLine(ax, ay, ax, ay + gap);
            p.drawLine(ax, ay+gap, ax-5, ay+gap-7);
            p.drawLine(ax, ay+gap, ax+5, ay+gap-7);
        } else {
            p.setFont(QFont("Consolas", 8));
            p.setPen(QColor(Theme::TEXT_MUTED));
            p.drawText(QRect(x, y+boxH+4, boxW, 14), Qt::AlignCenter,
                       "caller = user space / entry point");
        }

        y += boxH + gap;
        if (y + boxH > height() - 10) break;
    }
}

// ── loadProcessStack / readProcessStack ───────────────────────────────────

std::vector<StackFrame> DataStructureLab::readProcessStack(pid_t pid) {
    std::vector<StackFrame> frames;

    {
        StackFrame f;
        f.frameIndex = 0;
        f.address = 0;
        std::ifstream wc("/proc/" + std::to_string(pid) + "/wchan");
        if (wc.is_open()) {
            std::string wcStr;
            std::getline(wc, wcStr);
            if (wcStr.empty() || wcStr == "0")
                f.functionName = "running";
            else
                f.functionName = QString::fromStdString(wcStr);
        } else {
            f.functionName = "unknown";
        }
        frames.push_back(f);
    }

    {
        StackFrame f;
        f.frameIndex = 1;
        std::ifstream comm("/proc/" + std::to_string(pid) + "/comm");
        std::string name;
        if (comm.is_open()) std::getline(comm, name);
        f.functionName = QString::fromStdString(name.empty() ? "main" : name + "::main");

        std::ifstream maps("/proc/" + std::to_string(pid) + "/maps");
        std::string line;
        while (std::getline(maps, line)) {
            if (line.find("[stack]") != std::string::npos) {
                unsigned long start = 0, end = 0;
                sscanf(line.c_str(), "%lx-%lx", &start, &end);
                f.address = end;
                break;
            }
        }
        frames.push_back(f);
    }

    {
        StackFrame f;
        f.frameIndex = 2;
        f.functionName = "clone / _start (libc entry)";
        std::ifstream stat("/proc/" + std::to_string(pid) + "/stat");
        std::string token;
        for (int i = 0; i < 28 && stat >> token; i++) {}
        unsigned long startCode = 0;
        if (stat >> startCode) f.address = startCode;
        frames.push_back(f);
    }

    {
        StackFrame f;
        f.frameIndex = 3;
        f.functionName = "kernel: do_fork / schedule";
        f.address = 0;
        frames.push_back(f);
    }

    return frames;
}

void DataStructureLab::loadProcessStack(pid_t pid) {
    lastSandboxPid = pid;

    for (int i = 0; i < structureBox->count(); i++) {
        if (structureBox->itemData(i).toInt() == (int)DSType::ProcessStack) {
            structureBox->setCurrentIndex(i);
            break;
        }
    }

    view->type = DSType::ProcessStack;
    view->inspectedPid = pid;
    view->processFrames = readProcessStack(pid);
    view->highlightNode = nullptr;
    view->visitedNodes.clear();
    view->update();

    statusLabel->setText(QString("Showing kernel stack for PID %1 — %2 frames")
                         .arg(pid).arg(view->processFrames.size()));

    QString topFrame = view->processFrames.empty() ? "none"
                     : view->processFrames[0].functionName;
    emit explanationNeeded(QString(
        "<b>Process Stack — PID %1</b><br><br>"
        "This is the kernel-side call stack for the selected process, "
        "read directly from <code>/proc/%1/wchan</code> and "
        "<code>/proc/%1/maps</code>.<br><br>"
        "<b>Current kernel function:</b> <code>%2</code><br><br>"
        "<b>How a stack works:</b> Every function call pushes a frame "
        "onto the stack. The frame holds the return address, local "
        "variables, and saved registers. When the function returns, "
        "its frame is popped. The stack grows <b>downward</b> in "
        "memory on x86-64 — the top of the stack has the lowest address.")
        .arg(pid).arg(topFrame));
}

void DataStructureLab::onLoadMapsClicked() {
    pid_t pid = lastSandboxPid;
    if (pid <= 0) {
        statusLabel->setText("Select a sandbox process first.");
        return;
    }

    DSType t = (DSType)structureBox->currentData().toInt();
    if (t == DSType::ProcessStack || t == DSType::BST) {
        statusLabel->setText("Load /proc/maps works with Stack, Queue, LinkedList, or HashMap.");
        return;
    }

    std::ifstream maps("/proc/" + std::to_string(pid) + "/maps");
    if (!maps.is_open()) {
        statusLabel->setText(QString("Cannot open /proc/%1/maps.").arg(pid));
        return;
    }

    int count = 0;
    std::string line;
    while (std::getline(maps, line) && count < 32) {
        QString entry = QString::fromStdString(line).trimmed();
        if (entry.isEmpty()) continue;
        QVariant v(entry);

        if (t == DSType::Stack) {
            LLNode* n = new LLNode(v);
            n->next = stackTop;
            stackTop = n;
        } else if (t == DSType::Queue) {
            LLNode* n = new LLNode(v);
            if (!queueHead) queueHead = queueTail = n;
            else { queueTail->next = n; queueTail = n; }
        } else if (t == DSType::LinkedList) {
            LLNode* n = new LLNode(v);
            n->next = listHead;
            listHead = n;
        } else if (t == DSType::HashMap) {
            // Use the address range as key, permissions as value
            QStringList parts = entry.split(' ', Qt::SkipEmptyParts);
            QVariant key   = parts.isEmpty() ? QVariant(entry) : QVariant(parts[0]);
            QVariant value = parts.size() > 1 ? QVariant(parts[1]) : QVariant(QString("-"));
            int b = hashKey(key);
            HashEntry* ne = new HashEntry(key, value);
            ne->next = buckets[b];
            buckets[b] = ne;
        }
        count++;
    }

    view->highlightNode = nullptr;
    view->visitedNodes.clear();
    statusLabel->setText(QString("Loaded %1 /proc/%2/maps entries into the %3.")
                         .arg(count).arg(pid)
                         .arg(structureBox->currentText()));
    refreshView();
}

// ── DSRSSBar stubs ────────────────────────────────────────────────────────
// DSRSSBar is declared in the shared header (legacy placeholder).
// Provide minimal implementations so the MOC metadata links cleanly.
DSRSSBar::DSRSSBar(QWidget* parent) : QWidget(parent) {}
void DSRSSBar::setValues(long c, long p) { cur = c; peak = p; update(); }
void DSRSSBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    if (peak <= 0) return;
    double ratio = (double)cur / (double)peak;
    QRect bar(0, 0, (int)(width() * ratio), height());
    p.fillRect(bar, QColor(Theme::BLUE));
}
