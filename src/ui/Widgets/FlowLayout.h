#pragma once

#include <QLayout>
#include <QList>
#include <QRect>
#include <QStyle>

/**
 * Lightweight FlowLayout (Qt example style) that lays out child widgets in
 * rows and re-flows them when the available width changes, so app cards are
 * always inside the visible window instead of overflowing.
 */
class FlowLayout final : public QLayout
{
public:
    explicit FlowLayout(QWidget *parent = nullptr,
                        int margin = -1,
                        int hSpacing = -1,
                        int vSpacing = -1);
    ~FlowLayout() override;

    void addItem(QLayoutItem *item) override;
    int horizontalSpacing() const;
    int verticalSpacing() const;
    Qt::Orientations expandingDirections() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int width) const override;
    int count() const override;
    QLayoutItem *itemAt(int index) const override;
    QSize minimumSize() const override;
    void setGeometry(const QRect &rect) override;
    QSize sizeHint() const override;
    QLayoutItem *takeAt(int index) override;

private:
    int doLayout(const QRect &rect, bool testOnly) const;
    int smartSpacing(QStyle::PixelMetric pm) const;

    QList<QLayoutItem *> itemList;
    int m_hSpace;
    int m_vSpace;
};