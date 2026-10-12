# STM32 컨베이어 클라이언트

STM32F411RE 기반 컨베이어 제어 프로젝트입니다. `Core/Src`와 `Core/Inc`에 제어 코드, `Drivers`에 HAL/CMSIS, `.vscode`에 VS Code 설정이 있습니다.

## 로컬 설정

빌드 전에 예제 설정을 복사하고 사용하는 Wi-Fi와 서버 정보를 입력하세요. 실제 설정 파일은 Git에서 제외됩니다.

```sh
cp Core/Inc/wifi_config.h.example Core/Inc/wifi_config.h
```

ST의 Arm GCC 툴체인, CMake, Ninja를 준비한 뒤 VS Code에서 빌드하거나 다음 명령을 사용하세요.

```sh
cmake --preset Debug
cmake --build --preset Debug
```

프로젝트는 ST GCC의 `-fcyclomatic-complexity` 옵션을 사용합니다. 일반 Arm GCC에서는 별도 빌드 설정으로 이 측정 옵션을 제외해야 합니다. 실제 실행과 다운로드에는 STM32 보드와 연결된 장치가 필요합니다.
