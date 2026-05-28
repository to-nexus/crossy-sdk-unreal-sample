# CROSSx SDK — Unreal Engine Integration Guide

This guide helps external dApp developers integrate the **CROSSx Embedded Wallet SDK** into their own Unreal Engine 5 project. Use this sample project (`CrossySdkUnrealSamp`) as a reference.

---

## Table of Contents

1. [Prerequisites](#1-prerequisites)
2. [SDK Installation](#2-sdk-installation)
3. [Project ID Setup](#3-project-id-setup)
4. [Sample Project Structure](#4-sample-project-structure)
5. [Integrating into Your Own Project](#5-integrating-into-your-own-project)
6. [Core Code Patterns](#6-core-code-patterns)
7. [Platform Builds](#7-platform-builds)
8. [SDK Version Upgrade](#8-sdk-version-upgrade)
9. [Troubleshooting](#9-troubleshooting)

---

## 1. Prerequisites

| Item | Version / Notes |
|------|----------------|
| Unreal Engine | **5.7** |
| `make` | Build automation (pre-installed on macOS; use WSL or PowerShell on Windows) |
| `jq` | JSON processing (`brew install jq` / `winget install jqlang.jq`) |
| `curl` | Plugin download (pre-installed) |
| Xcode 15+ | Required for iOS builds |
| Android SDK + NDK | Required for Android builds |

---

## 2. SDK Installation

Unreal Engine has no official package manager equivalent to Unity UPM. This sample replicates the same experience using a **`crossx-plugins.json` + install script** combination.

| Unity | Unreal (this sample) |
|-------|---------------------|
| Edit `Packages/manifest.json` | Edit `crossx-plugins.json` |
| `npm install` (automatic) | `make sdk-install` |

### 2.1 Clone the Repository

```bash
git clone <your-forked-repo>
cd CrossySdkUnrealSamp
```

### 2.2 Install SDK Plugins

```bash
make sdk-install
```

This single command performs the following automatically:

1. Reads the version from `crossx-plugins.json`
2. Downloads `CROSSxSdkUnrealPlugin-{version}.zip` from GitHub Releases
3. Verifies SHA-256 checksum
4. Extracts to `Plugins/CROSSxSdkUnrealPlugin/`
5. Reads `CrossxDependencies` from `.uplugin` and installs `CROSSxWebkitSdkUnrealPlugin` automatically
6. Records the installation in `crossx-plugins.lock.json`

On subsequent runs, the download is skipped if the SHA-256 matches.

> `Plugins/CROSSx*` folders are excluded via `.gitignore`. `make sdk-install` recreates the same version each time.

**Windows PowerShell:**
```powershell
pwsh ./scripts/install-plugins.ps1
```

### 2.3 Verify Installation

```bash
make sdk-verify
```
A result of `[ok] CROSSxSdkUnrealPlugin: 0.0.0-beta.20` means the installation was successful.

---

## 3. Project ID Setup

### 3.1 Set via the Editor

```
Unreal Editor → Project Settings → Plugins → CROSSx SDK → Prod Project ID
```

Enter your issued Project ID.

### 3.2 Set via ini File (CI / Automation)

`Config/DefaultGame.ini`:

```ini
[/Script/CROSSxSdkUnrealPlugin.CROSSxSdkSettings]
ProdProjectId=YOUR_PROD_PROJECT_ID
Environment=Prod
```

> If the Project ID is empty, iOS/Android builds will immediately fail with a `BuildException`.

---

## 4. Sample Project Structure

```
CrossySdkUnrealSamp/
├── Source/CrossySdkUnrealSamp/
│   ├── Dapp/
│   │   ├── DappActor.h / .cpp        # SDK bootstrap (equivalent to Unity's Dapp.cs)
│   │   └── DappGameMode.h / .cpp     # Auto-spawns actor and widget on startup
│   └── UI/
│       ├── DappTestPanelBase.h / .cpp # UMG widget base (C++), subclassed in Blueprint
│       └── DappNotificationSubsystem.h/.cpp  # Toast notification system
│
├── Content/
│   ├── Maps/StartupMap.umap          # Startup map
│   ├── UI/WBP_DappTestPanel.uasset   # Test panel widget Blueprint
│   └── Localization/DT_DappStrings.uasset  # Localization DataTable
│
├── Plugins/
│   ├── CROSSxSdkUnrealPlugin/        # (auto) Main SDK plugin
│   └── CROSSxWebkitSdkUnrealPlugin/  # (auto) Webkit bridge (auto-dependency)
│
├── Config/
│   ├── DefaultGame.ini               # SDK Project ID, Game Mode settings
│   └── DefaultEngine.ini             # Map, iOS, Android base settings
│
├── crossx-plugins.json               # SDK plugin version declaration (edit this)
├── crossx-plugins.lock.json          # Installed version record (managed by script)
├── CrossySdkUnrealSamp.uproject      # Project metadata + plugin activation
├── Makefile                          # Build automation
├── HOW_TO_USE_DEPLOYED_SDK.md        # Detailed SDK install and version management guide
└── Documentation/
    ├── SAMPLE_WIDGET_GUIDE.md        # UMG widget and localization setup guide
    └── BUILD_GUIDE.md                # iOS/Android/Win64/Mac build guide
```

### Key Files

| File | Role |
|------|------|
| `crossx-plugins.json` | SDK version declaration. Change version, then run `make sdk-install`. |
| `DappActor.cpp` | SDK initialization and login event handling. Use as implementation reference. |
| `DappTestPanelBase.cpp` | Demonstrates all SDK features (login, signing, transactions). Use as widget reference. |
| `Config/DefaultGame.ini` | Project ID and game mode settings. |

---

## 5. Integrating into Your Own Project

### 5.1 Start from This Sample (Recommended)

```bash
# Copy or fork the sample to start
cp -r CrossySdkUnrealSamp MyProject
cd MyProject
make sdk-install
```

### 5.2 Add to an Existing Project

**Enable the plugin in your `.uproject` file:**

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

**Create `crossx-plugins.json`:**

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

**Copy the `scripts/` folder:**
- `scripts/install-plugins.sh`
- `scripts/install-plugins.ps1`

**Copy the `sdk-install` target from the `Makefile`** (or run the scripts directly).

**Add the SDK dependency to your module build file (`MyGame.Build.cs`):**

```csharp
PublicDependencyModuleNames.AddRange(new string[] {
    "Core", "CoreUObject", "Engine", "UMG", "Slate", "SlateCore",
    "CROSSxSdkUnrealPlugin"  // Add this
});
```

### 5.3 Asset Setup (First-Time Only)

For detailed instructions, see `Documentation/SAMPLE_WIDGET_GUIDE.md`:

1. Import `Localization/DT_DappStrings.csv` into `Content/Localization/` (Row Type: `FDappStringRow`)
2. Create `WBP_DappTestPanel` Blueprint → Reparent to `UDappTestPanelBase`
3. Create StartupMap → register it in `Project Settings → Maps & Modes`

---

## 6. Core Code Patterns

### 6.1 Get the SDK Subsystem

```cpp
// Provided as a GameInstance Subsystem
UCROSSxSdkSubsystem* Sdk = GetGameInstance()->GetSubsystem<UCROSSxSdkSubsystem>();
```

### 6.2 SDK Initialization (C++)

```cpp
// See DappActor.cpp for reference
FOnSdkInitialized InitDelegate;
InitDelegate.BindDynamic(this, &AMyActor::OnSdkInitialized);
Sdk->InitializeSdkAsync(InitDelegate);

void AMyActor::OnSdkInitialized(const FCROSSxAuthResult& Result)
{
    if (Result.bSuccess)
    {
        UE_LOG(LogTemp, Log, TEXT("Session restored: %s"), *Result.Address);
    }
}
```

### 6.3 Social Login (C++)

```cpp
FOnSignInComplete SignInDelegate;
SignInDelegate.BindDynamic(this, &AMyActor::OnSignInComplete);
Sdk->SignInAsync(SignInDelegate);

void AMyActor::OnSignInComplete(const FCROSSxAuthResult& Result)
{
    if (Result.bSuccess)
    {
        UE_LOG(LogTemp, Log, TEXT("Logged in: %s"), *Result.UserId);
    }
}
```

### 6.4 Sign Message (C++)

```cpp
FOnSignMessageComplete Delegate;
Delegate.BindDynamic(this, &AMyActor::OnSignMessageComplete);
Sdk->SignMessageAsync(TEXT("Message to sign"), TEXT("eip155:1"), Delegate);
```

### 6.5 Send Transaction (C++)

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

### 6.6 Using from Blueprint

All functions on `UCROSSxSdkSubsystem` are exposed to Blueprint:

1. Use a `Get Game Instance Subsystem` node in your Blueprint
2. Set Class to `CROSSxSdkSubsystem`
3. Connect nodes such as `Initialize Sdk Async`, `Sign In Async`, `Sign Message Async`

---

## 7. Platform Builds

```bash
# iOS
make ios

# Android (generate APK)
make android

# Android (install directly to device)
make install-android

# Windows
make win64

# macOS
make mac
```

The default UE path is `/Users/Shared/Epic Games/UE_5.7`. To override:
```bash
make ios UE_ROOT="/Applications/Unreal Engine 5.7"
```

**Shipping build:**
```bash
make android CONFIGURATION=Shipping
```

For detailed build options and platform-specific notes, see `Documentation/BUILD_GUIDE.md`.

---

## 8. SDK Version Upgrade

### 8.1 One-Line Update

```bash
make sdk-update name=CROSSxSdkUnrealPlugin version=0.0.0-beta.21
```

### 8.2 Manual Update

Edit the version in `crossx-plugins.json`:

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

The `CROSSxWebkitSdkUnrealPlugin` version is determined automatically from the main SDK's `CrossxDependencies`.

### 8.3 Check Available Versions

```bash
gh release list --repo to-nexus/crossy-sdk-unreal-sample --limit 20
```

Or browse in your browser: `https://github.com/to-nexus/crossy-sdk-unreal-sample/releases`

---

## 9. Troubleshooting

| Symptom | Cause | Fix |
|---------|-------|-----|
| `make sdk-install` fails — API rate limit | GitHub anonymous API quota exceeded (shared CI runners, etc.) | Add `GITHUB_TOKEN` to `.env` (see `.env.example`) |
| `BuildException: CROSSx Project ID is empty` | Project ID not set | Editor → Project Settings → Plugins → CROSSx SDK → enter Prod Project ID |
| Plugin fails to load | `Plugins/CROSSx*` folder missing | Run `make sdk-install` |
| `[warn] version mismatch` (`make sdk-verify`) | Lock and manifest out of sync | Run `make sdk-install` to re-sync |
| iOS build — Team ID error | Signing team not configured | Set `CodeSigningTeam` in `Config/IOS/IOSEngine.ini` |
| Android `Expand-Archive path too long` (Windows) | Path exceeds 260 characters | Move the project to a shorter path (e.g., `C:\dev\`) |

For more detail, refer to the dedicated guides:
- **SDK install and version management**: `HOW_TO_USE_DEPLOYED_SDK.md`
- **UMG widget and localization**: `Documentation/SAMPLE_WIDGET_GUIDE.md`
- **Platform builds**: `Documentation/BUILD_GUIDE.md`
