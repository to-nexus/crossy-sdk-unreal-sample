# CrossySdkUnrealSamp — 빌드·배포 가이드

> 외부 dApp 팀이 본 샘플 프로젝트로 **iOS / Android / Win64 / Mac 패키지**
> 를 만들고 실기기에 설치·실행하기 위한 절차입니다. Unity 샘플의
> `build/` 산출물 + 빌드 노트와 동일한 역할을 합니다.

본 가이드는 두 개의 선행 문서를 전제합니다:

- [`HOW_TO_USE_DEPLOYED_SDK.md`](../HOW_TO_USE_DEPLOYED_SDK.md) — 플러그인
  설치·버전 관리 (`make sdk-install`)
- [`SAMPLE_WIDGET_GUIDE.md`](./SAMPLE_WIDGET_GUIDE.md) — UMG 위젯과
  로컬라이제이션 DataTable 셋업

위 두 단계가 끝나 에디터 PIE 가 정상 동작하는 상태에서 시작합니다.

---

## 0. 공통 사전 점검

| 항목 | 확인 방법 |
|------|-----------|
| Unreal Engine 5.7 설치 | macOS 기본 경로 `/Users/Shared/Epic Games/UE_5.7`, Windows 기본 경로 `C:/Program Files/Epic Games/UE_5.7`. 다른 경로면 `make` 호출 시 `UE_ROOT="..."` 로 override |
| `crossx-plugins.lock.json` 갱신 | `make sdk-verify` 가 `[ok]` 만 출력해야 함 |
| **CROSSx Project ID** 입력 | Editor → Project Settings → Plugins → CROSSx SDK → **Prod Project ID**. 빈 값이면 iOS/Android 빌드는 `BuildException` 으로 즉시 실패 |
| Startup map 지정 | `Project Settings → Maps & Modes → Game Default Map` 이 `StartupMap` 등 실제 맵을 가리키는지 |
| `WBP_DappTestPanel` 존재 | `/Game/UI/WBP_DappTestPanel.uasset`. 다른 경로면 `Config/DefaultGame.ini` 의 `[/Script/CrossySdkUnrealSamp.DappGameMode] TestPanelWidgetClass` 수정 |

---

## 1. Makefile 변수 한눈에 보기

`Makefile` 상단 변수 모두 **명령행에서 override 가능** 합니다.

```bash
# 다른 UE 경로
make ios     UE_ROOT="/Applications/Unreal Engine 5.7"

# Shipping 빌드
make android CONFIGURATION=Shipping

# 다른 패키지 이름의 앱을 adb 로 실행
make run-android ANDROID_PACKAGE=com.example.dapp
```

| 변수 | 기본값 | 비고 |
|------|--------|------|
| `UE_ROOT` | `/Users/Shared/Epic Games/UE_5.7` | UAT 가 들어 있는 엔진 루트. Win 에서는 `RunUAT.bat`, 그 외엔 `.sh` 자동 선택. |
| `CONFIGURATION` | `Development` | `Development` / `Test` / `Shipping`. SDK 가 Prod 환경 강제 검증을 함 — `Shipping` 빌드 전에 ProjectId 가 채워졌는지 다시 확인하세요. |
| `ANDROID_PACKAGE` | `com.nexus.crossx.sdk.unrealsample.android` | `Config/DefaultEngine.ini` 의 `PackageName` 과 동일해야 `make run-android` 가 동작. |
| `ANDROID_ACTIVITY` | `com.epicgames.unreal.GameActivity` | UE 표준 진입 액티비티 — 변경할 일 거의 없음. |

---

## 2. iOS

### 2.1 빌드

```bash
make ios                 # cook + build + stage + package -> Saved/StagedBuilds/IOS
make ios-rebuild         # Binaries/Intermediate 삭제 후 다시 빌드
make ios-archive         # 동일 + IPA archive -> Saved/Archive/IOS
```

`make ios` 가 처음 실행되면 `Intermediate/ProjectFilesIOS/...xcodeproj`
와 `<프로젝트>.xcworkspace` 가 생성됩니다.

### 2.2 코드 서명

UAT 는 자동 서명이 꺼진 빈 프로파일로 빌드를 시도하므로, **TestFlight /
App Store 업로드 또는 실기기 인스톨** 단계에서는 **반드시 Xcode 의 Signing
& Capabilities** 를 사용해야 합니다.

```bash
make xcode-ios           # 생성된 xcworkspace 또는 IOS xcodeproj 자동 오픈
```

Xcode 에서:

1. Target = `<프로젝트명>` 선택 → **Signing & Capabilities**
2. **Team** 을 본인 / 회사의 Apple Developer 팀으로 지정
3. **Bundle Identifier** 가 `Config/DefaultEngine.ini` 의
   `[/Script/IOSRuntimeSettings.IOSRuntimeSettings] BundleIdentifier`
   값과 동일한지 확인. (다르게 두면 SDK 가 화이트리스트 검증을 못 함.)
4. `Product → Archive` 로 .ipa 생성 → Organizer 에서 TestFlight / Ad-hoc
   배포

### 2.3 실기기 설치 (개발용)

UE 의 `Launch on Device (iOS)` 를 쓰는 게 가장 빠릅니다 (Editor 메뉴).
또는 위에서 만든 `.ipa` 를 Xcode Devices and Simulators 에 드래그해
설치하거나, `cfgutil` / `ideviceinstaller` 등 외부 툴 사용.

> Makefile 에 `install-ios` 가 없는 이유: iOS 는 정식 ad-hoc / dev
> provisioning 없이 adb 처럼 무인증 설치를 할 수 없습니다. 인증 흐름은
> Xcode 에 위임하는 게 표준입니다.

### 2.4 자주 만나는 이슈

| 증상 | 원인 / 해결 |
|------|-------------|
| `[CROSSx SDK] ProjectId is empty in DefaultGame.ini. Aborting build.` | Project Settings 에서 ProjectId 입력 (위 §0 참조) |
| `Could not find IOS_xx Provision...` | UAT 자체 서명 시도. 위 §2.2 흐름으로 Xcode 서명 사용 |
| 앱이 OAuth / Webkit 후 복귀하지 않음 | `crossx-{ProjectId}` URL Scheme 미등록 — ProjectId 가 빈 채로 빌드된 경우. 다시 빌드 |

---

## 3. Android

### 3.1 사전 준비

UE 5.7 표준 Android 툴체인을 사용합니다 (`SetupAndroid.sh`/`.bat`). 이미
설치돼 있다면 추가 작업 없음. 아직이면 Engine 의 가이드 1회 실행:

```bash
"$UE_ROOT/Engine/Extras/Android/SetupAndroid.sh"
```

`Project Settings → Platforms → Android → Distribution Signing` 에 본인의
keystore 를 등록하세요. 외부 dApp 팀은 자체 키스토어를 사용해야 합니다
(SDK 팀의 키 사용 금지).

### 3.2 빌드 / 설치 / 실행

```bash
adb devices                          # 실기기 연결 확인

make android                         # arm64 .apk 패키징
make install-android                 # = adb install -r <apk>
make run-android                     # = adb shell am start ...

# 또는 한 번에
make deploy-android                  # android + install-android
```

빌드 산출물:
`Binaries/Android/CrossySdkUnrealSamp-arm64.apk`
`Saved/StagedBuilds/Android_ASTC/<...>` (스테이징 트리)

### 3.3 패키지 이름 / 딥링크 검증

설치 후 `adb shell pm dump <package>` 로 intent-filter 가 제대로
주입됐는지 확인:

```bash
adb shell pm dump com.nexus.crossx.sdk.unrealsample.android | \
  grep -E "scheme=(crossx|webkit)-"
```

`crossx-<ProjectId>` 와 `webkit-<ProjectId>` 두 줄이 보여야 정상입니다.
없으면 ProjectId 가 비어 있던 채로 빌드된 것 — 다시 입력 후 빌드.

### 3.4 자주 만나는 이슈

| 증상 | 원인 / 해결 |
|------|-------------|
| `INSTALL_FAILED_INSUFFICIENT_STORAGE` | 기기 저장공간 부족 |
| `INSTALL_FAILED_UPDATE_INCOMPATIBLE` | 동일 패키지명, 다른 keystore 의 빌드가 이미 설치됨. 기존 앱 제거 후 재설치 |
| `am start ... Activity class {...} does not exist` | `ANDROID_PACKAGE` 가 `Config/DefaultEngine.ini` 의 `PackageName` 과 불일치. Makefile 변수 또는 ini 수정 |
| Gradle 다운로드가 멈춤 | 사내망 프록시 — `~/.gradle/gradle.properties` 에 프록시 설정 |

---

## 4. Win64 / Mac (선택)

데스크톱은 SDK 동작 검증이 주 목적이며, 실제 배포는 외부 dApp 팀의 빌드
파이프라인에서 진행하는 것을 권장합니다.

```bash
make win64               # Windows 64-bit -> Saved/StagedBuilds/Windows
make win64-archive       # 동일 + Saved/Archive/Windows 에 복사
make mac                 # macOS -> Saved/StagedBuilds/Mac
make mac-archive         # 동일 + Saved/Archive/Mac
```

> Win64 / Mac 데스크톱 빌드의 딥링크 등록은 Unreal 의 표준 동작 범위
> 밖입니다. SDK 의 OAuth / Webkit 플로우는 모바일/Editor PIE 환경을
> 1순위로 검증합니다.

---

## 5. 정리 & 재시작

```bash
make package-clean       # Saved/StagedBuilds + Saved/Archive 삭제
make sdk-clean           # 설치된 SDK 플러그인 폴더 제거 (다음 sdk-install 이 재다운로드)
```

DerivedDataCache 는 의도적으로 보존됩니다 (재빌드 가속). 굳이 비워야 하면
`rm -rf DerivedDataCache/` 직접 실행.

---

## 6. CI 연계 (참고)

향후 GitHub Actions 등으로 자동화할 때 참고할 변수 매트릭스:

| 환경 변수 | 권장 값 |
|-----------|---------|
| `UE_ROOT` | self-hosted 러너의 Unreal 5.7 설치 경로 |
| `CONFIGURATION` | PR 검증: `Development`. 릴리스: `Shipping` |
| `GITHUB_TOKEN` | 익명 호출이 rate-limit 에 걸리는 공유 러너에서만 필요 (선택) |
| Android signing | keystore 를 base64 secret 으로 보관 후 step 에서 디코드 |
| iOS signing | Xcode + fastlane match 권장 (UAT 단독 서명은 비권장) |

CI 스크립트 예시는 추후 `scripts/ci/` 에 추가됩니다.
