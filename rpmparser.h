#ifndef RPMPARSER_H
#define RPMPARSER_H

// Serial RPM line protocol, extracted from MainWindow::readSerialData() so it can be
// unit-tested without a serial port. Behaviour is identical to the original inline code:
//   - bytes are appended to a persistent buffer
//   - if the buffer exceeds maxBufferBytes, the OLDEST bytes are discarded
//   - every '\n'-terminated line is consumed; lines longer than maxLineLen are skipped
//   - the line is trimmed (so "\r\n" and surrounding spaces are tolerated) and parsed as a
//     base-10 int; non-integers / out-of-int-range values are skipped
//   - an unterminated tail stays in the buffer for the next call
// Post-project fixes: the whole trimmed line must be [+|-]digits (DEF-SW-04) and the value must be
// <= kMaxRpm (DEF-SW-01), see parseLine().

#include <QByteArray>
#include <QVector>

namespace RpmParser {

// Largest RPM the application can process without undefined behaviour (DEF-SW-01):
// MainWindow::update_values() computes speed = rpm * (4.5/2.5) * (pi*2.5) * 10/60 in double and
// converts it to int. For 911420367 that is 2147483646.97 (fits); for 911420368 it is
// 2147483649.33 (> INT_MAX). Derived from the code and the int type, not from a vehicle RPM range.
// tst_mainwindow re-computes both values with the same expression.
constexpr int kMaxRpm = 911420367;

// One line -> raw RPM. After trimming ASCII whitespace the line must be an optional sign followed
// by one or more ASCII digits, and fit in int. Anything else is rejected, including an embedded
// NUL: Qt 5.12 QString::toInt() stops at a NUL and accepted "7\0abc" as 7 (DEF-SW-04).
// Values above kMaxRpm are rejected like out-of-int-range values (DEF-SW-01); negative values are
// accepted here and clamped to 0 by correct().
inline bool parseLine(const QByteArray &line, int &value)
{
    const QByteArray t = line.trimmed();
    const int first = (!t.isEmpty() && (t.at(0) == '+' || t.at(0) == '-')) ? 1 : 0;
    if (first == t.size())
        return false;                                   // empty, or a sign alone
    for (int i = first; i < t.size(); ++i)
        if (t.at(i) < '0' || t.at(i) > '9')
            return false;
    bool ok = false;
    value = t.toInt(&ok, 10);                           // digits only here; ok == false on overflow
    return ok && value <= kMaxRpm;
}

inline QVector<int> consume(QByteArray &buffer, const QByteArray &chunk,
                            int maxLineLen, int maxBufferBytes)
{
    QVector<int> values;
    buffer.append(chunk);

    if (buffer.size() > maxBufferBytes)
        buffer.remove(0, buffer.size() - maxBufferBytes);

    int nl;
    while ((nl = buffer.indexOf('\n')) != -1) {
        QByteArray line = buffer.left(nl);
        buffer.remove(0, nl + 1);

        if (line.size() > maxLineLen) continue;

        int rawRpm = 0;
        if (!parseLine(line, rawRpm)) continue;

        values.append(rawRpm);
    }
    return values;
}

// MainWindow::correctRpmValue(): negative values are clamped to 0. The upper bound is enforced
// when parsing (kMaxRpm), so correct() itself has none.
inline int correct(int rawRpm)
{
    return rawRpm < 0 ? 0 : rawRpm;
}

} // namespace RpmParser

#endif // RPMPARSER_H
