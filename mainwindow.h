#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QSerialPort>
#include <QPropertyAnimation>
#include <QFile>
#include <QTextStream>
#include <QElapsedTimer>
#include <QString>

#include "timestamplabel.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT
    Q_PROPERTY(int displayRpm READ displayRpm WRITE setDisplayRpm)

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    int  displayRpm() const noexcept;
    void setDisplayRpm(int rpm) noexcept;

private slots:
    void update_values();     // 100ms 주기: 속도 계산
    void readSerialData();    // 시리얼 수신 → 버퍼링
    void flushPendingRpm();   // 30Hz 주기 UI 반영
    void generateSimulatedRpm();

private:
    int correctRpmValue(int rawRpm);
    void logEvent(int seq, qint64 t_serial, qint64 t_paint, int rawRpm, int rpm);
    void acceptRpmSample(int rawRpm, const QString &source);
    void setupSerialPort();
    void setupSimulation();

private:
    Ui::MainWindow *ui{nullptr};

    // 타이머 & 속도
    QTimer *m_timer{nullptr};
    int m_speed{0};

    // 시리얼
    QSerialPort *m_serialPort{nullptr};
    QByteArray m_serialBuffer;

    // 애니메이션
    QPropertyAnimation *m_rpmAnimation{nullptr};
    int m_displayRpm{0};

    // UI 쓰로틀
    QTimer m_uiThrottle;
    QTimer m_simTimer;
    int m_pendingRpm{-1};
    int m_pendingRawRpm{-1};
    int m_pendingSeq{-1};
    qint64 m_pendingSerialTime{0};
    QString m_pendingSource{"serial"};
    int m_frameSeq{-1};
    int m_frameRawRpm{-1};
    int m_frameRpm{-1};
    qint64 m_frameSerialTime{0};
    QString m_frameSource{"serial"};

    // --- 계측 ---
    QElapsedTimer m_timerBase;
    QFile m_csvFile;
    QTextStream m_csv;
    int m_seq{0};
    int m_lastLoggedSeq{0};

    qint64 m_lastSerialTime{0};
    int m_lastRpmValue{0};
    int m_lastRawRpmValue{0};
    QString m_lastSource{"serial"};

    // 상수
    static constexpr int kUiHz = 30;
    static constexpr int kUiIntervalMs = 1000 / kUiHz;
    static constexpr int kSmallDelta = 5;
    static constexpr int kMaxLineLen = 64;
    static constexpr int kMaxBufferBytes = 8 * 1024;
};

#endif // MAINWINDOW_H
