# Memory Guardian 1.3.18

## 한국어
- 전용 커밋(Private Bytes)의 지속 증가와 회수를 분석하고 누수 의심 근거를 기록합니다.
- 프로세스 이력 누적 오류를 수정하고 PID 재사용과 측정 공백을 구분합니다.
- 프로세스 및 포트 검사를 작업 스레드로 옮겼습니다.
- 페이지 파일 실제 사용량을 측정하고 실패한 측정은 학습에서 제외합니다.
- 새 설치는 알림 전용이 기본입니다. 기존 설정은 유지합니다.
- 자체 정리는 Memory Guardian만 대상으로 하며 결과와 오류를 기록하고 재시도 간격을 조절합니다.
- 프로그램 강제 종료나 시스템 캐시 강제 비우기는 하지 않습니다.

## English
- Analyze sustained private-commit growth and recovery, recording reasons for suspected leaks.
- Fix history accumulation and distinguish PID reuse and observation gaps.
- Move process and port scans off the UI thread.
- Measure actual paging-file usage; exclude failed measurements from learning.
- Default new installations to alerts only; preserve existing preferences.
- Restrict cleanup to this application's working set and back off retries.

## Validation and Limits
- 25 automated checks passed. Suspicions are not confirmed leaks.
- Cleanup does not repair another application's allocations.
- Long-duration real-world and installer/update end-to-end testing remain outstanding.
- SHA-256 checks mismatches; it is not a publisher signature.
