# CrossySdkUnrealSamp — UI & Localization Setup Guide

This document describes how to finish the **UMG-side** work for the sample
project. Phase 3's C++ layer (`ADappActor`, `UDappTestPanelBase`,
`UDappLocalizationSubsystem`, `UDappNotificationSubsystem`) is already in place
and compiles against the SDK; what remains is editor-side asset creation:

1. Import the localization DataTable
2. Create the `WBP_DappTestPanel` blueprint that reparents
   `UDappTestPanelBase` and names its sub-widgets to match the C++ contract
3. Place the `ADappActor` and the panel widget in the startup map
4. (Optional) Re-skin the built-in toast host by subclassing
   `UDappNotificationHostWidget` — the C++ default is auto-attached and
   surfaces every `UDappNotificationSubsystem::OnNotification` broadcast
   as an on-screen toast out of the box.

External dApp teams that duplicate this project only need to:

- Rename the assets
- Re-style the WBP_DappTestPanel layout in Designer
- Optionally remove unused buttons (all bindings are `BindWidgetOptional`)

> **Bootstrapping is automated.** `ADappGameMode` (configured as the
> `GlobalDefaultGameMode` in `DefaultEngine.ini`) handles spawning the
> `ADappActor` and adding the test-panel widget to the viewport at level
> start. You only need to author the WBP at the conventional path; placing
> the actor manually in the level is **not required**.

---

## 1. DataTable import — `DT_DappStrings`

Unreal cannot treat `.csv` as a runtime asset directly; it must be imported as
a `UDataTable` backed by `FDappStringRow`. One-time setup:

1. In Content Browser, create folder `Content/Localization/`.
2. Drag `Localization/DT_DappStrings.csv` (repository root) into that folder.
3. When prompted, pick **Row Type = `FDappStringRow`** and uncheck _"Is Text
   Localization Table"_ (we are not using the Localization Dashboard).
4. Name the asset exactly **`DT_DappStrings`**, save.

The path **`/Game/Localization/DT_DappStrings.DT_DappStrings`** is hard-coded
as a fallback in `UDappLocalizationSubsystem::ResolveDefaultTableIfNeeded()`.
If you must store it elsewhere, call
`UDappLocalizationSubsystem::SetStringTable()` during your `UGameInstance`
initialization, or override the default path by editing
`DappLocalizationSubsystem.cpp`.

**Adding a new language** (e.g. Japanese):

1. Extend `FDappStringRow` with `UPROPERTY FText JA`.
2. Extend `EDappLang` with `JA` and update `UDappLocalizationSubsystem::ResolveText`.
3. Add a `JA` column to `DT_DappStrings.csv` and re-import.

---

## 2. `WBP_DappTestPanel` — widget contract

### Reparenting

1. Create a new **User Widget Blueprint** named `WBP_DappTestPanel`.
2. In _File → Reparent Blueprint_, change the parent to
   `DappTestPanelBase` (C++). Save.
3. (Recommended) Lay out your widget hierarchy using a `VerticalBox` inside a
   `ScrollBox` so you can easily add/remove sections later.

### BindWidget naming contract

All bindings use **`BindWidgetOptional`** — widgets you do not include simply
disable the matching click handler at runtime. Names are case-sensitive.

#### Login / Auth section (visible when logged-out)

Parent container: `Panel_Login` (any `UPanelWidget` — usually a
`VerticalBox`).

| Widget name         | Type                | Purpose                                          |
| ------------------- | ------------------- | ------------------------------------------------ |
| `Btn_Login`         | `Button`            | Provider selection modal (SignInWithUIAsync)     |
| `Btn_LoginGoogle`   | `Button`            | Direct Google login                              |
| `Btn_LoginApple`    | `Button`            | Direct Apple login                               |
| `Txt_Status`        | `TextBlock`         | Optional status line, localized                  |

#### Wallet / Address section (visible when logged-in)

Parent container: `Panel_Wallet`.

| Widget name          | Type               | Purpose                                          |
| -------------------- | ------------------ | ------------------------------------------------ |
| `Btn_CreateWallet`   | `Button`           | `SetupWalletWithUIAsync`                         |
| `Btn_GetAddress`     | `Button`           | `GetAddressWithUIAsync(0)`                       |
| `Btn_GetAllAddresses`| `Button`           | `GetAddressesAsync`                              |
| `Btn_SelectWallet`   | `Button`           | `SelectWalletWithUIAsync`                        |
| `Txt_UserId`         | `TextBlock`        | Current `FCROSSxUserInfo.Id`                     |
| `Txt_WalletAddress`  | `TextBlock`        | Selected wallet address                          |
| `Inp_From`           | `EditableTextBox`  | Override source address for ad-hoc tests         |

#### Feature section (visible when logged-in)

Parent container: `Panel_Features`.

| Widget name                  | Type                          | Purpose                               |
| ---------------------------- | ----------------------------- | ------------------------------------- |
| `Inp_ChainId`                | `EditableTextBox`             | CAIP-2 chain id (default `eip155:612044`) |
| `Inp_To`                     | `EditableTextBox`             | Native tx recipient                   |
| `Inp_Value`                  | `EditableTextBox`             | Native tx value, hex (`0x0`)          |
| `Inp_Data`                   | `MultiLineEditableTextBox`    | Optional calldata                     |
| `Btn_GetNativeBalance`       | `Button`                      | `GetBalanceAsync`                     |
| `Btn_SignTx`                 | `Button`                      | `SignTransactionWithUIAsync`          |
| `Btn_SendTx`                 | `Button`                      | `SendTransactionWithUIAsync`          |
| `Inp_TokenContract`          | `EditableTextBox`             | ERC-20 contract                       |
| `Inp_TokenTo`                | `EditableTextBox`             | Token transfer recipient              |
| `Inp_TokenAmount`            | `EditableTextBox`             | Decimal amount (e.g. `1.5`)           |
| `Inp_TokenDecimals`          | `EditableTextBox`             | Token decimals (default `18`)         |
| `Btn_GetTokenBalance`        | `Button`                      | `eth_call → balanceOf`                |
| `Btn_SendToken`              | `Button`                      | `SendTransactionWithUIAsync + transfer()`|
| `Inp_SignMessage`            | `EditableTextBox`             | personal_sign text                    |
| `Btn_SignPersonalMessage`    | `Button`                      | `SignMessageWithUIAsync`              |
| `Inp_SignTypedData`          | `MultiLineEditableTextBox`    | Raw EIP-712 JSON                      |
| `Btn_SignTypedData`          | `Button`                      | `SignTypedDataWithUIAsync`            |
| `Btn_CheckTokenExpiry`       | `Button`                      | `IsTokenExpired`                      |
| `Btn_RefreshToken`           | `Button`                      | `RefreshTokenAsync`                   |
| `Btn_GetUserInfo`            | `Button`                      | `GetUserInfoAsync`                    |
| `Btn_SignOut`                | `Button`                      | `SignOutAsync`                        |
| `Inp_WebkitUrl`                | `EditableTextBox`             | Full Webkit URL                         |
| `Btn_UseWebkit`                | `Button`                      | `CROSSxSDK OpenWebView` with Ramp URL  |
| `Btn_UseCrossPay`              | `Button`                      | `CROSSxSDK CreateCrossPayCheckoutUrl + OpenWebView` |
| `Btn_ToggleLanguage`         | `Button`                      | Flip KO ↔ EN                          |
| `Txt_Language`               | `TextBlock`                   | Current lang tag                      |

#### Editor-only (auto-hidden in packaged builds)

Parent container: `Panel_EditorTools`.

| Widget name                     | Type     | Purpose                            |
| ------------------------------- | -------- | ---------------------------------- |
| `Btn_EditorSimulateDeepLink`    | `Button` | Placeholder for OAuth deep-link simulation |

### Visibility rule

`UDappTestPanelBase::ApplyLoginState()` toggles the three main panels based
on session state:

| State         | `Panel_Login` | `Panel_Wallet` | `Panel_Features` |
| ------------- | ------------- | -------------- | ---------------- |
| Logged-out    | Visible       | Collapsed      | Collapsed        |
| Logged-in     | Collapsed     | Visible        | Visible          |

If your layout merges these sections, simply omit the panel widgets and
handle visibility yourself inside `OnLoginStateChanged` (BlueprintImplementable
event).

### Default text values

`UDappTestPanelBase` exposes these properties as `EditAnywhere` so you can set
them on the WBP's Class Default Object, without modifying C++:

| Property               | Default                                     |
| ---------------------- | ------------------------------------------- |
| `DefaultChainId`       | `eip155:612044` (CROSS testnet)             |
| `DefaultTxValueWei`    | `0x0`                                       |
| `DefaultSignMessage`   | `Hello from CROSSx Unreal Sample`           |
| `DefaultTokenDecimals` | `18`                                        |
| `DefaultTypedDataJson` | empty                                       |

Placeholders in `NativeConstruct()` will auto-fill the respective input fields
if the WBP author hasn't pre-filled them in Designer.

---

## 3. Startup map (one minute)

`ADappGameMode` is wired up as the project's `GlobalDefaultGameMode`
(`Config/DefaultEngine.ini`). It does the following at level `BeginPlay`:

1. Looks up an existing `ADappActor` in the level; spawns one if absent.
2. Loads the soft-referenced `WBP_DappTestPanel` (default path
   `/Game/UI/WBP_DappTestPanel.WBP_DappTestPanel_C`), creates the widget
   for the first PlayerController, and adds it to the viewport.
3. Switches input mode to **Game and UI** with the cursor visible.

So all you need is **a map**:

1. `File → New Level → Empty Level`. Save as
   `Content/Maps/StartupMap.umap`.
2. **Project Settings → Maps & Modes**
   - **Editor Startup Map** = `StartupMap`
   - **Game Default Map** = `StartupMap`
3. Hit **Play**. The login button appears; you should see
   `LogDappGameMode: Spawned ADappActor (...)` and
   `LogDappGameMode: Test panel widget '...' added to viewport.` in the
   Output Log.

### Customizing the auto-bootstrap

`ADappGameMode` exposes everything it does as `EditDefaultsOnly` so you can
tweak it from `Config/DefaultGame.ini` (already pre-filled with the
conventional path) **without** subclassing in Blueprint:

```ini
[/Script/CrossySdkUnrealSamp.DappGameMode]
TestPanelWidgetClass=/Game/UI/WBP_DappTestPanel.WBP_DappTestPanel_C
bAutoSpawnDappActor=True
bAutoCreateTestPanel=True
bAutoCreateNotificationHost=True
bAutoEnableUIInputMode=True
```

If you'd rather subclass it, create `BP_DappGameMode` (parent =
`DappGameMode`), set `TestPanelWidgetClass` in the BP defaults, then point
`Project Settings → Maps & Modes → Default GameMode` at it. Manual placement
of `ADappActor` in the level is also supported — the GameMode skips its
spawn step if it finds one already there.

---

## 4. Notification host widget

`UDappNotificationSubsystem` is broadcast-only. The sample ships a small
C++ renderer — `UDappNotificationHostWidget` — that `ADappGameMode`
instantiates and adds to the viewport (ZOrder 100) at level start, so toasts
work without any editor authoring. The host itself is `SelfHitTestInvisible`
and never intercepts clicks destined for the test panel.

### Default behavior (no setup required)

- Toasts stack from the bottom-center of the screen with a fixed 16/40px
  inset that also clears mobile safe-area gestures.
- Each toast lives for `DefaultDurationSeconds` (6 s by default) unless the
  broadcast carries an explicit `DurationSeconds`.
- Severity-tinted `UBorder` background (info / success / warning / error).
- Auto-wrapped multi-line `UTextBlock` — important for the CROSS Pay result
  message that includes line breaks.
- Hard cap (`MaxVisibleToasts`, default 5); the oldest entry is evicted
  when the cap is reached.

### Re-skinning via Blueprint

1. Create `WBP_DappNotificationHost`, reparent to
   `UDappNotificationHostWidget`.
2. Author whatever layout you like — anchored toasts, fade animations,
   custom borders, etc.
3. Add a `UVerticalBox` named **`Box_Toasts`** somewhere in the tree (this
   is a `BindWidgetOptional`). When the C++ base sees a non-null
   `Box_Toasts`, it skips its auto-built layout and appends toast cards
   into your container instead.
4. Point `ADappGameMode → NotificationHostWidgetClass` at your BP (either
   in the BP_DappGameMode defaults or via `DefaultGame.ini`).

If you want to disable the host entirely (e.g. because you wire toasts
through your own HUD), set `bAutoCreateNotificationHost = False` on the
GameMode. The subsystem keeps logging to `LogDappNotification` in that case,
so headless / automated runs still see every message.

---

## 5. Checklist (post-setup smoke test)

- [ ] Editor opens with no missing asset warnings
- [ ] `WBP_DappTestPanel` compiles with **no** "meta=BindWidget could not
      find" warnings (they become errors when missing `Optional`)
- [ ] Editor PIE: Output Log shows `LogDappGameMode: Spawned ADappActor`
      and `Test panel widget '...' added to viewport.`
- [ ] Editor PIE → `Btn_Login` click opens the SDK's provider modal
- [ ] After successful login, `Panel_Wallet` becomes visible and
      `Txt_WalletAddress` shows an address
- [ ] `Btn_ToggleLanguage` flips button labels (only if you bind them to the
      localization subsystem in Designer) and SDK modals respect the new locale
- [ ] `Btn_SignOut` restores `Panel_Login` visibility and clears
      `Txt_WalletAddress`
- [ ] CSV → DataTable edits are reflected after a re-import without
      recompiling C++

Anything broken? Check `Saved/Logs/*.log` — `LogDappPanel`, `LogDappActor`,
`LogCROSSxSdk`, and `LogCROSSxWebkit` are the four categories to grep.
