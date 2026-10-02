// Unit tests for the serial RPM line protocol (rpmparser.h).
// The protocol is what the existing code accepts: one base-10 integer per '\n'-terminated line.
// Limits are the MainWindow constants: kMaxLineLen = 64, kMaxBufferBytes = 8 * 1024.
#include <QtTest>
#include "rpmparser.h"

static constexpr int kMaxLineLen = 64;
static constexpr int kMaxBufferBytes = 8 * 1024;

class TstRpmParser : public QObject
{
    Q_OBJECT

    static QVector<int> feed(QByteArray &buf, const QByteArray &chunk)
    {
        return RpmParser::consume(buf, chunk, kMaxLineLen, kMaxBufferBytes);
    }

    // Verbatim copy of the loop in MainWindow::readSerialData() at research-baseline (4d4e3f8),
    // with acceptRpmSample(rawRpm, "serial") replaced by out.append(rawRpm).
    static QVector<int> baselineInline(QByteArray &m_serialBuffer, const QByteArray &readAll)
    {
        QVector<int> out;
        m_serialBuffer.append(readAll);

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

            out.append(rawRpm);
        }
        return out;
    }

private slots:
    void normalValue()
    {
        QByteArray buf;
        QCOMPARE(feed(buf, "2500\n"), QVector<int>({2500}));
        QVERIFY(buf.isEmpty());
    }

    void zero()
    {
        QByteArray buf;
        QCOMPARE(feed(buf, "0\n"), QVector<int>({0}));
    }

    void negativeIsParsedThenClampedToZero()
    {
        QByteArray buf;
        QCOMPARE(feed(buf, "-120\n"), QVector<int>({-120}));
        QCOMPARE(RpmParser::correct(-120), 0);
        QCOMPARE(RpmParser::correct(0), 0);
        QCOMPARE(RpmParser::correct(7000), 7000);   // no upper clamp in the existing code
    }

    void intBoundaries()
    {
        QByteArray buf;
        // INT_MAX is above RpmParser::kMaxRpm and rejected since the DEF-SW-01 fix; INT_MIN is
        // accepted and clamped to 0 by correct().
        QCOMPARE(feed(buf, "2147483647\n-2147483648\n"), QVector<int>({INT_MIN}));
        QCOMPARE(feed(buf, QByteArray::number(RpmParser::kMaxRpm) + "\n"), QVector<int>({RpmParser::kMaxRpm}));
    }

    void overflowIsSkipped()
    {
        QByteArray buf;
        QCOMPARE(feed(buf, "2147483648\n99999999999\n1500\n"), QVector<int>({1500}));
    }

    void invalidStringsAreSkipped()
    {
        QByteArray buf;
        QCOMPARE(feed(buf, "abc\n12a\n1.5\n0x10\n3000\n"), QVector<int>({3000}));
    }

    void emptyAndWhitespaceLinesAreSkipped()
    {
        QByteArray buf;
        QCOMPARE(feed(buf, "\n   \n2600\n"), QVector<int>({2600}));
    }

    void crlfAndSurroundingSpacesAreTolerated()
    {
        QByteArray buf;
        QCOMPARE(feed(buf, "2500\r\n  2512 \n\t2498\r\n"), QVector<int>({2500, 2512, 2498}));
    }

    void splitFrameAcrossReads()
    {
        QByteArray buf;
        QCOMPARE(feed(buf, "25"), QVector<int>());
        QCOMPARE(buf, QByteArray("25"));
        QCOMPARE(feed(buf, "00\n26"), QVector<int>({2500}));
        QCOMPARE(feed(buf, "00\n"), QVector<int>({2600}));
        QVERIFY(buf.isEmpty());
    }

    void multipleFramesInOneRead()
    {
        QByteArray buf;
        QCOMPARE(feed(buf, "1000\n2000\n3000\n"), QVector<int>({1000, 2000, 3000}));
    }

    void repeatedValuesAreAllReturned()
    {
        QByteArray buf;
        QCOMPARE(feed(buf, "2500\n2500\n2500\n"), QVector<int>({2500, 2500, 2500}));
    }

    void rapidChangesKeepOrder()
    {
        QByteArray buf;
        QCOMPARE(feed(buf, "0\n8000\n0\n8000\n"), QVector<int>({0, 8000, 0, 8000}));
    }

    void lineAtMaxLengthIsAcceptedLongerIsSkipped()
    {
        QByteArray buf;
        const QByteArray at64 = QByteArray(60, ' ') + "1234";       // 64 bytes
        const QByteArray at65 = QByteArray(61, ' ') + "1234";       // 65 bytes
        QCOMPARE(feed(buf, at64 + "\n" + at65 + "\n" + "5\n"), QVector<int>({1234, 5}));
    }

    void unterminatedTailIsKeptForNextRead()
    {
        QByteArray buf;
        QCOMPARE(feed(buf, "1000\n20"), QVector<int>({1000}));
        QCOMPARE(buf, QByteArray("20"));
    }

    void bufferOverflowDropsOldestBytes()
    {
        // Existing behaviour: when unterminated data exceeds 8 KiB the oldest bytes are discarded.
        // A line that straddles the cut is corrupted; whatever remains is parsed if it is an integer.
        QByteArray buf;
        QCOMPARE(feed(buf, QByteArray(kMaxBufferBytes, 'x')), QVector<int>());
        QCOMPARE(buf.size(), kMaxBufferBytes);
        QCOMPARE(feed(buf, "1234\n"), QVector<int>());   // tail of 'x' + "1234" -> line > 64 bytes -> skipped
        QVERIFY(buf.isEmpty());
        QCOMPARE(feed(buf, "4321\n"), QVector<int>({4321}));  // parser recovers on the next line
    }

    // Post-project robustness classes and the number-format contract of one line:
    //   optional surrounding ASCII whitespace, optional single sign, one or more decimal digits,
    //   value within int and <= 911420367 (largest RPM whose speed fits int in update_values()).
    // Expected values are the contract; "defect" names the row's known deviation (QEXPECT_FAIL).
    void robustnessInputClasses_data()
    {
        QTest::addColumn<QByteArray>("input");
        QTest::addColumn<QVector<int>>("expected");
        QTest::addColumn<QString>("defect");
        const QString none;
        // existing behaviour kept
        QTest::newRow("explicit plus sign") << QByteArray("+300\n") << QVector<int>{300} << none;
        QTest::newRow("minus zero") << QByteArray("-0\n") << QVector<int>{0} << none;
        QTest::newRow("leading zeros") << QByteArray("007\n") << QVector<int>{7} << none;
        QTest::newRow("inner space rejected") << QByteArray("1 2\n9\n") << QVector<int>{9} << none;
        QTest::newRow("bare CR is not a separator (both values lost)") << QByteArray("2500\r3000\n") << QVector<int>{} << none;
        QTest::newRow("CR CR LF") << QByteArray("2500\r\r\n") << QVector<int>{2500} << none;
        QTest::newRow("only newlines") << QByteArray("\n\n\r\n\n") << QVector<int>{} << none;
        QTest::newRow("high bytes rejected") << QByteArray("\xff\xfe\x80\n2\n") << QVector<int>{2} << none;
        QTest::newRow("10 000-digit line skipped") << QByteArray(10000, '9') + "\n3\n" << QVector<int>{3} << none;
        QTest::newRow("value then garbage then value") << QByteArray("1\n#\n2\n") << QVector<int>{1, 2} << none;
        // whitespace
        QTest::newRow("ws: spaces around") << QByteArray(" 7 \n") << QVector<int>{7} << none;
        QTest::newRow("ws: tab, CR around negative") << QByteArray("\t-7\r\n") << QVector<int>{-7} << none;
        QTest::newRow("ws: VT and FF around") << QByteArray("\v7\f\n") << QVector<int>{7} << none;
        QTest::newRow("ws: whitespace-only line") << QByteArray(" \t \n") << QVector<int>{} << none;
        // sign
        QTest::newRow("sign: plus alone") << QByteArray("+\n") << QVector<int>{} << none;
        QTest::newRow("sign: minus alone") << QByteArray("-\n") << QVector<int>{} << none;
        QTest::newRow("sign: two signs") << QByteArray("+-7\n--7\n") << QVector<int>{} << none;
        QTest::newRow("sign: trailing sign") << QByteArray("7-\n") << QVector<int>{} << none;
        QTest::newRow("sign: space after sign") << QByteArray("- 7\n") << QVector<int>{} << none;
        // trailing garbage / other number syntaxes
        QTest::newRow("garbage: letters") << QByteArray("7abc\n") << QVector<int>{} << none;
        QTest::newRow("garbage: space then letters") << QByteArray("7 abc\n") << QVector<int>{} << none;
        QTest::newRow("garbage: decimal point") << QByteArray("7.0\n") << QVector<int>{} << none;
        QTest::newRow("garbage: exponent") << QByteArray("1e3\n") << QVector<int>{} << none;
        QTest::newRow("garbage: hex") << QByteArray("0x1F\n") << QVector<int>{} << none;
        QTest::newRow("garbage: separator") << QByteArray("1,000\n1_000\n") << QVector<int>{} << none;
        // embedded NUL (DEF-SW-04, fixed: Qt 5.12 QString::toInt() stopped at NUL)
        QTest::newRow("NUL inside digits") << QByteArray("25\0" "00\n1\n", 8) << QVector<int>{1} << none;
        QTest::newRow("NUL then letters") << QByteArray("7\0" "abc\n", 6) << QVector<int>{} << none;
        QTest::newRow("trailing NUL") << QByteArray("7\0" "\n", 3) << QVector<int>{} << none;
        QTest::newRow("leading NUL") << QByteArray("\0" "7\n1\n", 5) << QVector<int>{1} << none;
        QTest::newRow("NUL-only line between frames") << QByteArray("7\n\0\n8\n", 6) << QVector<int>{7, 8} << none;
        // overflow and upper bound (DEF-SW-01, fixed: larger values made update_values() overflow)
        QTest::newRow("overflow: INT_MAX + 1") << QByteArray("2147483648\n") << QVector<int>{} << none;
        QTest::newRow("overflow: INT_MIN - 1") << QByteArray("-2147483649\n") << QVector<int>{} << none;
        QTest::newRow("overflow: 20 digits") << QByteArray("99999999999999999999\n") << QVector<int>{} << none;
        QTest::newRow("bound: largest speed-safe RPM") << QByteArray("911420367\n") << QVector<int>{911420367} << none;
        QTest::newRow("bound: one above") << QByteArray("911420368\n") << QVector<int>{} << none;
        QTest::newRow("bound: INT_MAX") << QByteArray("2147483647\n") << QVector<int>{} << none;
        QTest::newRow("bound: INT_MIN still accepted (clamped later)") << QByteArray("-2147483648\n") << QVector<int>{INT_MIN} << none;
    }
    void robustnessInputClasses()
    {
        QFETCH(QByteArray, input);
        QFETCH(QVector<int>, expected);
        QFETCH(QString, defect);
        QByteArray buf;
        if (!defect.isEmpty())
            QEXPECT_FAIL("", qPrintable(defect + " (known, not fixed yet)"), Continue);
        QCOMPARE(feed(buf, input), expected);
        QVERIFY(!buf.contains('\n'));
        QVERIFY(buf.size() <= kMaxBufferBytes);
    }

    void nulAcrossReadBoundaryIsRejected()
    {
        // frame boundary: the digits and the NUL-containing tail arrive in different reads
        QByteArray buf;
        QCOMPARE(feed(buf, "7"), QVector<int>());
        QCOMPARE(feed(buf, QByteArray("\0" "abc\n8\n", 7)), QVector<int>{8});
    }

    void extractedParserMatchesBaselineInlineCode()
    {
        // Differential check: same chunk sequence through both implementations, buffers compared too.
        // Since the post-project fixes the two intentionally differ only for lines with an embedded
        // NUL (DEF-SW-04) and values above 911420367 (DEF-SW-01); these chunks contain neither.
        const QList<QByteArray> chunks = {
            "2500\n", "25", "00\n26", "00\r\n", "abc\n-5\n", "\n \n", "2147483648\n",
            QByteArray(70, '7') + "\n", "1\n2\n3", "\n", QByteArray(9000, '9'), "\n42\n",
            "  8000  \n", "0x1F\n", "+300\n", "-0\n",
        };
        QByteArray a, b;
        for (const QByteArray &c : chunks) {
            QCOMPARE(feed(a, c), baselineInline(b, c));
            QCOMPARE(a, b);
        }
    }
};

QTEST_APPLESS_MAIN(TstRpmParser)
#include "tst_rpmparser.moc"
