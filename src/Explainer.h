#pragma once
#include <QWidget>
#include <QScrollArea>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>

class Explainer : public QWidget {
    Q_OBJECT

public:
    explicit Explainer(QWidget* parent = nullptr);

public slots:
    void setExplanation(const QString& html);

private:
    QLabel* textLabel;
    QLabel* titleLabel;
};
