#pragma once
#include <QString>

// LearnOS — Design System v2
// Single source of truth for every color, spacing, and component style.
namespace Theme {

    // ── Backgrounds ──────────────────────────────────────────────────────────
    inline const char* BG_APP      = "#ECEEF5";   // slightly blue-grey — not pure white
    inline const char* BG_CARD     = "#FFFFFF";
    inline const char* BG_SIDEBAR  = "#181C2E";   // deeper navy
    inline const char* BG_INPUT    = "#F4F6FB";

    // ── Brand / accent ───────────────────────────────────────────────────────
    inline const char* BLUE        = "#4F6EF7";
    inline const char* BLUE_DARK   = "#3B5BF6";
    inline const char* BLUE_LIGHT  = "#EEF2FF";
    inline const char* GREEN       = "#16A34A";
    inline const char* GREEN_LIGHT = "#DCFCE7";
    inline const char* ORANGE      = "#EA580C";
    inline const char* ORANGE_LIGHT= "#FFF7ED";
    inline const char* RED         = "#DC2626";
    inline const char* RED_LIGHT   = "#FEF2F2";
    inline const char* PURPLE      = "#7C3AED";
    inline const char* PURPLE_LIGHT= "#F5F3FF";
    inline const char* YELLOW      = "#CA8A04";
    inline const char* YELLOW_LIGHT= "#FEFCE8";
    inline const char* TEAL        = "#0D9488";
    inline const char* TEAL_LIGHT  = "#F0FDFA";

    // ── Text ─────────────────────────────────────────────────────────────────
    inline const char* TEXT_PRIMARY   = "#0F172A";
    inline const char* TEXT_SECONDARY = "#475569";
    inline const char* TEXT_MUTED     = "#94A3B8";
    inline const char* TEXT_WHITE     = "#FFFFFF";

    // ── Borders ──────────────────────────────────────────────────────────────
    inline const char* BORDER       = "#E2E8F0";
    inline const char* BORDER_FOCUS = "#4F6EF7";

    // ── Sidebar text (used on dark sidebar) ─────────────────────────────────
    inline const char* SIDEBAR_TEXT       = "#CBD5E1";
    inline const char* SIDEBAR_TEXT_MUTED = "#64748B";

    // ── Component helpers ────────────────────────────────────────────────────

    // Standard white card — subtle drop-shadow via border trick
    inline QString card(const char* extra = "") {
        return QString(
            "background: #FFFFFF;"
            "border-radius: 12px;"
            "border: 1px solid #E2E8F0;"
        ) + extra;
    }

    // Accent card — coloured left border, tinted bg (use for banners / hints)
    inline QString accentCard(const char* borderColor, const char* bgColor) {
        return QString(
            "background: %1;"
            "border-radius: 10px;"
            "border: 1px solid #E2E8F0;"
            "border-left: 4px solid %2;"
        ).arg(bgColor).arg(borderColor);
    }

    // ── Buttons ──────────────────────────────────────────────────────────────

    inline QString btnPrimary() {
        return
            "QPushButton {"
            "  background: #4F6EF7; color: white; border: none;"
            "  border-radius: 8px; padding: 8px 18px;"
            "  font-size: 12px; font-weight: 700; letter-spacing: .01em; }"
            "QPushButton:hover   { background: #3B5BF6; }"
            "QPushButton:pressed { background: #2D4AE8; }"
            "QPushButton:disabled{ background: #C7D2FE; color: #E0E7FF; }";
    }
    inline QString btnSuccess() {
        return
            "QPushButton {"
            "  background: #16A34A; color: white; border: none;"
            "  border-radius: 8px; padding: 8px 18px;"
            "  font-size: 12px; font-weight: 700; }"
            "QPushButton:hover   { background: #15803D; }"
            "QPushButton:pressed { background: #166534; }"
            "QPushButton:disabled{ background: #BBF7D0; color: #F0FDF4; }";
    }
    inline QString btnDanger() {
        return
            "QPushButton {"
            "  background: #DC2626; color: white; border: none;"
            "  border-radius: 8px; padding: 8px 18px;"
            "  font-size: 12px; font-weight: 700; }"
            "QPushButton:hover   { background: #B91C1C; }"
            "QPushButton:pressed { background: #991B1B; }"
            "QPushButton:disabled{ background: #FCA5A5; color: #FEF2F2; }";
    }
    inline QString btnGhost() {
        return
            "QPushButton {"
            "  background: transparent; color: #4F6EF7;"
            "  border: 1.5px solid #CBD5E1;"
            "  border-radius: 8px; padding: 8px 18px;"
            "  font-size: 12px; font-weight: 700; }"
            "QPushButton:hover   { background: #EEF2FF; border-color: #4F6EF7; }"
            "QPushButton:pressed { background: #E0E7FF; }"
            "QPushButton:disabled{ color: #94A3B8; }";
    }
    inline QString btnWarning() {
        return
            "QPushButton {"
            "  background: #EA580C; color: white; border: none;"
            "  border-radius: 8px; padding: 8px 18px;"
            "  font-size: 12px; font-weight: 700; }"
            "QPushButton:hover   { background: #C2410C; }"
            "QPushButton:pressed { background: #9A3412; }";
    }

    // ── Tab bar — taller, bolder selected state ───────────────────────────────
    inline QString tabs() {
        return
            "QTabWidget::pane   { border: none; background: #ECEEF5; }"
            "QTabWidget::tab-bar { alignment: left; }"
            "QTabBar             { background: #FFFFFF; border-bottom: 2px solid #E2E8F0; }"
            "QTabBar::tab {"
            "  background: transparent; color: #64748B;"
            "  padding: 11px 16px; border: none; margin: 0;"
            "  font-size: 12px; font-weight: 600; min-width: 0; }"
            "QTabBar::tab:selected {"
            "  color: #4F6EF7; font-weight: 700;"
            "  border-bottom: 3px solid #4F6EF7;"
            "  background: #FAFBFF; }"
            "QTabBar::tab:hover:!selected { color: #4F6EF7; background: #F4F6FD; }"
            "QTabBar::scroller { width: 28px; }"
            "QTabBar QToolButton {"
            "  background: white; border: none;"
            "  color: #64748B; font-size: 14px; }"
            "QTabBar QToolButton:hover { background: #EEF2FF; }";
    }

    // ── Table — clean, readable ───────────────────────────────────────────────
    inline QString table() {
        return
            "QTableWidget {"
            "  background: white; color: #0F172A;"
            "  border: 1px solid #E2E8F0; border-radius: 10px;"
            "  font-size: 12px; gridline-color: transparent;"
            "  outline: none; }"
            "QHeaderView::section {"
            "  background: #F8FAFC; color: #334155;"
            "  padding: 8px 10px; border: none; border-bottom: 2px solid #E2E8F0;"
            "  font-size: 11px; font-weight: 700; letter-spacing: .04em;"
            "  text-transform: uppercase; }"
            "QTableWidget::item { padding: 6px 10px; border-bottom: 1px solid #F1F5F9; }"
            "QTableWidget::item:selected { background: #EEF2FF; color: #3B5BF6; }"
            "QTableWidget::item:alternate { background: #FAFBFF; }"
            "QScrollBar:vertical { background: #F4F6FB; width: 5px; border-radius: 3px; margin: 0; }"
            "QScrollBar::handle:vertical { background: #CBD5E1; border-radius: 3px; min-height: 24px; }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
            "QScrollBar:horizontal { background: #F4F6FB; height: 5px; border-radius: 3px; }"
            "QScrollBar::handle:horizontal { background: #CBD5E1; border-radius: 3px; }";
    }

    // ── Input — consistent, focus ring ───────────────────────────────────────
    inline QString input() {
        return
            "QLineEdit, QComboBox, QSpinBox {"
            "  background: #F8FAFC; color: #0F172A;"
            "  border: 1.5px solid #E2E8F0; border-radius: 8px;"
            "  padding: 7px 11px; font-size: 12px; }"
            "QLineEdit:focus, QComboBox:focus, QSpinBox:focus {"
            "  border-color: #4F6EF7; background: white; outline: none; }"
            "QComboBox::drop-down { border: none; width: 22px; padding-right: 4px; }"
            "QComboBox::down-arrow { width: 10px; height: 10px; }"
            "QComboBox QAbstractItemView {"
            "  background: white; color: #0F172A;"
            "  border: 1.5px solid #E2E8F0; border-radius: 8px;"
            "  selection-background-color: #EEF2FF;"
            "  selection-color: #4F6EF7; padding: 4px; }"
            "QSpinBox::up-button, QSpinBox::down-button { width: 18px; border: none; }"
            "QSpinBox::up-arrow   { width: 8px; height: 8px; }"
            "QSpinBox::down-arrow { width: 8px; height: 8px; }";
    }

    // ── Section header label style ────────────────────────────────────────────
    inline QString sectionHeader() {
        return QString(
            "color: %1; font-size: 13px; font-weight: 700; "
            "padding-bottom: 2px;"
        ).arg(TEXT_PRIMARY);
    }

    // ── Status / hint label ───────────────────────────────────────────────────
    inline QString hint() {
        return QString("color: %1; font-size: 11px;").arg(TEXT_MUTED);
    }

    // ── Thin scrollbar ────────────────────────────────────────────────────────
    inline QString scrollbar() {
        return
            "QScrollBar:vertical   { background:#F4F6FB; width:5px;  border-radius:3px; margin:0; }"
            "QScrollBar::handle:vertical   { background:#CBD5E1; border-radius:3px; min-height:20px; }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }"
            "QScrollBar:horizontal { background:#F4F6FB; height:5px; border-radius:3px; }"
            "QScrollBar::handle:horizontal { background:#CBD5E1; border-radius:3px; }";
    }

    // ── Terminal / log text area ──────────────────────────────────────────────
    inline QString termLog() {
        return
            "QTextEdit {"
            "  background: #0D1117; color: #C9D1D9;"
            "  border: 1px solid #21262D; border-radius: 10px;"
            "  font-family: 'Consolas', 'Fira Code', 'Courier New', monospace;"
            "  font-size: 11px; line-height: 1.6; padding: 10px; }"
            "QScrollBar:vertical { background: #161B22; width: 4px; border-radius: 2px; }"
            "QScrollBar::handle:vertical { background: #30363D; border-radius: 2px; }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }";
    }

    // ── Progress bar ─────────────────────────────────────────────────────────
    inline QString progressBar(const char* color = "#4F6EF7") {
        return QString(
            "QProgressBar {"
            "  background: #E2E8F0; border: none; border-radius: 4px;"
            "  height: 6px; text-align: center; color: transparent; }"
            "QProgressBar::chunk { background: %1; border-radius: 4px; }"
        ).arg(color);
    }

    // ── Slider ───────────────────────────────────────────────────────────────
    inline QString slider(const char* color = "#4F6EF7") {
        return QString(
            "QSlider::groove:horizontal {"
            "  background: #E2E8F0; height: 4px; border-radius: 2px; }"
            "QSlider::sub-page:horizontal {"
            "  background: %1; height: 4px; border-radius: 2px; }"
            "QSlider::handle:horizontal {"
            "  background: %1; width: 14px; height: 14px;"
            "  margin: -5px 0; border-radius: 7px; }"
        ).arg(color);
    }

    // ── List widget ───────────────────────────────────────────────────────────
    inline QString listWidget() {
        return
            "QListWidget {"
            "  background: #F8FAFC; border: 1.5px solid #E2E8F0; border-radius: 10px;"
            "  font-size: 12px; color: #0F172A; outline: none; }"
            "QListWidget::item {"
            "  padding: 7px 12px; border-radius: 6px; margin: 1px 4px; }"
            "QListWidget::item:selected {"
            "  background: #EEF2FF; color: #3B5BF6; font-weight: 600; }"
            "QListWidget::item:hover:!selected { background: #F1F5F9; }"
            "QScrollBar:vertical { background: #F4F6FB; width: 4px; border-radius: 2px; }"
            "QScrollBar::handle:vertical { background: #CBD5E1; border-radius: 2px; }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }";
    }

    // ── Stat card (reusable mini KPI block) ───────────────────────────────────
    // Call this to get a stylesheet for a value+label card
    inline QString statCard() {
        return
            "background: white;"
            "border-radius: 12px;"
            "border: 1px solid #E2E8F0;";
    }
}
