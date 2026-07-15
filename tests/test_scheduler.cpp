// Scheduler logic unit tests — pure C++, no kernel or Qt widgets needed.
#include <QtTest/QtTest>
#include <vector>
#include <deque>

class TestScheduler : public QObject {
    Q_OBJECT

    struct Proc { int pid; int burstLeft; int state; }; // 0=ready 1=running 2=done

    static bool allDone(const std::vector<Proc>& procs) {
        for (auto& p : procs) if (p.state != 2) return false;
        return true;
    }
    static int stepFCFS(std::vector<Proc>& procs, std::deque<int>& q) {
        if (q.empty()) return -1;
        auto& p = procs[q.front()];
        p.state = 1; p.burstLeft--;
        if (p.burstLeft <= 0) { p.state = 2; q.pop_front(); }
        return p.pid;
    }
    static int stepRR(std::vector<Proc>& procs, std::deque<int>& q,
                      int quantum, int& rrTick) {
        while (!q.empty() && procs[q.front()].state == 2) q.pop_front();
        if (q.empty()) return -1;
        int idx = q.front();
        auto& p = procs[idx];
        p.state = 1; p.burstLeft--; rrTick++;
        if (p.burstLeft <= 0) {
            p.state = 2; q.pop_front(); rrTick = 0;
        } else if (rrTick >= quantum) {
            q.pop_front(); q.push_back(idx);
            p.state = 0; rrTick = 0;
        }
        return p.pid;
    }

private slots:

    void fcfs_order() {
        std::vector<Proc> procs = {{1,3,0},{2,2,0}};
        std::deque<int>   q     = {0,1};
        QCOMPARE(stepFCFS(procs,q), 1);
        QCOMPARE(stepFCFS(procs,q), 1);
        QCOMPARE(stepFCFS(procs,q), 1);
        QCOMPARE(stepFCFS(procs,q), 2);
        QCOMPARE(stepFCFS(procs,q), 2);
        QVERIFY(allDone(procs));
        QCOMPARE(stepFCFS(procs,q), -1);
    }

    void fcfs_total_ticks() {
        const std::vector<int> bursts = {5, 3, 7, 1, 4};
        int totalBurst = 0;
        std::vector<Proc> procs; std::deque<int> q;
        for (int i = 0; i < (int)bursts.size(); ++i) {
            procs.push_back({i+1, bursts[i], 0}); q.push_back(i);
            totalBurst += bursts[i];
        }
        int ticks = 0;
        while (!allDone(procs)) { stepFCFS(procs, q); ticks++; }
        QCOMPARE(ticks, totalBurst);
    }

    void rr_quantum_respected() {
        std::vector<Proc> procs = {{1,4,0},{2,4,0}};
        std::deque<int>   q     = {0,1};
        int t = 0;
        // Expected: 1,1,2,2,1,1,2,2
        QCOMPARE(stepRR(procs,q,2,t), 1);
        QCOMPARE(stepRR(procs,q,2,t), 1);
        QCOMPARE(stepRR(procs,q,2,t), 2);
        QCOMPARE(stepRR(procs,q,2,t), 2);
        QCOMPARE(stepRR(procs,q,2,t), 1);
        QCOMPARE(stepRR(procs,q,2,t), 1);
        QCOMPARE(stepRR(procs,q,2,t), 2);
        QCOMPARE(stepRR(procs,q,2,t), 2);
        QVERIFY(allDone(procs));
    }

    void rr_total_ticks() {
        const std::vector<int> bursts  = {6, 4, 3, 8};
        int totalBurst = 0;
        std::vector<Proc> procs; std::deque<int> q;
        for (int i = 0; i < (int)bursts.size(); ++i) {
            procs.push_back({i+1, bursts[i], 0}); q.push_back(i);
            totalBurst += bursts[i];
        }
        int ticks = 0, rrTick = 0;
        while (!allDone(procs)) {
            stepRR(procs, q, 3, rrTick);
            if (++ticks > totalBurst * 3) { QFAIL("infinite loop"); break; }
        }
        QCOMPARE(ticks, totalBurst);
    }

    void rr_short_no_preempt() {
        std::vector<Proc> procs = {{1,1,0},{2,4,0}};
        std::deque<int>   q     = {0,1};
        int t = 0;
        QCOMPARE(stepRR(procs,q,3,t), 1); // finishes in 1 tick
        QVERIFY(procs[0].state == 2);
    }
};

QTEST_APPLESS_MAIN(TestScheduler)
#include "test_scheduler.moc"
