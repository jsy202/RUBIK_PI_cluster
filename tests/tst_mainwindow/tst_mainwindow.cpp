// Host-side tests of the MainWindow application logic (post-project software verification).
// No production change: MainWindow is driven through its existing inputs (CLUSTER_* environment
// variables, a replay file, a pseudo-terminal as the serial port) and its private slots, which
// Qt's meta-object system can invoke by name. Runs with QT_QPA_PLATFORM=offscreen.
//
// What these tests are NOT: they do not run on RUBIK Pi 3, do not use a UART, an Arduino or a
// physical display, and assert nothing about latency values (timing is host-dependent).
#include <QtTest>
#include <QLabel>
#include <QPropertyAnimation>
#include <QTemporaryDir>
#include <QSocketNotifier>

#include <cmath>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>

#include "mainwindow.h"
#include "timestamplabel.h"

namespace {

// Sets environment variables for the lifetime of one MainWindow and restores them afterwards.
class EnvGuard
{
public:
    EnvGuard(std::initializer_list<std::pair<const char *, QByteArray>> vars)
    {
        for (const auto &v : vars) {
            m_saved.append({v.first, qgetenv(v.first), qEnvironmentVariableIsSet(v.first)});
            qputenv(v.first, v.second);
        }
    }
    ~EnvGuard()
    {
        for (const auto &s : m_saved) {
            if (s.wasSet) qputenv(s.name, s.value);
            else qunsetenv(s.name);
        }
    }

private:
    struct Saved { const char *name; QByteArray value; bool wasSet; };
    QVector<Saved> m_saved;
};

constexpr int kNeverMs = 3600 * 1000;   // replay timer interval that never fires during a test

void invoke(MainWindow &w, const char *slot)
{
    QVERIFY2(QMetaObject::invokeMethod(&w, slot, Qt::DirectConnection), slot);
}

} // namespace

class TstMainWindow : public QObject
{
    Q_OBJECT

    QTemporaryDir *m_dir{nullptr};
    QString m_oldCwd;

    // MainWindow writes latency_metrics_<timestamp>.csv into the working directory.
    QStringList csvRows() const
    {
        const QStringList files = QDir(m_dir->path()).entryList({"latency_metrics_*.csv"});
        if (files.size() != 1) return {QString("<%1 csv files>").arg(files.size())};
        QFile f(m_dir->filePath(files.first()));
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {"<unreadable>"};
        QStringList rows = QString::fromUtf8(f.readAll()).split('\n', QString::SkipEmptyParts);
        return rows;
    }
    QStringList dataRows() const { QStringList r = csvRows(); if (!r.isEmpty()) r.removeFirst(); return r; }

    QString writeReplay(const QByteArray &content)
    {
        const QString p = m_dir->filePath("replay.txt");
        QFile f(p);
        f.open(QIODevice::WriteOnly);
        f.write(content);
        return p;
    }

    static TimestampLabel *rpmLabel(MainWindow &w) { return w.findChild<TimestampLabel *>("rpmLabel"); }
    static QLabel *speedLabel(MainWindow &w) { return w.findChild<QLabel *>("speedLabel"); }

    static void showAndWait(MainWindow &w)
    {
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
    }

    // replay N samples into the pending slot, then run one 30 Hz flush, without the event loop
    static void feed(MainWindow &w, int n = 1)
    {
        for (int i = 0; i < n; ++i) invoke(w, "feedReplaySample");
    }

    // splits "seq,source,t_in_ms,t_frame_ms,e2e_latency_ms,raw_rpm,rpm"
    static QStringList fields(const QString &row) { return row.split(','); }

private slots:
    void init()
    {
        m_dir = new QTemporaryDir;
        QVERIFY(m_dir->isValid());
        m_oldCwd = QDir::currentPath();
        QDir::setCurrent(m_dir->path());
    }
    void cleanup()
    {
        QDir::setCurrent(m_oldCwd);
        delete m_dir;
        m_dir = nullptr;
    }

    // ---- REQ-MEAS-002: CSV format -------------------------------------------------------------
    void csvHeaderIsWrittenEvenWithoutFrames()
    {
        EnvGuard env{{"CLUSTER_REPLAY_FILE", writeReplay("1\n").toLocal8Bit()},
                     {"CLUSTER_REPLAY_INTERVAL_MS", QByteArray::number(kNeverMs)}};
        {
            MainWindow w;
        }
        QCOMPARE(csvRows(), QStringList{"seq,source,t_in_ms,t_frame_ms,e2e_latency_ms,raw_rpm,rpm"});
    }

    // REQ-MEAS-003: if the CSV cannot be created the display path keeps working, logging is skipped.
    // The file name is latency_metrics_<yyyyMMdd_hhmmss>.csv; directories with the names of the next
    // few seconds make QFile::open() fail (the container runs as root, so permissions do not).
    void displayWorksWhenCsvCannotBeCreated()
    {
        const QDateTime now = QDateTime::currentDateTime();
        for (int s = -1; s <= 5; ++s)
            QDir(m_dir->path()).mkdir(QString("latency_metrics_%1.csv").arg(now.addSecs(s).toString("yyyyMMdd_hhmmss")));
        EnvGuard env{{"CLUSTER_REPLAY_FILE", writeReplay("2500\n").toLocal8Bit()},
                     {"CLUSTER_REPLAY_INTERVAL_MS", QByteArray::number(kNeverMs)}};
        MainWindow w;
        showAndWait(w);
        feed(w);
        invoke(w, "flushPendingRpm");
        rpmLabel(w)->repaint();                      // painted -> logEvent() with no CSV device
        QCOMPARE(rpmLabel(w)->text(), QString("2500"));
        for (const QFileInfo &fi : QDir(m_dir->path()).entryInfoList({"latency_metrics_*"}))
            QVERIFY2(fi.isDir(), qPrintable(fi.fileName()));   // no CSV file was created
    }

    // ---- REQ-UI-001 / REQ-UI-002: 30 Hz flush shows the latest pending sample -----------------
    void sampleIsShownOnlyAfterFlush()
    {
        EnvGuard env{{"CLUSTER_REPLAY_FILE", writeReplay("2500\n").toLocal8Bit()},
                     {"CLUSTER_REPLAY_INTERVAL_MS", QByteArray::number(kNeverMs)}};
        MainWindow w;
        feed(w);
        QCOMPARE(rpmLabel(w)->text(), QString("RPM"));   // accepted, not yet displayed
        QCOMPARE(w.displayRpm(), 0);
        invoke(w, "flushPendingRpm");
        QCOMPARE(rpmLabel(w)->text(), QString("2500"));
        QCOMPARE(w.displayRpm(), 2500);
    }

    void flushWithoutPendingSampleChangesNothing()
    {
        EnvGuard env{{"CLUSTER_REPLAY_FILE", writeReplay("2500\n").toLocal8Bit()},
                     {"CLUSTER_REPLAY_INTERVAL_MS", QByteArray::number(kNeverMs)}};
        MainWindow w;
        showAndWait(w);
        feed(w);
        invoke(w, "flushPendingRpm");
        rpmLabel(w)->repaint();
        invoke(w, "flushPendingRpm");               // nothing pending: must not re-arm the frame
        rpmLabel(w)->repaint();
        QCOMPARE(rpmLabel(w)->text(), QString("2500"));
        QCOMPARE(dataRows().size(), 1);
    }

    void onlyLatestPendingSampleIsShownAndLogged()
    {
        EnvGuard env{{"CLUSTER_REPLAY_FILE", writeReplay("1000\n2000\n3000\n").toLocal8Bit()},
                     {"CLUSTER_REPLAY_INTERVAL_MS", QByteArray::number(kNeverMs)}};
        MainWindow w;
        showAndWait(w);
        feed(w, 3);                                  // three samples between two flushes
        invoke(w, "flushPendingRpm");
        rpmLabel(w)->repaint();
        QCOMPARE(rpmLabel(w)->text(), QString("3000"));
        const QStringList rows = dataRows();
        QCOMPARE(rows.size(), 1);
        QCOMPARE(fields(rows[0])[0], QString("3"));  // seq 1 and 2 were overwritten (seq gap)
        QCOMPARE(fields(rows[0])[5], QString("3000"));
    }

    // ---- REQ-IN-005 at application level: raw value logged, corrected value displayed -----------
    void negativeSampleIsDisplayedAsZeroAndRawIsLogged()
    {
        EnvGuard env{{"CLUSTER_REPLAY_FILE", writeReplay("500\n-120\n").toLocal8Bit()},
                     {"CLUSTER_REPLAY_INTERVAL_MS", QByteArray::number(kNeverMs)}};
        MainWindow w;
        showAndWait(w);
        feed(w);
        invoke(w, "flushPendingRpm");
        rpmLabel(w)->repaint();
        feed(w);
        invoke(w, "flushPendingRpm");
        rpmLabel(w)->repaint();
        QCOMPARE(rpmLabel(w)->text(), QString("0"));
        const QStringList rows = dataRows();
        QCOMPARE(rows.size(), 2);
        QCOMPARE(fields(rows[1])[5], QString("-120"));   // raw_rpm
        QCOMPARE(fields(rows[1])[6], QString("0"));      // rpm (displayed)
    }

    // ---- REQ-MEAS-002: one CSV row per displayed frame, after paintEvent ------------------------
    void eachFrameIsLoggedOnceAfterPaint()
    {
        EnvGuard env{{"CLUSTER_REPLAY_FILE", writeReplay("2500\n2600\n").toLocal8Bit()},
                     {"CLUSTER_REPLAY_INTERVAL_MS", QByteArray::number(kNeverMs)}};
        MainWindow w;
        showAndWait(w);
        feed(w);
        invoke(w, "flushPendingRpm");
        QCOMPARE(dataRows().size(), 0);              // flushed but not painted yet
        rpmLabel(w)->repaint();
        rpmLabel(w)->repaint();                      // second paint of the same frame: no new row
        feed(w);
        invoke(w, "flushPendingRpm");
        rpmLabel(w)->repaint();

        const QStringList rows = dataRows();
        QCOMPARE(rows.size(), 2);
        for (int i = 0; i < rows.size(); ++i) {
            const QStringList f = fields(rows[i]);
            QCOMPARE(f.size(), 7);
            QCOMPARE(f[0], QString::number(i + 1));
            QCOMPARE(f[1], QString("replay"));
            const qint64 tIn = f[2].toLongLong(), tFrame = f[3].toLongLong(), e2e = f[4].toLongLong();
            QCOMPARE(e2e, tFrame - tIn);             // e2e_latency_ms = t_frame - t_in
            QVERIFY(e2e >= 0);
        }
        QCOMPARE(fields(rows[0])[5], QString("2500"));
        QCOMPARE(fields(rows[1])[5], QString("2600"));
    }

    void paintBeforeFirstSampleIsNotLogged()
    {
        EnvGuard env{{"CLUSTER_REPLAY_FILE", writeReplay("2500\n").toLocal8Bit()},
                     {"CLUSTER_REPLAY_INTERVAL_MS", QByteArray::number(kNeverMs)}};
        MainWindow w;
        showAndWait(w);
        rpmLabel(w)->repaint();
        QCOMPARE(dataRows().size(), 0);
    }

    // ---- REQ-UI-003: speed is derived from the displayed RPM (existing formula) -----------------
    void speedIsDerivedFromDisplayedRpm()
    {
        EnvGuard env{{"CLUSTER_REPLAY_FILE", writeReplay("1000\n0\n").toLocal8Bit()},
                     {"CLUSTER_REPLAY_INTERVAL_MS", QByteArray::number(kNeverMs)}};
        MainWindow w;
        invoke(w, "update_values");
        QCOMPARE(speedLabel(w)->text(), QString("SPEED"));   // 0 rpm -> speed 0 == initial m_speed
        feed(w);
        invoke(w, "update_values");
        QCOMPARE(speedLabel(w)->text(), QString("SPEED"));   // uses displayed RPM, not pending
        invoke(w, "flushPendingRpm");
        invoke(w, "update_values");
        // existing formula: rpm * (4.5/2.5) * (pi*2.5) * 10 / 60, truncated -> 2356.19 -> 2356
        QCOMPARE(speedLabel(w)->text(), QString("2356"));
        feed(w);
        invoke(w, "flushPendingRpm");
        invoke(w, "update_values");
        QCOMPARE(speedLabel(w)->text(), QString("0"));
    }

    // ---- REQ-UI-004: optional animation (CLUSTER_ANIMATE=1, off for measurements) --------------
    void animation_data()
    {
        QTest::addColumn<int>("target");
        QTest::addColumn<bool>("animated");
        QTest::addColumn<int>("duration");
        QTest::newRow("delta 4: below kSmallDelta, set directly") << 4 << false << 0;
        QTest::newRow("delta 5: kSmallDelta, animated, clamped to 60") << 5 << true << 60;
        QTest::newRow("delta 45: 2*delta = 90 ms") << 45 << true << 90;
        QTest::newRow("delta 90: 2*delta = 180 ms (upper clamp)") << 90 << true << 180;
        QTest::newRow("delta 1000: clamped to 180 ms") << 1000 << true << 180;
        QTest::newRow("delta 911420367: largest accepted RPM") << 911420367 << true << 180;
    }
    void animation()
    {
        QFETCH(int, target);
        QFETCH(bool, animated);
        QFETCH(int, duration);
        EnvGuard env{{"CLUSTER_REPLAY_FILE", writeReplay(QByteArray::number(target) + "\n").toLocal8Bit()},
                     {"CLUSTER_REPLAY_INTERVAL_MS", QByteArray::number(kNeverMs)},
                     {"CLUSTER_ANIMATE", "1"}};
        MainWindow w;
        auto *anim = w.findChild<QPropertyAnimation *>();
        QVERIFY(anim);
        feed(w);
        invoke(w, "flushPendingRpm");
        if (!animated) {
            QCOMPARE(anim->state(), QAbstractAnimation::Stopped);
            QCOMPARE(w.displayRpm(), target);
            return;
        }
        QCOMPARE(anim->state(), QAbstractAnimation::Running);
        QCOMPARE(anim->duration(), duration);
        QCOMPARE(anim->startValue().toInt(), 0);
        QCOMPARE(anim->endValue().toInt(), target);
        QTRY_COMPARE_WITH_TIMEOUT(w.displayRpm(), target, 2000);   // animation reaches the target
    }

    void animationIsRestartedFromCurrentValue()
    {
        EnvGuard env{{"CLUSTER_REPLAY_FILE", writeReplay("1000\n3000\n").toLocal8Bit()},
                     {"CLUSTER_REPLAY_INTERVAL_MS", QByteArray::number(kNeverMs)},
                     {"CLUSTER_ANIMATE", "true"}};
        MainWindow w;
        auto *anim = w.findChild<QPropertyAnimation *>();
        feed(w);
        invoke(w, "flushPendingRpm");
        QCOMPARE(anim->state(), QAbstractAnimation::Running);
        w.setDisplayRpm(400);                        // mid-animation value
        feed(w);
        invoke(w, "flushPendingRpm");
        QCOMPARE(anim->state(), QAbstractAnimation::Running);
        QCOMPARE(anim->startValue().toInt(), 400);
        QCOMPARE(anim->endValue().toInt(), 3000);
    }

    // ---- REQ-MEAS-001: replay input (validation-only path) --------------------------------------
    void replayUsesSerialLineProtocol()
    {
        // same parser as the serial port; the last line has no '\n' (setupReplay appends one)
        EnvGuard env{{"CLUSTER_REPLAY_FILE", writeReplay("abc\n2500\r\n\n  -5").toLocal8Bit()},
                     {"CLUSTER_REPLAY_INTERVAL_MS", QByteArray::number(kNeverMs)}};
        MainWindow w;
        showAndWait(w);
        for (int i = 0; i < 3; ++i) {                // 2 samples, then the replay is finished
            feed(w);
            invoke(w, "flushPendingRpm");
            rpmLabel(w)->repaint();
        }
        const QStringList rows = dataRows();
        QCOMPARE(rows.size(), 2);
        QCOMPARE(fields(rows[0])[5], QString("2500"));
        QCOMPARE(fields(rows[1])[5], QString("-5"));
    }

    void replayPlaysAtConfiguredIntervalAndStops()
    {
        EnvGuard env{{"CLUSTER_REPLAY_FILE", writeReplay("1100\n1200\n").toLocal8Bit()},
                     {"CLUSTER_REPLAY_INTERVAL_MS", "100"}};   // > 33 ms: every sample gets its own flush
        MainWindow w;
        showAndWait(w);
        QTRY_COMPARE_WITH_TIMEOUT(dataRows().size(), 2, 3000);   // event loop: replay + 30 Hz flush + paint
        QTest::qWait(200);
        QCOMPARE(dataRows().size(), 2);
        QCOMPARE(rpmLabel(w)->text(), QString("1200"));
    }

    void missingReplayFileIsReported()
    {
        EnvGuard env{{"CLUSTER_REPLAY_FILE", m_dir->filePath("does-not-exist.txt").toLocal8Bit()}};
        MainWindow w;
        QCOMPARE(rpmLabel(w)->text(), QString("Replay Err"));
    }

    // ---- existing simulation input (CLUSTER_SIMULATE) -------------------------------------------
    void simulatedSamplesStayInGeneratorRange()
    {
        EnvGuard env{{"CLUSTER_SIMULATE", "1"}};
        MainWindow w;
        showAndWait(w);
        for (int i = 0; i < 50; ++i) {
            invoke(w, "generateSimulatedRpm");
            invoke(w, "flushPendingRpm");
            // 2500 + 1700*sin(t) + noise[-60, 60]
            QVERIFY2(w.displayRpm() >= 740 && w.displayRpm() <= 4260, qPrintable(QString::number(w.displayRpm())));
        }
        rpmLabel(w)->repaint();
        const QStringList rows = dataRows();
        QVERIFY(!rows.isEmpty());
        QCOMPARE(fields(rows.last())[1], QString("simulation"));
    }

    // ---- REQ-IN-001..003 through the real QSerialPort path, using a pseudo-terminal ---------------
    // A pty is not a UART: this checks readyRead -> readSerialData -> parser -> 30 Hz flush -> paint
    // -> CSV inside the application, not the serial link or its timing.
    void serialPathViaPseudoTerminal()
    {
        const int master = posix_openpt(O_RDWR | O_NOCTTY);
        QVERIFY(master >= 0);
        QVERIFY(grantpt(master) == 0 && unlockpt(master) == 0);
        const QByteArray slave = ptsname(master);
        {
            EnvGuard env{{"CLUSTER_SERIAL_PORT", slave}, {"CLUSTER_BAUD", "9600"}};
            MainWindow w;
            QVERIFY2(rpmLabel(w)->text() == "RPM", qPrintable(rpmLabel(w)->text()));   // port opened
            showAndWait(w);

            auto send = [&](const QByteArray &b) { QCOMPARE(::write(master, b.constData(), b.size()), ssize_t(b.size())); };
            send("25");                                          // partial frame
            QTest::qWait(100);
            QCOMPARE(dataRows().size(), 0);
            send("00\r\n");
            QTRY_COMPARE_WITH_TIMEOUT(rpmLabel(w)->text(), QString("2500"), 2000);
            send("garbage\n-7\n");                               // malformed line skipped, negative clamped
            QTRY_COMPARE_WITH_TIMEOUT(rpmLabel(w)->text(), QString("0"), 2000);
            QTRY_COMPARE_WITH_TIMEOUT(dataRows().size(), 2, 2000);
            const QStringList rows = dataRows();
            QCOMPARE(fields(rows[0])[1], QString("serial"));
            QCOMPARE(fields(rows[0])[5], QString("2500"));
            QCOMPARE(fields(rows[1])[5], QString("-7"));
            QCOMPARE(fields(rows[1])[6], QString("0"));
        }
        ::close(master);
    }

    void unopenableSerialPortIsReported()
    {
        EnvGuard env{{"CLUSTER_SERIAL_PORT", "/dev/rubik-no-such-port"}};
        MainWindow w;
        QCOMPARE(rpmLabel(w)->text(), QString("Open Err"));
    }

    // The strict sanitizer run (halt on first report) sets RUBIK_SKIP_KNOWN_UB and skips the two tests
    // below that execute UB on purpose; scripts/sw_verify.sh runs them separately and records the
    // sanitizer reports they trigger.
    // ---- Known defects found by this verification (not fixed: research code is kept as is) ------
    // Each check states the expected behaviour; QEXPECT_FAIL records the current deviation, so the
    // test turns into XPASS (= failure) if the behaviour changes and the record must be updated.

    // REQ-IN-005 / REQ-UI-003, DEF-SW-01 (fixed): update_values() converts rpm * 2.356... (double)
    // back to int; above 911420367 that was undefined behaviour (UBSan float-cast-overflow, x86-64
    // showed INT_MIN). The largest speed-safe RPM is accepted and gives a valid speed; a larger input
    // value is rejected by the parser before it reaches the display or the speed calculation.
    void speedStaysRepresentableUpToRpmBound()
    {
        EnvGuard env{{"CLUSTER_REPLAY_FILE", writeReplay("911420367\n2147483647\n").toLocal8Bit()},
                     {"CLUSTER_REPLAY_INTERVAL_MS", QByteArray::number(kNeverMs)}};
        MainWindow w;
        feed(w);
        invoke(w, "flushPendingRpm");
        invoke(w, "update_values");
        // same expression and evaluation order as update_values()
        const double maxSpeed = 911420367.0 * (4.5 / 2.5) * (M_PI * 2.5) * 10.0 / 60.0;
        QVERIFY(maxSpeed < 2147483648.0);
        QVERIFY((911420368.0 * (4.5 / 2.5) * (M_PI * 2.5) * 10.0 / 60.0) >= 2147483648.0);
        QCOMPARE(speedLabel(w)->text(), QString::number(static_cast<int>(maxSpeed)));
        feed(w);                                       // 2147483647
        invoke(w, "flushPendingRpm");
        invoke(w, "update_values");
        QCOMPARE(w.displayRpm(), 911420367);
        QVERIFY(speedLabel(w)->text().toInt() >= 0);
    }

    // REQ-UI-004, DEF-SW-02 (fixed): with CLUSTER_ANIMATE=1, delta * 2 overflowed int for
    // delta > INT_MAX/2 (UB, UBSan signed-integer-overflow; x86-64 wrapped negative -> 60 ms).
    // The start value is set through the public displayRpm property, so the calculation is
    // checked on its own, independent of the input bound of DEF-SW-01.
    void animationDurationHasNoOverflow()
    {
        EnvGuard env{{"CLUSTER_REPLAY_FILE", writeReplay("0\n").toLocal8Bit()},
                     {"CLUSTER_REPLAY_INTERVAL_MS", QByteArray::number(kNeverMs)},
                     {"CLUSTER_ANIMATE", "1"}};
        MainWindow w;
        auto *anim = w.findChild<QPropertyAnimation *>();
        w.setDisplayRpm(2000000000);                   // delta = 2e9 > INT_MAX / 2
        feed(w);
        invoke(w, "flushPendingRpm");
        QCOMPARE(anim->duration(), 180);
        QCOMPARE(anim->endValue().toInt(), 0);
    }

    // DEF-SW-03 (measurement semantics): a sample whose value equals the displayed value causes no
    // setText and no paint of its own, but it still becomes the "current frame". The next paint of
    // rpmLabel for any other reason (expose, resize, overlap) logs it with t_frame = that unrelated
    // paint, so its e2e_latency_ms is not "sample accepted -> paint of that sample".
    // (validation/measurement_boundary.md listed this as possible, statically; this reproduces it.)
    void knownDefect_repeatedValueLoggedAtUnrelatedRepaint()
    {
        EnvGuard env{{"CLUSTER_REPLAY_FILE", writeReplay("2500\n2500\n").toLocal8Bit()},
                     {"CLUSTER_REPLAY_INTERVAL_MS", QByteArray::number(kNeverMs)}};
        MainWindow w;
        showAndWait(w);
        feed(w);
        invoke(w, "flushPendingRpm");
        rpmLabel(w)->repaint();
        QCOMPARE(dataRows().size(), 1);
        feed(w);                                       // same value 2500
        invoke(w, "flushPendingRpm");                  // no setText -> no paint caused by this sample
        QTest::qWait(150);
        rpmLabel(w)->repaint();                        // unrelated repaint, e.g. an expose
        QEXPECT_FAIL("", "DEF-SW-03: repeated-value sample is logged at an unrelated repaint", Continue);
        QCOMPARE(dataRows().size(), 1);
    }
};

QTEST_MAIN(TstMainWindow)
#include "tst_mainwindow.moc"
