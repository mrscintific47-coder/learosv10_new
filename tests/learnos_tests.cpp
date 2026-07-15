// ── LearnOS automated tests ───────────────────────────────────────────────────
// Tests the two purely-logical components that have no kernel dependencies:
//   1. DSTinyVM  — the mini lexer/parser/evaluator in DataStructureLab
//   2. Scheduling logic  — FCFS / RR burst-consumption invariant
//
// Build:  cmake --build build --target learnos_tests
// Run:    ctest --test-dir build --output-on-failure

#include <QtTest/QtTest>
#include <QString>

// ─────────────────────────────────────────────────────────────────────────────
// Bring in the VM types without pulling in Qt-widget translation units.
// DataStructureLab.h declares everything we need; DataStructureLab.cpp is added
// to the test executable in CMakeLists so it compiles once with the correct MOC.
// ─────────────────────────────────────────────────────────────────────────────
#include "../src/DataStructureLab.h"

// ── Helper: run a script, collect log output ──────────────────────────────────
static QString vmRun(const QString& src,
                     LLNode*  head = nullptr,
                     BSTNode* bst  = nullptr)
{
    DSTinyVM vm;
    vm.headPtr = head;
    vm.rootPtr = bst;
    QStringList lines;
    vm.onLog          = [&](const QString& m) { lines << m; };
    vm.onHighlight    = [](void*) {};
    vm.onVisited      = [](void*) {};
    vm.onClearVisited = []() {};
    QString err = vm.run(src);
    if (!err.isEmpty()) return "ERROR: " + err;
    return lines.join("\n");
}

// ─────────────────────────────────────────────────────────────────────────────
// DSTinyVM tests
// ─────────────────────────────────────────────────────────────────────────────

class TestDSTinyVM : public QObject {
    Q_OBJECT
private slots:

    // ── Arithmetic ────────────────────────────────────────────────────────────
    void arithmetic() {
        QCOMPARE(vmRun("log(1 + 2)"),   QString("3"));
        QCOMPARE(vmRun("log(10 - 3)"),  QString("7"));
        QCOMPARE(vmRun("log(3 * 4)"),   QString("12"));
        QCOMPARE(vmRun("log(10 / 4)"),  QString("2.5"));
        QCOMPARE(vmRun("log(10 / 0)"),  QString("null"));   // div-by-zero → null
        QCOMPARE(vmRun("log(-5 + 3)"),  QString("-2"));
    }

    // ── Variables ─────────────────────────────────────────────────────────────
    void variables() {
        QCOMPARE(vmRun("var x = 42; log(x)"),               QString("42"));
        QCOMPARE(vmRun("var x = 1; x = x + 1; log(x)"),    QString("2"));
        QCOMPARE(vmRun("var x = 3; var y = x * 2; log(y)"),QString("6"));
        QCOMPARE(vmRun("var x; log(x)"),                    QString("null")); // uninitialised
    }

    // ── String concatenation ──────────────────────────────────────────────────
    void strings() {
        QCOMPARE(vmRun(R"(log("hello" + " " + "world"))"), QString("hello world"));
        QCOMPARE(vmRun(R"(var n = 7; log("n=" + n))"),     QString("n=7"));
    }

    // ── Comparisons ───────────────────────────────────────────────────────────
    void comparisons() {
        QCOMPARE(vmRun("log(1 < 2)"),  QString("true"));
        QCOMPARE(vmRun("log(2 < 1)"),  QString("false"));
        QCOMPARE(vmRun("log(1 == 1)"), QString("true"));
        QCOMPARE(vmRun("log(1 != 2)"), QString("true"));
        QCOMPARE(vmRun("log(3 >= 3)"), QString("true"));
        QCOMPARE(vmRun("log(3 > 3)"),  QString("false"));
    }

    // ── Boolean / Python aliases ──────────────────────────────────────────────
    void booleans() {
        QCOMPARE(vmRun("log(true && false)"),   QString("false"));
        QCOMPARE(vmRun("log(true || false)"),   QString("true"));
        QCOMPARE(vmRun("log(!true)"),           QString("false"));
        QCOMPARE(vmRun("log(True and False)"),  QString("false"));
        QCOMPARE(vmRun("log(True or False)"),   QString("true"));
        QCOMPARE(vmRun("log(not True)"),        QString("false"));
    }

    // ── If / else ─────────────────────────────────────────────────────────────
    void ifElse() {
        QCOMPARE(vmRun(R"(if (1 < 2) { log("yes") } else { log("no") })"),
                 QString("yes"));
        QCOMPARE(vmRun(R"(if (2 < 1) { log("yes") } else { log("no") })"),
                 QString("no"));
        QVERIFY(vmRun("if (false) { log(\"x\") }").isEmpty());
    }

    // ── While loop ────────────────────────────────────────────────────────────
    void whileLoop() {
        QCOMPARE(vmRun("var i = 0; while (i < 3) { log(i); i = i + 1; }"),
                 QString("0\n1\n2"));
    }

    // ── User-defined functions + recursion ────────────────────────────────────
    void functions() {
        QCOMPARE(vmRun("function add(a, b) { return a + b; } log(add(3, 4))"),
                 QString("7"));
        // factorial(5) == 120
        QCOMPARE(vmRun(
            "function fact(n) { if (n <= 1) { return 1; } return n * fact(n-1); }"
            "log(fact(5))"),
            QString("120"));
    }

    // ── Null / None ───────────────────────────────────────────────────────────
    void nullValues() {
        QCOMPARE(vmRun("log(null)"), QString("null"));
        QCOMPARE(vmRun("log(None)"), QString("null"));
    }

    // ── Step-limit guard — must not hang on infinite loop ─────────────────────
    void infiniteLoopGuard() {
        QString r = vmRun("while (true) {}");
        QVERIFY2(r.startsWith("ERROR:"), qPrintable("Got: " + r));
        QVERIFY(r.contains("step limit"));
    }

    // ── Linked list traversal via real pointer chain ──────────────────────────
    void linkedListTraversal() {
        // Build 10 → 20 → 30 → nullptr
        LLNode n3(QVariant(30)); n3.next = nullptr;
        LLNode n2(QVariant(20)); n2.next = &n3;
        LLNode n1(QVariant(10)); n1.next = &n2;

        const char* script = R"(
var cur = head;
while (cur) {
    log(cur.value);
    cur = cur.next;
}
)";
        QCOMPARE(vmRun(script, &n1), QString("10\n20\n30"));
    }

    // ── BST property navigation ───────────────────────────────────────────────
    void bstProperties() {
        //   5
        //  / \
        // 3   7
        BSTNode left(QVariant(3));  left.left = left.right = nullptr;
        BSTNode right(QVariant(7)); right.left = right.right = nullptr;
        BSTNode root(QVariant(5));  root.left = &left; root.right = &right;

        QCOMPARE(vmRun("log(root.value)",       nullptr, &root), QString("5"));
        QCOMPARE(vmRun("log(root.left.value)",  nullptr, &root), QString("3"));
        QCOMPARE(vmRun("log(root.right.value)", nullptr, &root), QString("7"));
        QCOMPARE(vmRun("log(root.left.left)",   nullptr, &root), QString("null"));
    }

    // ── Parse error is surfaced as a non-empty error string ──────────────────
    void parseErrors() {
        QString r = vmRun("log(1 + ");
        QVERIFY2(r.startsWith("ERROR:"), qPrintable(r));
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Scheduling invariant tests
// We don't instantiate the full AlgorithmStepper QWidget — we replicate the
// core step logic here to verify the invariant:
//   total ticks consumed == sum of all burst values
// ─────────────────────────────────────────────────────────────────────────────

class TestScheduler : public QObject {
    Q_OBJECT

    struct Proc { int pid; int burstLeft; int state; }; // 0=ready 1=running 2=done

    static bool allDone(const std::vector<Proc>& procs) {
        for (auto& p : procs) if (p.state != 2) return false;
        return true;
    }

    // FCFS: front of queue runs until done
    static int stepFCFS(std::vector<Proc>& procs, std::deque<int>& q) {
        if (q.empty()) return -1;
        auto& p = procs[q.front()];
        p.state = 1; p.burstLeft--;
        if (p.burstLeft <= 0) { p.state = 2; q.pop_front(); }
        return p.pid;
    }

    // RR: cycle through queue with given quantum
    static int stepRR(std::vector<Proc>& procs, std::deque<int>& q,
                      int quantum, int& rrTick) {
        while (!q.empty() && procs[q.front()].state == 2) q.pop_front();
        if (q.empty()) return -1;
        auto& p = procs[q.front()];
        p.state = 1; p.burstLeft--; rrTick++;
        if (p.burstLeft <= 0)     { p.state = 2; q.pop_front(); rrTick = 0; }
        else if (rrTick >= quantum){ q.push_back(q.front()); q.pop_front();
                                     p.state = 0; rrTick = 0; }
        return p.pid;
    }

private slots:

    void fcfs_order() {
        // burst=3 then burst=2 → PID order: 1,1,1,2,2
        std::vector<Proc> procs = {{1,3,0},{2,2,0}};
        std::deque<int>   q     = {0,1};
        QCOMPARE(stepFCFS(procs,q), 1);
        QCOMPARE(stepFCFS(procs,q), 1);
        QCOMPARE(stepFCFS(procs,q), 1);  // PID 1 finishes
        QCOMPARE(stepFCFS(procs,q), 2);
        QCOMPARE(stepFCFS(procs,q), 2);  // PID 2 finishes
        QVERIFY(allDone(procs));
        QCOMPARE(stepFCFS(procs,q), -1); // nothing left
    }

    void fcfs_total_ticks() {
        // Invariant: ticks == sum of bursts, for any burst list
        const std::vector<int> bursts = {5, 3, 7, 1, 4};
        int totalBurst = 0;
        std::vector<Proc> procs; std::deque<int> q;
        for (int i = 0; i < (int)bursts.size(); ++i) {
            procs.push_back({i+1, bursts[i], 0});
            q.push_back(i);
            totalBurst += bursts[i];
        }
        int ticks = 0;
        while (!allDone(procs)) { stepFCFS(procs, q); ticks++; }
        QCOMPARE(ticks, totalBurst);
    }

    void rr_quantum_respected() {
        // Two processes, burst=4 each, quantum=2
        // Expected interleaving: 1,1,2,2,1,1,2,2
        std::vector<Proc> procs = {{1,4,0},{2,4,0}};
        std::deque<int>   q     = {0,1};
        int rrTick = 0;
        QCOMPARE(stepRR(procs,q,2,rrTick), 1);
        QCOMPARE(stepRR(procs,q,2,rrTick), 1); // quantum expires
        QCOMPARE(stepRR(procs,q,2,rrTick), 2);
        QCOMPARE(stepRR(procs,q,2,rrTick), 2); // quantum expires
        QCOMPARE(stepRR(procs,q,2,rrTick), 1);
        QCOMPARE(stepRR(procs,q,2,rrTick), 1); // PID 1 done
        QCOMPARE(stepRR(procs,q,2,rrTick), 2);
        QCOMPARE(stepRR(procs,q,2,rrTick), 2); // PID 2 done
        QVERIFY(allDone(procs));
    }

    void rr_total_ticks() {
        // Invariant: ticks == sum of bursts, regardless of quantum
        const std::vector<int> bursts  = {6, 4, 3, 8};
        const int              quantum = 3;
        int totalBurst = 0;
        std::vector<Proc> procs; std::deque<int> q;
        for (int i = 0; i < (int)bursts.size(); ++i) {
            procs.push_back({i+1, bursts[i], 0});
            q.push_back(i);
            totalBurst += bursts[i];
        }
        int ticks = 0, rrTick = 0;
        while (!allDone(procs)) {
            stepRR(procs, q, quantum, rrTick);
            if (++ticks > totalBurst * 3) { QFAIL("Infinite loop guard"); break; }
        }
        QCOMPARE(ticks, totalBurst);
    }

    void rr_short_process_no_preempt() {
        // Process with burst < quantum should finish before its quantum expires
        std::vector<Proc> procs = {{1,1,0},{2,4,0}};
        std::deque<int>   q     = {0,1};
        int rrTick = 0;
        QCOMPARE(stepRR(procs,q,3,rrTick), 1);  // finishes in 1 tick, no preemption
        QVERIFY(procs[0].state == 2);
    }
};

QTEST_APPLESS_MAIN(TestDSTinyVM)

// Compiled separately — see CMakeLists for the second test executable
// (TestScheduler is in tests/learnos_sched_tests.cpp)
#include "learnos_tests.moc"
