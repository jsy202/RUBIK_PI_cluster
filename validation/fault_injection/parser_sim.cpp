// Parser-level fault-injection simulation (no GUI, no serial port, no real time).
// Uses the production headers unchanged: rpmparser.h (format check) and framecheck.h (CRC-8).
//
// A synthetic RPM profile (same shape as MainWindow::generateSimulatedRpm: 2500 +- 1700 sine,
// +-60 noise, one frame per 20 ms) is sent line by line. Faults are injected with a fixed seed at an
// average rate of one per <frames_per_fault> frames; type (bit flip / byte delete / byte insert) and
// position are random. The SAME fault set is replayed through three receivers:
//   1  format check only (current behaviour)
//   2  format check + change-width limit (D = 200 and D = 100; 3 consecutive rejects are accepted)
//   3  CRC-8 protected lines
// Screen exposure is derived from the 30 Hz throttle (33 ms flush, latest accepted sample wins).
//
// usage: parser_sim [frames=9000000] [frames_per_fault=32400] [seed=20261123]
#include "rpmparser.h"
#include "framecheck.h"
#include <random>
#include <vector>
#include <map>
#include <cmath>
#include <cstdio>
#include <cstdlib>
using std::vector;

struct Fault { long frame; int type; double pos; int bit; int byte; };   // 0 flip, 1 delete, 2 insert
struct Emit { long frame; int value; };
enum Stage { PLAIN, RANGE, CRC };

static void applyFault(QByteArray &l, const Fault &f)
{
    const int len = l.size();
    if (f.type == 0)      { const int p = int(f.pos * len);       l[p] = l[p] ^ (1 << f.bit); }
    else if (f.type == 1) { const int p = int(f.pos * len);       l.remove(p, 1); }
    else                  { const int p = int(f.pos * (len + 1)); l.insert(p, char(f.byte)); }
}

struct Out { vector<Emit> em; long rejects = 0; };

static Out run(Stage st, int D, const vector<int> &truth, const std::map<long, Fault> &faults)
{
    Out o; QByteArray buf; int lastAcc = -1, consec = 0;
    for (long i = 0; i < (long)truth.size(); ++i) {
        QByteArray l = (st == CRC) ? FrameCheck::encode(truth[i]) : QByteArray::number(truth[i]) + '\n';
        auto it = faults.find(i);
        if (it != faults.end()) applyFault(l, it->second);
        if (st == CRC) {
            const FrameCheck::Result r = FrameCheck::consume(buf, l, 64, 8192);
            o.rejects += r.rejects.size();
            for (int x : r.values) o.em.push_back({i, RpmParser::correct(x)});
        } else {
            const QVector<int> vals = RpmParser::consume(buf, l, 64, 8192);
            for (int x : vals) {
                x = RpmParser::correct(x);
                if (st == RANGE) {
                    if (lastAcc >= 0 && std::abs(x - lastAcc) > D) { o.rejects++; if (++consec < 3) continue; }
                    consec = 0; lastAcc = x;
                }
                o.em.push_back({i, x});
            }
        }
    }
    return o;
}

int main(int argc, char **argv)
{
    const long N = argc > 1 ? atol(argv[1]) : 9000000;
    const double fpf = argc > 2 ? atof(argv[2]) : 32400.0;
    const unsigned long long seed = argc > 3 ? strtoull(argv[3], nullptr, 10) : 20261123ULL;
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> U(0, 1);

    vector<int> truth(N);
    for (long i = 0; i < N; ++i) {
        const double t = i * 0.02;
        truth[i] = int(2500.0 + 1700.0 * std::sin(t * 1.3)) + int(rng() % 121) - 60;
    }
    std::map<long, Fault> faults; long cnt[3] = {0, 0, 0};
    for (long i = 2; i < N - 2; ++i)
        if (U(rng) < 1.0 / fpf) {
            Fault f{i, int(rng() % 3), U(rng), int(rng() % 8), int(rng() % 256)};
            faults[i] = f; cnt[f.type]++;
        }
    printf("# parser-level simulation: frames=%ld (20 ms => %.1f h), seed=%llu, 1 fault per ~%.0f frames\n",
           N, N * 0.02 / 3600.0, seed, fpf);
    printf("# injected=%zu (bit flip %ld / delete %ld / insert %ld)\n\n", faults.size(), cnt[0], cnt[1], cnt[2]);

    struct Cfg { Stage st; int D; const char *name; } cfgs[] = {
        {PLAIN, 0, "1 format check only (current)"}, {RANGE, 200, "2a change-width limit D=200"},
        {RANGE, 100, "2b change-width limit D=100"}, {CRC, 0, "3 CRC-8"}};
    const std::map<long, Fault> none;
    printf("stage | wrong accepted | wrong shown on screen | clean frames wrongly dropped | rejects logged\n");
    for (const auto &c : cfgs) {
        const Out o = run(c.st, c.D, truth, faults);
        const Out clean = run(c.st, c.D, truth, none);
        long wrong = 0, shown = 0;
        for (size_t k = 0; k < o.em.size(); ++k) {
            const Emit &e = o.em[k];
            if (e.value == std::max(0, truth[e.frame])) continue;
            ++wrong;
            const long tms = e.frame * 20, nextFlush = (tms / 33 + 1) * 33;
            if (k + 1 >= o.em.size() || o.em[k + 1].frame * 20 > nextFlush) ++shown;
        }
        vector<char> has(N, 0); for (const auto &e : clean.em) has[e.frame] = 1;
        long dropped = 0; for (long i = 0; i < N; ++i) dropped += !has[i];
        printf("%s | %ld | %ld | %ld / %ld | %ld\n", c.name, wrong, shown, dropped, N, o.rejects);
    }
    return 0;
}
