#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <QSerialPortInfo>
#include <QDebug>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <QDateTime>
#include <QRandomGenerator>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // === CSV 초기화 ===
    m_timerBase.start();

    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
    QString filename = QString("latency_metrics_%1.csv").arg(timestamp);

    m_csvFile.setFileName(filename);
    if (m_csvFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        m_csv.setDevice(&m_csvFile);
        m_csv << "seq,source,t_in_ms,t_frame_ms,e2e_latency_ms,raw_rpm,rpm\n";
    }

    // 속도 업데이트 타이머
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &MainWindow::update_values);
    m_timer->start(100);

    // RPM 애니메이션
    m_rpmAnimation = new QPropertyAnimation(this, "displayRpm", this);
    m_rpmAnimation->setEasingCurve(QEasingCurve::OutCubic);

    // UI 쓰로틀 (30Hz)
    m_uiThrottle.setInterval(kUiIntervalMs);
    m_uiThrottle.setTimerType(Qt::PreciseTimer);
    connect(&m_uiThrottle, &QTimer::timeout, this, &MainWindow::flushPendingRpm);
    m_uiThrottle.start();

    const QByteArray simulate = qgetenv("CLUSTER_SIMULATE");
    if (simulate == "1" || simulate.toLower() == "true")
        setupSimulation();
    else
        setupSerialPort();

    // paintEvent 시점 연결
    if (auto tsLabel = qobject_cast<TimestampLabel*>(ui->rpmLabel)) {
        connect(tsLabel, &TimestampLabel::painted, this, [this]() {
            if (m_frameSeq <= 0 || m_frameSeq == m_lastLoggedSeq)
                return;

            qint64 t_paint = m_timerBase.elapsed();
            logEvent(m_frameSeq, m_frameSerialTime, t_paint,
                     m_frameRawRpm, m_frameRpm);
            m_lastLoggedSeq = m_frameSeq;
        });
    }
}

MainWindow::~MainWindow()
{
    if (m_serialPort && m_serialPort->isOpen())
        m_serialPort->close();
    if (m_csvFile.isOpen())
        m_csvFile.close();
    delete ui;
}

int MainWindow::displayRpm() const noexcept { return m_displayRpm; }

void MainWindow::setDisplayRpm(int rpm) noexcept
{
    if (m_displayRpm == rpm) return;
    m_displayRpm = rpm;
    const QString text = QString::number(rpm);
    if (ui->rpmLabel && ui->rpmLabel->text() != text) {
        ui->rpmLabel->setText(text);
    }
}

void MainWindow::readSerialData()
{
    m_serialBuffer.append(m_serialPort->readAll());

    if (m_serialBuffer.size() > kMaxBufferBytes)
        m_serialBuffer.remove(0, m_serialBuffer.size() - kMaxBufferBytes);

    int nl;
    while ((nl = m_serialBuffer.indexOf('\n')) != -1) {
        QByteArray line = m_serialBuffer.left(nl);
        m_serialBuffer.remove(0, nl + 1);

        if (line.size() > kMaxLineLen) continue;

        bool ok = false;
        const int rawRpm = QString::fromLatin1(line.trimmed()).toInt(&ok);
        if (!ok) continue;

        acceptRpmSample(rawRpm, "serial");
    }
}

void MainWindow::flushPendingRpm()
{
    if (m_pendingRpm < 0) return;

    const int target = m_pendingRpm;
    const int rawTarget = m_pendingRawRpm;
    const int seq = m_pendingSeq;
    const qint64 tIn = m_pendingSerialTime;

    const int start = m_displayRpm;
    const int delta = std::abs(target - start);

    m_pendingRpm = -1;
    m_pendingRawRpm = -1;
    m_pendingSeq = -1;
    m_pendingSerialTime = 0;
    m_frameSeq = seq;
    m_frameRawRpm = rawTarget;
    m_frameRpm = target;
    m_frameSerialTime = tIn;
    m_frameSource = m_pendingSource;
    m_lastRawRpmValue = rawTarget;
    m_lastRpmValue = target;

    const QByteArray animate = qgetenv("CLUSTER_ANIMATE");
    if (!(animate == "1" || animate.toLower() == "true")) {
        setDisplayRpm(target);
        return;
    }

    if (delta < kSmallDelta) {
        setDisplayRpm(target);
        return;
    }

    const int dur = std::clamp(delta * 2, 60, 180);
    if (m_rpmAnimation->state() == QAbstractAnimation::Running)
        m_rpmAnimation->stop();
    m_rpmAnimation->setDuration(dur);
    m_rpmAnimation->setStartValue(start);
    m_rpmAnimation->setEndValue(target);
    m_rpmAnimation->start();
}

void MainWindow::update_values()
{
    const double currentRpm = static_cast<double>(m_displayRpm);
    const double wheelCirc  = M_PI * 2.5;
    const double calculated = currentRpm * (4.5 / 2.5) * wheelCirc * 10.0 / 60.0;

    const int newSpeed = static_cast<int>(calculated);
    if (newSpeed != m_speed) {
        m_speed = newSpeed;
        if (ui->speedLabel)
            ui->speedLabel->setText(QString::number(m_speed));
    }
}

int MainWindow::correctRpmValue(int rawRpm)
{
    if (rawRpm < 0) rawRpm = 0;
    return rawRpm;
}

void MainWindow::logEvent(int seq, qint64 t_serial, qint64 t_paint, int rawRpm, int rpm)
{
    if (!m_csv.device()) return;

    qint64 dt_sp = (t_paint - t_serial); // 엔드투엔드 지연시간

    m_csv << seq << "," << m_frameSource << "," << t_serial << "," << t_paint << ","
          << dt_sp << "," << rawRpm << "," << rpm << "\n";
    m_csv.flush();
}

void MainWindow::acceptRpmSample(int rawRpm, const QString &source)
{
    const int finalRpm = correctRpmValue(rawRpm);

    m_seq++;
    m_lastSerialTime = m_timerBase.elapsed();
    m_lastSource = source;
    m_pendingSeq = m_seq;
    m_pendingSerialTime = m_lastSerialTime;
    m_pendingSource = source;
    m_pendingRawRpm = rawRpm;
    m_pendingRpm = finalRpm;
}

void MainWindow::setupSerialPort()
{
    m_serialPort = new QSerialPort(this);
    m_serialPort->setReadBufferSize(1024);

    QString portName = QString::fromLocal8Bit(qgetenv("CLUSTER_SERIAL_PORT"));
    const int baudRate = QString::fromLocal8Bit(qgetenv("CLUSTER_BAUD")).toInt();

    if (portName.isEmpty()) {
        const auto infos = QSerialPortInfo::availablePorts();
        if (!infos.isEmpty())
            portName = infos.first().portName();
    }

    if (portName.isEmpty()) {
        qWarning() << "No serial ports available. Set CLUSTER_SERIAL_PORT or CLUSTER_SIMULATE=1.";
        if (ui->rpmLabel) ui->rpmLabel->setText("Port Err");
        return;
    }

    qDebug() << "Connecting to serial port:" << portName;

    m_serialPort->setPortName(portName);
    m_serialPort->setBaudRate(baudRate > 0 ? baudRate : QSerialPort::Baud9600);
    m_serialPort->setDataBits(QSerialPort::Data8);
    m_serialPort->setParity(QSerialPort::NoParity);
    m_serialPort->setStopBits(QSerialPort::OneStop);
    m_serialPort->setFlowControl(QSerialPort::NoFlowControl);

    if (m_serialPort->open(QIODevice::ReadOnly)) {
        connect(m_serialPort, &QSerialPort::readyRead,
                this, &MainWindow::readSerialData);
    } else {
        qWarning() << "Failed to open serial port:" << m_serialPort->errorString();
        if (ui->rpmLabel) ui->rpmLabel->setText("Open Err");
    }
}

void MainWindow::setupSimulation()
{
    m_lastSource = "simulation";
    qDebug() << "Running with simulated RPM input.";

    m_simTimer.setInterval(20);
    m_simTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_simTimer, &QTimer::timeout, this, &MainWindow::generateSimulatedRpm);
    m_simTimer.start();
}

void MainWindow::generateSimulatedRpm()
{
    const double t = static_cast<double>(m_timerBase.elapsed()) / 1000.0;
    const int wave = static_cast<int>(2500.0 + 1700.0 * std::sin(t * 1.3));
    const int noise = static_cast<int>(QRandomGenerator::global()->bounded(121)) - 60;
    acceptRpmSample(wave + noise, "simulation");
}

