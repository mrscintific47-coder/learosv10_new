#include <QApplication>
#include <QFont>
#include <QPalette>
#include <csignal>
#include <unistd.h>
#include <sys/shm.h>
#include "CleanupRegistry.h"
#include "MainWindow.h"

// ── Crash-safe cleanup ─────────────────────────────────────────────────────────
// Resources are tracked in fixed-size C arrays (not std::vector) so the signal
// handler can iterate them safely.  std::vector internals (size, capacity, data
// pointer) are NOT async-signal-safe — if SIGSEGV fires in the middle of a
// push_back() that is reallocating the buffer, the handler would read a
// half-updated data pointer and crash inside the crash handler.
//
// Instead:
//   • Arrays are statically allocated — no heap involved.
//   • The "live count" is a volatile sig_atomic_t, which the C standard
//     guarantees can be read atomically from a signal handler.
//   • The normal-path add/remove functions run only on the Qt main thread,
//     so they never race with each other.  Only the signal handler races with
//     them, and it only reads (never writes) the arrays.
// ─────────────────────────────────────────────────────────────────────────────

// LearnOSCleanup globals and shadow-array functions are defined in
// CleanupRegistry.cpp so that test executables can link them without
// pulling in this file.  Extern declarations come via CleanupRegistry.h.

static void crashHandler(int sig) {
    // ── Read counts once before the loop — the counts are sig_atomic_t so
    // reading them is always atomic. The arrays themselves are not being
    // modified by anyone else (the Qt thread is dead — it's the one that
    // crashed), so iterating them is safe.
    int nPids  = LearnOSCleanup::_pidCount;
    int nShms  = LearnOSCleanup::_shmCount;
    int nSocks = LearnOSCleanup::_sockCount;

    for (int i = 0; i < nPids; i++) {
        pid_t p = LearnOSCleanup::_pids[i];
        if (p > 0) kill(p, SIGKILL);
    }
    for (int i = 0; i < nShms; i++) {
        int id = LearnOSCleanup::_shms[i];
        if (id >= 0) shmctl(id, IPC_RMID, nullptr);
    }
    for (int i = 0; i < nSocks; i++) {
        // Cast away volatile for unlink — async-signal-safe, takes const char*
        unlink(const_cast<const char*>(
            reinterpret_cast<const volatile char*>(LearnOSCleanup::_socks[i])));
    }

    // Re-raise so the process exits with the correct status and a core dump
    // is still generated if the kernel is configured to do so.
    struct sigaction sa{};
    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sigaction(sig, &sa, nullptr);
    raise(sig);
}

static void installCrashHandlers() {
    struct sigaction sa{};
    sa.sa_handler = crashHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESETHAND; // restore default after first invocation
    for (int sig : {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS, SIGTERM}) {
        sigaction(sig, &sa, nullptr);
    }
}

int main(int argc, char* argv[]) {
    installCrashHandlers();

    QApplication app(argc, argv);

    // Clean light palette
    app.setStyle("Fusion");

    QPalette light;
    light.setColor(QPalette::Window,          QColor("#F8F9FE"));
    light.setColor(QPalette::WindowText,      QColor("#0F172A"));
    light.setColor(QPalette::Base,            QColor("#FFFFFF"));
    light.setColor(QPalette::AlternateBase,   QColor("#F8F9FE"));
    light.setColor(QPalette::Text,            QColor("#0F172A"));
    light.setColor(QPalette::Button,          QColor("#F1F5F9"));
    light.setColor(QPalette::ButtonText,      QColor("#0F172A"));
    light.setColor(QPalette::Highlight,       QColor("#4F6EF7"));
    light.setColor(QPalette::HighlightedText, QColor("#FFFFFF"));
    light.setColor(QPalette::ToolTipBase,     QColor("#FFFFFF"));
    light.setColor(QPalette::ToolTipText,     QColor("#0F172A"));
    light.setColor(QPalette::PlaceholderText, QColor("#94A3B8"));
    app.setPalette(light);

    QFont font("Segoe UI", 10);
    font.setHintingPreference(QFont::PreferFullHinting);
    app.setFont(font);

    MainWindow window;
    window.show();
    return app.exec();
}
