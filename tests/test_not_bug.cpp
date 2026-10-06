#include <QtTest/QtTest>
#include <QString>
#include "../src/DataStructureLab.h"

static QString vmRun(const QString& src, LLNode* head=nullptr, BSTNode* bst=nullptr) {
    DSTinyVM vm;
    vm.headPtr = head; vm.rootPtr = bst;
    QStringList lines;
    vm.onLog          = [&](const QString& m) { lines << m; };
    vm.onHighlight    = [](void*) {};
    vm.onVisited      = [](void*) {};
    vm.onClearVisited = []() {};
    QString err = vm.run(src);
    if (!err.isEmpty()) return "ERROR: " + err;
    return lines.join("\n");
}

class TestNotBug : public QObject {
    Q_OBJECT
private slots:
    void notAsUnary() {
        // 'not' as a statement-level unary works fine because Ident followed by
        // a non-call token falls through to parseUnary, but only if tokenizer
        // emits TK::Not for 'not'. Currently it does — so standalone:
        QCOMPARE(vmRun("var x=false; var y=not x; log(y)"), QString("true"));
    }
    void notInsideCall() {
        // Bug: 'not false' inside log() call — 'not' is tokenized as TK::Not
        // but parsePrimary() sees TK::Not which is not in its switch, so it
        // throws "unexpected token 'not'".
        QString r = vmRun("log(not false)");
        qDebug() << "log(not false) =>" << r;
        // This should log "true" but currently errors
        QEXPECT_FAIL("", "not inside call args is broken", Continue);
        QCOMPARE(r, QString("true"));
    }
    void notInIfCondition() {
        // 'not' in if/while condition (inside parens) — same issue
        QString r = vmRun("if (not false) { log(\"yes\") }");
        qDebug() << "if(not false) =>" << r;
        QEXPECT_FAIL("", "not in condition is broken", Continue);
        QCOMPARE(r, QString("yes"));
    }
    void elifAlias() {
        // elif alias — parsed as TK::Else which always triggers else, not else-if
        QString r = vmRun("var x=2; if(x==1){log(\"one\")} elif(x==2){log(\"two\")} else{log(\"other\")}");
        qDebug() << "elif =>" << r;
        // Should print "two"
        QEXPECT_FAIL("", "elif condition is not evaluated", Continue);
        QCOMPARE(r, QString("two"));
    }
};

QTEST_APPLESS_MAIN(TestNotBug)
#include "test_not_bug.moc"
