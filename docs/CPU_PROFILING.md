# CPU 사용량 측정 (기능별)

기기를 연결한 상태에서 각 기능이 CPU를 얼마나 쓰는지 측정할 때 쓰는 문서입니다.

- 관련 소스: `Core/Src/cpu_usage.c`, `Core/Inc/cpu_usage.h`
- 빌드 조건: 전처리기 심볼 `TX_ENABLE_EXECUTION_CHANGE_NOTIFY`(C, 어셈블러 모두). 현재 Debug 설정에 켜져 있습니다. 끄면 측정 코드가 모두 빠집니다.
- 결과는 1초마다 갱신되며, STM32CubeIDE **Live Expressions**에서 봅니다. 값 단위 `_x100`은 0.01 %입니다(예: 1234 = 12.34 %).

---

## 1. 무엇을 재는가

세 단위로 측정합니다. 모두 같은 DWT 사이클 카운터를 씁니다.

| 단위 | 변수 | 의미 |
|---|---|---|
| 전체 | `cpu_usage_total_x100` / `_peak_x100`, `cpu_usage_idle_x100` | 100 % − idle |
| ISR | `cpu_usage_isr[]`, `cpu_usage_isr_total_x100` | ISR별 점유율 (중첩 ISR은 각각 따로 계산) |
| 스레드 | `cpu_usage_thread[]` | 스레드별 점유율 (ISR 시간 제외) |
| **기능 구간** | `cpu_prof[]`, `cpu_prof_total_x100` | 스레드 안의 기능별 점유율, 호출 수, 1회 시간 |

### 기능 구간 (`cpu_prof[]`)

| index | 이름 | 스레드 | 측정 범위 | 예상 호출 수/s |
|---|---|---|---|---|
| 0 | ECG read (SPI) | ecg_acquisition | `ADS1292R_ReadData()` 9 B SPI | 500 |
| 1 | ECG CH2 filter | ecg_acquisition | `ADS1292R_ProcessCH2Sample()` | 500 |
| 2 | ECG R-peak detect | ecg_processing | Pan-Tompkins 1샘플 처리 | 500 |
| 3 | IMU read (SPI 12 B) | scg_acquisition | `ICM20948_ReadImuRaw()` (bank 선택 + 12 B burst) | 562 |
| 4 | Temp measure (I2C) | temperature | one-shot 시작 + raw 읽기 | 0.2 |
| 5 | USB CSV E row | USB write | E row 1개 생성 (숫자 변환 + snprintf) | 500 |
| 6 | USB CSV S row | USB write | S row 1개 생성 | 562 |
| 7 | USB CSV R row | USB write | R row 1개 생성 | 1~3 |
| 8 | USB CSV T row | USB write | T row 1개 생성 | 0.2 |
| 9 | USB CDC write | USB write | `ux_device_class_cdc_acm_write()` 1회 (배치) | 약 31 |
| 10 | BLE HCI events | ble_processing | `hci_user_evt_proc()` | 약 100 (루프 횟수, ThreadX tick 100 Hz) |
| 11 | BLE link setup | ble_processing | MTU/PHY/DLE/연결 파라미터 처리 | 약 100 |
| 12 | BLE Status | ble_processing | Status 값 갱신 + Notify | 1 |
| 13 | BLE R-peak | ble_processing | R-peak 큐 처리 + Notify | 약 100 |
| 14 | BLE Temperature | ble_processing | 온도 큐 처리 + Notify | 약 100 |
| 15 | BLE ECG stream | ble_processing | ECG 패킷 채우기 + Notify | 약 100 |
| 16 | BLE SCG stream | ble_processing | SCG 패킷 채우기 + Notify | 약 100 |

항목마다 아래 값이 나옵니다.

| 필드 | 의미 |
|---|---|
| `usage_x100` / `peak_x100` | 최근 1초 점유율 / 부팅 후 최대 |
| `calls_per_s` | 최근 1초 동안 끝난 호출 수 |
| `avg_ns` | 1회 평균 CPU 시간 |
| `max_ns` / `peak_max_ns` | 최근 1초 중 가장 긴 1회 / 부팅 후 최대 |

### 계산 방식과 주의할 점

- 구간에는 **그 스레드가 실제로 실행된 시간만** 들어갑니다. 아래 시간은 빠집니다.
  - 스레드가 막혀 있는 시간: 큐 대기, `tx_thread_sleep`, I2C/USB 전송 완료를 세마포어로 기다리는 시간. 예를 들어 온도 변환 대기 50 ms는 빠집니다.
  - 다른 스레드에 선점된 시간
  - ISR 시간. ISR 시간은 `cpu_usage_isr[]`에 따로 잡힙니다.
- 반대로 **polling으로 기다리는 시간은 CPU 시간으로 들어갑니다.**
  - SPI 읽기(`HAL_SPI_TransmitReceive`)는 polling 방식이라 전송 시간 전체가 들어갑니다.
  - BLE ACI 명령(`aci_gatt_srv_notify` 등)은 WB05N의 응답을 `hci_send_req()`에서 busy-wait로 기다립니다. 그래서 **BLE 구간 시간의 상당 부분은 UART 왕복 대기**일 수 있습니다.
- BLE 구간의 `calls_per_s`는 BLE 루프 횟수입니다. 보낼 데이터가 없어도 1회로 셉니다(`tx_thread_sleep(1)` = 10 ms tick이라 초당 약 100회). 따라서 BLE 구간은 `avg_ns`보다 `usage_x100`과 `max_ns`가 의미 있습니다.
- 측정 자체의 비용: 구간 시작/끝마다 약 100 사이클이 듭니다. 초당 약 1만 회 호출되므로 전체의 약 0.5 % 정도가 측정 오버헤드로 더해집니다.
- 한 스레드에서 구간은 하나씩만 열립니다. 오류 경로에서 END를 건너뛰면 다음 BEGIN에서 자동으로 닫힙니다.

---

## 2. Live Expressions에 등록할 항목

1. `cpu_usage_total_x100`, `cpu_usage_total_peak_x100`, `cpu_usage_isr_total_x100`
2. `cpu_usage_thread` (펼쳐서 name, usage_x100, peak_x100 확인)
3. `cpu_usage_isr`
4. `cpu_prof`, `cpu_prof_total_x100`
5. `cpu_usage_window_count`: 1초마다 1씩 늘면 측정이 동작하는 것입니다.
6. `cpu_usage_reset_peaks`: 1을 쓰면 다음 창에서 모든 peak 값이 지워집니다. 시나리오를 바꿀 때마다 씁니다.

참고용 진단 변수:
- `ecg_processing_max_execution_us`: R-peak 검출의 wall-clock 최대 시간(선점 포함)입니다. `cpu_prof[2].peak_max_ns`와 비교하면 선점 영향을 알 수 있습니다.
- `icm_drdy_irq_count`: 초당 약 562 증가해야 합니다(gyro 추가 후 확인 항목).

---

## 3. 측정 순서

시나리오마다 **`cpu_usage_reset_peaks = 1`을 쓴 뒤 30초 이상** 안정된 상태를 보고 기록합니다.

| 시나리오 | 조건 | 보는 것 |
|---|---|---|
| A. 기본 | 전원만, USB와 BLE 모두 연결 안 함 | 센서 수집과 ECG 처리만의 부하 |
| B. USB | USB CDC 연결, 터미널(또는 PC 프로그램)이 데이터를 읽는 중 | USB CSV 생성과 전송 부하 |
| C. BLE | 폰 연결, ECG/SCG/R-peak/Status/Temperature 모두 구독, MTU 160 확인 | BLE 스트림 부하 |
| D. 최대 | B + C 동시 | 실제 운용 최대 부하 |
| E. (선택) MTU 23 | C에서 앱이 MTU를 올리지 않게 함 | 큐 오버플로 시 BLE 부하 |

- B에서 터미널이 데이터를 읽지 않으면 USB 쓰기가 막혀서 값이 낮게 나옵니다. 반드시 데이터를 읽는 중에 측정합니다.
- C에서 MTU는 Status 패킷 offset 2 또는 `cardiac_gatt_att_mtu`로 확인합니다.

---

## 4. 결과 기록표

### 전체

| 항목 | A | B | C | D |
|---|---|---|---|---|
| `cpu_usage_total_x100` (peak) | | | | |
| `cpu_usage_isr_total_x100` | | | | |
| `cpu_prof_total_x100` | | | | |

### 스레드 (`cpu_usage_thread[]`, usage / peak)

| 스레드 | A | B | C | D |
|---|---|---|---|---|
| ecg_acquisition_thread | | | | |
| ecg_processing_thread | | | | |
| scg_acquisition_thread | | | | |
| ble_processing_thread | | | | |
| temperature_thread | | | | |
| USB CDC Write Thread / Read Thread | | | | |
| USBX 내부 스레드 | | | | |

### ISR (`cpu_usage_isr[]`, usage / peak)

| ISR | A | B | C | D |
|---|---|---|---|---|
| EXTI0 (ADS DRDY) | | | | |
| EXTI5 (ICM INT) | | | | |
| USART2 (BLE HCI) | | | | |
| GPDMA1_Ch0 | | | | |
| OTG_HS (USB) | | | | |
| SysTick / TIM6 / TIM2 | | | | |
| I2C1_EV / ER | | | | |

### 기능 구간 (`cpu_prof[]`, scenario D 기준)

| # | 기능 | usage % (peak) | calls/s | avg µs | max µs (peak) |
|---|---|---|---|---|---|
| 0 | ECG read (SPI) | | | | |
| 1 | ECG CH2 filter | | | | |
| 2 | ECG R-peak detect | | | | |
| 3 | IMU read (SPI 12 B) | | | | |
| 4 | Temp measure (I2C) | | | | |
| 5 | USB CSV E row | | | | |
| 6 | USB CSV S row | | | | |
| 7 | USB CSV R row | | | | |
| 8 | USB CSV T row | | | | |
| 9 | USB CDC write | | | | |
| 10 | BLE HCI events | | | | |
| 11 | BLE link setup | | | | |
| 12 | BLE Status | | | | |
| 13 | BLE R-peak | | | | |
| 14 | BLE Temperature | | | | |
| 15 | BLE ECG stream | | | | |
| 16 | BLE SCG stream | | | | |

---

## 5. 결과를 볼 때 확인할 점

- **전체 peak가 70 %를 넘는지** 봅니다. 넘으면 ML 같은 기능을 추가할 여유가 적습니다.
- **ECG/SCG 수집 구간의 `peak_max_ns`가 샘플 주기보다 충분히 짧은지** 봅니다(ECG 2 ms, SCG 1.78 ms). 길면 timestamp 큐에 샘플이 쌓입니다. `ecg_timestamp_queue_drop_count`, `scg_timestamp_queue_drop_count`도 함께 봅니다.
- **IMU read**: 6축으로 바꾸기 전 accel만 읽던 6 B에서 12 B로 늘었습니다. SPI 클럭 기준으로 늘어난 시간을 확인합니다.
- **USB CSV 생성**: S row는 6축으로 늘어 열이 많아졌습니다. `snprintf`가 무거우면 이 항목이 USB 스레드 시간의 대부분을 차지합니다. 크면 바이너리 프레임으로 바꾸는 것을 검토합니다.
- **BLE 구간**: 대부분 HCI 응답 busy-wait일 가능성이 큽니다. `USART2` ISR 시간과 함께 보고, 크면 ACI 호출 수(패킷 수)를 줄이는 쪽이 효과적입니다.
