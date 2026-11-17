# BLE 링크와 전송 예산 (Cardiac Firmware v1.1a)

BLE 전송 구조를 바꿀 때 기준이 되는 값과 개념을 정리한 문서입니다.
값을 바꿀 때는 해당 소스 위치와 이 문서를 함께 갱신해 주세요.

- 기준 환경: Galaxy S21, Android 15 / 네트워크 프로세서 STM32WB05N (BLE_TransparentMode 펌웨어)
- 관련 소스: `Core/Src/cardiac_ble.c` (링크 설정), `Core/Src/cardiac_gatt.c` (GATT 서비스, 패킷 생성)

---

## 1. 링크 파라미터

| 항목 | 값 | 비고 / 소스 |
|---|---|---|
| Connection interval | 요청 15~30 ms, **30 ms로 가정** | `CARDIAC_LINK_CONN_INTERVAL_MIN/MAX` = 12/24 (단위 1.25 ms). 최종 값은 폰이 정함. 실제 값: `cardiac_ble_conn_interval_us` |
| Peripheral latency | 0 | `CARDIAC_LINK_PERIPHERAL_LATENCY` |
| Supervision timeout | 4 s | `CARDIAC_LINK_SUPERVISION_TIMEOUT` = 400 (단위 10 ms) |
| 초당 connection event | 1000 / 30 = **33.3회/s** | |
| PHY | 2M 요청 | `CARDIAC_LINK_PHY_2M`. 실제 값: `cardiac_ble_tx_phy`, `cardiac_ble_rx_phy` |
| LL data length (DLE) | 251 B 요청 | `CARDIAC_LINK_TX_OCTETS`. 157 B notification 하나가 무선 패킷 1개에 들어감 |
| ATT MTU | 기본 23, 최대 160 | 아래 2장 참고 |

### Event당 notification 수 (S21, 측정 필요)

- connection event 한 번에 보낼 수 있는 패킷 수는 폰의 BLE 컨트롤러, WB05N TX 버퍼 수, PHY, 패킷 길이에 따라 달라집니다. 표준에서 정한 값이 아닙니다.
- S21 / Android 15에서 이 값을 공식적으로 확인한 자료는 없습니다. 이 문서에서는 **보수적으로 4개/event**로 가정합니다. Android 기기는 보통 4~6개 이상을 처리합니다.
- **측정 방법** (Live Expressions, 1초마다 갱신되는 디버그 변수)

  | 변수 | 의미 |
  |---|---|
  | `cardiac_gatt_notify_per_s` / `_peak` | 최근 1초 동안 성공한 notification 수 / 최대값 |
  | `cardiac_gatt_notify_per_event_x100` / `_peak_x100` | 위 값 × 실제 connection interval → event당 notification 수 × 100 |
  | `cardiac_gatt_notify_tx_full_count` | WB05N TX 버퍼가 꽉 차서 거절된 횟수 |
  | `cardiac_ble_conn_interval_us` | 폰이 정한 실제 connection interval |

  - ECG, SCG, R-peak, Status, Temperature를 모두 구독하고 측정합니다.
  - `tx_full_count`가 **늘지 않으면** 링크에 여유가 있는 상태입니다. 이때 `per_event`는 필요한 양이지 최대 용량이 아닙니다.
  - `tx_full_count`가 **계속 늘면** 링크가 포화된 상태이고, 이때의 `per_event`가 S21의 실제 용량입니다.
  - 상한을 정확히 알고 싶으면 BLE sniffer(nRF Sniffer 등)로 event당 패킷 수를 직접 셉니다.

### 가정한 notification 전송률

```
전송 가능 notification 수 = 초당 connection event × event당 notification
                         = 33.3 × 4 ≈ 133개/s   (가정치, S21 실측으로 갱신 필요)
```

---

## 2. ATT MTU

- **23**: BLE 표준에서 정한 시작값입니다. MTU 교환 전까지 이 값을 씁니다. notification payload는 23 − 3 = 20 B입니다.
- **160**: WB05N BLE_TransparentMode 펌웨어가 지원하는 최대값입니다. payload는 157 B입니다. U5 쪽 코드는 247(payload 244 B)까지 처리할 수 있지만(`CARDIAC_STREAM_MAX_PAYLOAD`), 160보다 크게 쓰려면 WB05N 펌웨어를 수정해야 합니다.
- 최종 MTU는 **min(폰의 최대값, 기기의 최대값)** 입니다.

### 협상 순서

1. 연결 직후에는 MTU = 23입니다.
2. 폰이 먼저 Exchange MTU를 요청하기를 기다립니다.
   - iOS는 OS가 자동으로 요청합니다.
   - Android는 앱이 `requestMtu()`를 호출해야 요청이 나갑니다.
3. 연결 1초 뒤에도 MTU가 23이면 기기가 `aci_gatt_clt_exchange_config()`로 직접 요청합니다. 관련 상수: `CARDIAC_LINK_MTU_DELAY_MS` = 1000.
4. 결과는 `aci_att_exchange_mtu_resp_event`에서 `cardiac_gatt_att_mtu`에 저장합니다.
5. **최종 160을 예상합니다.** 확인 방법: Status 패킷 offset 2의 u16, 또는 디버그 변수 `cardiac_gatt_att_mtu`.

---

## 3. GATT 서비스 구성

128-bit UUID를 쓰며, 아래 표는 base UUID의 byte 12~13에 들어가는 short ID입니다.

| UUID | 특성 | 속성 | 크기 | 전송 주기 |
|---|---|---|---|---|
| 0x0001 | Cardiac Service | | | |
| 0x0002 | ECG stream | Notify | 가변 (헤더 12 B + 3 B × N) | 패킷이 차면 |
| 0x0003 | SCG stream | Notify | 가변 (헤더 12 B + 6 B × N) | 패킷이 차면 |
| 0x0004 | R-peak | Notify | 20 B | 심박마다 (1~3회/s) |
| 0x0005 | Status | Read, Notify | 20 B | 1 s |
| 0x0006 | Temperature | Read, Notify | 10 B | 5 s |

서비스의 attribute record는 최대 20개(`CARDIAC_SERVICE_MAX_ATTR_RECORDS`)이고, 현재 16개를 씁니다.
특성 하나는 record 3개(선언, 값, CCCD)를 차지합니다.

CCCD 비트(`cardiac_gatt_cccd_mask`, Status offset 1): bit0 ECG, bit1 SCG, bit2 R-peak, bit3 Status, bit4 Temperature

### WB05N GATT 저장 공간

- WB05N의 GATT 데이터베이스는 광고 데이터와 공유하는 작은 버퍼에 저장됩니다(`CFG_BLE_GATT_NWK_BUFFER_SIZE` = 256 B, `app_conf.h`).
- 그래서 Temperature 특성은 **맨 마지막에 등록**하고, 등록에 실패해도 다른 특성은 계속 동작하게 했습니다.
- 등록 결과는 `cardiac_gatt_temp_init_status`에 남습니다. 0x00이면 성공이고, 다른 값이면 ACI 오류 코드입니다.
- 특성을 더 추가할 때(Control, ECG CH1 등)도 이 공간을 먼저 확인해야 합니다.

### Temperature 특성 (0x0006, version 1, 10 B, little-endian)

| offset | 타입 | 내용 |
|---|---|---|
| 0 | u8 | version (1) |
| 1 | u8 | flags: bit0 = 값 유효, bit1 = 센서 / I2C 오류 |
| 2 | u16 | seq (측정을 시도할 때마다 +1, 오류여도 증가) |
| 4 | u32 | timestamp (ms, HAL tick, Status의 uptime과 같은 기준) |
| 8 | i16 | MAX30205 raw 값. 1 LSB = 1/256 °C. flags bit0이 0이면 0 |

- 온도 환산: °C = raw / 256 (예: raw 9472 → 37.00 °C)
- 첫 측정 전에 Read하면 flags = 0으로 읽힙니다. 첫 측정은 부팅 3초 뒤, 그 뒤로는 5초마다 측정합니다.
- WB05N TX 버퍼가 꽉 차서 전송하지 못하면 다음 처리 주기에 다시 보냅니다. 그사이 새 값이 들어오면 새 값으로 바꿔서 보냅니다.

---

## 4. 샘플 형식

### ECG 샘플: 3 B (24-bit)

- ADS1292R **CH2만** 보냅니다. CH1은 사용하지 않습니다(power-down).
- ADS1292R 출력은 24-bit 2의 보수입니다. 펌웨어는 이를 `int32`로 sign-extend해서 처리하고, 보낼 때 **하위 3바이트를 little-endian으로** 넣습니다.

  | byte | 내용 |
  |---|---|
  | 0 | bit 7..0 |
  | 1 | bit 15..8 |
  | 2 | bit 23..16 (bit 23 = 부호) |

- 수신 측 복원: `v = b0 | b1<<8 | b2<<16; if (v & 0x800000) v -= 0x1000000;`
- 설정
  - 500 SPS (CONFIG1 = 0x02)
  - 내부 기준전압 2.42 V (CONFIG2 = 0xA0)
  - CH2 gain 6 (CH2SET = 0x00)
- 전압 환산: 1 LSB = (2 × 2.42 V / 6) / (2^24 − 1) ≈ **48.1 nV**
- 데이터 속도: 500 Hz × 3 B = **1,500 B/s**

### SCG 샘플: 6 B (3축)

- ICM-20948 가속도계 원시값입니다. 축마다 `int16`을 little-endian으로 넣습니다.

  | byte | 내용 |
  |---|---|
  | 0~1 | X (int16) |
  | 2~3 | Y (int16) |
  | 4~5 | Z (int16) |

- 설정
  - ODR 562.5 Hz (= 1125 / (1 + 1))
  - ±2 g (FS_SEL = 0)
  - DLPF 켜짐, 대역폭 약 111 Hz
- 환산: **16384 LSB/g**
- 데이터 속도: 562.5 Hz × 6 B = **3,375 B/s**

---

## 5. ECG / SCG 스트림 패킷

ECG와 SCG는 **스트림마다 버퍼가 따로 있고, 각자 다른 특성으로 Notify합니다.**
두 신호를 한 패킷에 섞지 않습니다.

### 헤더 (12 B, little-endian)

| offset | 타입 | 내용 |
|---|---|---|
| 0 | u16 | seq (전송에 실패해도 증가하므로 수신 측에서 손실을 확인할 수 있음) |
| 2 | u32 | 첫 샘플의 sample_counter |
| 6 | u32 | 첫 샘플의 timestamp (µs, TIM2) |
| 10 | u8 | 샘플 수 N |
| 11 | u8 | format: 상위 4bit = 버전(1), 하위 4bit = 채널 마스크 (ECG 0x2 = CH2, SCG 0x7 = XYZ) |

### Notify를 호출하는 조건

스트림마다 버퍼가 따로 있고, 아래 조건 중 하나가 되면 Notify를 호출합니다.

1. 현재 MTU 기준으로 패킷이 꽉 찼을 때
2. 첫 샘플이 들어온 지 200 ms(`CARDIAC_STREAM_FLUSH_MS`)가 지났을 때
3. 샘플이 빠진 구간(sample_counter gap)이 생겼을 때

그 밖의 동작은 다음과 같습니다.

- WB05N TX 버퍼가 꽉 차면 같은 패킷을 보관했다가 다음 처리 주기에 다시 보냅니다.
- 그 밖의 오류가 나면 패킷을 버리고 seq만 증가시킵니다.
- 구독(CCCD)이 꺼져 있으면 큐에 쌓인 샘플을 버립니다.
- Notify를 호출하면 패킷은 WB05N TX 버퍼에 들어갑니다. 실제 무선 전송은 다음 connection event에서 이루어지며, 버퍼에 쌓인 ECG와 SCG 패킷이 같은 event 안에서 별도 패킷으로 연달아 나갑니다.

### 패킷당 샘플 수

```
한 패킷의 샘플 수 = (MTU − 3 − 12) / 샘플당 바이트 수
```

| MTU | 샘플에 쓸 수 있는 크기 | ECG (3 B) | SCG (6 B) |
|---|---|---|---|
| 23 | 20 − 12 = 8 B | 2샘플/패킷 (4 ms) | 1샘플/패킷 (1.8 ms) |
| **160** | 157 − 12 = **145 B** | **48샘플/패킷 (약 96 ms)** | **24샘플/패킷 (약 43 ms)** |

---

## 6. 필요한 최소 notification 수

| 스트림 | MTU 160 | MTU 23 |
|---|---|---|
| ECG | 500 / 48 = 10.4개/s | 500 / 2 = 250개/s |
| SCG | 562.5 / 24 = 23.4개/s | 562.5 / 1 = 562.5개/s |
| R-peak | 1~3개/s (60~180 bpm) | 1~3개/s |
| Status | 1개/s | 1개/s |
| Temperature | 0.2개/s (5 s 주기) | 0.2개/s |
| **합계** | **약 38개/s** | **약 817개/s** |

- MTU 160에서 필요한 양은 약 38개/s입니다. 가정한 전송률 133개/s의 약 29%입니다.
  - event당 필요한 notification 수 = 38 / 33.3 ≈ 1.14이므로, **event당 2개만 보낼 수 있어도 충분합니다.**
- MTU 23으로 남으면 필요한 양이 약 817개/s로, 가정한 전송률(133개/s)을 크게 넘습니다. 이 경우 BLE 큐(약 2초분)가 넘치면서 샘플이 빠집니다.
  - **MTU가 160으로 협상되는지가 링크 전체에서 가장 중요한 조건입니다.**

---

## 7. 작은 데이터(온도 등)를 별도 특성으로 보낼 때

- notification 길이는 MTU 크기로 고정되지 않고 **실제 데이터 길이만큼만** 전송됩니다.
  - 10 B 온도 패킷은 157 B를 채워서 보내지 않습니다.
  - 무선 패킷은 10 B + L2CAP/ATT 헤더 7 B 정도입니다. 2M PHY에서 공중 전송 시간은 약 0.1 ms입니다.
- 대신 **notification 1개**를 차지하므로, 위의 event당 notification 예산에서 1개를 씁니다.
  - 5 s 주기면 0.2개/s로, 예산 133개/s의 0.15% 수준입니다.
- 작은 데이터를 다른 패킷과 합쳐 notification 수를 줄이는 방법은 전송 빈도가 높은 데이터에서만 의미가 있습니다. 예를 들면 MTU 23으로 수백 개/s를 보내야 하는 경우입니다.

---

## 8. 향후 확장 시 전송 예산 (검토용)

### 가정

| 항목 | 가정 | 샘플 크기 | 패킷당 샘플 수 (MTU 160, 145 B) |
|---|---|---|---|
| SCG 6축 | 가속도 3축 + 자이로 3축, int16, 562.5 Hz | 12 B | 12 (약 21 ms) |
| ECG CH1 + CH2 | CH1(impedance)을 CH2와 같은 패킷에 넣음, 둘 다 24-bit, 500 Hz | 6 B | 24 (48 ms) |
| Temperature | 10 B, 5 s 주기 | - | - |
| On-device ML 결과 | 20 B 이하, 최대 3회/s (심박마다 1회로 가정) | - | - |

CH1 impedance는 실제로 필요한 샘플링 속도에 따라 크게 달라집니다.
ECG와 같은 500 Hz로 보내는 경우가 최악의 경우이므로, 위 표는 그 기준으로 계산했습니다.

### 필요한 notification 수

| 스트림 | 현재 | 확장 후 |
|---|---|---|
| ECG | 10.4개/s | 500 / 24 = 20.8개/s |
| SCG | 23.4개/s | 562.5 / 12 = 46.9개/s |
| R-peak | 1~3개/s | 1~3개/s |
| Status | 1개/s | 1개/s |
| Temperature | - | 0.2개/s |
| ML 결과 | - | 최대 3개/s |
| **합계** | **약 38개/s** | **약 75개/s** |
| event당 필요 수 (33.3 event/s) | 1.14 | **2.25 → event당 3개 이상 필요** |
| 가정 전송률 133개/s 대비 사용률 | 29 % | **56 %** |

### 다른 병목 확인

| 구간 | 용량 | 확장 후 사용량 | 사용률 |
|---|---|---|---|
| 무선 데이터량 | 2M PHY, 30 ms event | ECG 3,000 + SCG 6,750 + 기타 약 0.1 KB/s ≈ 9.9 KB/s. 157 B 패킷 하나에 ACK와 IFS까지 약 1 ms가 걸리므로, 30 ms event당 약 2.3 ms 사용 | 약 8 % |
| U5 ↔ WB05N UART (HCI) | 921600 bps ≈ 92 KB/s | notification마다 ACI 헤더 약 13 B가 붙어 약 170 B. 75개/s × 170 B ≈ 12.8 KB/s | 약 14 % |
| WB05N MTU | `CFG_BLE_ATT_MTU_MAX` = 160 (`WB05_Firmware/.../BLE_TransparentMode/Core/Inc/app_conf.h:78`) | - | - |

### 결론

- **현재 설정(MTU 160, 30 ms, 2M PHY, DLE 251)으로 감당할 수 있습니다.**
  - 단, 조건이 있습니다. **S21이 event당 3개 이상의 notification을 처리해야 합니다.** 2개/event라면 66.7개/s에 그쳐 75개/s를 감당하지 못합니다. 그래서 이 값을 실측하는 것이 가장 중요합니다.
- 여유가 부족할 때 쓸 수 있는 방법을 효과가 큰 순서로 정리하면 다음과 같습니다.
  1. **Connection interval을 15 ms로 고정 요청**합니다. event가 66.7회/s로 늘어 전송 가능량이 2배가 됩니다. 대신 전력 소모가 늘어납니다.
  2. **WB05N MTU를 247로 올립니다.** `CFG_BLE_ATT_MTU_MAX`를 바꿔 WB05N을 다시 빌드합니다. payload가 244 B로 늘어 SCG 6축 패킷 수가 46.9개/s에서 약 28개/s로 줄어듭니다.
  3. CH1 impedance를 필요한 대역에 맞게 decimation합니다. 예를 들어 호흡용이면 50 Hz면 충분합니다.
  4. 작은 데이터(ML 결과, 온도, R-peak)를 하나의 "event" 특성으로 묶습니다.

---

## 9. 온도 측정 (MAX30205)

| 항목 | 값 | 소스 |
|---|---|---|
| 버스 | I2C1, PB6 = SCL, PB7 = SDA, 100 kHz, 외부 풀업 | CubeMX |
| 주소 | 0x48 (A0 = A1 = A2 = GND) | `MAX30205_I2C_ADDRESS_7BIT` |
| 측정 방식 | One-shot. 평소에는 shutdown 상태로 두어 센서 발열과 전력을 줄임 | `max30205.c` |
| 변환 시간 | 최대 50 ms. 스레드는 이 시간 동안 잠들어 있음 | `MAX30205_CONVERSION_TIME_MS` |
| 주기 | 5 s, 첫 측정은 부팅 3 s 뒤 | `TEMP_MEASURE_PERIOD_MS`, `TEMP_FIRST_MEASURE_DELAY_MS` |
| 스레드 | `temperature_thread`, 우선순위 12(가장 낮음), 스택 1 KB | `app_threadx.h` |
| I2C 인터럽트 | I2C1_EV / I2C1_ER, preemption priority 6 | CubeMX NVIC |

다른 측정과 통신에 영향을 주지 않도록 다음과 같이 구성했습니다.

- I2C 전송은 인터럽트 방식(`HAL_I2C_Mem_*_IT`)입니다. 전송하는 동안 스레드는 세마포어에서 잠들어 있으므로 CPU를 쓰지 않습니다.
- I2C1 인터럽트의 우선순위(6)는 ECG DRDY(1), HAL tick(3), IMU INT(4)보다 낮습니다.
- 온도 스레드는 가장 낮은 우선순위라서 ECG/SCG 수집, 처리, BLE, USB 스레드를 지연시키지 않습니다.
- 측정 결과는 `ble_temp_queue`를 통해 BLE 스레드로 넘깁니다. I2C1은 온도 스레드만 사용합니다.
- 오류가 나면 아래 순서로 복구합니다.
  1. I2C1을 DeInit합니다.
  2. SCL 클럭을 최대 9번 내보내 SDA가 풀리게 합니다.
  3. STOP 조건을 보냅니다.
  4. I2C1을 다시 Init합니다.
  5. 다음 측정 때 센서를 다시 초기화합니다.

디버그 변수:

- 측정: `temp_last_milli_c`, `temp_last_raw`, `temp_valid_count`, `temp_error_count`, `temp_last_status`
- I2C: `max30205_transfer_ok/error/timeout_count`, `max30205_bus_recovery_count`, `max30205_last_hal_error`
- BLE: `cardiac_gatt_temp_sent_count`, `cardiac_gatt_temp_init_status`

---

## 10. CubeMX Generate Code 시 주의사항

Generate Code를 하면 USER CODE 영역 밖의 코드는 지워집니다. 아래 표는 각 수정 사항을 어떻게 보호하는지 정리한 것입니다.

| 파일 | 수정 내용 | 보호 방법 |
|---|---|---|
| `Core/Src/custom_bus.c` | USART2 RX(PA3) 풀업 | `USER CODE BEGIN USART2_MspInit 1`에서 다시 설정 (자동 보존) |
| `STM32WB05N/Target/stm32wb_conf.h` | `HCI_READ_PACKET_NUM_MAX` = 16 | 빌드 설정의 전처리기 심볼(`-D`, Debug와 Release 모두)로 이동 (자동 보존) |
| `STM32WB05N/Target/hci_tl_interface.c` | UART 오류 후 수신 재시작, DMA wrap 처리, RESET 핀 open-drain | **Generate 후 git으로 복구 필요** |
| `Middlewares/ST/STM32WB05N/hci/hci_parser.c`, `includes/hci_parser.h` | 패킷 풀 보호, 재동기화, `hci_dropped_packet_count` | **Generate 후 git으로 복구 필요** (지워지면 컴파일 에러) |

Generate 후에는 다음 순서로 확인합니다.

1. `git status`와 `git diff`로 바뀐 파일을 확인합니다.
2. 의도하지 않은 파일은 `git checkout -- <파일>`로 되돌립니다.
   - 특히 `Drivers/`는 줄바꿈 문자(LF/CRLF)만 바뀌어 수정된 것으로 표시되는 경우가 있습니다.
