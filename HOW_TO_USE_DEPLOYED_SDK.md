# How to use the deployed CROSSx Unreal SDK

> 외부 dApp 개발사가 이 샘플 레포를 템플릿으로 시작해서, **배포된 Unreal SDK
> 플러그인** 을 설치·갱신·검증하기 위한 최소 절차 문서입니다. Unity 쪽
> [`HOW_TO_SWITCH_TO_DEPLOYED_SDK.md`](../crossy-sdk-unity-sample/HOW_TO_SWITCH_TO_DEPLOYED_SDK.md)
> 와 동일한 역할을 합니다.

---

## 0. 개요

Unreal 에는 Unity UPM 에 해당하는 공식 패키지 매니저가 없어, 본 샘플은
**manifest + install 스크립트** 조합으로 UPM 과 같은 경험을 재현합니다.

| Unity | Unreal (이 레포) |
|-------|-----------------|
| `Packages/manifest.json` | `crossx-plugins.json` (편집 대상) |
| `Packages/packages-lock.json` | `crossx-plugins.lock.json` (스크립트가 관리) |
| `npm install` (UPM 자동) | `make sdk-install` |
| npm registry (Public) | `to-nexus/crossy-sdk-unreal-sample` Releases (Public, **토큰 불필요**) |

`Plugins/CROSSxSdkUnrealPlugin/` 와 `Plugins/CROSSxRampSdkUnrealPlugin/` 는
`.gitignore` 로 제외되며, `make sdk-install` 가 매번 같은 버전으로 재생성합니다.

---

## 1. 사전 준비

### 1.1 필수 도구

| 도구 | 용도 | macOS 설치 | Windows 설치 |
|------|------|-----------|-------------|
| `jq` | manifest/lock JSON 편집 | `brew install jq` | `winget install jqlang.jq` |
| `curl` | Releases 다운로드 | 기본 탑재 | 기본 탑재 |
| `unzip` | 플러그인 zip 해제 | 기본 탑재 | PowerShell `Expand-Archive` 사용 |
| `bash` | `scripts/install-plugins.sh` | 기본 탑재 | WSL 또는 `install-plugins.ps1` 사용 |

### 1.2 GitHub Personal Access Token *(선택사항)*

> **TL;DR — 일반적인 사용에는 토큰이 필요 없습니다.** 기본 레지스트리
> (`to-nexus/crossy-sdk-unreal-sample`) 는 **퍼블릭** 이며, install 스크립트는
> `GITHUB_TOKEN` 이 없으면 자동으로 익명 요청으로 동작합니다.
>
> 아래 **둘 중 하나** 에 해당할 때만 PAT 를 준비하세요:
>
> - **Rate limit 회피**: 익명 요청은 IP 당 60회/시. 공유 CI 러너처럼 다른
>   사람과 같은 IP 를 쓰는 환경에서 한도를 초과할 때.
> - **레지스트리를 프라이빗 레포로 교체**: `crossx-plugins.json` 의
>   `registry.repo` 를 비공개 저장소로 바꾼 경우.

토큰이 필요하다면 가장 간단한 GitHub PAT 면 충분합니다 (퍼블릭 레포 읽기에는
별도 scope 가 필요하지 않습니다):

1. GitHub → Settings → Developer settings → **Personal access tokens (classic 또는 fine-grained 중 어느 쪽이든 OK)**
2. 발급 후 **`.env` 파일에 입력** (권장):

   ```bash
   cp .env.example .env
   # .env 를 열어 주석을 해제하고 GITHUB_TOKEN=… 입력
   make sdk-install
   ```

   Makefile 이 `.env` / `.env.local` 를 자동 로드합니다.
   `.env` 는 `.gitignore` 로 차단됩니다.

3. 또는 셸에 export:

   ```bash
   export GITHUB_TOKEN=github_pat_xxxxxxxx     # macOS / Linux
   ```

   ```powershell
   $env:GITHUB_TOKEN = "github_pat_xxxxxxxx"   # Windows PowerShell
   ```

---

## 2. 설치

리포지토리를 클론한 직후, 한 줄이면 됩니다:

```bash
make sdk-install
```

(필요 시 `crossx-plugins.json` 의 버전 핀을 먼저 원하는 값으로 수정)

스크립트가 수행하는 일:

1. `crossx-plugins.json` → `registry.owner/repo` 와 각 플러그인 버전 읽기
2. GitHub Releases 에서 태그 `<PluginName>@v<version>` 조회
3. 자산 `<PluginName>-<version>.zip` 다운로드 + SHA-256 계산
4. `Plugins/<PluginName>/` 아래로 해제
5. 해제된 `.uplugin` 의 `VersionName` 이 요청 버전과 일치하는지 검증
6. `crossx-plugins.lock.json` 에 version/tag/asset/sha256/installed_at 기록

두 번째 실행부터는 lock 의 sha256 과 동일하면 다운로드를 생략합니다.

Windows PowerShell:

```powershell
pwsh ./scripts/install-plugins.ps1
```

---

## 3. 버전 업데이트

### 3.1 한 줄 업데이트

```bash
make sdk-update name=CROSSxSdkUnrealPlugin     version=0.3.0
make sdk-update name=CROSSxRampSdkUnrealPlugin version=0.3.0
```

### 3.2 수동 편집 후 동기화

```diff
 "plugins": {
-  "CROSSxSdkUnrealPlugin":     "0.0.0-beta.1",
+  "CROSSxSdkUnrealPlugin":     "0.3.0",
   "CROSSxRampSdkUnrealPlugin": "0.0.0-beta.1"
 }
```

```bash
make sdk-install
```

### 3.3 사용 가능한 버전 확인

```bash
gh release list --repo to-nexus/crossy-sdk-unreal-sample --limit 50
```

브라우저에서 확인 (퍼블릭 레포이므로 로그인 없이도 보입니다):
<https://github.com/to-nexus/crossy-sdk-unreal-sample/releases>

---

## 4. 검증 / 문제 해결

### 4.1 설치 상태 점검

```bash
make sdk-verify
```

- `[ok] <Plugin>: <version>`: manifest·lock·`.uplugin` 3자가 일치
- `[warn] ...`: lock 과 manifest 불일치 → `make sdk-install` 로 재동기화
- `[fail] ...`: 미설치 또는 버전 불일치 → 위와 동일

### 4.2 강제 재설치

```bash
# sha256 일치해도 다시 다운로드
./scripts/install-plugins.sh --force

# 아예 지운 뒤 재설치
make sdk-clean && make sdk-install
```

### 4.3 흔한 에러

| 증상 | 원인 | 해결 |
|------|------|------|
| `API rate limit exceeded` | 익명 호출이 60회/시 초과 (공유 CI 러너 등) | §1.2 의 선택사항 PAT 를 `.env` 에 등록 |
| `tag '…@v…' not found` | 버전 오타 또는 아직 미배포 | `gh release list` 로 확인 후 manifest 수정 |
| `asset '…zip' not attached to tag` | 릴리스는 있으나 자산 부착 실패 | SDK 팀에 재업로드 요청 |
| `.uplugin VersionName mismatch` | CI 에서 `.uplugin` 버전 주입 누락 | SDK 팀에 리빌드 요청 |
| `Expand-Archive ... path too long` (Windows) | 경로 길이 260 초과 | 프로젝트 경로를 짧게 (C:\dev\…) |

### 4.4 로컬 개발 (SDK 소스 직접 수정 중일 때)

`crossx-plugins.json` 의 해당 엔트리를 문자열 → 객체로 바꿉니다:

```json
"CROSSxSdkUnrealPlugin": {
  "source": "local",
  "path":   "../CrossySdkUnreal/Plugins/CROSSxSdkUnrealPlugin"
}
```

이 경우 스크립트는 `[skip] ... local mode` 로 건너뛰며, 개발자는 해당 경로로
수동 심볼릭 링크/복사 관리를 합니다. 배포된 버전 테스트로 돌아갈 땐 값을
다시 버전 문자열로 되돌리세요.

---

## 5. 다음 단계

1. Unreal Editor 에서 `CrossySdkUnrealSamp.uproject` 를 엽니다.
2. Editor → Project Settings → CROSSx SDK → **Project ID** 를 입력합니다.
3. **에디터-측 에셋 셋업** 을 수행합니다 (최초 1회):
   - `Localization/DT_DappStrings.csv` 를 `Content/Localization/` 으로 임포트
     (Row Type = `FDappStringRow`)
   - `WBP_DappTestPanel` 을 생성하고 `DappTestPanelBase` 로 Reparent
   - 레벨에 `ADappActor` 배치 + `WBP_DappTestPanel` 을 PlayerController 에서
     viewport 에 추가
   - 상세 지침 및 BindWidget 네이밍 명세: **`Documentation/SAMPLE_WIDGET_GUIDE.md`**
4. 패널에서 Login → Create Wallet → Sign/Send Tx → Ramp 순서로 스모크 테스트.
5. Phase 4 의 `make ios` / `make android` / `make install` / `make run` 으로
   실기기 배포·실행을 검증합니다.

---

## 6. 레퍼런스

- **UI / 로컬라이제이션 셋업**: `Documentation/SAMPLE_WIDGET_GUIDE.md`
- SDK 공식 가이드: `../CrossySdkUnreal/docs/getting-started.ko.md`
- SDK 배포 규약: `../CrossySdkUnreal/DEPLOYMENT_GUIDE.md`
- Unity 버전 대응 문서: `../crossy-sdk-unity-sample/HOW_TO_SWITCH_TO_DEPLOYED_SDK.md`
