#define main guardianApplicationMain
#include "../src/main.cpp"
#undef main
#include <iostream>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
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
    check(!isKnownGameExecutable("steamwebhelper.exe") && !isKnownGameExecutable("steam.exe"), "launchers are not games");
    check(!isKnownGameExecutable("javaw.exe") && isKnownGameExecutable("Raft.exe"), "Java is not automatically a game");
    check(!inHistoryWindow(start.addSecs(1), start, 24) && inHistoryWindow(start.addSecs(-3600), start, 1), "history has inclusive past boundary and no future rows");
    QVector<ProcessHistoryPoint> javaGrowth;
    for (int i = 0; i <= 120; ++i) javaGrowth.push_back({start.addSecs(i * 30), 100, quint64(100 + i * 10)});
    check(analyzeLeak(javaGrowth, false, "javaw.exe").label == ko("지속 증가 관찰"), "Java defers diagnosis without GC data");
    check(analyzeLeak(javaGrowth, false, "chrome.exe").label == ko("지속 증가 관찰"), "browser growth is not a confirmed leak");

    const QString fixtureDir = QCoreApplication::applicationDirPath() + "/test-reports";
    QDir().mkpath(fixtureDir);
    const auto historicalEnd = QDateTime::currentDateTime().addDays(-2);
    auto writeFixture = [&](const QString &name, const QString &data) {
        QFile file(fixtureDir + "/" + name);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        return file.write(data.toUtf8()) >= 0;
    };
    QString rows = "time,load,ram,commit,limit,page,total,nonpaged,paged\n";
    for (int i = 0; i < 20; ++i) rows += historicalEnd.addSecs(-3600 + i * 180).toString(Qt::ISODate)
        + QString(",60,%1,22000,40000,100,2000,800,900\n").arg(16000 + i * 100);
    check(writeFixture("long-term-trends.csv", rows), "historical trend fixture saved");
    check(writeFixture("trend-events.csv", "time,label,type\n" + historicalEnd.addSecs(-900).toString(Qt::ISODate)
        + ",javaw.exe observation,warning\n"), "historical event fixture saved");
    GuardianWindow window(false, true);
    auto *tabs = window.findChild<QTabWidget *>();
    tabs->setCurrentIndex(2);
    window.findChild<QDateTimeEdit *>()->setDateTime(historicalEnd);
    for (auto *button : window.findChildren<QPushButton *>()) if (button->text() == ko("조회")) button->click();
    auto *model = window.findChild<QStandardItemModel *>();
    check(model && model->rowCount() == 1, "historical event displayed independently of today's date");
    auto *proxy = window.findChild<QSortFilterProxyModel *>();
    proxy->setFilterFixedString("javaw");
    check(proxy->rowCount() == 1, "event search matches process name");
    proxy->setFilterFixedString("unmatched");
    check(proxy->rowCount() == 0, "event search hides unmatched rows");
    proxy->setFilterFixedString("");
    for (auto *combo : window.findChildren<QComboBox *>()) {
        if (combo->findText(ko("라이트 모드")) >= 0) combo->setCurrentIndex(combo->findText(ko("라이트 모드")));
    }
    window.show();
    app.processEvents();
    check(window.grab().save(QCoreApplication::applicationDirPath() + "/history-light.png"), "history screenshot saved");
    for (auto *combo : window.findChildren<QComboBox *>()) {
        if (combo->findText(ko("다크 모드")) >= 0) combo->setCurrentIndex(combo->findText(ko("다크 모드")));
    }
    window.resize(940, 640);
    app.processEvents();
    check(window.grab().save(QCoreApplication::applicationDirPath() + "/history-compact.png"), "compact history screenshot saved");
    tabs->setCurrentIndex(0);
    app.processEvents();
    check(window.grab().save(QCoreApplication::applicationDirPath() + "/dashboard-compact.png"), "compact dashboard screenshot saved");
    if (QCoreApplication::arguments().contains("--installed-history")) {
        const QString source = qEnvironmentVariable("ProgramFiles") + "/Memory Guardian/reports/";
        for (const QString &name : {QString("long-term-trends.csv"), QString("trend-events.csv")}) {
            QFile file(source + name);
            check(file.open(QIODevice::ReadOnly), "installed history readable without modification");
            check(writeFixture(name, QString::fromUtf8(file.readAll())), "installed history copied to isolated test directory");
        }
        tabs->setCurrentIndex(2);
        for (auto *button : window.findChildren<QPushButton *>()) if (button->text() == ko("현재 기록")) button->click();
        check(model->rowCount() > 0, "installed events visible in history page");
        window.resize(1100, 800);
        app.processEvents();
        check(window.grab().save(QCoreApplication::applicationDirPath() + "/history-installed.png"), "installed history screenshot saved");
    }
    return failures == 0 ? 0 : 1;
}
