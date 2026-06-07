#include <QtWidgets>
#include <QtNetwork>
#include <algorithm>
#include <cmath>
#include <limits>
#include <windows.h>
#include <psapi.h>
#include <shellapi.h>
#include <tlhelp32.h>

using NtSetSystemInformationProc = LONG (WINAPI *)(ULONG, PVOID, ULONG);
static const char *APP_VERSION = "1.3.9";

static QString ko(const char *text) {
    return QString::fromUtf8(text);
}

static bool systemPrefersDarkMode() {
    QSettings settings("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                       QSettings::NativeFormat);
    QVariant value = settings.value("AppsUseLightTheme");
    if (!value.isValid()) {
        return false;
    }

    return value.toInt() == 0;
}

static int compareVersions(const QString &left, const QString &right) {
    QStringList a = left.split('.');
    QStringList b = right.split('.');
    int count = qMax(a.size(), b.size());
    for (int i = 0; i < count; ++i) {
        int av = i < a.size() ? a[i].toInt() : 0;
        int bv = i < b.size() ? b[i].toInt() : 0;
        if (av != bv) {
            return av < bv ? -1 : 1;
        }
    }
    return 0;
}

static bool runningAsAdmin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = nullptr;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuthority,
                                 2,
                                 SECURITY_BUILTIN_DOMAIN_RID,
                                 DOMAIN_ALIAS_RID_ADMINS,
                                 0,
                                 0,
                                 0,
                                 0,
                                 0,
                                 0,
                                 &adminGroup)) {
        CheckTokenMembership(nullptr, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin == TRUE;
}

static QString quotedArgument(const QString &value) {
    QString escaped = value;
    escaped.replace('"', "\\\"");
    return '"' + escaped + '"';
}

struct MemorySnapshot {
    QDateTime time;
    DWORD load = 0;
    quint64 totalMb = 0;
    quint64 usedMb = 0;
    quint64 availableMb = 0;
    quint64 commitMb = 0;
    quint64 commitLimitMb = 0;
    quint64 pageFileUsedMb = 0;
    quint64 pageFileTotalMb = 0;
    quint64 nonPagedPoolMb = 0;
    quint64 pagedPoolMb = 0;
};

struct QiState {
    int score = 100;
    int pressure = 0;
    int available = 0;
    int trend = 0;
    bool shouldOptimize = false;
};

struct HourStats {
    int count = 0;
    quint64 loadSum = 0;
    quint64 usedSum = 0;
    DWORD peakLoad = 0;
    quint64 peakUsedMb = 0;
};

struct ProcessUsage {
    QString name;
    QString executablePath;
    DWORD pid = 0;
    quint64 usedMb = 0;
};

struct ProcessTrend {
    QString name;
    DWORD pid = 0;
    quint64 firstMb = 0;
    quint64 lastMb = 0;
    QDateTime firstSeen;
    QDateTime lastSeen;
};

struct ServerProcessSummary {
    int node = 0;
    int python = 0;
    int java = 0;
    bool tailscale = false;
    bool cloudflare = false;
};

struct LongTermPoint {
    QDateTime time;
    int load = 0;
    quint64 usedMb = 0;
    quint64 commitMb = 0;
    quint64 nonPagedPoolMb = 0;
    quint64 pagedPoolMb = 0;
};

class HourChartWidget : public QWidget {
public:
    explicit HourChartWidget(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumHeight(190);
        setMouseTracking(true);
    }

    void setStats(const HourStats source[24], bool dark) {
        for (int i = 0; i < 24; ++i) {
            stats[i] = source[i];
        }
        darkMode = dark;
        update();
    }

protected:
    bool event(QEvent *event) override {
        if (event->type() == QEvent::ToolTip) {
            auto *helpEvent = static_cast<QHelpEvent *>(event);
            int hour = hourAt(helpEvent->pos());
            if (hour >= 0 && stats[hour].count > 0) {
                int average = int(stats[hour].loadSum / quint64(stats[hour].count));
                quint64 used = stats[hour].usedSum / quint64(stats[hour].count);
                QToolTip::showText(helpEvent->globalPos(),
                                   ko("%1:00\nRAM %2%\n%3 MB")
                                       .arg(hour, 2, 10, QChar('0'))
                                       .arg(average)
                                       .arg(used),
                                   this);
            } else {
                QToolTip::hideText();
                event->ignore();
            }
            return true;
        }
        return QWidget::event(event);
    }

    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        QColor text = darkMode ? QColor("#cbd5e1") : QColor("#334155");
        QColor muted = darkMode ? QColor("#64748b") : QColor("#94a3b8");
        QColor bar = QColor("#2563eb");
        QColor track = darkMode ? QColor("#1e293b") : QColor("#e7ecf4");

        QRect area = rect().adjusted(12, 12, -12, -24);
        int gap = 5;
        int barWidth = qMax(8, (area.width() - gap * 23) / 24);

        painter.setPen(text);
        painter.drawText(12, 18, ko("시간대별 평균 RAM 사용률"));

        int baseY = area.bottom();
        int topY = area.top() + 24;
        int maxHeight = baseY - topY;

        for (int i = 0; i < 24; ++i) {
            int x = area.left() + i * (barWidth + gap);
            int average = stats[i].count > 0 ? int(stats[i].loadSum / quint64(stats[i].count)) : 0;
            int h = qMax(4, maxHeight * average / 100);
            QRect trackRect(x, topY, barWidth, maxHeight);
            QRect barRect(x, baseY - h, barWidth, h);

            painter.setPen(Qt::NoPen);
            painter.setBrush(track);
            painter.drawRoundedRect(trackRect, 4, 4);
            painter.setBrush(average >= 85 ? QColor("#f59e0b") : bar);
            painter.drawRoundedRect(barRect, 4, 4);

            if (i % 3 == 0) {
                painter.setPen(muted);
                painter.drawText(QRect(x - 4, baseY + 4, barWidth + 12, 18),
                                 Qt::AlignCenter,
                                 QString::number(i));
            }
        }
    }

private:
    HourStats stats[24] {};
    bool darkMode = false;

    QRect chartArea() const {
        return rect().adjusted(12, 12, -12, -24);
    }

    int hourAt(const QPoint &point) const {
        QRect area = chartArea();
        int gap = 5;
        int barWidth = qMax(8, (area.width() - gap * 23) / 24);
        int relativeX = point.x() - area.left();
        if (relativeX < 0) {
            return -1;
        }
        int slot = barWidth + gap;
        int hour = relativeX / slot;
        int within = relativeX % slot;
        if (hour < 0 || hour >= 24 || within > barWidth) {
            return -1;
        }
        return hour;
    }
};

class LongTermChartWidget : public QWidget {
public:
    explicit LongTermChartWidget(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumHeight(260);
        setMouseTracking(true);
    }

    void setPoints(const QVector<LongTermPoint> &source, bool dark) {
        points = source;
        darkMode = dark;
        update();
    }

protected:
    bool event(QEvent *event) override {
        if (event->type() == QEvent::ToolTip) {
            auto *helpEvent = static_cast<QHelpEvent *>(event);
            int index = pointAt(helpEvent->pos());
            if (index >= 0 && index < points.size()) {
                const LongTermPoint &point = points[index];
                qint64 ramDelta = qint64(point.usedMb) - qint64(points.first().usedMb);
                qint64 commitDelta = qint64(point.commitMb) - qint64(points.first().commitMb);
                qint64 poolDelta = qint64(point.nonPagedPoolMb) - qint64(points.first().nonPagedPoolMb);
                QToolTip::showText(helpEvent->globalPos(),
                                   ko("%1\nRAM %2% · %3 MB (%4%5 MB)\n커밋 %6 MB (%7%8 MB)\nNon-Paged Pool %9 MB (%10%11 MB)")
                                       .arg(point.time.toString("yyyy-MM-dd HH:mm"))
                                       .arg(point.load)
                                       .arg(point.usedMb)
                                       .arg(ramDelta >= 0 ? "+" : "")
                                       .arg(ramDelta)
                                       .arg(point.commitMb)
                                       .arg(commitDelta >= 0 ? "+" : "")
                                       .arg(commitDelta)
                                       .arg(point.nonPagedPoolMb)
                                       .arg(poolDelta >= 0 ? "+" : "")
                                       .arg(poolDelta),
                                   this);
            } else {
                QToolTip::hideText();
                event->ignore();
            }
            return true;
        }
        return QWidget::event(event);
    }

    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        QColor text = darkMode ? QColor("#cbd5e1") : QColor("#334155");
        QColor muted = darkMode ? QColor("#64748b") : QColor("#94a3b8");
        QColor grid = darkMode ? QColor("#243244") : QColor("#e2e8f0");
        QRect area = rect().adjusted(18, 28, -18, -34);

        painter.setPen(text);
        painter.drawText(18, 18, ko("장기 메모리 추세"));

        painter.setPen(QPen(grid, 1));
        for (int i = 0; i <= 4; ++i) {
            int y = area.top() + area.height() * i / 4;
            painter.drawLine(area.left(), y, area.right(), y);
        }

        if (points.size() < 2) {
            painter.setPen(muted);
            painter.drawText(area, Qt::AlignCenter, ko("아직 장기 추세 데이터가 부족합니다."));
            return;
        }

        auto drawSeries = [&](auto valueFn, QColor color, quint64 maxValue) {
            QPainterPath path;
            for (int i = 0; i < points.size(); ++i) {
                double x = double(area.left()) + double(area.width()) * double(i) / double(points.size() - 1);
                double ratio = maxValue > 0 ? double(valueFn(points[i])) / double(maxValue) : 0.0;
                ratio = std::clamp(ratio, 0.0, 1.0);
                double y = double(area.bottom()) - double(area.height()) * ratio;
                if (i == 0) {
                    path.moveTo(x, y);
                } else {
                    path.lineTo(x, y);
                }
            }
            painter.setPen(QPen(color, 2.4));
            painter.drawPath(path);
        };

        quint64 maxMb = 1;
        for (const LongTermPoint &point : points) {
            maxMb = std::max(maxMb, point.usedMb);
            maxMb = std::max(maxMb, point.commitMb);
            maxMb = std::max(maxMb, point.nonPagedPoolMb * 10);
        }

        drawSeries([](const LongTermPoint &p) { return p.usedMb; }, QColor("#2563eb"), maxMb);
        drawSeries([](const LongTermPoint &p) { return p.commitMb; }, QColor("#10b981"), maxMb);
        drawSeries([](const LongTermPoint &p) { return p.nonPagedPoolMb * 10; }, QColor("#f59e0b"), maxMb);

        painter.setPen(text);
        painter.drawText(18, rect().bottom() - 10, ko("파랑 RAM · 초록 커밋 · 주황 Non-Paged Pool x10"));
        painter.setPen(muted);
        painter.drawText(rect().adjusted(18, 0, -18, -10), Qt::AlignRight | Qt::AlignBottom,
                         ko("%1개 기록").arg(points.size()));
    }

private:
    QVector<LongTermPoint> points;
    bool darkMode = false;

    QRect chartArea() const {
        return rect().adjusted(18, 28, -18, -34);
    }

    int pointAt(const QPoint &point) const {
        if (points.size() < 2) {
            return -1;
        }
        QRect area = chartArea();
        if (!area.adjusted(-8, -16, 8, 16).contains(point)) {
            return -1;
        }
        double ratio = double(point.x() - area.left()) / double(qMax(1, area.width()));
        int index = int(std::round(std::clamp(ratio, 0.0, 1.0) * double(points.size() - 1)));
        return std::clamp(index, 0, int(points.size()) - 1);
    }
};

static MemorySnapshot readMemory() {
    MEMORYSTATUSEX memory {};
    memory.dwLength = sizeof(memory);
    GlobalMemoryStatusEx(&memory);

    MemorySnapshot snapshot;
    snapshot.time = QDateTime::currentDateTime();
    snapshot.load = memory.dwMemoryLoad;
    snapshot.totalMb = memory.ullTotalPhys / (1024ULL * 1024ULL);
    snapshot.availableMb = memory.ullAvailPhys / (1024ULL * 1024ULL);
    snapshot.usedMb = snapshot.totalMb - snapshot.availableMb;
    snapshot.pageFileTotalMb = memory.ullTotalPageFile / (1024ULL * 1024ULL);
    snapshot.pageFileUsedMb = (memory.ullTotalPageFile - memory.ullAvailPageFile) / (1024ULL * 1024ULL);

    PERFORMANCE_INFORMATION perf {};
    perf.cb = sizeof(perf);
    if (GetPerformanceInfo(&perf, sizeof(perf))) {
        snapshot.commitMb = quint64(perf.CommitTotal) * perf.PageSize / (1024ULL * 1024ULL);
        snapshot.commitLimitMb = quint64(perf.CommitLimit) * perf.PageSize / (1024ULL * 1024ULL);
        snapshot.nonPagedPoolMb = quint64(perf.KernelNonpaged) * perf.PageSize / (1024ULL * 1024ULL);
        snapshot.pagedPoolMb = quint64(perf.KernelPaged) * perf.PageSize / (1024ULL * 1024ULL);
    }
    return snapshot;
}

static QVector<ProcessUsage> readTopProcesses(int limit = 0) {
    QVector<ProcessUsage> results;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return results;
    }

    PROCESSENTRY32W entry {};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot, &entry)) {
        CloseHandle(snapshot);
        return results;
    }

    do {
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, entry.th32ProcessID);
        if (!process) {
            continue;
        }

        PROCESS_MEMORY_COUNTERS_EX counters {};
        if (GetProcessMemoryInfo(process,
                                 reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),
                                 sizeof(counters))) {
            quint64 mb = counters.WorkingSetSize / (1024ULL * 1024ULL);
            if (mb > 0) {
                ProcessUsage usage;
                usage.name = QString::fromWCharArray(entry.szExeFile);
                usage.pid = entry.th32ProcessID;
                usage.usedMb = mb;
                wchar_t pathBuffer[32768] {};
                DWORD pathLength = 32768;
                if (QueryFullProcessImageNameW(process, 0, pathBuffer, &pathLength)) {
                    usage.executablePath = QString::fromWCharArray(pathBuffer, int(pathLength));
                }
                results.push_back(usage);
            }
        }
        CloseHandle(process);
    } while (Process32NextW(snapshot, &entry));

    CloseHandle(snapshot);
    std::sort(results.begin(), results.end(), [](const ProcessUsage &a, const ProcessUsage &b) {
        return a.usedMb > b.usedMb;
    });
    if (limit > 0) {
        while (results.size() > limit) {
            results.pop_back();
        }
    }
    return results;
}

static bool purgeMemoryList(ULONG command) {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) {
        return false;
    }

    FARPROC raw = GetProcAddress(ntdll, "NtSetSystemInformation");
    if (!raw) {
        return false;
    }

    union {
        FARPROC raw;
        NtSetSystemInformationProc typed;
    } proc {};
    proc.raw = raw;

    LONG status = proc.typed(80, &command, sizeof(command));
    return status >= 0;
}

static QString optimizeMemory() {
    bool workingSet = EmptyWorkingSet(GetCurrentProcess());
    bool allWorkingSets = purgeMemoryList(2);
    bool standby = purgeMemoryList(4);

    if (standby || allWorkingSets || workingSet) {
        return ko("메모리 정리를 요청했습니다. 관리자 권한이라면 대기 메모리까지 더 잘 정리됩니다.");
    }

    return ko("정리를 시도했지만 권한이 부족할 수 있습니다. 관리자 권한 실행을 권장합니다.");
}

static QiState evaluateQi(const QVector<MemorySnapshot> &samples, int threshold) {
    QiState qi;
    if (samples.isEmpty()) {
        return qi;
    }

    const MemorySnapshot newest = samples.last();
    const MemorySnapshot oldest = samples.first();

    int pressurePenalty = std::clamp((int(newest.load) - 55) * 2, 0, 70);
    int availablePenalty = 0;
    if (newest.availableMb < 2048) {
        availablePenalty = 18;
    } else if (newest.availableMb < 4096) {
        availablePenalty = 8;
    }

    int trendPenalty = 0;
    if (newest.load > oldest.load) {
        trendPenalty = std::clamp((int(newest.load) - int(oldest.load)) * 3, 0, 30);
    }

    qi.pressure = pressurePenalty;
    qi.available = availablePenalty;
    qi.trend = trendPenalty;
    qi.score = std::clamp(100 - pressurePenalty - availablePenalty - trendPenalty, 0, 100);
    qi.shouldOptimize = newest.load >= DWORD(threshold) || qi.score <= 45;
    return qi;
}

class GuardianWindow : public QWidget {
public:
    explicit GuardianWindow(bool backgroundStart = false) : startedInBackground(backgroundStart) {
        reportDate = QDate::currentDate();
        loadLearnedProfile();
        setWindowTitle(ko("메모리 자동 보호기"));
        setMinimumSize(980, 700);
        resize(1040, 790);

        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(22, 18, 22, 18);
        root->setSpacing(12);

        auto *header = new QHBoxLayout();
        auto *titleGroup = new QVBoxLayout();
        title = new QLabel(ko("메모리 자동 보호기"));
        subtitle = new QLabel(ko("하루 동안 PC 사용 패턴을 학습해 다음 날부터 자동 정리 기준을 맞춥니다."));
        creatorLabel = new QLabel(ko("HAIMEZ 제작"));
        titleGroup->addWidget(title);
        titleGroup->addWidget(subtitle);
        titleGroup->addWidget(creatorLabel);
        titleGroup->setSpacing(4);

        statusPill = new QLabel(ko("자동 보호 중"));
        statusPill->setAlignment(Qt::AlignCenter);
        header->addLayout(titleGroup, 1);
        header->addWidget(statusPill);
        root->addLayout(header);

        auto *hero = new QFrame();
        hero->setObjectName("hero");
        auto *heroLayout = new QVBoxLayout(hero);
        heroLayout->setContentsMargins(24, 20, 24, 18);
        heroLayout->setSpacing(14);

        auto *metrics = new QHBoxLayout();
        metrics->setContentsMargins(0, 0, 0, 0);
        metrics->setSpacing(22);
        ramPercent = new QLabel("0%");
        qiScore = new QLabel(ko("100점"));
        adaptiveValue = new QLabel("80%");
        qiHelpButton = new QPushButton("?");
        qiHelpButton->setObjectName("helpButton");
        qiHelpButton->setFixedSize(26, 26);
        qiHelpButton->setToolTip(ko("최적화 점수 계산 기준 보기"));
        metrics->addWidget(makeMetric(ko("현재 RAM 사용률"), ramPercent), 1);
        metrics->addWidget(makeMetric(ko("최적화 점수"), qiScore, qiHelpButton), 1);
        metrics->addWidget(makeMetric(ko("자동 정리 기준"), adaptiveValue), 1);
        heroLayout->addLayout(metrics);

        meter = new QProgressBar();
        meter->setRange(0, 100);
        meter->setTextVisible(false);
        heroLayout->addWidget(meter);

        summary = new QLabel(ko("메모리 상태를 확인하는 중입니다."));
        summary->setWordWrap(true);
        heroLayout->addWidget(summary);
        systemDetail = new QLabel(ko("커밋 메모리와 페이지 파일 상태를 확인하는 중입니다."));
        systemDetail->setObjectName("metricLabel");
        systemDetail->setWordWrap(true);
        heroLayout->addWidget(systemDetail);
        root->addWidget(hero);

        auto *controls = new QFrame();
        controls->setObjectName("panel");
        auto *controlLayout = new QGridLayout(controls);
        controlLayout->setContentsMargins(20, 14, 20, 14);
        controlLayout->setHorizontalSpacing(12);
        controlLayout->setVerticalSpacing(7);

        autoTune = new QCheckBox(ko("PC에 맞춰 자동 기준 학습"));
        autoTune->setChecked(true);

        threshold = new QSpinBox();
        threshold->setRange(50, 98);
        threshold->setValue(74);
        threshold->setSuffix("%");
        threshold->setEnabled(false);

        action = new QComboBox();
        action->addItems({ko("자동 정리"), ko("알림만 표시")});

        usageMode = new QComboBox();
        usageMode->addItems({ko("일반 PC"), ko("집 서버/개발 PC")});
        usageMode->setCurrentIndex(0);

        themeMode = new QComboBox();
        themeMode->addItems({ko("시스템 설정"), ko("라이트 모드"), ko("다크 모드")});

        startButton = new QPushButton(ko("보호 중지"));
        startButton->setObjectName("primaryButton");

        reportButton = new QPushButton(ko("오늘 리포트"));
        trendButton = new QPushButton(ko("장기 추세"));
        updateButton = new QPushButton(ko("업데이트 확인"));
        errorReportButton = new QPushButton(ko("오류 신고"));

        controlLayout->addWidget(autoTune, 0, 0, 1, 3);
        controlLayout->addWidget(new QLabel(ko("정리 기준")), 1, 0);
        controlLayout->addWidget(new QLabel(ko("정리 방식")), 1, 1);
        controlLayout->addWidget(new QLabel(ko("운영 모드")), 1, 2);
        controlLayout->addWidget(new QLabel(ko("화면 모드")), 1, 3);
        controlLayout->addWidget(threshold, 2, 0);
        controlLayout->addWidget(action, 2, 1);
        controlLayout->addWidget(usageMode, 2, 2);
        controlLayout->addWidget(themeMode, 2, 3);
        controlLayout->addWidget(startButton, 2, 4);
        controlLayout->addWidget(reportButton, 2, 5);
        controlLayout->addWidget(trendButton, 2, 6);
        controlLayout->addWidget(updateButton, 2, 7);
        controlLayout->addWidget(errorReportButton, 2, 8);
        controlLayout->setColumnMinimumWidth(0, 82);
        controlLayout->setColumnMinimumWidth(1, 112);
        controlLayout->setColumnMinimumWidth(2, 120);
        controlLayout->setColumnMinimumWidth(3, 128);
        controlLayout->setColumnStretch(3, 1);
        controlLayout->setColumnStretch(5, 1);
        controlLayout->setColumnStretch(6, 1);
        controlLayout->setColumnStretch(7, 1);
        controlLayout->setColumnStretch(8, 1);
        root->addWidget(controls);

        auto *reportPanel = new QFrame();
        reportPanel->setObjectName("panel");
        auto *reportLayout = new QGridLayout(reportPanel);
        reportLayout->setContentsMargins(20, 12, 20, 12);
        reportLayout->setHorizontalSpacing(10);
        reportLayout->setVerticalSpacing(6);
        todayAverage = new QLabel(ko("오늘 평균: -"));
        todayPeak = new QLabel(ko("오늘 최고: -"));
        busyHour = new QLabel(ko("가장 무거운 시간: -"));
        leakStatus = new QLabel(ko("누수 의심: 확인 중"));
        optimizeCountLabel = new QLabel(ko("자동 정리: 0회"));
        for (QLabel *label : {todayAverage, todayPeak, busyHour, leakStatus, optimizeCountLabel}) {
            label->setWordWrap(true);
            label->setMinimumHeight(24);
        }
        reportLayout->addWidget(todayAverage, 0, 0);
        reportLayout->addWidget(todayPeak, 0, 1);
        reportLayout->addWidget(busyHour, 0, 2);
        reportLayout->addWidget(optimizeCountLabel, 0, 3);
        reportLayout->addWidget(leakStatus, 1, 0, 1, 4);
        for (int i = 0; i < 5; ++i) {
            reportLayout->setColumnStretch(i, 1);
        }
        root->addWidget(reportPanel);

        serverPanel = new QFrame();
        serverPanel->setObjectName("panel");
        auto *serverLayout = new QHBoxLayout(serverPanel);
        serverLayout->setContentsMargins(20, 12, 20, 12);
        serverLayout->setSpacing(18);
        serverStatus = new QLabel(ko("서버 프로그램을 확인하는 중입니다."));
        securityStatus = new QLabel(ko("원격 접속 상태를 확인하는 중입니다."));
        rebootStatus = new QLabel(ko("재부팅 필요 여부를 확인하는 중입니다."));
        for (QLabel *label : {serverStatus, securityStatus, rebootStatus}) {
            label->setWordWrap(true);
            label->setObjectName("serverCard");
        }
        serverLayout->addWidget(serverStatus, 2);
        serverLayout->addWidget(securityStatus, 2);
        serverLayout->addWidget(rebootStatus, 1);
        root->addWidget(serverPanel);

        lowerTabs = new QTabWidget();
        lowerTabs->setObjectName("lowerTabs");
        auto *processPage = new QWidget();
        auto *processPageLayout = new QHBoxLayout(processPage);
        processPageLayout->setContentsMargins(0, 8, 0, 0);
        processPageLayout->setSpacing(14);
        auto *processPanel = new QFrame();
        processPanel->setObjectName("panel");
        auto *processLayout = new QVBoxLayout(processPanel);
        processLayout->setContentsMargins(20, 14, 20, 14);
        processLayout->setSpacing(8);
        auto *processHeader = new QHBoxLayout();
        auto *processTitle = new QLabel(ko("실행 중인 프로세스"));
        processTitle->setObjectName("metricLabel");
        processCountLabel = new QLabel(ko("확인 중"));
        processCountLabel->setObjectName("metricLabel");
        processSearch = new QLineEdit();
        processSearch->setObjectName("processSearch");
        processSearch->setPlaceholderText(ko("프로세스 이름 또는 PID 검색"));
        processSearch->setClearButtonEnabled(true);
        processSearch->setMaximumWidth(300);
        processHeader->addWidget(processTitle);
        processHeader->addWidget(processCountLabel);
        processHeader->addStretch(1);
        processHeader->addWidget(processSearch);
        topProcesses = new QTableWidget();
        topProcesses->setObjectName("processTable");
        topProcesses->setColumnCount(5);
        topProcesses->setHorizontalHeaderLabels({ko("#"), ko("프로세스"), ko("사용 중"), ko("증가량"), ko("판정")});
        topProcesses->verticalHeader()->setVisible(false);
        topProcesses->horizontalHeader()->setStretchLastSection(true);
        topProcesses->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        topProcesses->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        topProcesses->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        topProcesses->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        topProcesses->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
        topProcesses->setEditTriggers(QAbstractItemView::NoEditTriggers);
        topProcesses->setSelectionBehavior(QAbstractItemView::SelectRows);
        topProcesses->setSelectionMode(QAbstractItemView::SingleSelection);
        topProcesses->setShowGrid(false);
        topProcesses->setAlternatingRowColors(true);
        topProcesses->setIconSize(QSize(24, 24));
        topProcesses->setMinimumHeight(210);
        topProcesses->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        topProcesses->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        processLayout->addLayout(processHeader);
        processLayout->addWidget(topProcesses, 1);
        processPageLayout->addWidget(processPanel, 7);

        auto *detailPanel = new QFrame();
        detailPanel->setObjectName("panel");
        auto *detailLayout = new QVBoxLayout(detailPanel);
        detailLayout->setContentsMargins(20, 14, 20, 14);
        detailLayout->setSpacing(8);
        auto *detailTitle = new QLabel(ko("프로세스 상세"));
        detailTitle->setObjectName("metricLabel");
        processDetail = new QLabel(ko("증가량이 있는 프로세스를 관찰하는 중입니다."));
        processDetail->setObjectName("processDetail");
        processDetail->setWordWrap(true);
        detailLayout->addWidget(detailTitle);
        detailLayout->addWidget(processDetail);
        detailLayout->addStretch(1);
        processPageLayout->addWidget(detailPanel, 4);

        log = new QTextEdit();
        log->setObjectName("log");
        log->setReadOnly(true);
        log->setMinimumHeight(210);
        log->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        log->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        lowerTabs->addTab(processPage, ko("프로세스"));
        lowerTabs->addTab(log, ko("활동 로그"));
        root->addWidget(lowerTabs, 1);

        timer.setInterval(2000);
        loadUiSettings();

        connect(&timer, &QTimer::timeout, this, [this] { sample(); });
        connect(startButton, &QPushButton::clicked, this, [this] { toggle(); });
        connect(autoTune, &QCheckBox::toggled, this, [this](bool checked) {
            threshold->setEnabled(!checked);
            appendLog(checked ? ko("PC 맞춤 자동 기준을 사용합니다.") : ko("수동 정리 기준을 사용합니다."));
            saveUiSettings();
        });
        connect(usageMode, &QComboBox::currentIndexChanged, this, [this] {
            appendLog(usageMode->currentIndex() == 1
                          ? ko("서버/개발 모드: 자동 정리 기준을 더 보수적으로 잡습니다.")
                          : ko("일반 PC 모드: 기본 정리 기준을 사용합니다."));
            if (!autoTune->isChecked()) {
                threshold->setValue(usageMode->currentIndex() == 1 ? 74 : 80);
            }
            updateServerPanelVisibility();
            saveUiSettings();
        });
        connect(threshold, QOverload<int>::of(&QSpinBox::valueChanged), this, [this] {
            if (!autoTune->isChecked()) {
                saveUiSettings();
            }
        });
        connect(action, &QComboBox::currentIndexChanged, this, [this] { saveUiSettings(); });
        connect(reportButton, &QPushButton::clicked, this, [this] {
            writeDailyReport();
            showDailyReportDialog();
        });
        connect(trendButton, &QPushButton::clicked, this, [this] {
            showLongTermTrendDialog();
        });
        connect(updateButton, &QPushButton::clicked, this, [this] {
            checkForUpdates(false);
        });
        connect(errorReportButton, &QPushButton::clicked, this, [this] {
            showErrorReportDialog(ko("사용자 오류 신고"), QString());
        });
        connect(topProcesses, &QTableWidget::cellClicked, this, [this](int, int) {
            updateProcessDetail();
        });
        connect(processSearch, &QLineEdit::textChanged, this, [this] {
            updateTopProcesses(latestProcesses);
            updateProcessDetail();
        });
        connect(qiHelpButton, &QPushButton::clicked, this, [this] {
            showQiHelpDialog();
        });
        connect(themeMode, &QComboBox::currentIndexChanged, this, [this] {
            saveUiSettings();
            applyStyle();
        });

        applyStyle();
        updateServerPanelVisibility();
        setupTray();
        checkPreviousCrash();
        writeRunMarker();
        timer.start();
        appendLog(ko("전체 RAM 자동 보호를 시작했습니다."));
        appendLog(ko("현재 버전: %1").arg(APP_VERSION));
        appendLog(ko("1시간 임시 학습 후 빠르게 기준을 보정하고, 하루 기록으로 정식 기준을 저장합니다."));
        appendLog(ko("다른 프로그램을 강제로 종료하지 않고 RAM 정리와 상태 감시만 수행합니다."));
        appendLog(ko("닫기 버튼을 눌러도 창만 숨겨지고 보호는 백그라운드에서 계속됩니다."));
        appendLog(ko("PC를 켤 때 자동으로 백그라운드 보호가 시작되도록 설치 프로그램이 등록합니다."));
        appendLog(learnedThreshold > 0
                      ? ko("이전에 하루 동안 학습한 자동 정리 기준을 적용합니다.")
                      : ko("하루 학습 데이터가 아직 없어 PC 사양 기준으로 보호합니다."));
        sample();
        QTimer::singleShot(1200, this, [this] {
            checkForUpdates(true);
        });
    }

protected:
    void closeEvent(QCloseEvent *event) override {
        if (quitRequested) {
            writeDailyReport();
            event->accept();
            return;
        }

        QMessageBox box(this);
        box.setWindowTitle(ko("메모리 자동 보호기"));
        box.setIcon(QMessageBox::Question);
        box.setText(ko("창을 닫아도 보호 기능은 계속 실행할 수 있습니다."));
        box.setInformativeText(ko("원하는 동작을 선택하세요."));

        QPushButton *hideButton = box.addButton(ko("창만 닫기"), QMessageBox::AcceptRole);
        QPushButton *quitButton = box.addButton(ko("완전 종료"), QMessageBox::DestructiveRole);
        QPushButton *cancelButton = box.addButton(ko("취소"), QMessageBox::RejectRole);
        box.setDefaultButton(hideButton);
        box.exec();

        if (box.clickedButton() == hideButton) {
            event->ignore();
            hideToTray();
            return;
        }

        if (box.clickedButton() == quitButton) {
            quitRequested = true;
            clearRunMarker();
            writeDailyReport();
            event->accept();
            qApp->quit();
            return;
        }

        Q_UNUSED(cancelButton);
        event->ignore();
    }

private:
    QLabel *title = nullptr;
    QLabel *subtitle = nullptr;
    QLabel *creatorLabel = nullptr;
    QLabel *statusPill = nullptr;
    QLabel *ramPercent = nullptr;
    QLabel *qiScore = nullptr;
    QLabel *adaptiveValue = nullptr;
    QLabel *summary = nullptr;
    QLabel *systemDetail = nullptr;
    QLabel *todayAverage = nullptr;
    QLabel *todayPeak = nullptr;
    QLabel *busyHour = nullptr;
    QLabel *leakStatus = nullptr;
    QLabel *optimizeCountLabel = nullptr;
    QLabel *serverStatus = nullptr;
    QLabel *securityStatus = nullptr;
    QLabel *rebootStatus = nullptr;
    QLabel *processDetail = nullptr;
    QLabel *processCountLabel = nullptr;
    QFrame *serverPanel = nullptr;
    QProgressBar *meter = nullptr;
    QCheckBox *autoTune = nullptr;
    QSpinBox *threshold = nullptr;
    QComboBox *action = nullptr;
    QComboBox *usageMode = nullptr;
    QComboBox *themeMode = nullptr;
    QPushButton *startButton = nullptr;
    QPushButton *reportButton = nullptr;
    QPushButton *trendButton = nullptr;
    QPushButton *updateButton = nullptr;
    QPushButton *errorReportButton = nullptr;
    QPushButton *qiHelpButton = nullptr;
    QTableWidget *topProcesses = nullptr;
    QLineEdit *processSearch = nullptr;
    QTextEdit *log = nullptr;
    QTabWidget *lowerTabs = nullptr;
    QSystemTrayIcon *trayIcon = nullptr;
    QMenu *trayMenu = nullptr;
    QTimer timer;
    QVector<MemorySnapshot> samples;
    QVector<ProcessUsage> latestProcesses;
    QHash<QString, ProcessTrend> processTrends;
    QHash<QString, QIcon> processIconCache;
    HourStats hours[24];
    QDate reportDate;
    qint64 lastOptimizeMs = 0;
    qint64 lastProcessCsvMs = 0;
    qint64 lastTrendCsvMs = 0;
    qint64 lastSecurityScanMs = 0;
    qint64 optimizeSuppressedUntilMs = 0;
    bool running = true;
    int adaptiveThreshold = 80;
    int learnedThreshold = 0;
    int optimizeCount = 0;
    quint64 totalLoadSum = 0;
    quint64 totalUsedSum = 0;
    int totalSampleCount = 0;
    int lastOptimizeLoad = -1;
    int optimizeCheckSamples = 0;
    int ineffectiveOptimizeCount = 0;
    DWORD peakLoad = 0;
    bool quitRequested = false;
    bool trayNoticeShown = false;
    bool startedInBackground = false;
    bool startupUpdateChecked = false;
    bool optimizeEffectPending = false;
    bool cachedPublicPort = false;
    bool cachedPublicRdp = false;
    bool previousCrashPromptShown = false;

    QWidget *makeMetric(const QString &labelText, QLabel *value, QWidget *sideWidget = nullptr) {
        auto *box = new QWidget();
        box->setMinimumHeight(88);
        auto *layout = new QVBoxLayout(box);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(6);

        auto *label = new QLabel(labelText);
        label->setObjectName("metricLabel");
        label->setMinimumHeight(20);
        auto *labelRow = new QHBoxLayout();
        labelRow->setContentsMargins(0, 0, 0, 0);
        labelRow->setSpacing(6);
        labelRow->addWidget(label);
        if (sideWidget) {
            labelRow->addWidget(sideWidget);
        }
        labelRow->addStretch(1);
        value->setObjectName("metricValue");
        value->setMinimumHeight(54);
        value->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        layout->addLayout(labelRow);
        layout->addWidget(value);
        return box;
    }

    QString reportDir() const {
        QString dir = QCoreApplication::applicationDirPath() + "/reports";
        QDir().mkpath(dir);
        return dir;
    }

    QString todayReportPath() const {
        return reportDir() + "/" + reportDate.toString("yyyy-MM-dd") + "-daily-report.txt";
    }

    QString todayCsvPath() const {
        return reportDir() + "/" + reportDate.toString("yyyy-MM-dd") + "-samples.csv";
    }

    QString todayProcessCsvPath() const {
        return reportDir() + "/" + reportDate.toString("yyyy-MM-dd") + "-processes.csv";
    }

    QString longTermTrendPath() const {
        return reportDir() + "/long-term-trends.csv";
    }

    QString appLogPath() const {
        return reportDir() + "/app.log";
    }

    QString runMarkerPath() const {
        return reportDir() + "/running-session.json";
    }

    bool fetchUrl(const QUrl &url, QByteArray *payload, QString *errorText) {
        if (!url.isValid() || url.scheme().isEmpty()) {
            if (errorText) {
                *errorText = ko("주소 형식이 올바르지 않습니다.");
            }
            return false;
        }

        QNetworkAccessManager manager;
        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::UserAgentHeader,
                          QString("MemoryGuardian/%1").arg(APP_VERSION));
        QNetworkReply *reply = manager.get(request);

        QEventLoop loop;
        connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QTimer timeout;
        timeout.setSingleShot(true);
        connect(&timeout, &QTimer::timeout, reply, &QNetworkReply::abort);
        timeout.start(15000);
        loop.exec();

        if (reply->error() != QNetworkReply::NoError) {
            if (errorText) {
                *errorText = reply->errorString();
            }
            reply->deleteLater();
            return false;
        }

        if (payload) {
            *payload = reply->readAll();
        }
        reply->deleteLater();
        return true;
    }

    void writeRunMarker() {
        QJsonObject object;
        object["app_version"] = QString::fromUtf8(APP_VERSION);
        object["started_at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        object["last_event"] = ko("앱 실행 중");
        QFile file(runMarkerPath());
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
        }
    }

    void clearRunMarker() {
        QFile::remove(runMarkerPath());
    }

    QString errorReportCode() const {
        return "MG-" + QDateTime::currentDateTimeUtc().toString("yyyyMMdd-hhmmss");
    }

    QString recentAppLog(int maxLines = 40) const {
        QFile file(appLogPath());
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return QString();
        }
        QStringList lines = QString::fromUtf8(file.readAll()).split('\n', Qt::SkipEmptyParts);
        while (lines.size() > maxLines) {
            lines.removeFirst();
        }
        return lines.join('\n');
    }

    QJsonObject systemSpecObject() const {
        QJsonObject object;
        object["os"] = QSysInfo::prettyProductName();
        object["kernel"] = QSysInfo::kernelType() + " " + QSysInfo::kernelVersion();
        object["cpu_arch"] = QSysInfo::currentCpuArchitecture();
        object["logical_cores"] = QThread::idealThreadCount();
        if (!samples.isEmpty()) {
            const MemorySnapshot &snapshot = samples.last();
            object["total_ram_mb"] = QString::number(snapshot.totalMb);
            object["current_ram_percent"] = int(snapshot.load);
            object["current_used_ram_mb"] = QString::number(snapshot.usedMb);
            object["commit_mb"] = QString::number(snapshot.commitMb);
            object["commit_limit_mb"] = QString::number(snapshot.commitLimitMb);
            object["page_file_used_mb"] = QString::number(snapshot.pageFileUsedMb);
            object["page_file_total_mb"] = QString::number(snapshot.pageFileTotalMb);
            object["non_paged_pool_mb"] = QString::number(snapshot.nonPagedPoolMb);
            object["paged_pool_mb"] = QString::number(snapshot.pagedPoolMb);
        }
        object["admin_mode"] = runningAsAdmin();
        object["usage_mode"] = usageMode && usageMode->currentIndex() == 1 ? ko("집 서버/개발 PC") : ko("일반 PC");
        return object;
    }

    QString includedErrorReportInfoText(const QString &reportCode) const {
        return ko("전송 대상: https://mg.haimez.kr/api/error-reports\n\n"
                  "전송되는 정보\n"
                  "- 오류 코드: %1\n"
                  "- 앱 버전\n"
                  "- 사용자가 직접 입력한 연락처, 요약, 상세 설명\n"
                  "- Windows 버전, CPU 아키텍처, 논리 코어 수\n"
                  "- 전체 RAM, 현재 RAM 사용률, 커밋 메모리, 페이지 파일, Pool 메모리 수치\n"
                  "- 관리자 권한 실행 여부, 사용 모드\n"
                  "- 최근 앱 내부 로그 일부\n\n"
                  "전송하지 않는 정보\n"
                  "- 파일 내용, 개인 문서, 브라우저 기록, 키 입력, 비밀번호\n"
                  "- 브라우저 탭 제목, 환경 변수, 명령줄 인자\n"
                  "- 전체 파일 경로 또는 사용자 폴더 경로\n\n"
                  "동의하지 않으면 아무 정보도 전송되지 않습니다.")
            .arg(reportCode);
    }

    QJsonObject buildErrorReportPayload(const QString &reportCode,
                                        const QString &contact,
                                        const QString &summaryText,
                                        const QString &detailsText,
                                        const QString &reason) const {
        QJsonObject object;
        object["app_version"] = QString("v%1").arg(QString::fromUtf8(APP_VERSION));
        object["report_code"] = reportCode;
        object["reason"] = reason;
        object["contact"] = contact;
        object["summary"] = summaryText;
        object["details"] = detailsText;
        object["created_at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        object["system"] = systemSpecObject();
        object["recent_log"] = recentAppLog();
        return object;
    }

    bool postErrorReport(const QJsonObject &payload, QString *errorText) {
        QNetworkAccessManager manager;
        QNetworkRequest request(QUrl("https://mg.haimez.kr/api/error-reports"));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json; charset=utf-8");
        request.setHeader(QNetworkRequest::UserAgentHeader, QString("MemoryGuardian/%1").arg(APP_VERSION));
        QNetworkReply *reply = manager.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));

        QEventLoop loop;
        connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QTimer timeout;
        timeout.setSingleShot(true);
        connect(&timeout, &QTimer::timeout, reply, &QNetworkReply::abort);
        timeout.start(15000);
        loop.exec();

        bool ok = reply->error() == QNetworkReply::NoError;
        if (!ok && errorText) {
            *errorText = reply->errorString();
        }
        reply->deleteLater();
        return ok;
    }

    void showErrorReportDialog(const QString &reason, const QString &prefillDetails) {
        QString reportCode = errorReportCode();
        QDialog dialog(this);
        dialog.setWindowTitle(ko("오류 로그 전송"));
        dialog.resize(620, 640);

        auto *layout = new QVBoxLayout(&dialog);
        layout->setContentsMargins(22, 20, 22, 20);
        layout->setSpacing(12);

        auto *titleLabel = new QLabel(ko("오류 로그를 HAIMEZ에 전송할까요?"));
        titleLabel->setStyleSheet("font-size: 20px; font-weight: 800;");
        auto *infoLabel = new QLabel(includedErrorReportInfoText(reportCode));
        infoLabel->setWordWrap(true);

        auto *contactEdit = new QLineEdit();
        contactEdit->setPlaceholderText(ko("연락처 선택 입력: 이메일, 디스코드 닉네임 등"));
        auto *summaryEdit = new QLineEdit(reason);
        summaryEdit->setPlaceholderText(ko("요약: 어떤 문제가 있었나요?"));
        auto *detailsEdit = new QTextEdit(prefillDetails);
        detailsEdit->setPlaceholderText(ko("상세 설명: 언제, 어떤 버튼을 눌렀는지 적어주세요."));
        detailsEdit->setMinimumHeight(110);

        auto *consent = new QCheckBox(ko("위 정보를 확인했으며 오류 분석을 위해 전송하는 데 동의합니다."));

        layout->addWidget(titleLabel);
        layout->addWidget(infoLabel);
        layout->addWidget(new QLabel(ko("연락처")));
        layout->addWidget(contactEdit);
        layout->addWidget(new QLabel(ko("요약")));
        layout->addWidget(summaryEdit);
        layout->addWidget(new QLabel(ko("상세 설명")));
        layout->addWidget(detailsEdit, 1);
        layout->addWidget(consent);

        auto *buttons = new QHBoxLayout();
        buttons->addStretch();
        auto *sendButton = new QPushButton(ko("전송"));
        sendButton->setObjectName("primaryButton");
        auto *cancelButton = new QPushButton(ko("취소"));
        sendButton->setEnabled(false);
        buttons->addWidget(cancelButton);
        buttons->addWidget(sendButton);
        layout->addLayout(buttons);

        connect(consent, &QCheckBox::toggled, sendButton, &QPushButton::setEnabled);
        connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);
        connect(sendButton, &QPushButton::clicked, &dialog, [&] {
            QJsonObject payload = buildErrorReportPayload(reportCode,
                                                          contactEdit->text().trimmed(),
                                                          summaryEdit->text().trimmed(),
                                                          detailsEdit->toPlainText().trimmed(),
                                                          reason);
            QString errorText;
            if (postErrorReport(payload, &errorText)) {
                appendLog(ko("오류 로그 전송 완료: %1").arg(reportCode));
                QMessageBox::information(&dialog, ko("오류 로그 전송"), ko("오류 로그를 전송했습니다.\n오류 코드: %1").arg(reportCode));
                dialog.accept();
            } else {
                appendLog(ko("오류 로그 전송 실패: %1").arg(errorText));
                QMessageBox::warning(&dialog, ko("오류 로그 전송"), ko("전송에 실패했습니다.\n%1").arg(errorText));
            }
        });

        dialog.setStyleSheet(darkModeEnabled() ? R"(
            QDialog { background: #0f172a; color: #e5e7eb; font-family: "Malgun Gothic"; }
            QLabel { background: transparent; }
            QLineEdit, QTextEdit { background: #111827; color: #e5e7eb; border: 1px solid #334155; border-radius: 9px; padding: 8px; }
            QPushButton { min-height: 36px; background: #111827; color: #e5e7eb; border: 1px solid #334155; border-radius: 9px; padding: 0 16px; font-weight: 600; }
            QPushButton#primaryButton { background: #2563eb; color: white; border: 0; font-weight: 800; }
        )" : R"(
            QDialog { background: #f6f8fb; color: #111827; font-family: "Malgun Gothic"; }
            QLabel { background: transparent; }
            QLineEdit, QTextEdit { background: white; color: #111827; border: 1px solid #d7dee9; border-radius: 9px; padding: 8px; }
            QPushButton { min-height: 36px; background: white; color: #111827; border: 1px solid #d7dee9; border-radius: 9px; padding: 0 16px; font-weight: 600; }
            QPushButton#primaryButton { background: #2563eb; color: white; border: 0; font-weight: 800; }
        )");

        dialog.exec();
    }

    void checkPreviousCrash() {
        if (previousCrashPromptShown || !QFile::exists(runMarkerPath())) {
            return;
        }
        previousCrashPromptShown = true;
        QFile file(runMarkerPath());
        QString marker;
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            marker = QString::fromUtf8(file.readAll());
        }
        QMessageBox box(this);
        box.setWindowTitle(ko("이전 비정상 종료 감지"));
        box.setIcon(QMessageBox::Warning);
        box.setText(ko("이전 실행이 정상적으로 종료되지 않은 것 같습니다."));
        box.setInformativeText(ko("크래시나 강제 종료가 있었다면 오류 로그를 전송해 분석을 도울 수 있습니다.\n전송 전 포함되는 정보를 확인하고 동의해야 합니다."));
        QPushButton *sendButton = box.addButton(ko("오류 로그 확인 후 전송"), QMessageBox::AcceptRole);
        box.addButton(ko("보내지 않음"), QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() == sendButton) {
            showErrorReportDialog(ko("이전 비정상 종료 감지"), marker);
        }
        writeRunMarker();
    }

    void toggle() {
        running = !running;
        if (running) {
            timer.start();
            startButton->setText(ko("보호 중지"));
            statusPill->setText(ko("자동 보호 중"));
            appendLog(ko("자동 보호를 다시 시작했습니다."));
        } else {
            timer.stop();
            startButton->setText(ko("보호 시작"));
            statusPill->setText(ko("일시 중지"));
            appendLog(ko("자동 보호를 중지했습니다."));
        }
    }

    QIcon appIcon() const {
        return QIcon(":/branding/haimez.ico");
    }

    void setupTray() {
        if (!QSystemTrayIcon::isSystemTrayAvailable()) {
            appendLog(ko("시스템 트레이를 사용할 수 없어 창 닫기 보호만 사용합니다."));
            return;
        }

        trayIcon = new QSystemTrayIcon(appIcon(), this);
        trayIcon->setToolTip(ko("메모리 자동 보호기 - 백그라운드 보호 중"));
        trayMenu = new QMenu(this);

        QAction *showAction = trayMenu->addAction(ko("창 열기"));
        QAction *reportAction = trayMenu->addAction(ko("오늘 리포트 보기"));
        trayMenu->addSeparator();
        QAction *quitAction = trayMenu->addAction(ko("완전 종료"));

        connect(showAction, &QAction::triggered, this, [this] { showFromTray(); });
        connect(reportAction, &QAction::triggered, this, [this] {
            showFromTray();
            writeDailyReport();
            showDailyReportDialog();
        });
        connect(quitAction, &QAction::triggered, this, [this] {
            quitRequested = true;
            clearRunMarker();
            writeDailyReport();
            qApp->quit();
        });
        connect(trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick) {
                showFromTray();
            }
        });

        trayIcon->setContextMenu(trayMenu);
        trayIcon->show();
    }

    void hideToTray() {
        hide();
        appendLog(startedInBackground
                      ? ko("자동 시작으로 백그라운드 보호를 실행했습니다.")
                      : ko("창을 숨겼습니다. 보호 기능은 백그라운드에서 계속 실행됩니다."));
        if (trayIcon && !trayNoticeShown) {
            trayIcon->showMessage(ko("백그라운드 보호 중"),
                                  startedInBackground
                                      ? ko("PC 시작과 함께 메모리 보호가 자동 실행되었습니다.")
                                      : ko("창만 닫혔고 메모리 보호는 계속 실행됩니다."),
                                  QSystemTrayIcon::Information,
                                  3500);
            trayNoticeShown = true;
        }
    }

    void showFromTray() {
        show();
        raise();
        activateWindow();
        appendLog(ko("창을 다시 열었습니다."));
    }

    void sample() {
        if (QDate::currentDate() != reportDate) {
            writeDailyReport();
            finalizeDailyLearning();
            resetDailyStats();
        }

        MemorySnapshot snapshot = readMemory();
        samples.push_back(snapshot);
        while (samples.size() > 30) {
            samples.pop_front();
        }

        updateStats(snapshot);
        adaptiveThreshold = calculateAdaptiveThreshold(snapshot.totalMb);
        int activeThreshold = autoTune->isChecked() ? adaptiveThreshold : threshold->value();
        if (autoTune->isChecked()) {
            threshold->setValue(activeThreshold);
        }

        QiState qi = evaluateQi(samples, activeThreshold);
        QVector<ProcessUsage> processes = readTopProcesses();
        updateProcessTrends(processes, snapshot.time);
        updateUi(snapshot, qi, activeThreshold);
        updateTopProcesses(processes);
        updateProcessDetail();
        appendCsv(snapshot, qi, activeThreshold);
        appendProcessCsv(processes, snapshot.time);
        appendLongTermTrend(snapshot);
        updateServerHealthCards(snapshot, processes);

        qint64 now = QDateTime::currentMSecsSinceEpoch();
        checkOptimizeEffect(snapshot, now);

        if (qi.shouldOptimize
            && now >= optimizeSuppressedUntilMs
            && now - lastOptimizeMs > 60000
            && !optimizeEffectPending) {
            lastOptimizeMs = now;
            lastOptimizeLoad = int(snapshot.load);
            optimizeCheckSamples = 0;
            optimizeEffectPending = true;
            optimizeCount++;
            appendLog(ko("최적화 점수가 낮거나 RAM 사용률이 기준을 넘어 자동 처리를 시작합니다."));
            if (action->currentIndex() == 0) {
                appendLog(optimizeMemory());
            } else {
                QApplication::beep();
                appendLog(ko("알림만 표시했습니다."));
            }
            writeDailyReport();
        }
    }

    void checkOptimizeEffect(const MemorySnapshot &snapshot, qint64 now) {
        if (!optimizeEffectPending) {
            return;
        }

        optimizeCheckSamples++;
        if (optimizeCheckSamples < 3) {
            return;
        }

        optimizeEffectPending = false;
        int currentLoad = int(snapshot.load);
        if (lastOptimizeLoad >= 0 && currentLoad >= lastOptimizeLoad - 1) {
            ineffectiveOptimizeCount++;
            optimizeSuppressedUntilMs = now + 10 * 60 * 1000;
            QString growth = biggestGrowthText();
            appendLog(growth.isEmpty()
                          ? ko("정리 후에도 RAM 사용률이 거의 내려가지 않았습니다. 반복 정리를 10분간 멈추고 누수 추세를 감시합니다.")
                          : ko("정리 후에도 RAM 사용률이 거의 내려가지 않았습니다. 반복 정리를 10분간 멈추고 %1 증가를 감시합니다.").arg(growth));
            return;
        }

        ineffectiveOptimizeCount = 0;
        appendLog(ko("메모리 정리 효과 확인: RAM 사용률이 %1%에서 %2%로 낮아졌습니다.")
                  .arg(lastOptimizeLoad)
                  .arg(currentLoad));
    }

    void resetDailyStats() {
        reportDate = QDate::currentDate();
        std::fill(std::begin(hours), std::end(hours), HourStats {});
        optimizeCount = 0;
        totalLoadSum = 0;
        totalUsedSum = 0;
        totalSampleCount = 0;
        peakLoad = 0;
        samples.clear();
        processTrends.clear();
        lastProcessCsvMs = 0;
        optimizeSuppressedUntilMs = 0;
        optimizeEffectPending = false;
        lastOptimizeLoad = -1;
        optimizeCheckSamples = 0;
        ineffectiveOptimizeCount = 0;
        appendLog(ko("새 날짜가 시작되어 오늘의 기록을 새로 시작합니다."));
    }

    void updateStats(const MemorySnapshot &snapshot) {
        int hour = snapshot.time.time().hour();
        HourStats &stat = hours[hour];
        stat.count++;
        stat.loadSum += snapshot.load;
        stat.usedSum += snapshot.usedMb;
        stat.peakLoad = std::max(stat.peakLoad, snapshot.load);
        stat.peakUsedMb = std::max(stat.peakUsedMb, snapshot.usedMb);

        totalSampleCount++;
        totalLoadSum += snapshot.load;
        totalUsedSum += snapshot.usedMb;
        peakLoad = std::max(peakLoad, snapshot.load);
    }

    ServerProcessSummary summarizeServerProcesses(const QVector<ProcessUsage> &processes) const {
        ServerProcessSummary summary;
        for (const ProcessUsage &process : processes) {
            QString name = process.name.toLower();
            if (name == "node.exe" || name == "node") {
                summary.node++;
            } else if (name == "python.exe" || name == "pythonw.exe" || name == "python") {
                summary.python++;
            } else if (name == "java.exe" || name == "javaw.exe" || name == "java") {
                summary.java++;
            } else if (name.contains("tailscale")) {
                summary.tailscale = true;
            } else if (name.contains("cloudflared")) {
                summary.cloudflare = true;
            }
        }
        return summary;
    }

    QString commandOutput(const QString &program, const QStringList &arguments, int timeoutMs = 1500) const {
        QProcess process;
        process.start(program, arguments);
        if (!process.waitForFinished(timeoutMs)) {
            process.kill();
            process.waitForFinished(300);
            return QString();
        }
        return QString::fromLocal8Bit(process.readAllStandardOutput());
    }

    bool hasPublicListenPort() const {
        QString output = commandOutput("netstat", {"-ano", "-p", "tcp"});
        const QStringList lines = output.split('\n');
        for (const QString &rawLine : lines) {
            QString line = rawLine.simplified();
            if (!line.contains("LISTENING")) {
                continue;
            }
            if (line.startsWith("TCP 0.0.0.0:") || line.startsWith("TCP [::]:")) {
                return true;
            }
        }
        return false;
    }

    bool hasPublicRdp() const {
        QString output = commandOutput("netstat", {"-ano", "-p", "tcp"});
        const QStringList lines = output.split('\n');
        for (const QString &rawLine : lines) {
            QString line = rawLine.simplified();
            if (!line.contains("LISTENING")) {
                continue;
            }
            if (line.startsWith("TCP 0.0.0.0:3389") || line.startsWith("TCP [::]:3389")) {
                return true;
            }
        }
        return false;
    }

    int calculateAdaptiveThreshold(quint64 totalMb) const {
        if (learnedThreshold > 0) {
            return learnedThreshold;
        }

        int temporary = temporaryThresholdFromToday(totalMb);
        if (temporary > 0) {
            return temporary;
        }

        return hardwareThreshold(totalMb);
    }

    int hardwareThreshold(quint64 totalMb) const {
        bool serverMode = usageMode && usageMode->currentIndex() == 1;
        if (serverMode) {
            if (totalMb <= 8192) {
                return 70;
            } else if (totalMb <= 16384) {
                return 74;
            } else if (totalMb >= 32768) {
                return 80;
            }
            return 76;
        }

        if (totalMb <= 8192) {
            return 74;
        } else if (totalMb <= 16384) {
            return 80;
        } else if (totalMb >= 32768) {
            return 86;
        }

        return 82;
    }

    int temporaryThresholdFromToday(quint64 totalMb) const {
        if (totalSampleCount < 1800) {
            return 0;
        }

        int averageLoad = int(totalLoadSum / quint64(totalSampleCount));
        int base = hardwareThreshold(totalMb);
        int learned = std::clamp(averageLoad + (usageMode && usageMode->currentIndex() == 1 ? 8 : 12), 66, 90);
        int blended = (base * 50 + learned * 50) / 100;
        return std::clamp(blended, usageMode && usageMode->currentIndex() == 1 ? 66 : 70, 90);
    }

    int coveredHours() const {
        int covered = 0;
        for (int i = 0; i < 24; ++i) {
            if (hours[i].count > 0) {
                covered++;
            }
        }
        return covered;
    }

    int learnedThresholdFromToday() const {
        if (totalSampleCount == 0) {
            return 0;
        }

        int averageLoad = int(totalLoadSum / quint64(totalSampleCount));
        int hardwareBase = samples.isEmpty() ? 80 : hardwareThreshold(samples.last().totalMb);
        int learnedBase = std::clamp(averageLoad + (usageMode && usageMode->currentIndex() == 1 ? 10 : 16), 66, 92);
        int blended = (hardwareBase * 35 + learnedBase * 65) / 100;
        return std::clamp(blended, usageMode && usageMode->currentIndex() == 1 ? 66 : 68, 92);
    }

    void loadLearnedProfile() {
        QSettings settings(reportDir() + "/profile.ini", QSettings::IniFormat);
        learnedThreshold = settings.value("learnedThreshold", 0).toInt();
        if (learnedThreshold < 50 || learnedThreshold > 98) {
            learnedThreshold = 0;
        }
    }

    void loadUiSettings() {
        QSettings settings(reportDir() + "/profile.ini", QSettings::IniFormat);
        settings.beginGroup("ui");
        autoTune->setChecked(settings.value("autoTune", true).toBool());
        threshold->setValue(std::clamp(settings.value("manualThreshold", threshold->value()).toInt(), 50, 98));
        action->setCurrentIndex(std::clamp(settings.value("cleanupAction", action->currentIndex()).toInt(), 0, action->count() - 1));
        usageMode->setCurrentIndex(std::clamp(settings.value("usageMode", usageMode->currentIndex()).toInt(), 0, usageMode->count() - 1));
        themeMode->setCurrentIndex(std::clamp(settings.value("themeMode", themeMode->currentIndex()).toInt(), 0, themeMode->count() - 1));
        settings.endGroup();
        threshold->setEnabled(!autoTune->isChecked());
    }

    void saveUiSettings() {
        if (!autoTune || !threshold || !action || !usageMode || !themeMode) {
            return;
        }
        QSettings settings(reportDir() + "/profile.ini", QSettings::IniFormat);
        settings.beginGroup("ui");
        settings.setValue("autoTune", autoTune->isChecked());
        settings.setValue("manualThreshold", threshold->value());
        settings.setValue("cleanupAction", action->currentIndex());
        settings.setValue("usageMode", usageMode->currentIndex());
        settings.setValue("themeMode", themeMode->currentIndex());
        settings.endGroup();
        settings.sync();
    }

    void updateServerPanelVisibility() {
        if (!serverPanel || !usageMode) {
            return;
        }
        serverPanel->setVisible(usageMode->currentIndex() == 1);
    }

    void showQiHelpDialog() {
        if (samples.isEmpty()) {
            QMessageBox::information(this,
                                     ko("최적화 점수 기준"),
                                     ko("아직 메모리 샘플을 수집하는 중입니다.\n몇 초 뒤 다시 눌러주세요."));
            return;
        }

        int activeThreshold = autoTune->isChecked() ? adaptiveThreshold : threshold->value();
        QiState qi = evaluateQi(samples, activeThreshold);
        const MemorySnapshot &snapshot = samples.last();
        QString modeText = usageMode && usageMode->currentIndex() == 1 ? ko("집 서버/개발 PC") : ko("일반 PC");
        QString actionText = qi.shouldOptimize
                                 ? ko("현재 기준에서는 자동 정리 또는 알림 대상입니다.")
                                 : ko("현재 기준에서는 자동 정리 대상이 아닙니다.");

        QMessageBox::information(this,
                                 ko("최적화 점수 기준"),
                                 ko("최적화 점수는 100점에서 위험 요소를 빼는 방식입니다.\n\n"
                                    "현재 점수: %1점\n"
                                    "운영 모드: %2\n"
                                    "자동 정리 기준: %3%\n\n"
                                    "감점 내역\n"
                                    "- RAM 사용 압박: -%4점\n"
                                    "- 여유 메모리 부족: -%5점\n"
                                    "- 최근 RAM 증가 추세: -%6점\n\n"
                                    "현재 상태\n"
                                    "- RAM 사용률: %7%\n"
                                    "- 사용 중 RAM: %8 MB\n"
                                    "- 여유 RAM: %9 MB\n"
                                    "- 커밋 메모리: %10 / %11 MB\n\n"
                                    "%12")
                                     .arg(qi.score)
                                     .arg(modeText)
                                     .arg(activeThreshold)
                                     .arg(qi.pressure)
                                     .arg(qi.available)
                                     .arg(qi.trend)
                                     .arg(snapshot.load)
                                     .arg(snapshot.usedMb)
                                     .arg(snapshot.availableMb)
                                     .arg(snapshot.commitMb)
                                     .arg(snapshot.commitLimitMb)
                                     .arg(actionText));
    }

    void saveLearnedProfile(int value) {
        QSettings settings(reportDir() + "/profile.ini", QSettings::IniFormat);
        settings.setValue("learnedThreshold", value);
        settings.setValue("learnedFromDate", reportDate.toString(Qt::ISODate));
        settings.setValue("coveredHours", coveredHours());
        settings.sync();
        learnedThreshold = value;
    }

    void finalizeDailyLearning() {
        if (coveredHours() < 18 || totalSampleCount < 200) {
            appendLog(ko("하루 학습 시간이 부족해 자동 기준을 갱신하지 않았습니다."));
            return;
        }

        int value = learnedThresholdFromToday();
        saveLearnedProfile(value);
        appendLog(ko("하루 기록을 바탕으로 자동 정리 기준을 %1%로 저장했습니다.").arg(value));
    }

    void updateUi(const MemorySnapshot &snapshot, const QiState &qi, int activeThreshold) {
        ramPercent->setText(QString("%1%").arg(snapshot.load));
        qiScore->setText(ko("%1점").arg(qi.score));

        QString thresholdText = QString("%1%").arg(activeThreshold);
        if (autoTune->isChecked() && learnedThreshold == 0) {
            thresholdText = totalSampleCount >= 1800
                                ? ko("임시 %1%").arg(activeThreshold)
                                : ko("학습 중 %1%").arg(activeThreshold);
        }
        adaptiveValue->setText(thresholdText);
        meter->setValue(int(snapshot.load));

        qint64 cleanupLeftMb = qint64(snapshot.totalMb) * qint64(activeThreshold - int(snapshot.load)) / 100;
        QString cleanupText = cleanupLeftMb > 0
                                  ? ko("예상 정리까지 약 %1 MB").arg(cleanupLeftMb)
                                  : ko("정리 기준 초과");
        summary->setText(ko("사용 중 %1 MB / 전체 %2 MB    여유 %3 MB    %4")
                         .arg(snapshot.usedMb)
                         .arg(snapshot.totalMb)
                         .arg(snapshot.availableMb)
                         .arg(cleanupText));
        systemDetail->setText(ko("커밋 %1/%2 MB    페이지 파일 %3/%4 MB    Non-Paged Pool %5 MB    Paged Pool %6 MB")
                              .arg(snapshot.commitMb)
                              .arg(snapshot.commitLimitMb)
                              .arg(snapshot.pageFileUsedMb)
                              .arg(snapshot.pageFileTotalMb)
                              .arg(snapshot.nonPagedPoolMb)
                              .arg(snapshot.pagedPoolMb));

        if (qi.score <= 45 || snapshot.load >= DWORD(activeThreshold)) {
            statusPill->setText(ko("정리 필요"));
            meter->setProperty("state", "warn");
        } else if (qi.score <= 70 || snapshot.load + 5 >= DWORD(activeThreshold)) {
            statusPill->setText(ko("상태 보통"));
            meter->setProperty("state", "warn");
        } else {
            statusPill->setText(ko("상태 좋음"));
            meter->setProperty("state", "good");
        }
        meter->style()->unpolish(meter);
        meter->style()->polish(meter);

        int averageLoad = totalSampleCount ? int(totalLoadSum / quint64(totalSampleCount)) : 0;
        todayAverage->setText(ko("오늘 평균: %1%").arg(averageLoad));
        todayPeak->setText(ko("오늘 최고: %1%").arg(peakLoad));
        busyHour->setText(totalSampleCount >= 1800
                              ? ko("임시 학습 완료")
                              : ko("임시 학습: %1%").arg(std::min(99, totalSampleCount * 100 / 1800)));
        leakStatus->setText(leakStatusText(snapshot));
        optimizeCountLabel->setText(ko("자동 정리: %1회").arg(optimizeCount));
    }

    void updateProcessTrends(const QVector<ProcessUsage> &processes, const QDateTime &now) {
        for (const ProcessUsage &process : processes) {
            QString key = QString("%1:%2").arg(process.name).arg(process.pid);
            if (!processTrends.contains(key)) {
                ProcessTrend trend;
                trend.name = process.name;
                trend.pid = process.pid;
                trend.firstMb = process.usedMb;
                trend.firstSeen = now;
                processTrends.insert(key, trend);
            }

            ProcessTrend &trend = processTrends[key];
            trend.lastMb = process.usedMb;
            trend.lastSeen = now;
        }

        QList<QString> keys = processTrends.keys();
        for (const QString &key : keys) {
            if (processTrends[key].lastSeen.secsTo(now) > 900) {
                processTrends.remove(key);
            }
        }
    }

    qint64 processDeltaMb(const ProcessUsage &process) const {
        QString key = QString("%1:%2").arg(process.name).arg(process.pid);
        if (!processTrends.contains(key)) {
            return 0;
        }
        const ProcessTrend &trend = processTrends[key];
        return qint64(trend.lastMb) - qint64(trend.firstMb);
    }

    double processGrowthPerHour(const ProcessTrend &trend) const {
        qint64 seconds = trend.firstSeen.secsTo(trend.lastSeen);
        if (seconds < 600) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        qint64 delta = qint64(trend.lastMb) - qint64(trend.firstMb);
        return double(delta) * 3600.0 / double(seconds);
    }

    QString processPatternLabel(const ProcessTrend &trend) const {
        qint64 delta = qint64(trend.lastMb) - qint64(trend.firstMb);
        double speed = processGrowthPerHour(trend);
        qint64 observedMinutes = std::max<qint64>(1, trend.firstSeen.secsTo(trend.lastSeen) / 60);
        bool speedReady = std::isfinite(speed);

        if (delta >= 1024 || (observedMinutes >= 30 && speedReady && speed >= 300.0)) {
            return ko("누수 의심");
        }
        if (delta >= 300 || (observedMinutes >= 20 && speedReady && speed >= 120.0)) {
            return ko("주의");
        }
        if (delta < 0) {
            return ko("감소 중");
        }
        if (observedMinutes < 10) {
            return ko("관찰 중");
        }
        return ko("정상 패턴");
    }

    const ProcessTrend *mostImportantProcessTrend() const {
        const ProcessTrend *best = nullptr;
        double bestScore = -1.0;
        for (auto it = processTrends.constBegin(); it != processTrends.constEnd(); ++it) {
            const ProcessTrend &trend = it.value();
            qint64 delta = qint64(trend.lastMb) - qint64(trend.firstMb);
            double speed = processGrowthPerHour(trend);
            double speedScore = std::isfinite(speed) ? std::max(0.0, speed) : 0.0;
            double score = double(std::max<qint64>(0, delta)) + speedScore * 0.4 + double(trend.lastMb) * 0.05;
            if (score > bestScore) {
                bestScore = score;
                best = &it.value();
            }
        }
        return best;
    }

    QString biggestGrowthText() const {
        const ProcessTrend *best = mostImportantProcessTrend();
        qint64 bestDelta = best ? qint64(best->lastMb) - qint64(best->firstMb) : 0;

        if (!best || bestDelta < 300) {
            return QString();
        }
        return ko("%1 +%2 MB").arg(best->name, QString::number(bestDelta));
    }

    QString leakStatusText(const MemorySnapshot &snapshot) const {
        QString growth = biggestGrowthText();
        if (!growth.isEmpty()) {
            return ko("누수 의심: %1").arg(growth);
        }

        if (totalSampleCount >= 1800 && snapshot.load >= DWORD(averageLoadToday() + 8)) {
            return ko("누수 의심: RAM 증가 추세");
        }

        if (snapshot.nonPagedPoolMb >= 1024) {
            return ko("누수 의심: Non-Paged Pool 높음");
        }

        if (totalSampleCount < 1800) {
            return ko("누수 의심: 학습 중");
        }

        return ko("누수 의심: 없음");
    }

    void updateTopProcesses(const QVector<ProcessUsage> &processes) {
        if (!topProcesses) {
            return;
        }
        latestProcesses = processes;

        QString selectedKey;
        if (topProcesses->currentRow() >= 0) {
            QTableWidgetItem *selected = topProcesses->item(topProcesses->currentRow(), 1);
            if (selected) {
                selectedKey = selected->data(Qt::UserRole).toString();
            }
        }

        QString query = processSearch ? processSearch->text().trimmed() : QString();
        QVector<ProcessUsage> visibleProcesses;
        visibleProcesses.reserve(processes.size());
        for (const ProcessUsage &process : processes) {
            bool nameMatch = query.isEmpty() || process.name.contains(query, Qt::CaseInsensitive);
            bool pidMatch = query.isEmpty() || QString::number(process.pid).contains(query);
            if (nameMatch || pidMatch) {
                visibleProcesses.push_back(process);
            }
        }
        if (processCountLabel) {
            processCountLabel->setText(query.isEmpty()
                                           ? ko("전체 %1개").arg(processes.size())
                                           : ko("검색 %1개 / 전체 %2개").arg(visibleProcesses.size()).arg(processes.size()));
        }

        topProcesses->setUpdatesEnabled(false);
        topProcesses->setRowCount(visibleProcesses.size());
        QFileIconProvider iconProvider;
        int rank = 0;
        int selectedRow = -1;
        for (const ProcessUsage &process : visibleProcesses) {
            qint64 delta = processDeltaMb(process);
            QString deltaText = delta >= 0 ? QString("+%1 MB").arg(delta) : QString("%1 MB").arg(delta);
            QString key = QString("%1:%2").arg(process.name).arg(process.pid);
            QString pattern = processTrends.contains(key) ? processPatternLabel(processTrends[key]) : ko("관찰 중");

            auto *rankItem = new QTableWidgetItem(QString::number(rank + 1));
            rankItem->setTextAlignment(Qt::AlignCenter);

            auto *nameItem = new QTableWidgetItem(process.name);
            nameItem->setData(Qt::UserRole, key);
            nameItem->setToolTip(ko("%1\nPID %2").arg(process.executablePath.isEmpty() ? process.name : process.executablePath)
                                                     .arg(process.pid));
            QString iconKey = process.executablePath.isEmpty() ? process.name : process.executablePath;
            if (!processIconCache.contains(iconKey)) {
                QIcon icon = process.executablePath.isEmpty()
                                 ? style()->standardIcon(QStyle::SP_ComputerIcon)
                                 : iconProvider.icon(QFileInfo(process.executablePath));
                processIconCache.insert(iconKey, icon);
            }
            nameItem->setIcon(processIconCache.value(iconKey));

            auto *usageItem = new QTableWidgetItem(ko("%1 MB").arg(process.usedMb));
            usageItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            auto *deltaItem = new QTableWidgetItem(deltaText);
            deltaItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            auto *patternItem = new QTableWidgetItem(pattern);
            patternItem->setTextAlignment(Qt::AlignCenter);

            topProcesses->setItem(rank, 0, rankItem);
            topProcesses->setItem(rank, 1, nameItem);
            topProcesses->setItem(rank, 2, usageItem);
            topProcesses->setItem(rank, 3, deltaItem);
            topProcesses->setItem(rank, 4, patternItem);
            topProcesses->setRowHeight(rank, 40);

            if (key == selectedKey) {
                selectedRow = rank;
            }
            rank++;
        }

        if (selectedRow >= 0) {
            topProcesses->selectRow(selectedRow);
        } else if (!visibleProcesses.isEmpty()) {
            topProcesses->selectRow(0);
        }
        topProcesses->setUpdatesEnabled(true);
    }

    void updateProcessDetail() {
        if (!processDetail) {
            return;
        }
        if (processSearch && !processSearch->text().trimmed().isEmpty()
            && topProcesses && topProcesses->rowCount() == 0) {
            processDetail->setText(ko("검색 조건과 일치하는 프로세스가 없습니다."));
            return;
        }

        const ProcessTrend *trend = nullptr;
        if (topProcesses && topProcesses->currentRow() >= 0) {
            QTableWidgetItem *selected = topProcesses->item(topProcesses->currentRow(), 1);
            QString key = selected ? selected->data(Qt::UserRole).toString() : QString();
            auto it = processTrends.constFind(key);
            if (it != processTrends.constEnd()) {
                trend = &it.value();
            }
        }
        if (!trend) {
            trend = mostImportantProcessTrend();
        }
        if (!trend) {
            processDetail->setText(ko("증가량이 있는 프로세스를 관찰하는 중입니다."));
            return;
        }

        qint64 delta = qint64(trend->lastMb) - qint64(trend->firstMb);
        double speed = processGrowthPerHour(*trend);
        QString deltaText = delta >= 0 ? ko("+%1 MB").arg(delta) : ko("%1 MB").arg(delta);
        QString speedText = std::isfinite(speed)
                                ? (speed >= 0.0 ? ko("+%1 MB/h").arg(QString::number(speed, 'f', 0))
                                                : ko("%1 MB/h").arg(QString::number(speed, 'f', 0)))
                                : ko("계산 중 (10분 필요)");
        QString pattern = processPatternLabel(*trend);

        processDetail->setText(ko("%1\nPID %2\n시작 메모리 %3 MB    현재 메모리 %4 MB\n증가량 %5    증가속도 %6\n판정: %7")
                                   .arg(trend->name)
                                   .arg(trend->pid)
                                   .arg(trend->firstMb)
                                   .arg(trend->lastMb)
                                   .arg(deltaText)
                                   .arg(speedText)
                                   .arg(pattern));
    }

    void updateServerHealthCards(const MemorySnapshot &snapshot, const QVector<ProcessUsage> &processes) {
        ServerProcessSummary server = summarizeServerProcesses(processes);
        QString serverDetected = (server.node + server.python + server.java) > 0
                                     ? ko("감지됨")
                                     : ko("감지 안 됨");
        serverStatus->setText(ko("<b>서버 프로그램 감지</b><br>상태: %1<br>Node 서버 %2개 · Python %3개 · Java %4개<br>원격망: Tailscale %5<br>터널: Cloudflare %6")
                                  .arg(serverDetected)
                                  .arg(server.node)
                                  .arg(server.python)
                                  .arg(server.java)
                                  .arg(server.tailscale ? ko("정상") : ko("없음"))
                                  .arg(server.cloudflare ? ko("정상") : ko("없음")));

        qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - lastSecurityScanMs > 60000) {
            cachedPublicPort = hasPublicListenPort();
            cachedPublicRdp = hasPublicRdp();
            lastSecurityScanMs = now;
        }
        QString portText = cachedPublicPort ? ko("열린 포트 있음") : ko("없음");
        QString rdpText = cachedPublicRdp ? ko("주의 필요") : ko("없음");
        securityStatus->setText(ko("<b>원격 접속 점검</b><br>안전한 원격망: %1<br>외부 노출 의심 포트: %2<br>RDP 원격 데스크톱: %3")
                                    .arg(server.tailscale ? ko("Tailscale") : ko("확인 필요"))
                                    .arg(portText)
                                    .arg(rdpText));

        bool rebootRecommended = snapshot.nonPagedPoolMb >= 2048
                                 || snapshot.pagedPoolMb >= 3072
                                 || (snapshot.commitLimitMb > 0 && snapshot.commitMb * 100 / snapshot.commitLimitMb >= 90);
        rebootStatus->setText(rebootRecommended
                                  ? ko("<b>재부팅 판단</b><br>권장됨<br>커널/커밋 메모리가 높습니다.")
                                  : ko("<b>재부팅 판단</b><br>필요 없음<br>현재 수치는 안정적입니다."));
    }

    int heaviestHour() const {
        int bestHour = 0;
        double bestAverage = -1.0;
        for (int i = 0; i < 24; ++i) {
            if (hours[i].count == 0) {
                continue;
            }
            double avg = double(hours[i].loadSum) / double(hours[i].count);
            if (avg > bestAverage) {
                bestAverage = avg;
                bestHour = i;
            }
        }
        return bestHour;
    }

    int averageLoadToday() const {
        return totalSampleCount ? int(totalLoadSum / quint64(totalSampleCount)) : 0;
    }

    quint64 averageUsedToday() const {
        return totalSampleCount ? totalUsedSum / quint64(totalSampleCount) : 0;
    }

    QLabel *reportMetric(const QString &label, const QString &value) {
        auto *box = new QLabel(QString("<div style='font-size:12px;color:#94a3b8'>%1</div>"
                                       "<div style='font-size:22px;font-weight:800;margin-top:6px;line-height:30px'>%2</div>")
                                   .arg(label, value));
        box->setTextFormat(Qt::RichText);
        box->setMinimumHeight(84);
        box->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        box->setObjectName("reportMetric");
        return box;
    }

    QVector<ProcessTrend> sortedProcessGrowth() const {
        QVector<ProcessTrend> trends;
        for (auto it = processTrends.constBegin(); it != processTrends.constEnd(); ++it) {
            trends.push_back(it.value());
        }
        std::sort(trends.begin(), trends.end(), [](const ProcessTrend &a, const ProcessTrend &b) {
            return qint64(a.lastMb) - qint64(a.firstMb) > qint64(b.lastMb) - qint64(b.firstMb);
        });
        return trends;
    }

    QString processGrowthSummaryHtml() const {
        QVector<ProcessTrend> trends = sortedProcessGrowth();
        QStringList lines;
        int rank = 1;
        for (const ProcessTrend &trend : trends) {
            if (rank > 5) {
                break;
            }
            qint64 delta = qint64(trend.lastMb) - qint64(trend.firstMb);
            if (delta == 0) {
                continue;
            }
            QString sign = delta > 0 ? "+" : "";
            lines << ko("<div style='font-size:14px;line-height:24px'><b>%1. %2</b> %3%4 MB · %5</div>")
                         .arg(rank++)
                         .arg(trend.name.toHtmlEscaped())
                         .arg(sign)
                         .arg(delta)
                         .arg(processPatternLabel(trend));
        }
        if (lines.isEmpty()) {
            return ko("<div style='font-size:14px;line-height:24px;color:#94a3b8'>아직 눈에 띄는 증가 프로세스가 없습니다.</div>");
        }
        return lines.join("");
    }

    void showDailyReportDialog() {
        auto *dialog = new QDialog(this);
        dialog->setWindowTitle(ko("오늘의 메모리 리포트"));
        dialog->resize(820, 660);

        auto *layout = new QVBoxLayout(dialog);
        layout->setContentsMargins(24, 22, 24, 22);
        layout->setSpacing(16);

        auto *titleLabel = new QLabel(ko("오늘의 메모리 리포트"));
        titleLabel->setStyleSheet("font-size: 24px; font-weight: 800;");
        auto *subLabel = new QLabel(ko("오늘 하루 기록된 RAM 사용 패턴과 자동 정리 기준입니다."));
        subLabel->setStyleSheet("color: #94a3b8;");
        layout->addWidget(titleLabel);
        layout->addWidget(subLabel);

        auto *metrics = new QGridLayout();
        metrics->setHorizontalSpacing(12);
        metrics->setVerticalSpacing(14);
        metrics->addWidget(reportMetric(ko("평균 RAM"), QString("%1%").arg(averageLoadToday())), 0, 0);
        metrics->addWidget(reportMetric(ko("최고 RAM"), QString("%1%").arg(peakLoad)), 0, 1);
        metrics->addWidget(reportMetric(ko("평균 사용량"), QString("%1 MB").arg(averageUsedToday())), 0, 2);
        metrics->addWidget(reportMetric(ko("자동 정리"), ko("%1회").arg(optimizeCount)), 1, 0);
        metrics->addWidget(reportMetric(ko("학습된 시간"), ko("%1/24").arg(coveredHours())), 1, 1);
        metrics->addWidget(reportMetric(ko("다음 기준"), learnedThreshold > 0
                                        ? QString("%1%").arg(learnedThreshold)
                                        : ko("학습 중")), 1, 2);
        if (!samples.isEmpty()) {
            const MemorySnapshot latest = samples.last();
            metrics->addWidget(reportMetric(ko("커밋 메모리"), QString("%1 / %2 MB").arg(latest.commitMb).arg(latest.commitLimitMb)), 2, 0);
            metrics->addWidget(reportMetric(ko("페이지 파일"), QString("%1 / %2 MB").arg(latest.pageFileUsedMb).arg(latest.pageFileTotalMb)), 2, 1);
            metrics->addWidget(reportMetric(ko("Non-Paged Pool"), QString("%1 MB").arg(latest.nonPagedPoolMb)), 2, 2);
            metrics->addWidget(reportMetric(ko("Paged Pool"), QString("%1 MB").arg(latest.pagedPoolMb)), 3, 0);
        }
        layout->addLayout(metrics);

        auto *growthTitle = new QLabel(ko("오늘 가장 많이 증가한 프로세스"));
        growthTitle->setStyleSheet("font-size: 15px; font-weight: 800;");
        auto *growthList = new QLabel(processGrowthSummaryHtml());
        growthList->setTextFormat(Qt::RichText);
        growthList->setObjectName("reportMetric");
        growthList->setMinimumHeight(86);
        layout->addWidget(growthTitle);
        layout->addWidget(growthList);

        auto *chart = new HourChartWidget(dialog);
        chart->setStats(hours, darkModeEnabled());
        chart->setObjectName("reportChart");
        layout->addWidget(chart);

        auto *note = new QLabel(ko("하루 중 18시간 이상 기록되면 날짜가 바뀔 때 PC 맞춤 정리 기준이 저장됩니다."));
        note->setWordWrap(true);
        note->setStyleSheet("color: #94a3b8;");
        layout->addWidget(note);

        auto *buttons = new QHBoxLayout();
        buttons->addStretch();
        auto *saveButton = new QPushButton(ko("파일로 저장"));
        auto *closeButton = new QPushButton(ko("닫기"));
        buttons->addWidget(saveButton);
        buttons->addWidget(closeButton);
        layout->addLayout(buttons);

        connect(saveButton, &QPushButton::clicked, dialog, [this] {
            writeDailyReport();
            QDesktopServices::openUrl(QUrl::fromLocalFile(todayReportPath()));
        });
        connect(closeButton, &QPushButton::clicked, dialog, &QDialog::accept);

        dialog->setStyleSheet(darkModeEnabled() ? R"(
            QDialog { background: #0f172a; color: #e5e7eb; font-family: "Malgun Gothic"; }
            QLabel#reportMetric, HourChartWidget#reportChart {
                background: #111827; border: 1px solid #243244; border-radius: 14px; padding: 14px;
            }
            QPushButton { min-height: 36px; background: #111827; color: #e5e7eb; border: 1px solid #334155; border-radius: 9px; padding: 0 16px; }
        )" : R"(
            QDialog { background: #f6f8fb; color: #111827; font-family: "Malgun Gothic"; }
            QLabel#reportMetric, HourChartWidget#reportChart {
                background: white; border: 1px solid #dae1eb; border-radius: 14px; padding: 14px;
            }
            QPushButton { min-height: 36px; background: white; border: 1px solid #d7dee9; border-radius: 9px; padding: 0 16px; }
        )");

        dialog->exec();
        dialog->deleteLater();
    }

    QVector<LongTermPoint> loadLongTermPoints(int hoursBack) const {
        QVector<LongTermPoint> result;
        QFile file(longTermTrendPath());
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            return result;
        }

        QDateTime cutoff = QDateTime::currentDateTime().addSecs(-hoursBack * 3600);
        QTextStream in(&file);
        bool first = true;
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            if (line.isEmpty()) {
                continue;
            }
            if (first) {
                first = false;
                if (line.startsWith("time,")) {
                    continue;
                }
            }

            QStringList parts = line.split(',');
            if (parts.size() < 9) {
                continue;
            }

            LongTermPoint point;
            point.time = QDateTime::fromString(parts[0], Qt::ISODate);
            if (!point.time.isValid() || point.time < cutoff) {
                continue;
            }
            point.load = parts[1].toInt();
            point.usedMb = parts[2].toULongLong();
            point.commitMb = parts[3].toULongLong();
            point.nonPagedPoolMb = parts[7].toULongLong();
            point.pagedPoolMb = parts[8].toULongLong();
            result.push_back(point);
        }
        return result;
    }

    QString longTermSummary(const QVector<LongTermPoint> &points) const {
        if (points.isEmpty()) {
            return ko("아직 장기 추세 데이터가 없습니다. 5분마다 자동 저장됩니다.");
        }

        const LongTermPoint &first = points.first();
        const LongTermPoint &last = points.last();
        qint64 ramDelta = qint64(last.usedMb) - qint64(first.usedMb);
        qint64 commitDelta = qint64(last.commitMb) - qint64(first.commitMb);
        qint64 nonPagedDelta = qint64(last.nonPagedPoolMb) - qint64(first.nonPagedPoolMb);
        return ko("기록 %1개 · RAM %2%3 MB · 커밋 %4%5 MB · Non-Paged Pool %6%7 MB")
            .arg(points.size())
            .arg(ramDelta >= 0 ? "+" : "")
            .arg(ramDelta)
            .arg(commitDelta >= 0 ? "+" : "")
            .arg(commitDelta)
            .arg(nonPagedDelta >= 0 ? "+" : "")
            .arg(nonPagedDelta);
    }

    void showLongTermTrendDialog() {
        auto *dialog = new QDialog(this);
        dialog->setWindowTitle(ko("장기 메모리 추세"));
        dialog->resize(860, 520);

        auto *layout = new QVBoxLayout(dialog);
        layout->setContentsMargins(24, 22, 24, 22);
        layout->setSpacing(14);

        auto *header = new QHBoxLayout();
        auto *titleLabel = new QLabel(ko("장기 메모리 추세"));
        titleLabel->setStyleSheet("font-size: 24px; font-weight: 800;");
        auto *range = new QComboBox();
        range->addItems({ko("1시간"), ko("24시간"), ko("7일"), ko("30일")});
        range->setCurrentIndex(1);
        header->addWidget(titleLabel, 1);
        header->addWidget(range);
        layout->addLayout(header);

        auto *summaryLabel = new QLabel();
        summaryLabel->setWordWrap(true);
        summaryLabel->setStyleSheet("color: #94a3b8;");
        layout->addWidget(summaryLabel);

        auto *chart = new LongTermChartWidget(dialog);
        chart->setObjectName("reportChart");
        layout->addWidget(chart, 1);

        auto refresh = [this, range, chart, summaryLabel] {
            int hours = 24;
            if (range->currentIndex() == 0) {
                hours = 1;
            } else if (range->currentIndex() == 2) {
                hours = 24 * 7;
            } else if (range->currentIndex() == 3) {
                hours = 24 * 30;
            }
            QVector<LongTermPoint> points = loadLongTermPoints(hours);
            chart->setPoints(points, darkModeEnabled());
            summaryLabel->setText(longTermSummary(points));
        };
        connect(range, &QComboBox::currentIndexChanged, dialog, refresh);
        refresh();

        auto *buttons = new QHBoxLayout();
        buttons->addStretch();
        auto *openFileButton = new QPushButton(ko("기록 파일 열기"));
        auto *closeButton = new QPushButton(ko("닫기"));
        buttons->addWidget(openFileButton);
        buttons->addWidget(closeButton);
        layout->addLayout(buttons);

        connect(openFileButton, &QPushButton::clicked, dialog, [this] {
            QDesktopServices::openUrl(QUrl::fromLocalFile(longTermTrendPath()));
        });
        connect(closeButton, &QPushButton::clicked, dialog, &QDialog::accept);

        dialog->setStyleSheet(darkModeEnabled() ? R"(
            QDialog { background: #0f172a; color: #e5e7eb; font-family: "Malgun Gothic"; }
            LongTermChartWidget#reportChart {
                background: #111827; border: 1px solid #243244; border-radius: 14px; padding: 14px;
            }
            QPushButton, QComboBox { min-height: 36px; background: #111827; color: #e5e7eb; border: 1px solid #334155; border-radius: 9px; padding: 0 16px; }
        )" : R"(
            QDialog { background: #f6f8fb; color: #111827; font-family: "Malgun Gothic"; }
            LongTermChartWidget#reportChart {
                background: white; border: 1px solid #dae1eb; border-radius: 14px; padding: 14px;
            }
            QPushButton, QComboBox { min-height: 36px; background: white; border: 1px solid #d7dee9; border-radius: 9px; padding: 0 16px; }
        )");

        dialog->exec();
        dialog->deleteLater();
    }

    void showStartupUpdateStatus(const QString &message, int closeAfterMs = 3500) {
        auto *box = new QMessageBox(QMessageBox::Information,
                                    ko("업데이트 상태"),
                                    message,
                                    QMessageBox::NoButton,
                                    this);
        box->setAttribute(Qt::WA_DeleteOnClose);
        box->addButton(ko("확인"), QMessageBox::AcceptRole);
        box->setModal(false);
        box->show();
        if (closeAfterMs > 0) {
            QTimer::singleShot(closeAfterMs, box, &QMessageBox::accept);
        }
    }

    void checkForUpdates(bool startupCheck) {
        if (startupCheck) {
            if (startupUpdateChecked) {
                return;
            }
            startupUpdateChecked = true;
            appendLog(ko("시작 시 업데이트 상태를 확인합니다."));
        }

        QByteArray payload;
        QString manifestSource;
        QString localManifest = QCoreApplication::applicationDirPath() + "/update.json";

        QFile localFile(localManifest);
        if (localFile.open(QIODevice::ReadOnly)) {
            payload = localFile.readAll();
            manifestSource = localManifest;
        } else {
            QSettings settings(reportDir() + "/profile.ini", QSettings::IniFormat);
            QString url = settings.value("updateUrl").toString();
            if (url.isEmpty()) {
                if (startupCheck) {
                    showStartupUpdateStatus(ko("업데이트 정보를 읽을 수 없습니다.\n설치 폴더의 update.json이 필요합니다."));
                } else {
                    QMessageBox::information(this,
                                             ko("업데이트 확인"),
                                             ko("업데이트 서버가 아직 설정되지 않았습니다.\n설치 폴더의 update.json 또는 profile.ini의 updateUrl을 사용합니다."));
                }
                appendLog(ko("업데이트 확인: 업데이트 서버가 설정되지 않았습니다."));
                return;
            }

            QString fetchError;
            if (!fetchUrl(QUrl(url), &payload, &fetchError)) {
                if (startupCheck) {
                    showStartupUpdateStatus(ko("업데이트 상태를 확인하지 못했습니다.\n%1").arg(fetchError));
                } else {
                    QMessageBox::warning(this, ko("업데이트 확인"), ko("업데이트 정보를 가져오지 못했습니다."));
                }
                appendLog(ko("업데이트 확인 실패: %1").arg(fetchError));
                return;
            }

            manifestSource = url;
        }

        QJsonParseError error {};
        QJsonDocument document = QJsonDocument::fromJson(payload, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            if (startupCheck) {
                showStartupUpdateStatus(ko("업데이트 정보 형식이 올바르지 않습니다."));
            } else {
                QMessageBox::warning(this, ko("업데이트 확인"), ko("업데이트 정보 형식이 올바르지 않습니다."));
            }
            return;
        }

        QJsonObject object = document.object();
        QString remoteManifestUrl = object.value("updateUrl").toString();
        if (!remoteManifestUrl.isEmpty()) {
            QString fetchError;
            if (!fetchUrl(QUrl(remoteManifestUrl), &payload, &fetchError)) {
                if (startupCheck) {
                    showStartupUpdateStatus(ko("GitHub 업데이트 정보를 가져오지 못했습니다.\n%1").arg(fetchError));
                } else {
                    QMessageBox::warning(this, ko("업데이트 확인"), ko("GitHub 업데이트 정보를 가져오지 못했습니다."));
                }
                appendLog(ko("GitHub 업데이트 확인 실패: %1").arg(fetchError));
                return;
            }

            manifestSource = remoteManifestUrl;

            document = QJsonDocument::fromJson(payload, &error);
            if (error.error != QJsonParseError::NoError || !document.isObject()) {
                if (startupCheck) {
                    showStartupUpdateStatus(ko("GitHub 업데이트 정보 형식이 올바르지 않습니다."));
                } else {
                    QMessageBox::warning(this, ko("업데이트 확인"), ko("GitHub 업데이트 정보 형식이 올바르지 않습니다."));
                }
                return;
            }
            object = document.object();
        }

        QString latest = object.value("version").toString();
        QString downloadUrl = object.value("downloadUrl").toString();
        QString sha256 = object.value("sha256").toString().trimmed().toUpper();
        QString checksumUrl = object.value("checksumUrl").toString();
        QString notes = object.value("notes").toString();

        if (latest.isEmpty()) {
            if (startupCheck) {
                showStartupUpdateStatus(ko("업데이트 정보에 버전이 없습니다."));
            } else {
                QMessageBox::warning(this, ko("업데이트 확인"), ko("업데이트 정보에 버전이 없습니다."));
            }
            return;
        }

        if (compareVersions(QString::fromUtf8(APP_VERSION), latest) >= 0) {
            if (startupCheck) {
                showStartupUpdateStatus(ko("업데이트 상태: 최신 버전입니다.\n현재 버전: %1").arg(APP_VERSION), 2500);
            } else {
                QMessageBox::information(this,
                                         ko("업데이트 확인"),
                                         ko("현재 최신 버전을 사용 중입니다.\n현재 버전: %1").arg(APP_VERSION));
            }
            appendLog(ko("업데이트 확인: 최신 버전입니다."));
            return;
        }

        bool checksumLooksValid = sha256.size() == 64;
        QString verifyText = checksumLooksValid
                                 ? ko("검증 SHA-256:\n%1").arg(sha256)
                                 : ko("주의: 업데이트 정보에 검증용 SHA-256 해시가 없습니다.");
        if (!checksumUrl.isEmpty()) {
            verifyText += ko("\n체크섬 파일:\n%1").arg(checksumUrl);
        }

        QString message = ko("새 버전이 있습니다.\n\n현재 버전: %1\n새 버전: %2\n\n%3\n\n%4")
                              .arg(APP_VERSION, latest, notes, verifyText);
        appendLog(checksumLooksValid
                      ? ko("업데이트 발견: %1, SHA-256 검증 정보 포함, 출처: %2").arg(latest, manifestSource)
                      : ko("업데이트 발견: %1, 검증 해시 없음, 출처: %2").arg(latest, manifestSource));

        if (startupCheck) {
            if (!checksumLooksValid) {
                showStartupUpdateStatus(ko("새 버전 %1이 있지만 검증용 SHA-256이 없어 자동 업데이트를 중단했습니다.").arg(latest), 0);
                appendLog(ko("자동 업데이트 중단: SHA-256 검증 정보가 없습니다."));
                return;
            }

            showStartupUpdateStatus(ko("새 버전 %1을 발견했습니다.\n설정과 리포트는 유지하고 앱 파일만 새로 설치합니다.").arg(latest), 2500);
            downloadAndInstallUpdate(downloadUrl, sha256, latest);
            return;
        }

        QMessageBox box(this);
        box.setWindowTitle(ko("업데이트 확인"));
        box.setText(message);
        QPushButton *installButton = box.addButton(ko("자동 업데이트 설치"), QMessageBox::AcceptRole);
        QPushButton *openButton = box.addButton(ko("브라우저로 열기"), QMessageBox::ActionRole);
        box.addButton(ko("나중에"), QMessageBox::RejectRole);
        box.exec();

        if (box.clickedButton() == installButton) {
            downloadAndInstallUpdate(downloadUrl, sha256, latest);
        } else if (box.clickedButton() == openButton && !downloadUrl.isEmpty()) {
            QDesktopServices::openUrl(QUrl(downloadUrl));
        }
    }

    void downloadAndInstallUpdate(const QString &downloadUrl, const QString &expectedSha256, const QString &latestVersion) {
        QUrl url(downloadUrl);
        if (!url.isValid() || downloadUrl.isEmpty()) {
            QMessageBox::warning(this, ko("업데이트 설치"), ko("다운로드 주소가 올바르지 않습니다."));
            return;
        }

        QNetworkAccessManager manager;
        QNetworkRequest request(url);
        request.setHeader(QNetworkRequest::UserAgentHeader,
                          QString("MemoryGuardian/%1").arg(APP_VERSION));
        QNetworkReply *reply = manager.get(request);

        QProgressDialog progress(ko("업데이트 설치 파일을 다운로드하는 중입니다."),
                                 ko("취소"),
                                 0,
                                 100,
                                 this);
        progress.setWindowTitle(ko("업데이트 설치"));
        progress.setWindowModality(Qt::WindowModal);
        progress.setMinimumDuration(0);

        connect(reply, &QNetworkReply::downloadProgress, this, [&progress](qint64 received, qint64 total) {
            if (total > 0) {
                progress.setValue(int(received * 100 / total));
            }
        });
        connect(&progress, &QProgressDialog::canceled, reply, &QNetworkReply::abort);

        QEventLoop loop;
        connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        loop.exec();

        if (reply->error() != QNetworkReply::NoError) {
            QString reason = reply->errorString();
            reply->deleteLater();
            QMessageBox::warning(this, ko("업데이트 설치"), ko("업데이트 다운로드에 실패했습니다.\n%1").arg(reason));
            appendLog(ko("업데이트 다운로드 실패: %1").arg(reason));
            return;
        }

        QByteArray installerData = reply->readAll();
        reply->deleteLater();
        progress.setValue(100);

        QString actualSha256 = QString::fromLatin1(QCryptographicHash::hash(installerData, QCryptographicHash::Sha256).toHex()).toUpper();
        if (expectedSha256.size() == 64 && actualSha256 != expectedSha256.toUpper()) {
            QMessageBox::critical(this,
                                  ko("업데이트 차단"),
                                  ko("다운로드한 설치 파일의 SHA-256 값이 업데이트 정보와 다릅니다.\n\n설치가 차단되었습니다."));
            appendLog(ko("업데이트 차단: SHA-256 불일치. 예상 %1, 실제 %2").arg(expectedSha256, actualSha256));
            return;
        }

        QString tempPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        if (tempPath.isEmpty()) {
            tempPath = QDir::tempPath();
        }
        QDir().mkpath(tempPath);
        QString installerPath = tempPath + QString("/MemoryGuardianSetup-%1.exe").arg(latestVersion);

        QFile installerFile(installerPath);
        if (!installerFile.open(QIODevice::WriteOnly)) {
            QMessageBox::warning(this, ko("업데이트 설치"), ko("설치 파일을 임시 폴더에 저장하지 못했습니다."));
            appendLog(ko("업데이트 설치 파일 저장 실패: %1").arg(installerPath));
            return;
        }
        installerFile.write(installerData);
        installerFile.close();

        appendLog(expectedSha256.size() == 64
                      ? ko("업데이트 파일 검증 완료: SHA-256 일치")
                      : ko("업데이트 파일 다운로드 완료: 검증 해시가 없어 수동 확인이 필요합니다."));

        QMessageBox::information(this,
                                 ko("업데이트 설치"),
                                 ko("설치 파일 검증이 끝났습니다.\n기존 앱 파일을 정리한 뒤 재설치합니다.\n리포트와 학습 설정은 유지됩니다.\nWindows 권한 요청에서 예를 누르세요."));

        QString installerArgs = ko("/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CLOSEAPPLICATIONS /RESTARTAPP=1 /MERGETASKS=\"startup,desktopicon\"");

        HINSTANCE result = ShellExecuteW(nullptr,
                                         L"runas",
                                         reinterpret_cast<LPCWSTR>(installerPath.utf16()),
                                         reinterpret_cast<LPCWSTR>(installerArgs.utf16()),
                                         nullptr,
                                         SW_SHOWNORMAL);
        if (reinterpret_cast<intptr_t>(result) <= 32) {
            QMessageBox::warning(this, ko("업데이트 설치"), ko("설치 프로그램을 실행하지 못했습니다."));
            appendLog(ko("업데이트 설치 프로그램 실행 실패"));
            return;
        }

        appendLog(ko("자동 업데이트 설치를 시작했습니다. 기존 앱 파일은 정리하고 설정과 리포트는 유지합니다."));
        quitRequested = true;
        clearRunMarker();
        writeDailyReport();
        qApp->quit();
    }

    void appendCsv(const MemorySnapshot &snapshot, const QiState &qi, int activeThreshold) {
        QFile file(todayCsvPath());
        bool fresh = !file.exists();
        if (!file.open(QIODevice::Append | QIODevice::Text)) {
            return;
        }
        QTextStream out(&file);
        if (fresh) {
            out << "time,load_percent,used_mb,total_mb,available_mb,commit_mb,commit_limit_mb,page_file_used_mb,page_file_total_mb,non_paged_pool_mb,paged_pool_mb,qi_score,threshold,optimize_count\n";
        }
        out << snapshot.time.toString(Qt::ISODate) << ','
            << snapshot.load << ','
            << snapshot.usedMb << ','
            << snapshot.totalMb << ','
            << snapshot.availableMb << ','
            << snapshot.commitMb << ','
            << snapshot.commitLimitMb << ','
            << snapshot.pageFileUsedMb << ','
            << snapshot.pageFileTotalMb << ','
            << snapshot.nonPagedPoolMb << ','
            << snapshot.pagedPoolMb << ','
            << qi.score << ','
            << activeThreshold << ','
            << optimizeCount << '\n';
    }

    void appendProcessCsv(const QVector<ProcessUsage> &processes, const QDateTime &time) {
        qint64 now = time.toMSecsSinceEpoch();
        if (lastProcessCsvMs > 0 && now - lastProcessCsvMs < 10 * 60 * 1000) {
            return;
        }
        lastProcessCsvMs = now;

        QFile file(todayProcessCsvPath());
        bool fresh = !file.exists();
        if (!file.open(QIODevice::Append | QIODevice::Text)) {
            return;
        }

        QTextStream out(&file);
        if (fresh) {
            out << "time,process_name,pid,ram_mb,delta_today_mb,growth_mb_per_hour,pattern\n";
        }

        int written = 0;
        for (const ProcessUsage &process : processes) {
            if (written >= 20) {
                break;
            }
            QString safeName = process.name;
            safeName.replace('"', "\"\"");
            QString key = QString("%1:%2").arg(process.name).arg(process.pid);
            double speed = processTrends.contains(key) ? processGrowthPerHour(processTrends[key]) : 0.0;
            QString speedValue = std::isfinite(speed) ? QString::number(speed, 'f', 1) : "";
            QString pattern = processTrends.contains(key) ? processPatternLabel(processTrends[key]) : ko("관찰 중");
            out << time.toString(Qt::ISODate) << ','
                << '"' << safeName << '"' << ','
                << process.pid << ','
                << process.usedMb << ','
                << processDeltaMb(process) << ','
                << speedValue << ','
                << '"' << pattern << '"' << '\n';
            written++;
        }
    }

    void appendLongTermTrend(const MemorySnapshot &snapshot) {
        qint64 now = snapshot.time.toMSecsSinceEpoch();
        if (lastTrendCsvMs > 0 && now - lastTrendCsvMs < 5 * 60 * 1000) {
            return;
        }
        lastTrendCsvMs = now;

        QFile file(longTermTrendPath());
        bool fresh = !file.exists();
        if (!file.open(QIODevice::Append | QIODevice::Text)) {
            return;
        }

        QTextStream out(&file);
        if (fresh) {
            out << "time,load_percent,used_mb,commit_mb,commit_limit_mb,page_file_used_mb,page_file_total_mb,non_paged_pool_mb,paged_pool_mb\n";
        }
        out << snapshot.time.toString(Qt::ISODate) << ','
            << snapshot.load << ','
            << snapshot.usedMb << ','
            << snapshot.commitMb << ','
            << snapshot.commitLimitMb << ','
            << snapshot.pageFileUsedMb << ','
            << snapshot.pageFileTotalMb << ','
            << snapshot.nonPagedPoolMb << ','
            << snapshot.pagedPoolMb << '\n';
    }

    void writeDailyReport() {
        QFile file(todayReportPath());
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            appendLog(ko("일일 리포트를 저장하지 못했습니다."));
            return;
        }

        QTextStream out(&file);
        int averageLoad = totalSampleCount ? int(totalLoadSum / quint64(totalSampleCount)) : 0;
        quint64 averageUsed = totalSampleCount ? totalUsedSum / quint64(totalSampleCount) : 0;

        out << "메모리 자동 보호기 일일 리포트\n";
        out << "날짜: " << reportDate.toString("yyyy-MM-dd") << "\n\n";
        out << "요약\n";
        out << "- 오늘 평균 RAM 사용률: " << averageLoad << "%\n";
        out << "- 오늘 평균 사용 RAM: " << averageUsed << " MB\n";
        out << "- 오늘 최고 RAM 사용률: " << peakLoad << "%\n";
        out << "- 자동 정리 횟수: " << optimizeCount << "회\n";
        out << "- 현재 PC 맞춤 정리 기준: " << adaptiveThreshold << "%\n";
        out << "- 가장 무거운 시간대: " << heaviestHour() << "시\n\n";

        if (!samples.isEmpty()) {
            const MemorySnapshot latest = samples.last();
            out << "누수 탐지 참고 수치\n";
            out << "- 커밋 메모리: " << latest.commitMb << " / " << latest.commitLimitMb << " MB\n";
            out << "- 페이지 파일: " << latest.pageFileUsedMb << " / " << latest.pageFileTotalMb << " MB\n";
            out << "- Non-Paged Pool: " << latest.nonPagedPoolMb << " MB\n";
            out << "- Paged Pool: " << latest.pagedPoolMb << " MB\n";
            out << "- 현재 판단: " << leakStatusText(latest) << "\n\n";
        }

        out << "오늘 RAM 증가 상위 프로세스\n";
        QVector<ProcessTrend> trends = sortedProcessGrowth();
        int listed = 0;
        for (const ProcessTrend &trend : trends) {
            qint64 delta = qint64(trend.lastMb) - qint64(trend.firstMb);
            if (delta <= 0 || listed >= 5) {
                continue;
            }
            double speed = processGrowthPerHour(trend);
            QString speedText = std::isfinite(speed) ? QString("%1 MB/h").arg(QString::number(speed, 'f', 0))
                                                     : ko("증가속도 계산 중");
            out << QString("- %1 PID %2: %3 MB -> %4 MB (%5%6 MB, %7, %8)\n")
                       .arg(trend.name)
                       .arg(trend.pid)
                       .arg(trend.firstMb)
                       .arg(trend.lastMb)
                       .arg(delta > 0 ? "+" : "")
                       .arg(delta)
                       .arg(speedText)
                       .arg(processPatternLabel(trend));
            listed++;
        }
        if (listed == 0) {
            out << "- 아직 눈에 띄는 증가 프로세스가 없습니다.\n";
        }
        out << "\n";

        out << "시간대별 RAM 사용량\n";
        for (int i = 0; i < 24; ++i) {
            if (hours[i].count == 0) {
                out << QString("%1시: 기록 없음\n").arg(i, 2, 10, QChar('0'));
                continue;
            }
            int avgLoad = int(hours[i].loadSum / quint64(hours[i].count));
            quint64 avgUsed = hours[i].usedSum / quint64(hours[i].count);
            out << QString("%1시: 평균 %2%, 평균 사용 %3 MB, 최고 %4%\n")
                       .arg(i, 2, 10, QChar('0'))
                       .arg(avgLoad)
                       .arg(avgUsed)
                       .arg(hours[i].peakLoad);
        }
    }

    void appendLog(const QString &message) {
        QString line = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss") + "  " + message;
        if (log) {
            log->append(QTime::currentTime().toString("HH:mm:ss") + "  " + message);
        }
        QFile file(appLogPath());
        if (file.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream out(&file);
            out << line << '\n';
        }

        QFile marker(runMarkerPath());
        if (marker.open(QIODevice::ReadWrite | QIODevice::Text)) {
            QJsonObject object;
            object["app_version"] = QString::fromUtf8(APP_VERSION);
            object["started_at"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
            object["last_event"] = message;
            marker.resize(0);
            marker.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
        }
    }

    bool darkModeEnabled() const {
        if (!themeMode || themeMode->currentIndex() == 0) {
            return systemPrefersDarkMode();
        }

        return themeMode->currentIndex() == 2;
    }

    void applyStyle() {
        bool dark = darkModeEnabled();
        if (dark) {
            setStyleSheet(R"(
            QWidget {
                background: #0f172a;
                color: #e5e7eb;
                font-family: "Malgun Gothic";
                font-size: 14px;
            }
            QLabel {
                background: transparent;
            }
            QLabel#metricLabel {
                color: #94a3b8;
                font-size: 13px;
                font-weight: 600;
            }
            QLabel#metricValue {
                color: #f8fafc;
                font-size: 30px;
                font-weight: 800;
            }
            QFrame#hero, QFrame#panel, QTextEdit#log {
                background: #111827;
                border: 1px solid #243244;
                border-radius: 14px;
            }
            QLineEdit, QSpinBox, QComboBox {
                min-height: 34px;
                background: #0b1220;
                color: #e5e7eb;
                border: 1px solid #334155;
                border-radius: 9px;
                padding: 0 10px;
            }
            QLineEdit:focus, QSpinBox:focus, QComboBox:focus {
                border: 1px solid #2563eb;
            }
            QProgressBar {
                height: 16px;
                border: 0;
                border-radius: 9px;
                background: #243244;
            }
            QProgressBar::chunk {
                border-radius: 9px;
                background: #10b981;
            }
            QProgressBar[state="warn"]::chunk {
                background: #f59e0b;
            }
            QPushButton {
                min-height: 36px;
                background: #111827;
                color: #e5e7eb;
                border: 1px solid #334155;
                border-radius: 10px;
                padding: 0 16px;
                font-weight: 600;
            }
            QPushButton#primaryButton {
                background: #2563eb;
                color: white;
                border: 0;
                font-weight: 700;
            }
            QPushButton#primaryButton:hover {
                background: #1d4ed8;
            }
            QPushButton#helpButton {
                min-width: 26px;
                min-height: 26px;
                max-width: 26px;
                max-height: 26px;
                border-radius: 13px;
                padding: 0;
                background: #1e293b;
                color: #bfdbfe;
                border: 1px solid #334155;
                font-weight: 800;
            }
            QTextEdit#log {
                padding: 12px;
                color: #cbd5e1;
                selection-background-color: #1e40af;
            }
            QTabWidget#lowerTabs::pane {
                border: 0;
                background: transparent;
            }
            QTabBar::tab {
                min-width: 110px;
                min-height: 34px;
                padding: 0 14px;
                margin-right: 6px;
                background: #111827;
                color: #94a3b8;
                border: 1px solid #243244;
                border-radius: 8px;
                font-weight: 700;
            }
            QTabBar::tab:selected {
                background: #1d4ed8;
                color: white;
                border-color: #2563eb;
            }
            QTableWidget#processTable {
                background: #111827;
                alternate-background-color: #0f172a;
                color: #e5e7eb;
                border: 0;
                outline: 0;
                selection-background-color: #1e3a8a;
                selection-color: white;
            }
            QTableWidget#processTable::item {
                border-bottom: 1px solid #1f2937;
                padding: 4px 8px;
            }
            QHeaderView::section {
                background: #0b1220;
                color: #94a3b8;
                border: 0;
                border-bottom: 1px solid #334155;
                padding: 8px;
                font-weight: 700;
            }
            QLabel#serverCard, QLabel#processDetail {
                color: #cbd5e1;
                line-height: 20px;
            }
            QScrollBar:vertical {
                background: transparent;
                width: 10px;
                margin: 2px;
            }
            QScrollBar::handle:vertical {
                background: #334155;
                border-radius: 5px;
                min-height: 28px;
            }
            QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
                height: 0;
                border: 0;
            }
        )");

            title->setStyleSheet("font-size: 26px; font-weight: 800; color: #f8fafc;");
            subtitle->setStyleSheet("color: #94a3b8;");
            creatorLabel->setStyleSheet("color: #60a5fa; font-size: 12px; font-weight: 700;");
            statusPill->setStyleSheet("background: #172554; color: #bfdbfe; border-radius: 14px; padding: 8px 16px; font-weight: 700; min-width: 86px;");
            return;
        }

        setStyleSheet(R"(
            QWidget {
                background: #f6f8fb;
                color: #1c2330;
                font-family: "Malgun Gothic";
                font-size: 14px;
            }
            QLabel {
                background: transparent;
            }
            QLabel#metricLabel {
                color: #697486;
                font-size: 13px;
                font-weight: 600;
            }
            QLabel#metricValue {
                color: #111827;
                font-size: 30px;
                font-weight: 800;
            }
            QFrame#hero, QFrame#panel, QTextEdit#log {
                background: white;
                border: 1px solid #dae1eb;
                border-radius: 14px;
            }
            QLineEdit, QSpinBox, QComboBox {
                min-height: 34px;
                background: white;
                border: 1px solid #d7dee9;
                border-radius: 9px;
                padding: 0 10px;
            }
            QLineEdit:focus, QSpinBox:focus, QComboBox:focus {
                border: 1px solid #2563eb;
            }
            QProgressBar {
                height: 16px;
                border: 0;
                border-radius: 9px;
                background: #e7ecf4;
            }
            QProgressBar::chunk {
                border-radius: 9px;
                background: #10b981;
            }
            QProgressBar[state="warn"]::chunk {
                background: #f59e0b;
            }
            QPushButton {
                min-height: 36px;
                background: white;
                border: 1px solid #d7dee9;
                border-radius: 10px;
                padding: 0 16px;
                font-weight: 600;
            }
            QPushButton#primaryButton {
                background: #2563eb;
                color: white;
                border: 0;
                font-weight: 700;
            }
            QPushButton#primaryButton:hover {
                background: #1d4ed8;
            }
            QPushButton#helpButton {
                min-width: 26px;
                min-height: 26px;
                max-width: 26px;
                max-height: 26px;
                border-radius: 13px;
                padding: 0;
                background: #eef4ff;
                color: #1d4ed8;
                border: 1px solid #bfdbfe;
                font-weight: 800;
            }
            QTextEdit#log {
                padding: 12px;
                color: #334155;
                selection-background-color: #dbeafe;
            }
            QTabWidget#lowerTabs::pane {
                border: 0;
                background: transparent;
            }
            QTabBar::tab {
                min-width: 110px;
                min-height: 34px;
                padding: 0 14px;
                margin-right: 6px;
                background: white;
                color: #697486;
                border: 1px solid #d7dee9;
                border-radius: 8px;
                font-weight: 700;
            }
            QTabBar::tab:selected {
                background: #2563eb;
                color: white;
                border-color: #2563eb;
            }
            QTableWidget#processTable {
                background: white;
                alternate-background-color: #f8fafc;
                color: #1f2937;
                border: 0;
                outline: 0;
                selection-background-color: #dbeafe;
                selection-color: #1e3a8a;
            }
            QTableWidget#processTable::item {
                border-bottom: 1px solid #e5e7eb;
                padding: 4px 8px;
            }
            QHeaderView::section {
                background: #f8fafc;
                color: #697486;
                border: 0;
                border-bottom: 1px solid #d7dee9;
                padding: 8px;
                font-weight: 700;
            }
            QLabel#serverCard, QLabel#processDetail {
                color: #334155;
                line-height: 20px;
            }
            QScrollBar:vertical {
                background: transparent;
                width: 10px;
                margin: 2px;
            }
            QScrollBar::handle:vertical {
                background: #cbd5e1;
                border-radius: 5px;
                min-height: 28px;
            }
            QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
                height: 0;
                border: 0;
            }
        )");

        title->setStyleSheet("font-size: 26px; font-weight: 800; color: #111827;");
        subtitle->setStyleSheet("color: #697486;");
        creatorLabel->setStyleSheet("color: #2563eb; font-size: 12px; font-weight: 700;");
        statusPill->setStyleSheet("background: #e8f1ff; color: #1d4ed8; border-radius: 14px; padding: 8px 16px; font-weight: 700; min-width: 86px;");
    }
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setStyle("Fusion");
    QApplication::setWindowIcon(QIcon(":/branding/haimez.ico"));
    QApplication::setQuitOnLastWindowClosed(false);

    QStringList arguments = QCoreApplication::arguments();
    bool backgroundStart = arguments.contains("--background");

    if (!runningAsAdmin() && !arguments.contains("--admin-requested")) {
        QMessageBox::information(nullptr,
                                 ko("관리자 권한 요청"),
                                 ko("메모리 정리와 대기 메모리 정리를 제대로 수행하려면 관리자 권한이 필요합니다.\n\n이제 Windows 권한 요청 창이 뜨면 '예'를 눌러주세요."));

        QStringList relaunchArgs;
        for (int i = 1; i < arguments.size(); ++i) {
            relaunchArgs << quotedArgument(arguments.at(i));
        }
        relaunchArgs << "--admin-requested";

        HINSTANCE result = ShellExecuteW(nullptr,
                                         L"runas",
                                         reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(QCoreApplication::applicationFilePath()).utf16()),
                                         reinterpret_cast<LPCWSTR>(relaunchArgs.join(' ').utf16()),
                                         nullptr,
                                         SW_SHOWNORMAL);
        if (reinterpret_cast<intptr_t>(result) > 32) {
            return 0;
        }

        QMessageBox::warning(nullptr,
                             ko("제한 모드"),
                             ko("관리자 권한이 허용되지 않아 제한 모드로 계속 실행합니다.\n이 경우 RAM 사용률이 바로 낮아지지 않을 수 있습니다."));
    }

    GuardianWindow window(backgroundStart);
    if (backgroundStart) {
        QTimer::singleShot(0, &window, [&window] { window.hide(); });
    } else {
        window.show();
        QTimer::singleShot(600, &window, [&window] {
            QSettings settings(QCoreApplication::applicationDirPath() + "/reports/profile.ini", QSettings::IniFormat);
            if (settings.value("permissionNoticeShown", false).toBool()) {
                return;
            }
            QMessageBox::information(&window,
                                     ko("자동 보호 안내"),
                                     ko("메모리 자동 보호기는 PC를 켤 때 백그라운드에서 자동으로 시작되도록 등록됩니다.\n\nWindows가 관리자 권한 허용 창을 보여주면 '예'를 눌러야 메모리 정리 기능이 제대로 작동합니다."));
            settings.setValue("permissionNoticeShown", true);
            settings.sync();
        });
    }

    return app.exec();
}





