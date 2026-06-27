// libFuzzer target for the serial RPM line parser (rpmparser.h), built with ASan + UBSan.
// Post-project software verification; host only.
//
// Input: arbitrary bytes. The first byte chooses how the rest is split into serial reads
// (1..4 chunks), the remainder is the byte stream. Oracles, besides sanitizer reports:
//   O1  after every call no '\n' is left in the buffer and buffer.size() <= kMaxBufferBytes
//   O2  values returned <= '\n' count of (previous buffer + chunk)
//   O3  differential: same output and buffer as the verbatim research-baseline loop
//   O4  split invariance: while the 8 KiB cap is not reached, splitting the stream into several
//       reads gives the same values as one read (partial frames are reassembled)
//   O5  RpmParser::correct() never returns a negative value and keeps non-negative values
// A violated oracle calls abort(), which libFuzzer reports as a crash with the reproducer.
#include <QByteArray>
#include <QString>
#include <QVector>
#include <cstdint>
#include <cstdlib>
#include <cstdio>

#include "rpmparser.h"

static constexpr int kMaxLineLen = 64;          // MainWindow::kMaxLineLen
static constexpr int kMaxBufferBytes = 8 * 1024; // MainWindow::kMaxBufferBytes

// Verbatim copy of MainWindow::readSerialData() at research-baseline (4d4e3f8), as in tst_rpmparser.
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

#define CHECK(cond, what) do { if (!(cond)) { fprintf(stderr, "ORACLE VIOLATION: %s\n", what); abort(); } } while (0)

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size == 0) return 0;
    const int nChunks = 1 + (data[0] & 3);
    const QByteArray stream(reinterpret_cast<const char *>(data + 1), int(size - 1));

    QByteArray buf, ref;
    QVector<int> all;
    int start = 0;
    for (int i = 0; i < nChunks; ++i) {
        const int end = (i == nChunks - 1) ? stream.size() : stream.size() * (i + 1) / nChunks;
        const QByteArray chunk = stream.mid(start, end - start);
        const int newlines = buf.count('\n') + chunk.count('\n');
        start = end;

        const QVector<int> got = RpmParser::consume(buf, chunk, kMaxLineLen, kMaxBufferBytes);
        CHECK(!buf.contains('\n'), "O1 newline left in buffer");
        CHECK(buf.size() <= kMaxBufferBytes, "O1 buffer above cap");
        CHECK(got.size() <= newlines, "O2 more values than lines");
        CHECK(got == baselineInline(ref, chunk) && buf == ref, "O3 differs from baseline loop");
        for (const int v : got) {
            const int c = RpmParser::correct(v);
            CHECK(c >= 0 && (v < 0 || c == v), "O5 correct()");
        }
        all += got;
    }

    if (stream.size() <= kMaxBufferBytes) {
        QByteArray one;
        CHECK(RpmParser::consume(one, stream, kMaxLineLen, kMaxBufferBytes) == all, "O4 split changes values");
    }
    return 0;
}
