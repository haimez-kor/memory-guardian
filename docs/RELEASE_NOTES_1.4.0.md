# Memory Guardian 1.4.0

## 변경 사항
- 다크/라이트 테마와 대시보드 스크롤, 설정·명령 배치를 개선했습니다.
- 날짜 및 기간별 장기 추세 조회, 검색·정렬 가능한 이벤트 목록과 상세 보기를 추가했습니다.
- 기존 기록 폴더를 읽기 전용으로 선택할 수 있습니다.
- 그래프와 이벤트 시간축을 실제 경과 시간으로 통일했습니다.
- Steam 런처와 일반 Java를 게임으로 판단하지 않습니다. 알려진 게임과 전면 창을 함께 확인합니다.
- 같은 게임의 반복 보류 로그를 없애고 감지 상태 전환을 기록합니다.
- Java/Chrome/Edge는 GC 정보 없이 누수로 단정하지 않고 지속 증가 관찰로 표시합니다.
- 점수 툴팁에 평활 참고값을 추가하고 자체 정리 후 1분·5분 변화를 기록합니다.

## English
- Refreshed dark/light UI, scrollable dashboard and clearer controls.
- Added historical date/range selection, searchable event table, details and read-only history-folder selection.
- Aligned charts and events to elapsed time; removed launcher-based game false positives and repeated deferral logs.
- Report managed-runtime/browser growth conservatively without claiming GC-aware diagnosis.
- Added a smoothed reference score and post-cleanup observations.

## 검증 및 한계
- Automated regression checks and light/dark compact UI checks passed, including existing historical records.
- GC/heap integration, per-process learned models and FPS/latency measurement are not implemented.
- Suspicions are not confirmed leaks; cleanup only targets this app's working set.
- Post-cleanup observations require the app to remain running. SHA-256 is not a publisher signature.
