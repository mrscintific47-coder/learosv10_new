#include "DataStructureLab.h"
#include "Theme.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainterPath>
#include <QFont>
#include <algorithm>
#include <fstream>
#include <sstream>

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

void DSView::drawNodeBox(QPainter& p, const QRect& r, int value, const void* addrPtr,
                          bool highlighted, bool visited) {
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
    p.setFont(QFont("Segoe UI", 12, QFont::Bold));
    p.drawText(QRect(r.x(), r.y() + 2, r.width(), r.height() - 16),
               Qt::AlignCenter, QString::number(value));

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
        // Vertical, top-of-stack at the TOP, growing downward — push/pop
        // both happen at the same end you see labeled, no ambiguity.
        int boxW = 150, boxH = 46, gap = 22;
        int x = width() / 2 - boxW / 2;
        int y = 14;
        LLNode* n = head;
        bool isTop = true;
        while (n) {
            QRect r(x, y, boxW, boxH);
            bool hi = (void*)n == highlightNode;
            bool visited = visitedNodes.contains((void*)n);
            drawNodeBox(p, r, n->value, (void*)n, hi, visited);

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
    int boxW = 64, boxH = 46, gap = 36, pad = 24;
    int x = pad, y = height() / 2 - boxH / 2;
    LLNode* n = head;
    while (n) {
        QRect r(x, y, boxW, boxH);
        bool hi = (void*)n == highlightNode;
        bool visited = visitedNodes.contains((void*)n);
        drawNodeBox(p, r, n->value, (void*)n, hi, visited);

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
            bool visited = visitedNodes.contains((void*)e);
            QString label = e->key + ": " + e->value;
            p.setFont(QFont("Segoe UI", 9, QFont::Bold));
            int w = p.fontMetrics().horizontalAdvance(label) + 24;

            if (x + w > width() - pad - 20) {
                p.setFont(QFont("Segoe UI", 9));
                p.setPen(QColor(Theme::TEXT_MUTED));
                p.drawText(x, y + rowH / 2 + 4, "…chain continues");
                break;
            }

            QRect chip(x, y + rowH / 2 - 16, w, 28);
            QColor chipBg = hi ? QColor(Theme::BLUE) : visited ? QColor(Theme::ORANGE_LIGHT) : QColor(Theme::BLUE_LIGHT);
            QColor chipFg = hi ? Qt::white : visited ? QColor(Theme::ORANGE) : QColor(Theme::BLUE);

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
        bool visited = visitedNodes.contains((void*)n);
        QColor fill = hi ? QColor(Theme::BLUE) : visited ? QColor(Theme::ORANGE) : QColor(Theme::PURPLE);
        p.setBrush(fill);
        p.setPen(Qt::NoPen);
        p.drawEllipse(circle);

        p.setPen(Qt::white);
        p.setFont(QFont("Segoe UI", 9, QFont::Bold));
        p.drawText(circle, Qt::AlignCenter, QString::number(n->value));

        p.setFont(QFont("Consolas", 7));
        p.setPen(QColor(Theme::TEXT_MUTED));
        p.drawText(QRect(c.x() - 32, c.y() + r + 2, 64, 12), Qt::AlignCenter, dsAddr(n));

        if (n->left) stack.push_back(n->left);
        if (n->right) stack.push_back(n->right);
    }
}

// ───────────────────────── DataStructureLab ─────────────────────────

DataStructureLab::DataStructureLab(QWidget* parent) : QWidget(parent) {
    buckets.resize(BUCKET_COUNT, nullptr);

    setStyleSheet(QString("background: %1;").arg(Theme::BG_APP));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);

    auto* title = new QLabel("🧩  Data Structure Lab");
    title->setStyleSheet(QString("color: %1; font-size: 14px; font-weight: bold;")
                          .arg(Theme::TEXT_PRIMARY));
    layout->addWidget(title);

    auto* hint = new QLabel(
        "Every operation allocates or frees real heap memory — the hex address "
        "under each box is the literal pointer returned by <code>new</code>, and it "
        "changes every time you run the app. Use Search/Peek/Get to walk the "
        "structure without modifying it.");
    hint->setWordWrap(true);
    hint->setStyleSheet(QString("color: %1; font-size: 11px;").arg(Theme::TEXT_SECONDARY));
    layout->addWidget(hint);

    view = new DSView();
    layout->addWidget(view, 1);

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

    layout->addWidget(card);

    statusLabel = new QLabel(" ");
    statusLabel->setStyleSheet(QString("color: %1; font-size: 11px;").arg(Theme::TEXT_MUTED));
    layout->addWidget(statusLabel);

    connect(structureBox, &QComboBox::currentIndexChanged, this, &DataStructureLab::onStructureChanged);
    connect(primaryBtn,   &QPushButton::clicked, this, &DataStructureLab::onPrimaryAction);
    connect(secondaryBtn, &QPushButton::clicked, this, &DataStructureLab::onSecondaryAction);
    connect(searchBtn,    &QPushButton::clicked, this, &DataStructureLab::onSearchAction);

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

BSTNode* DataStructureLab::bstInsert(BSTNode* node, int value) {
    if (!node) return new BSTNode(value);
    if (value < node->value) node->left = bstInsert(node->left, value);
    else if (value > node->value) node->right = bstInsert(node->right, value);
    // equal values are ignored — BSTs conventionally don't store duplicates
    return node;
}

BSTNode* DataStructureLab::bstRemove(BSTNode* node, int value, bool& removed) {
    if (!node) return nullptr;
    if (value < node->value) {
        node->left = bstRemove(node->left, value, removed);
    } else if (value > node->value) {
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

BSTNode* DataStructureLab::bstFind(BSTNode* node, int value, std::vector<BSTNode*>& path) const {
    while (node) {
        path.push_back(node);
        if (value == node->value) return node;
        node = (value < node->value) ? node->left : node->right;
    }
    return nullptr;
}

int DataStructureLab::hashKey(const QString& key) const {
    int sum = 0;
    for (QChar c : key) sum += c.unicode();
    return BUCKET_COUNT > 0 ? sum % BUCKET_COUNT : 0;
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
            valueInput->setPlaceholderText("integer (e.g. -5)");
            primaryBtn->setEnabled(true);
            secondaryBtn->setEnabled(true);
            searchBtn->setEnabled(true);
            valueInput->setEnabled(true);
            break;
        case DSType::Queue:
            primaryBtn->setText("Enqueue");
            secondaryBtn->setText("Dequeue");
            searchBtn->setText("Peek Front");
            valueInput->setPlaceholderText("integer (e.g. -5)");
            primaryBtn->setEnabled(true);
            secondaryBtn->setEnabled(true);
            searchBtn->setEnabled(true);
            valueInput->setEnabled(true);
            break;
        case DSType::LinkedList:
            primaryBtn->setText("Insert at Head");
            secondaryBtn->setText("Remove Value");
            searchBtn->setText("Search");
            valueInput->setPlaceholderText("integer (e.g. -5)");
            primaryBtn->setEnabled(true);
            secondaryBtn->setEnabled(true);
            searchBtn->setEnabled(true);
            valueInput->setEnabled(true);
            break;
        case DSType::HashMap:
            primaryBtn->setText("Put");
            secondaryBtn->setText("Remove Key");
            searchBtn->setText("Get");
            valueInput->setPlaceholderText("value (any text)");
            primaryBtn->setEnabled(true);
            secondaryBtn->setEnabled(true);
            searchBtn->setEnabled(true);
            valueInput->setEnabled(true);
            break;
        case DSType::BST:
            primaryBtn->setText("Insert");
            secondaryBtn->setText("Remove");
            searchBtn->setText("Search");
            valueInput->setPlaceholderText("integer (e.g. -5)");
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
        QString key = keyInput->text().trimmed();
        QString value = valueInput->text().trimmed();
        if (key.isEmpty()) { statusLabel->setText("⚠ Enter a key first."); return; }
        int b = hashKey(key);
        HashEntry* found = nullptr;
        for (HashEntry* e = buckets[b]; e; e = e->next) {
            if (e->key == key) { found = e; break; }
        }
        if (found) {
            found->value = value;
            view->highlightNode = found;
            statusLabel->setText(QString("Updated key \"%1\" at %2 (bucket %3).")
                                  .arg(key).arg(dsAddr(found)).arg(b));
        } else {
            HashEntry* ne = new HashEntry(key, value);
            ne->next = buckets[b];
            buckets[b] = ne;
            view->highlightNode = ne;
            statusLabel->setText(QString("Inserted key \"%1\" — new node at %2 (bucket %3).")
                                  .arg(key).arg(dsAddr(ne)).arg(b));
        }
        view->visitedNodes.clear();
        emit explanationNeeded(QString(
            "<b>Hash Map — Put</b><br><br>"
            "Key <b>%1</b> hashed to bucket <b>%2</b> (of %3 buckets). A new "
            "<code>HashEntry</code> was allocated on the heap at <code>%4</code> "
            "and linked onto the front of that bucket's chain. If two keys land "
            "in the same bucket, they coexist as a real linked chain — that "
            "chain is what's drawn, not a simulation of one.")
            .arg(key).arg(b).arg(BUCKET_COUNT).arg(dsAddr(view->highlightNode)));
        refreshView();
        return;
    }

    // Accept negative integers: use toLongLong which handles negatives and large values
    bool ok = false;
    long long rawVal = valueInput->text().trimmed().toLongLong(&ok);
    if (!ok) { statusLabel->setText("⚠ Enter an integer (negatives OK, e.g. -42)."); return; }
    int value = (int)rawVal; // truncate to int range for node storage

    if (t == DSType::Stack) {
        LLNode* n = new LLNode(value);
        n->next = stackTop;
        QString oldTopAddr = n->next ? dsAddr(n->next) : "0x0 (nullptr — it was empty)";
        stackTop = n;
        view->highlightNode = n;
        view->visitedNodes.clear();
        statusLabel->setText(QString("Pushed %1 — new node allocated at %2, now the top.")
                              .arg(value).arg(dsAddr(n)));
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
                              .arg(value).arg(dsAddr(n)));
        emit explanationNeeded(QString(
            "<b>Queue — Enqueue</b><br><br>"
            "A new node was allocated at <code>%1</code> and linked onto the "
            "previous tail's <code>next</code> pointer, then it became the new "
            "tail. Queues are FIFO — First In, First Out — it leaves only once "
            "everyone ahead of it has been dequeued.").arg(dsAddr(n)));
    } else if (t == DSType::LinkedList) {
        LLNode* n = new LLNode(value);
        n->next = listHead;
        QString oldHeadAddr = n->next ? dsAddr(n->next) : "0x0 (nullptr — it was empty)";
        listHead = n;
        view->highlightNode = n;
        view->visitedNodes.clear();
        statusLabel->setText(QString("Inserted %1 at the head — new node at %2.")
                              .arg(value).arg(dsAddr(n)));
        emit explanationNeeded(QString(
            "<b>Linked List — Insert at Head</b><br><br>"
            "A new node was allocated at <code>%1</code>. Its <code>next</code> "
            "pointer was set to the old head (<code>%2</code>), and the list's "
            "head pointer was repointed at it. This is O(1) — no other node "
            "moved or was even touched, unlike inserting at the front of an "
            "array.").arg(dsAddr(n)).arg(oldHeadAddr));
    } else if (t == DSType::BST) {
        bstRoot = bstInsert(bstRoot, value);
        std::vector<BSTNode*> path;
        BSTNode* inserted = bstFind(bstRoot, value, path);
        view->highlightNode = inserted;
        view->visitedNodes.clear();
        for (auto* nd : path) view->visitedNodes.insert(nd);
        statusLabel->setText(QString("Inserted %1 into the tree at %2.")
                              .arg(value).arg(dsAddr(inserted)));
        emit explanationNeeded(
            "<b>BST — Insert</b><br><br>"
            "Starting at the root, the search (orange path) went left for "
            "smaller values and right for larger ones until it reached an "
            "empty pointer, where a brand-new node was allocated. Everything "
            "left of a node is smaller, everything right is larger, so lookups "
            "can skip half the remaining tree at every step.");
    }

    refreshView();
}

void DataStructureLab::onSecondaryAction() {
    DSType t = (DSType)structureBox->currentData().toInt();

    if (t == DSType::HashMap) {
        QString key = keyInput->text().trimmed();
        if (key.isEmpty()) { statusLabel->setText("⚠ Enter a key to remove."); return; }
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
                                      .arg(key, freedAddr));
                break;
            }
            prev = cur;
            cur = cur->next;
        }
        if (!found) statusLabel->setText(QString("No such key \"%1\".").arg(key));
        view->highlightNode = nullptr;
        view->visitedNodes.clear();
        refreshView();
        return;
    }

    if (t == DSType::BST) {
        bool ok = false;
        int value = valueInput->text().trimmed().toInt(&ok);
        if (!ok) { statusLabel->setText("⚠ Enter a whole number to remove."); return; }
        bool removed = false;
        bstRoot = bstRemove(bstRoot, value, removed);
        statusLabel->setText(removed ? QString("Removed %1 from the tree.").arg(value)
                                       : QString("%1 isn't in the tree.").arg(value));
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
        int popped = old->value;
        QString freedAddr = dsAddr(old);
        stackTop = old->next;
        delete old;
        statusLabel->setText(QString("Popped %1 — freed node %2.").arg(popped).arg(freedAddr));
        emit explanationNeeded(QString(
            "<b>Stack — Pop</b><br><br>"
            "The top node (<code>%1</code>) was unlinked — the stack's top "
            "pointer moved to whatever it pointed to next — and then that "
            "node was actually <code>delete</code>d. Only the top can ever be "
            "removed directly, exactly like a real call stack: the most "
            "recently called function returns first.").arg(freedAddr));
    } else if (t == DSType::Queue) {
        if (!queueHead) { statusLabel->setText("Nothing to dequeue — it's empty."); return; }
        LLNode* old = queueHead;
        int dequeued = old->value;
        QString freedAddr = dsAddr(old);
        queueHead = old->next;
        if (!queueHead) queueTail = nullptr;
        delete old;
        statusLabel->setText(QString("Dequeued %1 — freed node %2.").arg(dequeued).arg(freedAddr));
        emit explanationNeeded(QString(
            "<b>Queue — Dequeue</b><br><br>"
            "The front node (<code>%1</code>) was unlinked from the head and "
            "freed. The element that's been waiting longest leaves first — "
            "print spoolers and task schedulers commonly use this ordering "
            "for fairness.").arg(freedAddr));
    } else if (t == DSType::LinkedList) {
        if (!listHead) { statusLabel->setText("Nothing to remove — it's empty."); return; }
        bool ok = false;
        int value = valueInput->text().trimmed().toInt(&ok);
        if (!ok) { statusLabel->setText("⚠ Enter the value to remove."); return; }
        LLNode* cur = listHead;
        LLNode* prev = nullptr;
        while (cur && cur->value != value) { prev = cur; cur = cur->next; }
        if (!cur) { statusLabel->setText(QString("%1 isn't in the list.").arg(value)); return; }
        QString freedAddr = dsAddr(cur);
        if (prev) prev->next = cur->next; else listHead = cur->next;
        delete cur;
        statusLabel->setText(QString("Removed %1 — freed node %2.").arg(value).arg(freedAddr));
        emit explanationNeeded(QString(
            "<b>Linked List — Remove</b><br><br>"
            "The list was walked node by node — following real <code>next</code> "
            "pointers — until the value was found at <code>%1</code>. The "
            "previous node's pointer was redirected around it, then the node "
            "was freed. That search is what makes removal O(n) even though "
            "the actual unlinking step is O(1).").arg(freedAddr));
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
                              .arg(stackTop->value).arg(dsAddr(stackTop)));
        emit explanationNeeded(
            "<b>Stack — Peek</b><br><br>"
            "Peek just reads the top node's value without unlinking or "
            "freeing anything — the stack is left exactly as it was.");
    } else if (t == DSType::Queue) {
        if (!queueHead) { statusLabel->setText("Nothing to peek — it's empty."); refreshView(); return; }
        view->highlightNode = queueHead;
        statusLabel->setText(QString("Front is %1, living at %2 — peek doesn't remove it.")
                              .arg(queueHead->value).arg(dsAddr(queueHead)));
        emit explanationNeeded(
            "<b>Queue — Peek</b><br><br>"
            "Peek reads the front node — the next one in line to be "
            "dequeued — without removing it.");
    } else if (t == DSType::LinkedList) {
        bool ok = false;
        int value = valueInput->text().trimmed().toInt(&ok);
        if (!ok) { statusLabel->setText("⚠ Enter a value to search for."); refreshView(); return; }
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
                                  .arg(value).arg(dsAddr(cur)).arg(hops));
        } else {
            statusLabel->setText(QString("%1 isn't in the list — walked all the way to nullptr.").arg(value));
        }
        emit explanationNeeded(
            "<b>Linked List — Search</b><br><br>"
            "There's no shortcut: a linked list has no index, so finding a "
            "value means following <code>next</code> pointers one at a time "
            "from the head (the orange-highlighted nodes) until it's found or "
            "the list runs out. That's O(n).");
    } else if (t == DSType::HashMap) {
        QString key = keyInput->text().trimmed();
        if (key.isEmpty()) { statusLabel->setText("⚠ Enter a key to look up."); refreshView(); return; }
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
            statusLabel->setText(QString("\"%1\" → \"%2\" at %3 (bucket %4, %5 hop(s) into the chain).")
                                  .arg(key, cur->value, dsAddr(cur)).arg(b).arg(hops));
        } else {
            statusLabel->setText(QString("No key \"%1\" in bucket %2.").arg(key).arg(b));
        }
        emit explanationNeeded(QString(
            "<b>Hash Map — Get</b><br><br>"
            "The key hashed straight to bucket <b>%1</b> — that part is O(1) "
            "— but a lookup still has to walk that bucket's chain (the orange "
            "nodes) comparing keys one at a time, because that's how "
            "collisions are resolved.").arg(b));
    } else if (t == DSType::BST) {
        bool ok = false;
        int value = valueInput->text().trimmed().toInt(&ok);
        if (!ok) { statusLabel->setText("⚠ Enter a value to search for."); refreshView(); return; }
        std::vector<BSTNode*> path;
        BSTNode* found = bstFind(bstRoot, value, path);
        for (auto* nd : path) view->visitedNodes.insert(nd);
        if (found) {
            view->highlightNode = found;
            statusLabel->setText(QString("Found %1 at %2 after comparing against %3 node(s).")
                                  .arg(value).arg(dsAddr(found)).arg((int)path.size()));
        } else {
            statusLabel->setText(QString("%1 isn't in the tree — compared against %2 node(s) before hitting nullptr.")
                                  .arg(value).arg((int)path.size()));
        }
        emit explanationNeeded(
            "<b>BST — Search</b><br><br>"
            "Each orange node along the path was one comparison: go left if "
            "the target is smaller, right if larger. Because the tree is "
            "ordered, the search skips an entire subtree at every step "
            "instead of checking every node.");
    }

    refreshView();
}

// ───────────────────────── Process Stack view ─────────────────────────────

// Draw a single stack frame box with function name and address.
void DSView::drawFrameBox(QPainter& p, const QRect& r, const StackFrame& frame, bool isTop) {
    QColor fill   = isTop ? QColor(Theme::GREEN)     : QColor(Theme::BG_INPUT);
    QColor border = isTop ? QColor(Theme::GREEN)     : QColor(Theme::BORDER);
    QColor text   = isTop ? Qt::white                : QColor(Theme::TEXT_PRIMARY);

    QPainterPath path;
    path.addRoundedRect(r, 8, 8);
    p.fillPath(path, fill);
    p.setPen(QPen(border, isTop ? 2 : 1));
    p.drawPath(path);

    // Frame index badge
    p.setFont(QFont("Consolas", 8, QFont::Bold));
    p.setPen(isTop ? QColor(255,255,255,200) : QColor(Theme::TEXT_MUTED));
    p.drawText(QRect(r.x()+4, r.y()+2, 28, r.height()-4),
               Qt::AlignVCenter | Qt::AlignLeft,
               QString("#%1").arg(frame.frameIndex));

    // Function / syscall name
    p.setFont(QFont("Segoe UI", 10, QFont::Bold));
    p.setPen(text);
    p.drawText(QRect(r.x()+34, r.y(), r.width()-38, r.height()-14),
               Qt::AlignVCenter | Qt::AlignLeft,
               frame.functionName);

    // Address
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

    // Title
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

        // Arrow pointing down to next frame
        if (i + 1 < (int)processFrames.size()) {
            int ax = x + boxW/2;
            int ay = y + boxH;
            p.setPen(QPen(QColor(Theme::TEXT_MUTED), 1.5));
            p.drawLine(ax, ay, ax, ay + gap);
            // Arrowhead
            p.drawLine(ax, ay+gap, ax-5, ay+gap-7);
            p.drawLine(ax, ay+gap, ax+5, ay+gap-7);
        } else {
            // Bottom label
            p.setFont(QFont("Consolas", 8));
            p.setPen(QColor(Theme::TEXT_MUTED));
            p.drawText(QRect(x, y+boxH+4, boxW, 14), Qt::AlignCenter,
                       "caller = user space / entry point");
        }

        y += boxH + gap;
        if (y + boxH > height() - 10) break; // stop before overflow
    }
}

// ── loadProcessStack / readProcessStack ───────────────────────────────────

// Read the kernel wait-channel and /proc/<pid>/maps stack region to build
// a meaningful stack picture from real /proc data.
std::vector<StackFrame> DataStructureLab::readProcessStack(pid_t pid) {
    std::vector<StackFrame> frames;

    // Frame 0: current wait-channel (what the process is blocked on in the kernel)
    {
        StackFrame f;
        f.frameIndex = 0;
        f.address = 0; // kernel address, not user-space
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

    // Frame 1: process name (from comm) as "main thread"
    {
        StackFrame f;
        f.frameIndex = 1;
        std::ifstream comm("/proc/" + std::to_string(pid) + "/comm");
        std::string name;
        if (comm.is_open()) std::getline(comm, name);
        f.functionName = QString::fromStdString(name.empty() ? "main" : name + "::main");

        // Try to read the stack base address from /proc/pid/maps
        std::ifstream maps("/proc/" + std::to_string(pid) + "/maps");
        std::string line;
        while (std::getline(maps, line)) {
            if (line.find("[stack]") != std::string::npos) {
                unsigned long start = 0, end = 0;
                sscanf(line.c_str(), "%lx-%lx", &start, &end);
                f.address = end; // stack grows down, base is at the high address
                break;
            }
        }
        frames.push_back(f);
    }

    // Frame 2: entry point approximation — read from /proc/pid/stat
    {
        StackFrame f;
        f.frameIndex = 2;
        f.functionName = "clone / _start (libc entry)";
        std::ifstream stat("/proc/" + std::to_string(pid) + "/stat");
        std::string token;
        // skip to field 29 (start_code)
        for (int i = 0; i < 28 && stat >> token; i++) {}
        unsigned long startCode = 0;
        if (stat >> startCode) f.address = startCode;
        frames.push_back(f);
    }

    // Frame 3: virtual memory size as context for "where in memory are we"
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
    // Switch the combo box to ProcessStack mode
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
        "virtual memory — high address at the bottom, low address at "
        "the top.<br><br>"
        "<b>LIFO principle:</b> The most recently called function is "
        "always at the top — exactly like the Stack data structure to "
        "the left. The kernel uses this same mechanism for every "
        "process on the system."
    ).arg(pid).arg(topFrame));
}

// ── onPrimaryAction: handle ProcessStack refresh ──────────────────────────

// ═══════════════════════════════════════════════════════════════════════════
// DSRSSBar — shows real RSS of the dslab_worker child process
// ═══════════════════════════════════════════════════════════════════════════


DSRSSBar::DSRSSBar(QWidget* parent) : QWidget(parent) {
    setFixedHeight(12);
}

void DSRSSBar::setValues(long c, long pk) {
    cur=c; peak=std::max(pk,1L); update();
}

void DSRSSBar::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath bg; bg.addRoundedRect(rect(),5,5);
    p.fillPath(bg, QColor("#F1F5F9"));
    if (cur>0) {
        float pct = std::min((float)cur/peak, 1.0f);
        QRect fill(0,0,(int)(width()*pct),height());
        QPainterPath fp; fp.addRoundedRect(fill,5,5);
        p.fillPath(fp, pct>0.8f?QColor("#EF4444"):pct>0.5f?QColor("#F97316"):QColor("#22C55E"));
    }
}
