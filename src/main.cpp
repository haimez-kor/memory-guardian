#include <QtWidgets>
#include <QtNetwork>
#include <algorithm>
#include <windows.h>
#include <psapi.h>

using NtSetSystemInformationProc = LONG (WINAPI *)(ULONG, PVOID, ULONG);
static const char *APP_VERSION = "1.1.1";

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
    return snapshot;
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
    GuardianWindow() {
        reportDate = QDate::currentDate();
        loadLearnedProfile();
        setWindowTitle(ko("메모리 자동 보호기"));
        setMinimumSize(940, 700);

        auto *root = new QVBoxLayout(this);
        root->setContentsMargins(28, 26, 28, 26);
        root->setSpacing(18);

        auto *header = new QHBoxLayout();
        auto *titleGroup = new QVBoxLayout();
        title = new QLabel(ko("메모리 자동 보호기"));
        subtitle = new QLabel(ko("하루 동안 PC 사용 패턴을 학습해 다음 날부터 자동 정리 기준을 맞춥니다."));
        titleGroup->addWidget(title);
        titleGroup->addWidget(subtitle);
        titleGroup->setSpacing(4);

        statusPill = new QLabel(ko("자동 보호 중"));
        statusPill->setAlignment(Qt::AlignCenter);
        header->addLayout(titleGroup, 1);
        header->addWidget(statusPill);
        root->addLayout(header);

        auto *hero = new QFrame();
        hero->setObjectName("hero");
        auto *heroLayout = new QVBoxLayout(hero);
        heroLayout->setContentsMargins(26, 22, 26, 22);
        heroLayout->setSpacing(16);

        auto *metrics = new QHBoxLayout();
        ramPercent = new QLabel("0%");
        qiScore = new QLabel("QI 100");
        adaptiveValue = new QLabel("80%");
        metrics->addWidget(makeMetric(ko("현재 RAM 사용률"), ramPercent), 1);
        metrics->addWidget(makeMetric(ko("상태 점수"), qiScore), 1);
        metrics->addWidget(makeMetric(ko("자동 정리 기준"), adaptiveValue), 1);
        heroLayout->addLayout(metrics);

        meter = new QProgressBar();
        meter->setRange(0, 100);
        meter->setTextVisible(false);
        heroLayout->addWidget(meter);

        summary = new QLabel(ko("메모리 상태를 확인하는 중입니다."));
        heroLayout->addWidget(summary);
        root->addWidget(hero);

        auto *controls = new QFrame();
        controls->setObjectName("panel");
        auto *controlLayout = new QGridLayout(controls);
        controlLayout->setContentsMargins(22, 18, 22, 18);
        controlLayout->setHorizontalSpacing(14);
        controlLayout->setVerticalSpacing(8);

        autoTune = new QCheckBox(ko("하루 학습 후 PC 맞춤 기준 사용"));
        autoTune->setChecked(true);

        threshold = new QSpinBox();
        threshold->setRange(50, 98);
        threshold->setValue(80);
        threshold->setSuffix("%");
        threshold->setEnabled(false);

        action = new QComboBox();
        action->addItems({ko("자동 정리"), ko("알림만")});

        themeMode = new QComboBox();
        themeMode->addItems({ko("시스템 설정"), ko("라이트 모드"), ko("다크 모드")});

        startButton = new QPushButton(ko("보호 중지"));
        startButton->setObjectName("primaryButton");

        reportButton = new QPushButton(ko("오늘 리포트 보기"));
        updateButton = new QPushButton(ko("업데이트 확인"));

        controlLayout->addWidget(autoTune, 0, 0, 1, 2);
        controlLayout->addWidget(new QLabel(ko("기준값")), 1, 0);
        controlLayout->addWidget(new QLabel(ko("자동 처리")), 1, 1);
        controlLayout->addWidget(new QLabel(ko("고급: 프로그램 번호")), 1, 2);
        controlLayout->addWidget(new QLabel(ko("화면 모드")), 1, 3);
        controlLayout->addWidget(threshold, 2, 0);
        controlLayout->addWidget(action, 2, 1);
        processId = new QLineEdit();
        processId->setPlaceholderText(ko("비워두면 전체 RAM 보호"));
        controlLayout->addWidget(processId, 2, 2);
        controlLayout->addWidget(themeMode, 2, 3);
        controlLayout->addWidget(startButton, 2, 4);
        controlLayout->addWidget(reportButton, 2, 5);
        controlLayout->addWidget(updateButton, 2, 6);
        controlLayout->setColumnStretch(2, 1);
        root->addWidget(controls);

        auto *reportPanel = new QFrame();
        reportPanel->setObjectName("panel");
        auto *reportLayout = new QHBoxLayout(reportPanel);
        reportLayout->setContentsMargins(22, 16, 22, 16);
        reportLayout->setSpacing(14);
        todayAverage = new QLabel(ko("오늘 평균: -"));
        todayPeak = new QLabel(ko("오늘 최고: -"));
        busyHour = new QLabel(ko("가장 무거운 시간: -"));
        optimizeCountLabel = new QLabel(ko("자동 정리: 0회"));
        reportLayout->addWidget(todayAverage, 1);
        reportLayout->addWidget(todayPeak, 1);
        reportLayout->addWidget(busyHour, 1);
        reportLayout->addWidget(optimizeCountLabel, 1);
        root->addWidget(reportPanel);

        log = new QTextEdit();
        log->setObjectName("log");
        log->setReadOnly(true);
        log->setMinimumHeight(150);
        root->addWidget(log);

        timer.setInterval(2000);
        connect(&timer, &QTimer::timeout, this, [this] { sample(); });
        connect(startButton, &QPushButton::clicked, this, [this] { toggle(); });
        connect(autoTune, &QCheckBox::toggled, this, [this](bool checked) {
            threshold->setEnabled(!checked);
            appendLog(checked ? ko("PC 맞춤 자동 기준을 사용합니다.") : ko("수동 정리 기준을 사용합니다."));
        });
        connect(reportButton, &QPushButton::clicked, this, [this] {
            writeDailyReport();
            showDailyReportDialog();
        });
        connect(updateButton, &QPushButton::clicked, this, [this] {
            checkForUpdates();
        });
        connect(themeMode, &QComboBox::currentIndexChanged, this, [this] {
            applyStyle();
        });

        applyStyle();
        timer.start();
        appendLog(ko("전체 RAM 자동 보호를 시작했습니다."));
        appendLog(ko("현재 버전: %1").arg(APP_VERSION));
        appendLog(ko("하루 동안 시간대별 RAM 사용량을 기록해 자동 정리 기준을 학습합니다."));
        appendLog(learnedThreshold > 0
                      ? ko("이전에 하루 동안 학습한 자동 정리 기준을 적용합니다.")
                      : ko("하루 학습 데이터가 아직 없어 PC 사양 기준으로 보호합니다."));
        sample();
    }

private:
    QLabel *title = nullptr;
    QLabel *subtitle = nullptr;
    QLabel *statusPill = nullptr;
    QLabel *ramPercent = nullptr;
    QLabel *qiScore = nullptr;
    QLabel *adaptiveValue = nullptr;
    QLabel *summary = nullptr;
    QLabel *todayAverage = nullptr;
    QLabel *todayPeak = nullptr;
    QLabel *busyHour = nullptr;
    QLabel *optimizeCountLabel = nullptr;
    QProgressBar *meter = nullptr;
    QCheckBox *autoTune = nullptr;
    QSpinBox *threshold = nullptr;
    QComboBox *action = nullptr;
    QComboBox *themeMode = nullptr;
    QLineEdit *processId = nullptr;
    QPushButton *startButton = nullptr;
    QPushButton *reportButton = nullptr;
    QPushButton *updateButton = nullptr;
    QTextEdit *log = nullptr;
    QTimer timer;
    QVector<MemorySnapshot> samples;
    HourStats hours[24];
    QDate reportDate;
    qint64 lastOptimizeMs = 0;
    bool running = true;
    int adaptiveThreshold = 80;
    int learnedThreshold = 0;
    int optimizeCount = 0;
    quint64 totalLoadSum = 0;
    quint64 totalUsedSum = 0;
    int totalSampleCount = 0;
    DWORD peakLoad = 0;

    QWidget *makeMetric(const QString &labelText, QLabel *value) {
        auto *box = new QWidget();
        auto *layout = new QVBoxLayout(box);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);

        auto *label = new QLabel(labelText);
        label->setObjectName("metricLabel");
        value->setObjectName("metricValue");
        layout->addWidget(label);
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
        updateUi(snapshot, qi, activeThreshold);
        appendCsv(snapshot, qi, activeThreshold);

        qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (qi.shouldOptimize && now - lastOptimizeMs > 15000) {
            lastOptimizeMs = now;
            optimizeCount++;
            appendLog(ko("QI 점수가 낮거나 RAM 사용률이 기준을 넘어 자동 처리를 시작합니다."));
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

        return hardwareThreshold(totalMb);
    }

    int hardwareThreshold(quint64 totalMb) const {
        if (totalMb <= 8192) {
            return 74;
        } else if (totalMb <= 16384) {
            return 80;
        } else if (totalMb >= 32768) {
            return 86;
        }

        return 82;
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
        int learnedBase = std::clamp(averageLoad + 16, 70, 92);
        int blended = (hardwareBase * 35 + learnedBase * 65) / 100;
        return std::clamp(blended, 68, 92);
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
        qiScore->setText(QString("QI %1").arg(qi.score));
        adaptiveValue->setText(autoTune->isChecked() && learnedThreshold == 0
                                   ? ko("학습 중 %1%").arg(activeThreshold)
                                   : QString("%1%").arg(activeThreshold));
        meter->setValue(int(snapshot.load));
        summary->setText(ko("사용 중 %1 MB / 전체 %2 MB    여유 %3 MB")
                         .arg(snapshot.usedMb)
                         .arg(snapshot.totalMb)
                         .arg(snapshot.availableMb));

        if (qi.score <= 45 || snapshot.load >= DWORD(activeThreshold)) {
            statusPill->setText(ko("정리 필요"));
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
        busyHour->setText(ko("학습된 시간: %1/24").arg(coveredHours()));
        optimizeCountLabel->setText(ko("자동 정리: %1회").arg(optimizeCount));
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

    void checkForUpdates() {
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
                QMessageBox::information(this,
                                         ko("업데이트 확인"),
                                         ko("업데이트 서버가 아직 설정되지 않았습니다.\n설치 폴더의 update.json 또는 profile.ini의 updateUrl을 사용합니다."));
                appendLog(ko("업데이트 확인: 업데이트 서버가 설정되지 않았습니다."));
                return;
            }

            QNetworkAccessManager manager;
            QNetworkReply *reply = manager.get(QNetworkRequest(QUrl(url)));
            QEventLoop loop;
            connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            QTimer::singleShot(10000, &loop, &QEventLoop::quit);
            loop.exec();

            if (reply->error() != QNetworkReply::NoError) {
                QMessageBox::warning(this, ko("업데이트 확인"), ko("업데이트 정보를 가져오지 못했습니다."));
                appendLog(ko("업데이트 확인 실패: %1").arg(reply->errorString()));
                reply->deleteLater();
                return;
            }

            payload = reply->readAll();
            manifestSource = url;
            reply->deleteLater();
        }

        QJsonParseError error {};
        QJsonDocument document = QJsonDocument::fromJson(payload, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            QMessageBox::warning(this, ko("업데이트 확인"), ko("업데이트 정보 형식이 올바르지 않습니다."));
            return;
        }

        QJsonObject object = document.object();
        QString latest = object.value("version").toString();
        QString downloadUrl = object.value("downloadUrl").toString();
        QString notes = object.value("notes").toString();

        if (latest.isEmpty()) {
            QMessageBox::warning(this, ko("업데이트 확인"), ko("업데이트 정보에 버전이 없습니다."));
            return;
        }

        if (compareVersions(QString::fromUtf8(APP_VERSION), latest) >= 0) {
            QMessageBox::information(this,
                                     ko("업데이트 확인"),
                                     ko("현재 최신 버전을 사용 중입니다.\n현재 버전: %1").arg(APP_VERSION));
            appendLog(ko("업데이트 확인: 최신 버전입니다."));
            return;
        }

        QString message = ko("새 버전이 있습니다.\n\n현재 버전: %1\n새 버전: %2\n\n%3")
                              .arg(APP_VERSION, latest, notes);
        QMessageBox box(this);
        box.setWindowTitle(ko("업데이트 확인"));
        box.setText(message);
        QPushButton *openButton = box.addButton(ko("다운로드 열기"), QMessageBox::AcceptRole);
        box.addButton(ko("나중에"), QMessageBox::RejectRole);
        box.exec();

        appendLog(ko("업데이트 발견: %1, 출처: %2").arg(latest, manifestSource));
        if (box.clickedButton() == openButton && !downloadUrl.isEmpty()) {
            QDesktopServices::openUrl(QUrl(downloadUrl));
        }
    }

    void appendCsv(const MemorySnapshot &snapshot, const QiState &qi, int activeThreshold) {
        QFile file(todayCsvPath());
        bool fresh = !file.exists();
        if (!file.open(QIODevice::Append | QIODevice::Text)) {
            return;
        }
        QTextStream out(&file);
        if (fresh) {
            out << "time,load_percent,used_mb,total_mb,available_mb,qi_score,threshold,optimize_count\n";
        }
        out << snapshot.time.toString(Qt::ISODate) << ','
            << snapshot.load << ','
            << snapshot.usedMb << ','
            << snapshot.totalMb << ','
            << snapshot.availableMb << ','
            << qi.score << ','
            << activeThreshold << ','
            << optimizeCount << '\n';
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
                font-size: 34px;
                font-weight: 800;
            }
            QFrame#hero, QFrame#panel, QTextEdit#log {
                background: #111827;
                border: 1px solid #243244;
                border-radius: 18px;
            }
            QLineEdit, QSpinBox, QComboBox {
                min-height: 36px;
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
                height: 18px;
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
                min-height: 38px;
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
                padding: 14px;
                color: #cbd5e1;
                selection-background-color: #1e40af;
            }
        )");

            title->setStyleSheet("font-size: 28px; font-weight: 800; color: #f8fafc;");
            subtitle->setStyleSheet("color: #94a3b8;");
            statusPill->setStyleSheet("background: #172554; color: #bfdbfe; border-radius: 16px; padding: 8px 16px; font-weight: 700;");
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
                font-size: 34px;
                font-weight: 800;
            }
            QFrame#hero, QFrame#panel, QTextEdit#log {
                background: white;
                border: 1px solid #dae1eb;
                border-radius: 18px;
            }
            QLineEdit, QSpinBox, QComboBox {
                min-height: 36px;
                background: white;
                border: 1px solid #d7dee9;
                border-radius: 9px;
                padding: 0 10px;
            }
            QLineEdit:focus, QSpinBox:focus, QComboBox:focus {
                border: 1px solid #2563eb;
            }
            QProgressBar {
                height: 18px;
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
                min-height: 38px;
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
                padding: 14px;
                color: #334155;
                selection-background-color: #dbeafe;
            }
        )");

        title->setStyleSheet("font-size: 28px; font-weight: 800; color: #111827;");
        subtitle->setStyleSheet("color: #697486;");
        statusPill->setStyleSheet("background: #e8f1ff; color: #1d4ed8; border-radius: 16px; padding: 8px 16px; font-weight: 700;");
    }
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setStyle("Fusion");

    GuardianWindow window;
    window.show();

    return app.exec();
}
