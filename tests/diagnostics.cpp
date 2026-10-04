#define main guardianApplicationMain
#include "../src/main.cpp"
#undef main
#include <iostream>

int main() {
    int failures = 0;
    auto check = [&](bool passed, const char *name) {
        std::cout << (passed ? "PASS " : "FAIL ") << name << '\n';
        if (!passed) ++failures;
    };
    MemorySnapshot sample;
    sample.valid = true;
    sample.totalMb = 16384;
    sample.availableMb = 100;
    sample.load = 70;
    sample.commitMb = 99;
    sample.commitLimitMb = 100;
    QVector<MemorySnapshot> history(3, sample);
    check(!evaluateQi(history, 80).shouldOptimize, "low score alone never cleans");
    history.last().load = 90;
    check(!evaluateQi(history, 80).shouldOptimize, "single spike never cleans");
    for (auto &s : history) s.load = 90;
    check(evaluateQi(history, 80).shouldOptimize, "three high samples trigger");
    history.last().valid = false;
    check(!evaluateQi(history, 80).shouldOptimize, "invalid sample blocks cleanup");
    check(!evaluateQi({}, 80).shouldOptimize, "empty history never cleans");
    history = QVector<MemorySnapshot>(1, sample);
    check(!evaluateQi(history, 60).shouldOptimize, "startup never cleans immediately");
    sample.totalMb = 4096;
    sample.availableMb = 2048;
    check(evaluateQi({sample}, 80).available == 0, "small PC with half RAM free is not penalized");
    const auto live = readMemory();
    check(live.valid && live.totalMb > 0, "Windows metrics readable");
    check(live.pageFileUsedMb <= live.pageFileTotalMb, "pagefile usage within capacity");
    check(live.commitMb <= live.commitLimitMb, "commit usage within limit");
    check(!readTopProcesses().isEmpty(), "background process collector returns data");
    const auto start = QDateTime::fromSecsSinceEpoch(1700000000);
    QVector<ProcessHistoryPoint> points;
    for (int i = 0; i <= 240; ++i) recordProcessSample(points, {start.addSecs(i * 15), 100, quint64(100 + i * 3)});
    check(points.size() == 121, "15-second scans retain 30-second history");
    check(analyzeLeak(points).label == ko("누수 의심"), "sustained private growth flagged");
    check(analyzeLeak(points, true).label != ko("누수 의심"), "developer allowance applied");
    auto stablePrivate = points;
    for (int i = 0; i < stablePrivate.size(); ++i) {
        stablePrivate[i].privateMb = 100;
        stablePrivate[i].usedMb = 100 + i * 20;
    }
    check(analyzeLeak(stablePrivate).label == ko("관측 범위 내 안정"), "working set growth alone is not a leak");
    auto spike = points;
    for (int i = 0; i < spike.size(); ++i) spike[i].privateMb = i >= 50 && i <= 70 ? 2000 : 100;
    check(analyzeLeak(spike).label == ko("증가 후 회수 관측"), "spike with recovery is distinguished");
    auto step = points;
    for (int i = 0; i < step.size(); ++i) step[i].privateMb = i < 60 ? 100 : 2000;
    check(analyzeLeak(step).label != ko("누수 의심"), "single allocation then plateau is not sustained");
    check(analyzeLeak(points.mid(0, 40)).label == ko("관찰 중"), "short history cannot diagnose");
    auto gap = points;
    gap.remove(30, 15);
    check(analyzeLeak(gap).label == ko("관찰 중"), "measurement gap blocks diagnosis");
    auto decreasing = points;
    for (int i = 0; i < decreasing.size(); ++i) decreasing[i].privateMb = 2000 - i * 5;
    check(analyzeLeak(decreasing).label == ko("감소 중"), "decreasing private memory recognized");
    recordProcessSample(points, {start.addSecs(4000), 100, 100});
    check(points.size() == 1, "collection resets after a gap");
    CleanupResult cleanup;
    cleanup.succeeded = true;
    cleanup.measured = true;
    cleanup.beforeBytes = 64ULL * 1024 * 1024;
    cleanup.afterBytes = 16ULL * 1024 * 1024;
    check(cleanupHadReduction(cleanup), "cleanup reduction measured on own working set");
    cleanup.measured = false;
    check(!cleanupHadReduction(cleanup), "failed measurement is not success");
    cleanup.measured = true;
    cleanup.afterBytes = 80ULL * 1024 * 1024;
    check(!cleanupHadReduction(cleanup), "increase cannot underflow into claimed savings");
    check(cleanupCooldownMinutes(0) == 10 && cleanupCooldownMinutes(1) == 20
        && cleanupCooldownMinutes(2) == 40 && cleanupCooldownMinutes(3) == 60
        && cleanupCooldownMinutes(100) == 60, "cleanup backoff is bounded");
    return failures == 0 ? 0 : 1;
}
