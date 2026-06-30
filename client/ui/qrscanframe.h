#ifndef QRSCANFRAME_H
#define QRSCANFRAME_H

#include <QWidget>
#include <QPainter>
#include <QPen>
#include <QEvent>

class QRScanFrame : public QWidget
{
    Q_OBJECT
public:
    explicit QRScanFrame(QWidget *parent = nullptr) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_TranslucentBackground);
        if (parent) {
            parent->installEventFilter(this);
            setGeometry(parent->rect());
        }
    }

    bool eventFilter(QObject *obj, QEvent *ev) override {
        if (obj == parent() && ev->type() == QEvent::Resize) {
            setGeometry(static_cast<QWidget*>(parent())->rect());
        }
        return QWidget::eventFilter(obj, ev);
    }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        int w = width(), h = height();
        int boxSize = qMin(w, h) * 0.6;
        int x = (w - boxSize) / 2, y = (h - boxSize) / 2;

        // Semi-transparent overlay outside the scan box
        p.setBrush(QColor(0, 0, 0, 100));
        p.setPen(Qt::NoPen);
        p.drawRect(0, 0, w, y);           // top
        p.drawRect(0, y + boxSize, w, h); // bottom
        p.drawRect(0, y, x, boxSize);     // left
        p.drawRect(x + boxSize, y, w, boxSize); // right

        // Border
        p.setBrush(Qt::NoBrush);
        QPen pen(QColor(0x2C, 0x6B, 0xED), 2);
        p.setPen(pen);
        p.drawRect(x, y, boxSize, boxSize);

        // Corner brackets
        int bl = boxSize / 5; // bracket length
        pen.setWidth(4);
        pen.setColor(QColor(0x2C, 0x6B, 0xED));
        p.setPen(pen);

        p.drawLine(x, y + bl, x, y); p.drawLine(x, y, x + bl, y);         // top-left
        p.drawLine(x + boxSize - bl, y, x + boxSize, y); p.drawLine(x + boxSize, y, x + boxSize, y + bl); // top-right
        p.drawLine(x, y + boxSize - bl, x, y + boxSize); p.drawLine(x, y + boxSize, x + bl, y + boxSize); // bottom-left
        p.drawLine(x + boxSize - bl, y + boxSize, x + boxSize, y + boxSize); p.drawLine(x + boxSize, y + boxSize - bl, x + boxSize, y + boxSize); // bottom-right
    }
};

#endif
