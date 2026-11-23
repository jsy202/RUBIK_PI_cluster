#ifndef FRAMECHECK_H
#define FRAMECHECK_H

// Per-line integrity check for the serial RPM link ("value,HH\n"), enabled with CLUSTER_PROTECT=1.
//
// Why: a format-only parser accepts any digit string, so a corrupted but still well-formed number
// (e.g. 2500 -> 3500) is shown as if it were a real reading. Each line therefore carries a CRC-8 of
// its value text; a line whose check does not match is dropped and recorded instead of displayed.
//
// CRC-8: polynomial 0x1D, initial value 0, no reflection, no final XOR (catalogue name CRC-8/GSM-A).
// Inspired by the CRC idea used by CAN and AUTOSAR E2E; this is NOT an E2E profile implementation.
// It only protects the link between the sender's CRC computation and this receiver: a value that was
// already wrong before the sender computed the CRC passes.

#include <QByteArray>
#include <QVector>

#include "rpmparser.h"

namespace FrameCheck {

enum class Reject { Format, Crc, TooLong };

struct RejectInfo {
    Reject reason;
    QByteArray raw;      // the line exactly as received (without '\n')
};

struct Result {
    QVector<int> values;             // accepted raw RPM values (negative clamping is done by the caller)
    QVector<RejectInfo> rejects;     // every dropped line, in order
};

inline unsigned crc8(const QByteArray &data)
{
    unsigned crc = 0;
    for (const char ch : data) {
        crc ^= static_cast<unsigned char>(ch);
        for (int i = 0; i < 8; ++i)
            crc = (crc & 0x80u) ? (((crc << 1) ^ 0x1Du) & 0xFFu) : ((crc << 1) & 0xFFu);
    }
    return crc;
}

inline QByteArray encode(int value)
{
    const QByteArray text = QByteArray::number(value);
    char hex[8];
    snprintf(hex, sizeof hex, "%02X", crc8(text));
    return text + ',' + QByteArray(hex) + '\n';
}

inline Result consume(QByteArray &buffer, const QByteArray &chunk, int maxLineLen, int maxBufferBytes)
{
    Result result;
    buffer.append(chunk);
    if (buffer.size() > maxBufferBytes)
        buffer.remove(0, buffer.size() - maxBufferBytes);

    int nl;
    while ((nl = buffer.indexOf('\n')) != -1) {
        const QByteArray line = buffer.left(nl);
        buffer.remove(0, nl + 1);

        if (line.size() > maxLineLen) {
            result.rejects.append({Reject::TooLong, line});
            continue;
        }
        const QByteArray trimmed = line.trimmed();
        const int comma = trimmed.lastIndexOf(',');
        if (comma < 0 || trimmed.size() - comma - 1 != 2) {
            result.rejects.append({Reject::Format, line});
            continue;
        }
        const QByteArray body = trimmed.left(comma);
        bool hexOk = false;
        const unsigned got = trimmed.mid(comma + 1).toUInt(&hexOk, 16);
        if (!hexOk) {
            result.rejects.append({Reject::Format, line});
            continue;
        }
        if (got != crc8(body)) {
            result.rejects.append({Reject::Crc, line});
            continue;
        }
        int value = 0;
        if (!RpmParser::parseLine(body, value)) {
            result.rejects.append({Reject::Format, line});
            continue;
        }
        result.values.append(value);
    }
    return result;
}

} // namespace FrameCheck

#endif // FRAMECHECK_H
