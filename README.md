# 메모리 자동 보호기 Qt 버전

Win32 직접 그리기 대신 Qt Widgets로 만든 고급 UI 버전입니다.

## 들어간 기능

- 관리자 권한 요청 매니페스트 포함
- Qt 기반 카드형 UI
- QI 알고리즘 기반 RAM 상태 판단
- RAM 사용률, QI 점수, 자동 정리 기준 표시
- PC 사양과 사용 패턴에 맞춘 자동 정리 기준 계산
- 하루 동안 충분히 측정한 뒤 다음 날부터 학습된 정리 기준 적용
- 시간대별 RAM 사용량 기록
- 일일 리포트 자동 생성
- RAM 사용률 또는 QI 점수가 나쁘면 자동 최적화
- 실행에 필요한 Qt/MinGW DLL 포함

## 실행

```text
build\MemoryGuardian.exe
```

실행하면 Windows 관리자 권한 요청 창이 뜹니다. 메모리 정리 기능을 제대로 쓰려면 허용하세요.

## 설치 프로그램

```text
MemoryGuardianSetup.exe
```

설치 프로그램도 관리자 권한으로 실행됩니다.

설치 내용:

- `C:\Program Files\Memory Guardian`에 앱 설치
- 바탕화면 바로가기 생성
- 시작 메뉴 바로가기 생성
- 설치 후 앱 자동 실행
- `uninstall.ps1` 제거 스크립트 포함

## 리포트 저장 위치

```text
build\reports\YYYY-MM-DD-daily-report.txt
build\reports\YYYY-MM-DD-samples.csv
```

## 자동 정리 기준 학습

앱은 실행 중 시간대별 RAM 사용량을 계속 기록합니다.

- 하루 중 18시간 이상 데이터가 쌓이면 그날의 사용 패턴을 학습합니다.
- 학습이 끝나면 `build\reports\profile.ini`에 PC 맞춤 정리 기준을 저장합니다.
- 다음 날부터 저장된 기준값을 자동 정리 기준으로 사용합니다.
- 학습 전에는 PC RAM 용량에 맞춘 기본 기준을 사용합니다.

## 다시 빌드

```bat
build.bat
```

빌드 후 실행에 필요한 DLL도 자동으로 복사합니다.

설치 파일까지 다시 만들려면:

```bat
package-installer.bat
```

## 업데이트 확인

앱 안의 `업데이트 확인` 버튼은 설치 폴더의 `update.json` 또는 `reports\profile.ini`의 `updateUrl` 값을 확인합니다.

`update.json` 예:

```json
{
  "version": "1.2.0",
  "downloadUrl": "https://example.com/MemoryGuardianSetup.exe",
  "notes": "새 자동 정리 로직과 UI 개선"
}
```

현재 버전보다 높은 버전이면 다운로드 링크를 열 수 있습니다.
