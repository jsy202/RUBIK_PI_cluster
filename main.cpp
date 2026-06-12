// main.cpp
#include "mainwindow.h"
#include <QApplication>
#include <QScreen>
#include <QPixmap>
#include <QPalette>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    MainWindow w;

    // === 배경: 1회 스케일 후 팔레트 적용(매 프레임 스케일 방지) ===
    if (auto *central = w.centralWidget()) {
        central->setAutoFillBackground(true);

        QScreen *scr = QApplication::primaryScreen();
        const QSize target = scr ? scr->availableSize() : QSize(1280, 720);

        QPixmap bg(":/images/images/background4.jpg");
        if (!bg.isNull()) {
            QPixmap scaled = bg.scaled(target, Qt::KeepAspectRatioByExpanding,
                                       Qt::SmoothTransformation);
            QPalette pal = central->palette();
            pal.setBrush(QPalette::Window, scaled);
            central->setPalette(pal);
        }
    }

    // === 일반 창 모드로 실행 ===
    w.show();

    return a.exec();
}

