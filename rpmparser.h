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

#include <QByteArray>
#include <QString>
#include <QVector>

namespace RpmParser {

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

        bool ok = false;
        const int rawRpm = QString::fromLatin1(line.trimmed()).toInt(&ok);
        if (!ok) continue;

        values.append(rawRpm);
    }
    return values;
}

// MainWindow::correctRpmValue(): negative values are clamped to 0, no upper bound.
inline int correct(int rawRpm)
{
    return rawRpm < 0 ? 0 : rawRpm;
}

} // namespace RpmParser

#endif // RPMPARSER_H
