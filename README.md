# 컨베이어 QC 모니터링 시스템

Jetson에서 카메라 영상으로 지우개의 품질을 판정하고, STM32에서 컨베이어와 분류 장치를 제어하며, 판정 결과를 MySQL에 저장하는 프로젝트입니다.

## 시스템 구성

```text
카메라 → Jetson (YOLO 분류) → TCP 중계 서버
                              ├─ UTW_ARD → STM32 → 컨베이어·서보·센서·LCD
                              └─ UTW_SQL → SQL 클라이언트 → MySQL
```

TCP 중계 서버 구현, 학습 모델(`best.pt`), DB 초기화 스크립트는 이 저장소에 포함되어 있지 않습니다. 전체 시스템을 실행하려면 별도로 준비해야 합니다.

| 경로 | 역할 |
| --- | --- |
| `jetson_client/qc_client.py` | 웹캠 영상 분류 및 판정 결과 전송 |
| `stm32_client/` | STM32F411RE 펌웨어, HAL/CMSIS, CMake 및 STM32CubeIDE 프로젝트 |
| `sql_client/sql_client.c` | TCP 메시지 수신 및 MySQL 검사 결과 저장 |

## 주요 동작

- Jetson은 `background` 판정 시 수집 중인 결과를 초기화합니다.
- 배경이 아닌 분류 결과 5개를 모아 다수결로 최종 클래스를 결정합니다. 신뢰도는 최종 클래스에 해당하는 결과들의 평균입니다.
- 클래스가 `pass`이면 정상, 나머지는 불량으로 전송하며, 판정 후 2초 동안 다음 측정을 기다립니다.
- STM32는 판정과 센서 상태를 이용해 서보 분류 및 컨베이어를 제어하고 정상·불량 개수를 관리합니다. 판정 누락, 출구 감지 시간 초과, 경로 불일치 등의 오류를 처리합니다.
- SQL 클라이언트는 `IMAGE` 메시지를 `qc_db.qc_result`에 저장합니다.

## 준비 사항

| 구성 요소 | 필요 항목 |
| --- | --- |
| Jetson | Python 3, OpenCV, Ultralytics 및 장치에 맞는 PyTorch, 카메라, GUI 화면, 분류 모델 |
| STM32 | STM32F411RE 보드, 연결된 컨베이어·서보·센서·LCD·AT 명령 Wi-Fi 모듈, ST Arm GCC, CMake 3.20 이상, Ninja, 다운로드 도구 |
| SQL 클라이언트 | Linux C 컴파일러, pthread, MySQL/MariaDB 클라이언트 개발 라이브러리, MySQL 호환 DB |
| 공통 | 각 클라이언트가 접근할 수 있는 TCP 중계 서버와 서로 다른 로그인 ID |

실제 핀과 주변장치 설정은 `stm32_client/Core/Inc/main.h`, `Core/Src/main.c`, `conveyor_control.ioc`를 확인하세요.

## 실행 방법

### 1. 중계 서버와 DB 준비

중계 서버를 먼저 실행하고 클라이언트 계정을 준비합니다. 현재 Jetson 코드의 서버 주소는 `10.10.16.78:5000`, 로그인 ID는 `QC_CHK`, 수신 대상은 `UTW_SQL`과 `UTW_ARD`입니다. 실제 서버 설정과 일치해야 합니다.

SQL 코드에는 DB 주소 `127.0.0.1`, 사용자 `db_username`, 비밀번호 `db_password`, DB 이름 `qc_db`가 고정되어 있습니다. 현재 환경변수나 설정 파일을 읽는 기능은 없으므로 실제 환경에 맞추려면 별도의 코드 수정이 필요합니다. 실제 비밀번호를 저장소에 커밋하지 마세요.

`qc_result`에는 다음 INSERT 대상 열이 있어야 합니다. 저장소에 공식 DDL이 없으므로 DB 관리 시 적절한 자료형과 제약조건을 정의하세요.

| 열 | 저장 값 |
| --- | --- |
| `inspected_at` | DB 서버의 현재 시각 (`NOW()`) |
| `qc_pass` | 정상 `1`, 불량 `0` |
| `fail_reason` | 정상일 때 SQL `NULL`, 불량일 때 클래스 이름의 대문자 |
| `confidence` | 백분율 신뢰도 |

### 2. SQL 클라이언트 빌드 및 실행

Debian/Ubuntu에서는 `build-essential`, `default-libmysqlclient-dev` 패키지가 필요합니다.

저장소 루트에서:

```sh
mkdir -p /tmp/qc-build
cc sql_client/sql_client.c -o /tmp/qc-build/sql_client \
  $(mysql_config --cflags --libs) -pthread
/tmp/qc-build/sql_client <서버_IP> <포트> UTW_SQL
```

클라이언트는 중계 서버에 `[UTW_SQL:PASSWD]` 형식으로 로그인합니다. 종료할 때 표준 입력에 `quit`을 입력합니다.

### 3. STM32 설정 및 빌드

```sh
cd stm32_client
cp Core/Inc/wifi_config.h.example Core/Inc/wifi_config.h
```

생성한 로컬 헤더의 Wi-Fi SSID·비밀번호, 서버 주소·포트, 로그인 ID·비밀번호를 설정합니다. Jetson의 수신 대상과 맞추려면 STM32 로그인 ID는 `UTW_ARD`를 사용합니다. `WIFI_REPORT_TO`가 `X`이면 상태 보고 전송이 비활성화되며, 수신 계정을 지정하면 보고를 전송합니다. 이 헤더는 Git에서 제외됩니다.

ST Arm GCC가 PATH에 있는 상태에서:

```sh
cmake --preset Debug
cmake --build --preset Debug
```

결과물은 `build/Debug/conveyor_control.elf`입니다. ST-Link 등 보드에 맞는 도구로 다운로드하세요. 빌드 설정에 ST GCC의 `-fcyclomatic-complexity` 옵션이 포함되어 있어 일반 Arm GCC에서는 별도 CMake 설정으로 해당 측정 옵션을 제외해야 합니다. 자세한 내용은 [STM32 README](stm32_client/README.md)를 참고하세요.

### 4. Jetson 영상 판정 실행

저장소 루트에서 가상환경을 준비합니다.

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install ultralytics opencv-python
```

Jetson의 GPU 가속을 사용하려면 JetPack 버전에 맞는 NVIDIA PyTorch 설치 방법을 적용하세요. 일반 pip 설치만으로 CUDA 호환성이 보장되지는 않습니다. 저장소에는 의존성 버전 잠금 파일이 없습니다.

1. 학습한 **분류 모델**을 `jetson_client/best.pt`에 배치합니다. 코드는 `result.probs`를 사용하므로 객체 검출 모델과는 호환되지 않습니다.
2. 코드의 `HOST`, `PORT`, 로그인 정보와 수신 계정이 서버 설정과 일치하는지 확인합니다. 현재 CLI 및 환경변수 설정 기능은 없습니다.
3. 카메라 인덱스 `0`과 GUI 화면을 사용할 수 있는 환경에서 실행합니다.

```sh
python jetson_client/qc_client.py
```

영상 창에서 `q`, `Q` 또는 `Esc`로 종료합니다.

## 통신 형식

Jetson은 동일한 판정 결과를 두 수신 계정에 전송합니다.

```text
[UTW_SQL]IMAGE@PASS@NULL@98.5
[UTW_ARD]IMAGE@PASS@NULL@98.5
[UTW_SQL]IMAGE@FAIL@<불량_클래스_대문자>@92.1
[UTW_ARD]IMAGE@FAIL@<불량_클래스_대문자>@92.1
```

중계 서버는 수신자에게 전달할 때 `[QC_CHK]` 같은 발신자 표기를 붙이는 방식이어야 합니다. STM32는 이 표기를 제거한 뒤 `IMAGE@PASS@…`, `IMAGE@FAIL@…`을 처리하며, 단독 문자열 `PASS`/`NORMAL`, `FAIL`/`DEFECT`도 받습니다.

STM32 상태 보고의 본문 형식은 다음과 같습니다. 설정된 수신자 접두사가 앞에 붙습니다.

```text
DONE,NORMAL,<정상_개수>,<불량_개수>
DONE,DEFECT,<정상_개수>,<불량_개수>
STATE,<RUN|MANUAL_STOP|ERROR_STOP|RESTART>
ERROR,<오류_이름>
```

## 검증 및 제한 사항

저장소에는 자동화된 테스트 스위트가 없습니다. 구성 요소별 빌드 후 실제 환경에서 다음을 확인하세요.

- Jetson: 카메라 열기, 모델 로드, 정상·불량 판정 및 두 계정으로 결과 전송
- STM32: 서버 연결, 판정 수신, 센서 감지에 따른 분류, 개수 집계 및 오류 정지
- SQL: 판정 메시지 수신 후 `qc_result`에 정상·불량 레코드 저장

클라우드의 일반 x86 Linux 환경에서는 코드 분석과 빌드, CPU 기반 소프트웨어 검증이 가능합니다. Jetson GPU, 카메라 GUI, STM32 다운로드 및 모터·센서 동작은 해당 장비에서 검증해야 합니다. 모델과 중계 서버가 없으면 전체 검사 흐름을 실행할 수 없습니다.
