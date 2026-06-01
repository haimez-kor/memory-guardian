#include <QtWidgets>
#include <QtNetwork>
#include <algorithm>
#include <windows.h>
#include <psapi.h>
#include <shellapi.h>
#include <tlhelp32.h>

using NtSetSystemInformationProc = LONG (WINAPI *)(ULONG, PVOID, ULONG);
static const char *APP_VERSION = "1.1.7";

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

class HourChartWidget : public QWidget {
public:
    explicit HourChartWidget(QWidget *parent = nullptr) : QWidget(parent) {
        setMinimumHeight(190);
    }

    void setStats(const HourStats source[24], bool dark) {
        for (int i = 0; i < 24; ++i) {
            stats[i] = source[i];
        }
        darkMode = dark;
        update();
    }

protected:
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

static QVector<ProcessUsage> readTopProcesses(int limit = 5) {
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
                results.push_back(usage);
            }
        }
        CloseHandle(process);
    } while (Process32NextW(snapshot, &entry));

    CloseHandle(snapshot);
    std::sort(results.begin(), results.end(), [](const ProcessUsage &a, const ProcessUsage &b) {
        return a.usedMb > b.usedMb;
    });
    while (results.size() > limit) {
        results.pop_back();
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

    qi.pressure = pressurePenalty + availablePenalty;
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
        setMinimumSize(1000, 720);
        resize(1040, 780);

        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(24, 22, 24, 22);
        root->setSpacing(14);

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
        heroLayout->setContentsMargins(24, 18, 24, 18);
        heroLayout->setSpacing(12);

        auto *metrics = new QHBoxLayout();
        metrics->setContentsMargins(0, 0, 0, 0);
        metrics->setSpacing(28);
        ramPercent = new QLabel("0%");
        qiScore = new QLabel(ko("100점"));
        adaptiveValue = new QLabel("80%");
        metrics->addWidget(makeMetric(ko("현재 RAM 사용률"), ramPercent), 1);
        metrics->addWidget(makeMetric(ko("최적화 점수"), qiScore), 1);
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

        autoTune = new QCheckBox(ko("1시간 임시 학습 + 하루 정식 학습 기준 사용"));
        autoTune->setChecked(true);

        threshold = new QSpinBox();
        threshold->setRange(50, 98);
        threshold->setValue(74);
        threshold->setSuffix("%");
        threshold->setEnabled(false);

        action = new QComboBox();
        action->addItems({ko("자동 정리"), ko("알림만")});

        usageMode = new QComboBox();
        usageMode->addItems({ko("일반 PC"), ko("서버/개발")});
        usageMode->setCurrentIndex(1);

        themeMode = new QComboBox();
        themeMode->addItems({ko("시스템 설정"), ko("라이트 모드"), ko("다크 모드")});

        startButton = new QPushButton(ko("보호 중지"));
        startButton->setObjectName("primaryButton");

        reportButton = new QPushButton(ko("오늘 리포트 보기"));
        updateButton = new QPushButton(ko("업데이트 확인"));

        controlLayout->addWidget(autoTune, 0, 0, 1, 4);
        controlLayout->addWidget(new QLabel(ko("기준값")), 1, 0);
        controlLayout->addWidget(new QLabel(ko("자동 처리")), 1, 1);
        controlLayout->addWidget(new QLabel(ko("사용 모드")), 1, 2);
        controlLayout->addWidget(new QLabel(ko("화면 모드")), 1, 3);
        controlLayout->addWidget(threshold, 2, 0);
        controlLayout->addWidget(action, 2, 1);
        controlLayout->addWidget(usageMode, 2, 2);
        controlLayout->addWidget(themeMode, 2, 3);
        controlLayout->addWidget(startButton, 2, 4);
        controlLayout->addWidget(reportButton, 2, 5);
        controlLayout->addWidget(updateButton, 2, 6);
        controlLayout->setColumnMinimumWidth(0, 82);
        controlLayout->setColumnMinimumWidth(1, 112);
        controlLayout->setColumnMinimumWidth(2, 120);
        controlLayout->setColumnMinimumWidth(3, 128);
        controlLayout->setColumnStretch(3, 1);
        controlLayout->setColumnStretch(5, 1);
        controlLayout->setColumnStretch(6, 1);
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
        reportLayout->addWidget(leakStatus, 0, 3);
        reportLayout->addWidget(optimizeCountLabel, 0, 4);
        for (int i = 0; i < 5; ++i) {
            reportLayout->setColumnStretch(i, 1);
        }
        root->addWidget(reportPanel);

        auto *lowerLayout = new QHBoxLayout();
        lowerLayout->setSpacing(14);

        auto *processPanel = new QFrame();
        processPanel->setObjectName("panel");
        auto *processLayout = new QVBoxLayout(processPanel);
        processLayout->setContentsMargins(20, 14, 20, 14);
        processLayout->setSpacing(8);
        auto *processTitle = new QLabel(ko("RAM 사용 상위 프로그램 / 오늘 증가량"));
        processTitle->setObjectName("metricLabel");
        topProcesses = new QTextEdit();
        topProcesses->setObjectName("log");
        topProcesses->setReadOnly(true);
        topProcesses->setMinimumHeight(164);
        topProcesses->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        topProcesses->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        processLayout->addWidget(processTitle);
        processLayout->addWidget(topProcesses, 1);
        lowerLayout->addWidget(processPanel, 5);

        log = new QTextEdit();
        log->setObjectName("log");
        log->setReadOnly(true);
        log->setMinimumHeight(164);
        log->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        log->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        lowerLayout->addWidget(log, 6);
        root->addLayout(lowerLayout, 1);

        timer.setInterval(2000);
        connect(&timer, &QTimer::timeout, this, [this] { sample(); });
        connect(startButton, &QPushButton::clicked, this, [this] { toggle(); });
        connect(autoTune, &QCheckBox::toggled, this, [this](bool checked) {
            threshold->setEnabled(!checked);
            appendLog(checked ? ko("PC 맞춤 자동 기준을 사용합니다.") : ko("수동 정리 기준을 사용합니다."));
        });
        connect(usageMode, &QComboBox::currentIndexChanged, this, [this] {
            appendLog(usageMode->currentIndex() == 1
                          ? ko("서버/개발 모드: 자동 정리 기준을 더 보수적으로 잡습니다.")
                          : ko("일반 PC 모드: 기본 정리 기준을 사용합니다."));
            if (!autoTune->isChecked()) {
                threshold->setValue(usageMode->currentIndex() == 1 ? 74 : 80);
            }
        });
        connect(reportButton, &QPushButton::clicked, this, [this] {
            writeDailyReport();
            showDailyReportDialog();
        });
        connect(updateButton, &QPushButton::clicked, this, [this] {
            checkForUpdates(false);
        });
        connect(themeMode, &QComboBox::currentIndexChanged, this, [this] {
            applyStyle();
        });

        applyStyle();
        setupTray();
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
    QProgressBar *meter = nullptr;
    QCheckBox *autoTune = nullptr;
    QSpinBox *threshold = nullptr;
    QComboBox *action = nullptr;
    QComboBox *usageMode = nullptr;
    QComboBox *themeMode = nullptr;
    QPushButton *startButton = nullptr;
    QPushButton *reportButton = nullptr;
    QPushButton *updateButton = nullptr;
    QTextEdit *topProcesses = nullptr;
    QTextEdit *log = nullptr;
    QSystemTrayIcon *trayIcon = nullptr;
    QMenu *trayMenu = nullptr;
    QTimer timer;
    QVector<MemorySnapshot> samples;
    QHash<QString, ProcessTrend> processTrends;
    HourStats hours[24];
    QDate reportDate;
    qint64 lastOptimizeMs = 0;
    qint64 lastProcessCsvMs = 0;
    bool running = true;
    int adaptiveThreshold = 80;
    int learnedThreshold = 0;
    int optimizeCount = 0;
    quint64 totalLoadSum = 0;
    quint64 totalUsedSum = 0;
    int totalSampleCount = 0;
    DWORD peakLoad = 0;
    bool quitRequested = false;
    bool trayNoticeShown = false;
    bool startedInBackground = false;
    bool startupUpdateChecked = false;

    QWidget *makeMetric(const QString &labelText, QLabel *value) {
        auto *box = new QWidget();
        box->setMinimumHeight(72);
        auto *layout = new QVBoxLayout(box);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(2);

        auto *label = new QLabel(labelText);
        label->setObjectName("metricLabel");
        label->setMinimumHeight(18);
        value->setObjectName("metricValue");
        value->setMinimumHeight(42);
        value->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        layout->addWidget(label);
        layout->addWidget(value);
        layout->addStretch(1);
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
        QPixmap pixmap(64, 64);
        pixmap.fill(Qt::transparent);

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setBrush(QColor("#2563eb"));
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(QRect(4, 4, 56, 56), 14, 14);
        painter.setBrush(QColor("#10b981"));
        painter.drawEllipse(QRect(18, 18, 28, 28));
        painter.setPen(QPen(QColor("#ffffff"), 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawLine(QPoint(25, 32), QPoint(31, 38));
        painter.drawLine(QPoint(31, 38), QPoint(41, 26));
        return QIcon(pixmap);
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
        QVector<ProcessUsage> processes = readTopProcesses(20);
        updateProcessTrends(processes, snapshot.time);
        updateUi(snapshot, qi, activeThreshold);
        updateTopProcesses(processes);
        appendCsv(snapshot, qi, activeThreshold);
        appendProcessCsv(processes, snapshot.time);

        qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (qi.shouldOptimize && now - lastOptimizeMs > 15000) {
            lastOptimizeMs = now;
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

    QString biggestGrowthText() const {
        const ProcessTrend *best = nullptr;
        qint64 bestDelta = 0;
        for (auto it = processTrends.constBegin(); it != processTrends.constEnd(); ++it) {
            qint64 delta = qint64(it.value().lastMb) - qint64(it.value().firstMb);
            if (delta > bestDelta) {
                bestDelta = delta;
                best = &it.value();
            }
        }

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

        QStringList lines;
        int rank = 1;
        for (const ProcessUsage &process : processes) {
            if (rank > 5) {
                break;
            }
            qint64 delta = processDeltaMb(process);
            QString deltaText = delta >= 0 ? QString("+%1 MB").arg(delta) : QString("%1 MB").arg(delta);
            lines << QString("%1. %2%3 MB  오늘 %4  PID %5")
                         .arg(rank++)
                         .arg(process.name.left(22), -24)
                         .arg(process.usedMb, 5)
                         .arg(deltaText, 9)
                         .arg(process.pid);
        }
        if (lines.isEmpty()) {
            lines << ko("프로세스 정보를 읽는 중입니다. 관리자 권한이면 더 정확합니다.");
        }
        topProcesses->setPlainText(lines.join('\n'));
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
                                       "<div style='font-size:24px;font-weight:800;margin-top:4px'>%2</div>")
                                   .arg(label, value));
        box->setTextFormat(Qt::RichText);
        box->setMinimumHeight(70);
        box->setObjectName("reportMetric");
        return box;
    }

    void showDailyReportDialog() {
        auto *dialog = new QDialog(this);
        dialog->setWindowTitle(ko("오늘의 메모리 리포트"));
        dialog->resize(760, 620);

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
        metrics->setSpacing(12);
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
            metrics->addWidget(reportMetric(ko("커밋 메모리"), QString("%1 MB").arg(latest.commitMb)), 2, 0);
            metrics->addWidget(reportMetric(ko("페이지 파일"), QString("%1 MB").arg(latest.pageFileUsedMb)), 2, 1);
            metrics->addWidget(reportMetric(ko("풀 메모리"), ko("%1 / %2 MB").arg(latest.nonPagedPoolMb).arg(latest.pagedPoolMb)), 2, 2);
        }
        layout->addLayout(metrics);

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
        QMessageBox box(this);
        box.setWindowTitle(startupCheck ? ko("업데이트 상태") : ko("업데이트 확인"));
        box.setText(message);
        QPushButton *installButton = box.addButton(ko("다운로드 후 설치"), QMessageBox::AcceptRole);
        QPushButton *openButton = box.addButton(ko("브라우저로 열기"), QMessageBox::ActionRole);
        box.addButton(ko("나중에"), QMessageBox::RejectRole);
        box.exec();

        appendLog(checksumLooksValid
                      ? ko("업데이트 발견: %1, SHA-256 검증 정보 포함, 출처: %2").arg(latest, manifestSource)
                      : ko("업데이트 발견: %1, 검증 해시 없음, 출처: %2").arg(latest, manifestSource));
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
                                 ko("설치 파일 검증이 끝났습니다.\n이제 관리자 권한 설치 창이 열립니다.\n설치를 계속하려면 Windows 권한 요청에서 예를 누르세요."));

        HINSTANCE result = ShellExecuteW(nullptr,
                                         L"runas",
                                         reinterpret_cast<LPCWSTR>(installerPath.utf16()),
                                         nullptr,
                                         nullptr,
                                         SW_SHOWNORMAL);
        if (reinterpret_cast<intptr_t>(result) <= 32) {
            QMessageBox::warning(this, ko("업데이트 설치"), ko("설치 프로그램을 실행하지 못했습니다."));
            appendLog(ko("업데이트 설치 프로그램 실행 실패"));
            return;
        }

        appendLog(ko("업데이트 설치 프로그램을 관리자 권한으로 실행했습니다. 현재 앱을 종료합니다."));
        quitRequested = true;
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
            out << "time,process_name,pid,ram_mb,delta_today_mb\n";
        }

        int written = 0;
        for (const ProcessUsage &process : processes) {
            if (written >= 20) {
                break;
            }
            QString safeName = process.name;
            safeName.replace('"', "\"\"");
            out << time.toString(Qt::ISODate) << ','
                << '"' << safeName << '"' << ','
                << process.pid << ','
                << process.usedMb << ','
                << processDeltaMb(process) << '\n';
            written++;
        }
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
        QVector<ProcessTrend> trends;
        for (auto it = processTrends.constBegin(); it != processTrends.constEnd(); ++it) {
            trends.push_back(it.value());
        }
        std::sort(trends.begin(), trends.end(), [](const ProcessTrend &a, const ProcessTrend &b) {
            return qint64(a.lastMb) - qint64(a.firstMb) > qint64(b.lastMb) - qint64(b.firstMb);
        });
        int listed = 0;
        for (const ProcessTrend &trend : trends) {
            qint64 delta = qint64(trend.lastMb) - qint64(trend.firstMb);
            if (delta <= 0 || listed >= 5) {
                continue;
            }
            out << QString("- %1 PID %2: %3 MB -> %4 MB (%5%6 MB)\n")
                       .arg(trend.name)
                       .arg(trend.pid)
                       .arg(trend.firstMb)
                       .arg(trend.lastMb)
                       .arg(delta > 0 ? "+" : "")
                       .arg(delta);
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
        log->append(QTime::currentTime().toString("HH:mm:ss") + "  " + message);
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
            QTextEdit#log {
                padding: 12px;
                color: #cbd5e1;
                selection-background-color: #1e40af;
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
            QTextEdit#log {
                padding: 12px;
                color: #334155;
                selection-background-color: #dbeafe;
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
    QApplication::setQuitOnLastWindowClosed(false);

    bool backgroundStart = QCoreApplication::arguments().contains("--background");

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

