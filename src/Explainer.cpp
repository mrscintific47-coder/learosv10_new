#include "Explainer.h"
#include "Theme.h"

Explainer::Explainer(QWidget* parent) : QWidget(parent) {
    setStyleSheet(QString(
        "background: #FAFBFF;"
        "border-left: 1px solid %1;"
    ).arg(Theme::BORDER));
    setFixedWidth(360);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // ── Header ────────────────────────────────────────────────────────────────
    auto* header = new QWidget();
    header->setFixedHeight(52);
    header->setStyleSheet(
        "background: white;"
        "border-bottom: 2px solid #E2E8F0;");
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(16, 0, 14, 0);
    headerLayout->setSpacing(8);

    auto* headerIcon = new QLabel("📖");
    headerIcon->setStyleSheet("font-size: 16px; background: transparent;");

    titleLabel = new QLabel("What's Happening");
    titleLabel->setStyleSheet(
        "color: #0F172A; font-size: 13px; font-weight: 800;"
        "letter-spacing: -0.01em; background: transparent;");

    auto* tag = new QLabel("Plain English");
    tag->setStyleSheet(
        "color: #4F6EF7; background: #EEF2FF;"
        "border-radius: 8px; padding: 3px 9px;"
        "font-size: 9px; font-weight: 700; letter-spacing: .04em;");

    headerLayout->addWidget(headerIcon);
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();
    headerLayout->addWidget(tag);
    layout->addWidget(header);

    // ── Scrollable content ────────────────────────────────────────────────────
    auto* scroll = new QScrollArea();
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(
        "QScrollArea { border: none; background: #FAFBFF; }"
        "QScrollBar:vertical { background: #F1F5F9; width: 4px; margin: 0; }"
        "QScrollBar::handle:vertical { background: #CBD5E1; border-radius: 2px; min-height: 20px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
    );

    auto* container = new QWidget();
    container->setStyleSheet("background: #FAFBFF;");
    auto* containerLayout = new QVBoxLayout(container);
    containerLayout->setContentsMargins(18, 16, 18, 16);
    containerLayout->setSpacing(0);

    textLabel = new QLabel();
    textLabel->setWordWrap(true);
    textLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    textLabel->setTextFormat(Qt::RichText);
    textLabel->setStyleSheet(
        "color: #1E293B;"
        "font-size: 12px;"
        "line-height: 1.75;"
        "background: transparent;");
    textLabel->setOpenExternalLinks(false);

    containerLayout->addWidget(textLabel);
    containerLayout->addStretch();
    scroll->setWidget(container);
    layout->addWidget(scroll, 1);

    // ── Footer hint ───────────────────────────────────────────────────────────
    auto* footer = new QWidget();
    footer->setFixedHeight(32);
    footer->setStyleSheet(
        "background: white;"
        "border-top: 1px solid #E2E8F0;");
    auto* footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(16, 0, 16, 0);
    auto* footerHint = new QLabel("Click anything to update this panel");
    footerHint->setStyleSheet("color: #64748B; font-size: 10px;");
    footerLayout->addWidget(footerHint);
    layout->addWidget(footer);

    setExplanation(
        "<div style='margin-bottom:14px;'>"
        "<span style='font-size:22px;'>👋</span>"
        "</div>"
        "<div style='font-size:14px;font-weight:800;color:#0F172A;"
        "letter-spacing:-0.01em;margin-bottom:6px;'>Welcome to LearnOS</div>"
        "<div style='color:#64748B;font-size:11px;margin-bottom:16px;'>"
        "A live Linux systems laboratory. Everything here reads real kernel data."
        "</div>"
        "<div style='color:#0F172A;font-size:11px;font-weight:700;margin-bottom:8px;'>"
        "TRY THESE</div>"
        "<div style='color:#475569;font-size:12px;line-height:1.9;'>"
        "▶ <b>Processes</b> — click any row for deep stats<br>"
        "▶ <b>Sandbox</b> — spawn a CPU Burner, watch the heatmap<br>"
        "▶ <b>Threads</b> — spawn workers, see TIDs in /proc<br>"
        "▶ <b>Signals</b> — fire SIGKILL and watch a process die<br>"
        "▶ <b>Experiments</b> — one-button full labs<br>"
        "▶ <b>Scheduler</b> — step FCFS vs Round Robin side-by-side"
        "</div>"
    );
}

void Explainer::setExplanation(const QString& html) {
    textLabel->setText(html);
}
