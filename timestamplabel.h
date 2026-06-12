#ifndef TIMESTAMPLABEL_H
#define TIMESTAMPLABEL_H

#include <QLabel>

class TimestampLabel : public QLabel {
    Q_OBJECT
public:
    using QLabel::QLabel;

signals:
    void painted();   // paintEvent 끝났을 때 알림

protected:
    void paintEvent(QPaintEvent *event) override {
        QLabel::paintEvent(event);
        emit painted();
    }
};

#endif // TIMESTAMPLABEL_H
