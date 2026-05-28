# CROSSx SDK — Unreal Engine 통합 가이드

외부 dApp 개발사가 **CROSSx Embedded Wallet SDK**를 자체 Unreal Engine 5 프로젝트에 탑재하기 위한 가이드입니다. 이 샘플 프로젝트(`CrossySdkUnrealSamp`)를 레퍼런스로 삼아 따라가면 됩니다.

---

## 목차

1. [사전 요구사항](#1-사전-요구사항)
2. [SDK 설치](#2-sdk-설치)
3. [Project ID 설정](#3-project-id-설정)
4. [샘플 프로젝트 구조](#4-샘플-프로젝트-구조)
5. [내 프로젝트에 적용하기](#5-내-프로젝트에-적용하기)
6. [핵심 코드 패턴](#6-핵심-코드-패턴)
7. [플랫폼별 빌드](#7-플랫폼별-빌드)
8. [SDK 버전 업그레이드](#8-sdk-버전-업그레이드)
9. [트러블슈팅](#9-트러블슈팅)

---

## 1. 사전 요구사항

| 항목 | 버전 / 비고 |
|------|------------|
| Unreal Engine | **5.7** |
| `make` | 빌드 자동화용 (macOS 기본 탑재, Windows는 WSL 또는 PowerShell 사용) |
| `jq` | JSON 처리 (`brew install jq` / `winget install jqlang.jq`) |
| `curl` | 플러그인 다운로드 (기본 탑재) |
| Xcode 15+ | iOS 빌드 시 필요 |
| Android SDK + NDK | Android 빌드 시 필요 |

---

## 2. SDK 설치

Unreal에는 Unity UPM 같은 공식 패키지 매니저가 없습니다. 이 샘플은 **`crossx-plugins.json` + install 스크립트** 조합으로 동일한 경험을 제공합니다.

| Unity | Unreal (이 샘플) |
|-------|----------------|
| `Packages/manifest.json` 편집 | `crossx-plugins.json` 편집 |
| `npm install` (자동) | `make sdk-install` |

### 2.1 저장소 클론

```bash
git clone <your-forked-repo>
cd CrossySdkUnrealSamp
```

### 2.2 SDK 플러그인 설치

```bash
make sdk-install
```

이 한 줄로 아래 작업이 자동 수행됩니다:

1. `crossx-plugins.json`에서 버전 읽기
2. GitHub Releases에서 `CROSSxSdkUnrealPlugin-{version}.zip` 다운로드
3. SHA-256 검증
4. `Plugins/CROSSxSdkUnrealPlugin/`에 압축 해제
5. `.uplugin`의 `CrossxDependencies`를 읽어 `CROSSxWebkitSdkUnrealPlugin` 자동 설치
6. `crossx-plugins.lock.json`에 설치 기록

두 번째 실행부터는 SHA-256이 일치하면 다운로드를 생략합니다.

> `Plugins/CROSSx*` 폴더는 `.gitignore`로 제외됩니다. `make sdk-install`이 매번 동일 버전을 재생성합니다.

**Windows PowerShell:**
```powershell
pwsh ./scripts/install-plugins.ps1
```

### 2.3 설치 확인

```bash
make sdk-verify
```
`[ok] CROSSxSdkUnrealPlugin: 0.0.0-beta.20` 출력 시 정상.

---

## 3. Project ID 설정

### 3.1 에디터에서 설정

```
Unreal Editor → Project Settings → Plugins → CROSSx SDK → Prod Project ID
```

발급받은 Project ID를 입력합니다.

### 3.2 ini 파일로 설정 (CI/자동화)

`Config/DefaultGame.ini`:

```ini
[/Script/CROSSxSdkUnrealPlugin.CROSSxSdkSettings]
ProdProjectId=YOUR_PROD_PROJECT_ID
Environment=Prod
```

> Project ID가 비어 있으면 iOS/Android 빌드 시 `BuildException`으로 즉시 실패합니다.

---

## 4. 샘플 프로젝트 구조

```
CrossySdkUnrealSamp/
├── Source/CrossySdkUnrealSamp/
│   ├── Dapp/
│   │   ├── DappActor.h / .cpp        # SDK 부트스트랩 (Unity의 Dapp.cs와 동일 역할)
│   │   └── DappGameMode.h / .cpp     # 시작 시 액터·위젯 자동 스폰
│   └── UI/
│       ├── DappTestPanelBase.h / .cpp # UMG 위젯 베이스 (C++), Blueprint로 상속
│       └── DappNotificationSubsystem.h/.cpp  # 토스트 알림
│
├── Content/
│   ├── Maps/StartupMap.umap          # 시작 맵
│   ├── UI/WBP_DappTestPanel.uasset   # 테스트 패널 위젯 Blueprint
│   └── Localization/DT_DappStrings.uasset  # 다국어 DataTable
│
├── Plugins/
│   ├── CROSSxSdkUnrealPlugin/        # (auto) 메인 SDK 플러그인
│   └── CROSSxWebkitSdkUnrealPlugin/  # (auto) Webkit 브리지 (자동 의존성)
│
├── Config/
│   ├── DefaultGame.ini               # SDK Project ID, Game Mode 설정
│   └── DefaultEngine.ini             # 맵·iOS·Android 기본 설정
│
├── crossx-plugins.json               # SDK 플러그인 버전 선언 (편집 대상)
├── crossx-plugins.lock.json          # 설치된 버전 기록 (스크립트 관리)
├── CrossySdkUnrealSamp.uproject      # 프로젝트 메타 + 플러그인 활성화
├── Makefile                          # 빌드 자동화
├── HOW_TO_USE_DEPLOYED_SDK.md        # SDK 설치·버전 관리 상세 가이드
└── Documentation/
    ├── SAMPLE_WIDGET_GUIDE.md        # UMG 위젯·로컬라이제이션 셋업 가이드
    └── BUILD_GUIDE.md                # iOS/Android/Win64/Mac 빌드 가이드
```

### 핵심 파일 설명

| 파일 | 역할 |
|------|------|
| `crossx-plugins.json` | SDK 버전 선언. 버전 변경 후 `make sdk-install` 실행. |
| `DappActor.cpp` | SDK 초기화, 로그인 이벤트 핸들링. 자체 앱 구현 시 참고. |
| `DappTestPanelBase.cpp` | 모든 SDK 기능(로그인·서명·전송 등) UI 시연. 자체 위젯 구현 시 참고. |
| `Config/DefaultGame.ini` | Project ID와 게임 모드 설정. |

---

## 5. 내 프로젝트에 적용하기

### 5.1 이 샘플을 템플릿으로 시작 (권장)

```bash
# 샘플을 복사하거나 fork하여 시작
cp -r CrossySdkUnrealSamp MyProject
cd MyProject
make sdk-install
```

### 5.2 기존 프로젝트에 추가

**`.uproject` 파일에 플러그인 활성화 추가:**

```json
{
  "Plugins": [
    {
      "Name": "CROSSxSdkUnrealPlugin",
      "Enabled": true
    }
  ]
}
```

**`crossx-plugins.json` 생성:**

```json
{
  "registry": {
    "type": "github-releases",
    "owner": "to-nexus",
    "repo": "crossy-sdk-unreal-sample"
  },
  "plugins": {
    "CROSSxSdkUnrealPlugin": "0.0.0-beta.20"
  }
}
```

**`scripts/` 폴더 복사:**
- `scripts/install-plugins.sh`
- `scripts/install-plugins.ps1`

**`Makefile`에서 `sdk-install` 타겟 복사** (또는 스크립트를 직접 실행).

**모듈 빌드 파일에 SDK 의존성 추가 (`MyGame.Build.cs`):**

```csharp
PublicDependencyModuleNames.AddRange(new string[] {
    "Core", "CoreUObject", "Engine", "UMG", "Slate", "SlateCore",
    "CROSSxSdkUnrealPlugin"  // 추가
});
```

### 5.3 에셋 셋업 (최초 1회)

상세 절차는 `Documentation/SAMPLE_WIDGET_GUIDE.md` 참고:

1. `Localization/DT_DappStrings.csv`를 `Content/Localization/`에 임포트 (Row Type: `FDappStringRow`)
2. `WBP_DappTestPanel` Blueprint 생성 → `UDappTestPanelBase`로 Reparent
3. StartupMap 생성 → `Project Settings → Maps & Modes`에 등록

---

## 6. 핵심 코드 패턴

### 6.1 SDK Subsystem 획득

```cpp
// GameInstance Subsystem으로 제공됨
UCROSSxSdkSubsystem* Sdk = GetGameInstance()->GetSubsystem<UCROSSxSdkSubsystem>();
```

### 6.2 SDK 초기화 (C++)

```cpp
// DappActor.cpp 참고
FOnSdkInitialized InitDelegate;
InitDelegate.BindDynamic(this, &AMyActor::OnSdkInitialized);
Sdk->InitializeSdkAsync(InitDelegate);

void AMyActor::OnSdkInitialized(const FCROSSxAuthResult& Result)
{
    if (Result.bSuccess)
    {
        UE_LOG(LogTemp, Log, TEXT("세션 복원됨: %s"), *Result.Address);
    }
}
```

### 6.3 소셜 로그인 (C++)

```cpp
FOnSignInComplete SignInDelegate;
SignInDelegate.BindDynamic(this, &AMyActor::OnSignInComplete);
Sdk->SignInAsync(SignInDelegate);

void AMyActor::OnSignInComplete(const FCROSSxAuthResult& Result)
{
    if (Result.bSuccess)
    {
        UE_LOG(LogTemp, Log, TEXT("로그인 성공: %s"), *Result.UserId);
    }
}
```

### 6.4 메시지 서명 (C++)

```cpp
FOnSignMessageComplete Delegate;
Delegate.BindDynamic(this, &AMyActor::OnSignMessageComplete);
Sdk->SignMessageAsync(TEXT("서명할 메시지"), TEXT("eip155:1"), Delegate);
```

### 6.5 트랜잭션 전송 (C++)

```cpp
FCROSSxUnsignedTx Tx;
Tx.ChainId = TEXT("eip155:1");
Tx.To = TEXT("0xRecipientAddress");
Tx.Value = TEXT("0x16345785D8A0000");  // 0.1 ETH
Tx.GasLimit = TEXT("0x5208");

FOnSendTransactionComplete Delegate;
Delegate.BindDynamic(this, &AMyActor::OnSendTxComplete);
Sdk->SendTransactionAsync(Tx, Delegate);
```

### 6.6 Blueprint에서 사용

`UCROSSxSdkSubsystem`의 모든 함수는 Blueprint에 노출됩니다:

1. Blueprint에서 `Get Game Instance Subsystem` 노드 사용
2. Class: `CROSSxSdkSubsystem` 선택
3. `Initialize Sdk Async`, `Sign In Async`, `Sign Message Async` 등 노드 연결

---

## 7. 플랫폼별 빌드

```bash
# iOS
make ios

# Android (APK 생성)
make android

# Android (기기에 직접 설치)
make install-android

# Windows
make win64

# macOS
make mac
```

기본 경로는 `/Users/Shared/Epic Games/UE_5.7`입니다. 다른 경로면:
```bash
make ios UE_ROOT="/Applications/Unreal Engine 5.7"
```

**Shipping 빌드:**
```bash
make android CONFIGURATION=Shipping
```

상세 빌드 옵션 및 플랫폼별 주의사항은 `Documentation/BUILD_GUIDE.md` 참고.

---

## 8. SDK 버전 업그레이드

### 8.1 한 줄 업데이트

```bash
make sdk-update name=CROSSxSdkUnrealPlugin version=0.0.0-beta.21
```

### 8.2 수동 업데이트

`crossx-plugins.json`에서 버전 변경:

```json
{
  "plugins": {
    "CROSSxSdkUnrealPlugin": "0.0.0-beta.21"
  }
}
```

```bash
make sdk-install
```

`CROSSxWebkitSdkUnrealPlugin` 버전은 메인 SDK의 `CrossxDependencies`에서 자동으로 결정됩니다.

### 8.3 사용 가능한 버전 확인

```bash
gh release list --repo to-nexus/crossy-sdk-unreal-sample --limit 20
```

또는 브라우저에서: `https://github.com/to-nexus/crossy-sdk-unreal-sample/releases`

---

## 9. 트러블슈팅

| 증상 | 원인 | 해결 |
|------|------|------|
| `make sdk-install` 실패 — API rate limit | GitHub 익명 API 한도 초과 | `.env`에 `GITHUB_TOKEN` 등록 (선택사항, `.env.example` 참고) |
| `BuildException: CROSSx Project ID is empty` | Project ID 미설정 | Editor → Project Settings → Plugins → CROSSx SDK → Prod Project ID 입력 |
| Plugin 로드 실패 | `Plugins/CROSSx*` 폴더 없음 | `make sdk-install` 실행 |
| `[warn] version mismatch` (`make sdk-verify`) | lock과 manifest 불일치 | `make sdk-install`로 재동기화 |
| iOS 빌드 — Team ID 오류 | 서명 팀 미설정 | `Config/IOS/IOSEngine.ini`의 `CodeSigningTeam` 입력 |
| Android `Expand-Archive path too long` (Windows) | 경로 260자 초과 | 프로젝트를 `C:\dev\` 등 짧은 경로로 이동 |

더 자세한 내용은 각 전문 가이드를 참고하세요:
- **SDK 설치·버전 관리**: `HOW_TO_USE_DEPLOYED_SDK.md`
- **UMG 위젯·로컬라이제이션**: `Documentation/SAMPLE_WIDGET_GUIDE.md`
- **플랫폼 빌드**: `Documentation/BUILD_GUIDE.md`
