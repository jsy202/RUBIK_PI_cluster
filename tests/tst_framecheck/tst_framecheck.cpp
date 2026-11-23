// Tests for the per-line CRC-8 protection ("value,HH\n") added as a containment measure for
// intermittent corruption on the serial link. Every test names the behaviour it protects.
#include <QtTest>
#include "framecheck.h"

using namespace FrameCheck;

class TstFrameCheck : public QObject
{
    Q_OBJECT
private slots:
    // CRC-8 poly 0x1D, init 0, no reflection, xorout 0 = catalogue "CRC-8/GSM-A"; its published
    // check value for "123456789" is 0x37. Guards against a wrong polynomial/initial value.
    void crcMatchesCatalogueCheckValue()
    {
        QCOMPARE(crc8("123456789"), 0x37u);
    }

    void encodeProducesValueCommaHexNewline()
    {
        const QByteArray f = encode(2500);
        QVERIFY(f.startsWith("2500,"));
        QVERIFY(f.endsWith("\n"));
        QCOMPARE(f.size(), 4 + 1 + 2 + 1);
    }

    void validFrameIsAccepted()
    {
        QByteArray buf;
        const Result r = consume(buf, encode(2500), 64, 8192);
        QCOMPARE(r.values, (QVector<int>{2500}));
        QVERIFY(r.rejects.isEmpty());
    }

    void frameSplitAcrossReadsIsAssembled()
    {
        QByteArray buf;
        const QByteArray f = encode(2500);
        QVERIFY(consume(buf, f.left(3), 64, 8192).values.isEmpty());
        QCOMPARE(consume(buf, f.mid(3), 64, 8192).values, (QVector<int>{2500}));
    }

    // The failure this whole change exists for: a format-valid but wrong number must not be shown.
    // Exhaustive over every single-bit flip of every byte of the frame (line terminator included).
    void everySingleBitFlipNeverYieldsAWrongValue()
    {
        for (int v : {0, 800, 2500, 2600, 8000}) {
            const QByteArray frame = encode(v);
            for (int p = 0; p < frame.size(); ++p) {
                for (int b = 0; b < 8; ++b) {
                    QByteArray bad = frame;
                    bad[p] = bad[p] ^ (1 << b);
                    QByteArray buf;
                    // a following clean frame flushes any merged-line effect
                    const Result r = consume(buf, bad + encode(v), 64, 8192);
                    for (int got : r.values)
                        QVERIFY2(got == v, qPrintable(QString("value %1, byte %2, bit %3 -> %4").arg(v).arg(p).arg(b).arg(got)));
                }
            }
        }
    }

    void everySingleByteDeletionNeverYieldsAWrongValue()
    {
        for (int v : {0, 800, 2500, 8000}) {
            const QByteArray frame = encode(v);
            for (int p = 0; p < frame.size(); ++p) {
                QByteArray bad = frame;
                bad.remove(p, 1);
                QByteArray buf;
                const Result r = consume(buf, bad + encode(v), 64, 8192);
                for (int got : r.values)
                    QVERIFY2(got == v, qPrintable(QString("value %1, deleted byte %2 -> %3").arg(v).arg(p).arg(got)));
            }
        }
    }

    // A lost line terminator merges two frames into one number in a format-only parser.
    void lostNewlineDoesNotMergeTwoFrames()
    {
        QByteArray merged = encode(800);
        merged.chop(1);                       // drop '\n'
        QByteArray buf;
        const Result r = consume(buf, merged + encode(8000), 64, 8192);
        QVERIFY(!r.values.contains(8008000));
        QVERIFY(r.values.isEmpty());
        QVERIFY(!r.rejects.isEmpty());
    }

    void crcMismatchIsRejectedWithRawBytesAndReason()
    {
        QByteArray bad = encode(2500);
        bad[0] = '3';                         // 3500,62: still a perfectly valid integer
        QByteArray buf;
        const Result r = consume(buf, bad, 64, 8192);
        QVERIFY(r.values.isEmpty());
        QCOMPARE(r.rejects.size(), 1);
        QCOMPARE(r.rejects[0].reason, Reject::Crc);
        QCOMPARE(r.rejects[0].raw, QByteArray("3500,62"));
    }

    void validCrcButBadNumberIsRejectedAsFormat()
    {
        QByteArray body("12a");
        char h[4]; snprintf(h, sizeof h, "%02X", crc8(body));
        QByteArray buf;
        const Result r = consume(buf, body + ',' + QByteArray(h) + '\n', 64, 8192);
        QVERIFY(r.values.isEmpty());
        QCOMPARE(r.rejects.size(), 1);
        QCOMPARE(r.rejects[0].reason, Reject::Format);
    }

    void lineWithoutCrcFieldIsRejected()
    {
        QByteArray buf;
        const Result r = consume(buf, "2500\n", 64, 8192);   // old unprotected format
        QVERIFY(r.values.isEmpty());
        QCOMPARE(r.rejects.size(), 1);
        QCOMPARE(r.rejects[0].reason, Reject::Format);
    }

    void overlongLineIsRejected()
    {
        QByteArray buf;
        const Result r = consume(buf, QByteArray(70, '1') + "\n", 64, 8192);
        QCOMPARE(r.rejects.size(), 1);
        QCOMPARE(r.rejects[0].reason, Reject::TooLong);
    }

    void cleanStreamIsNeverRejected()
    {
        QByteArray buf, all;
        QVector<int> sent;
        for (int i = 0; i < 2000; ++i) { const int v = (i * 37) % 9000; sent << v; all += encode(v); }
        const Result r = consume(buf, all, 64, 1 << 20);
        QCOMPARE(r.values, sent);
        QVERIFY(r.rejects.isEmpty());
    }
};

QTEST_APPLESS_MAIN(TstFrameCheck)
#include "tst_framecheck.moc"
