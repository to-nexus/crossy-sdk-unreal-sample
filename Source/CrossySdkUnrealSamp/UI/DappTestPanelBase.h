#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Core/Types/CROSSxSdkConfig.h"
#include "Core/Types/CROSSxSdkTypes.h"
#include "Localization/DappLocalizationSubsystem.h"
#include "DappTestPanelBase.generated.h"

class UButton;
class UTextBlock;
class UEditableTextBox;
class UMultiLineEditableTextBox;
class UPanelWidget;
class UScrollBox;

class ADappActor;
class UDappLocalizationSubsystem;
class UDappNotificationSubsystem;
class UCROSSxSdkSubsystem;

/**
 * UDappTestPanelBase — C++ base class for the sample test panel widget.
 *
 * Designed to be paired with a UMG asset (e.g. WBP_DappTestPanel) that
 * reparents this class. All bound widgets use BindWidgetOptional so the WBP
 * can implement whichever buttons/sections it wants — unused handlers simply
 * no-op at runtime.
 *
 * The WBP → C++ contract is the widget *name*. See the SAMPLE_WIDGET_GUIDE.md
 * document for the canonical list of required widget names. External teams
 * duplicating WBP_DappTestPanel only need to keep the names intact to inherit
 * all SDK wiring for free.
 *
 * Mental model mirrors Unity sample's Dapp.cs — each SDK feature is a single
 * `OnClick*` handler that either fires a high-level `*WithUIAsync` SDK method
 * (which displays the SDK's built-in modal flow) or a lower-level *Async
 * method whose result we surface through the notification bus + status text.
 */
// Tracks which user-initiated action (if any) is waiting on the SDK's
// SetupWalletWithUIAsync flow to finish. The dev-panel demo
// (CROSSxSdkTestPanelWidget::EnsureFromAddressThen) uses a lambda chain for
// this; UFUNCTION-bound delegates can't capture lambdas, so we encode the
// pending action as a plain enum and retry it in HandleCreateWalletResult.
UENUM()
enum class EPendingWalletAction : uint8
{
	None,
	SignPersonalMessage,
	SignTypedData,
	SignTx,
	SendTx,
	GetTokenBalance,
	SendToken,
};

UCLASS(Abstract, Blueprintable, BlueprintType, meta = (DisplayName = "Dapp Test Panel"))
class CROSSYSDKUNREALSAMP_API UDappTestPanelBase : public UUserWidget
{
	GENERATED_BODY()

public:
	UDappTestPanelBase(const FObjectInitializer& ObjectInitializer);

	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	// Mobile ScrollBox + Button capture race workaround. When the user
	// presses on top of any interactive child of Scroll_Root we record the
	// touch start; NativeTick then nudges the ScrollBox by the per-frame
	// delta so dragging works even though the Button keeps mouse capture.
	// See implementation comment for the long story.
	virtual FReply NativeOnPreviewMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnTouchStarted(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent) override;
	virtual FReply NativeOnTouchMoved(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent) override;
	virtual FReply NativeOnTouchEnded(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent) override;

	// ═══════════════ Exposed defaults (editable on the WBP) ═══════════════

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dapp|Defaults")
	FString DefaultChainId = TEXT("eip155:612044");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dapp|Defaults")
	FString DefaultTxValueWei = TEXT("0x0");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dapp|Defaults")
	FString DefaultSignMessage = TEXT("Hello from CROSSx Unreal Sample");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dapp|Defaults")
	FString DefaultTokenDecimals = TEXT("18");

	// Minimal EIP-712 typed-data payload that's safe to sign on any EVM
	// chain (no on-chain effect; pure off-chain signature for testing).
	// Without a default, OnClickSignTypedData() short-circuits with
	// `sample.message.invalidTypedData` whenever the user hasn't typed
	// anything into Inp_SignTypedData — which feels like "the button does
	// nothing" because the toast is easy to miss.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dapp|Defaults", meta = (MultiLine = true))
	FString DefaultTypedDataJson;

protected:
	// ═══════════════ Bound widgets (all optional) ═══════════════
	//
	// ── Buttons ─────────────────────────────────────────────
	UPROPERTY(meta = (BindWidgetOptional)) UButton* Btn_Login;
	UPROPERTY(meta = (BindWidgetOptional)) UButton* Btn_LoginGoogle;
	UPROPERTY(meta = (BindWidgetOptional)) UButton* Btn_LoginApple;
	UPROPERTY(meta = (BindWidgetOptional)) UButton* Btn_UseWebkit;
	UPROPERTY(meta = (BindWidgetOptional)) UButton* Btn_UseCrossPay;
	UPROPERTY(meta = (BindWidgetOptional)) UButton* Btn_CreateWallet;
	UPROPERTY(meta = (BindWidgetOptional)) UButton* Btn_GetAddress;
	UPROPERTY(meta = (BindWidgetOptional)) UButton* Btn_GetAllAddresses;
	UPROPERTY(meta = (BindWidgetOptional)) UButton* Btn_SelectWallet;
	UPROPERTY(meta = (BindWidgetOptional)) UButton* Btn_GetNativeBalance;
	UPROPERTY(meta = (BindWidgetOptional)) UButton* Btn_SignTx;
	UPROPERTY(meta = (BindWidgetOptional)) UButton* Btn_SendTx;
	UPROPERTY(meta = (BindWidgetOptional)) UButton* Btn_GetTokenBalance;
	UPROPERTY(meta = (BindWidgetOptional)) UButton* Btn_SendToken;
	UPROPERTY(meta = (BindWidgetOptional)) UButton* Btn_SignPersonalMessage;
	UPROPERTY(meta = (BindWidgetOptional)) UButton* Btn_SignTypedData;
	UPROPERTY(meta = (BindWidgetOptional)) UButton* Btn_CheckTokenExpiry;
	UPROPERTY(meta = (BindWidgetOptional)) UButton* Btn_RefreshToken;
	UPROPERTY(meta = (BindWidgetOptional)) UButton* Btn_GetUserInfo;
	UPROPERTY(meta = (BindWidgetOptional)) UButton* Btn_SignOut;
	UPROPERTY(meta = (BindWidgetOptional)) UButton* Btn_ToggleLanguage;
	UPROPERTY(meta = (BindWidgetOptional)) UButton* Btn_EditorSimulateDeepLink;

	// ── Inputs ──────────────────────────────────────────────
	UPROPERTY(meta = (BindWidgetOptional)) UEditableTextBox* Inp_ChainId;
	UPROPERTY(meta = (BindWidgetOptional)) UEditableTextBox* Inp_From;
	UPROPERTY(meta = (BindWidgetOptional)) UEditableTextBox* Inp_To;
	UPROPERTY(meta = (BindWidgetOptional)) UEditableTextBox* Inp_Value;
	UPROPERTY(meta = (BindWidgetOptional)) UMultiLineEditableTextBox* Inp_Data;
	UPROPERTY(meta = (BindWidgetOptional)) UEditableTextBox* Inp_TokenContract;
	UPROPERTY(meta = (BindWidgetOptional)) UEditableTextBox* Inp_TokenTo;
	UPROPERTY(meta = (BindWidgetOptional)) UEditableTextBox* Inp_TokenAmount;
	UPROPERTY(meta = (BindWidgetOptional)) UEditableTextBox* Inp_TokenDecimals;
	UPROPERTY(meta = (BindWidgetOptional)) UEditableTextBox* Inp_SignMessage;
	UPROPERTY(meta = (BindWidgetOptional)) UMultiLineEditableTextBox* Inp_SignTypedData;
	UPROPERTY(meta = (BindWidgetOptional)) UEditableTextBox* Inp_WebkitUrl;

	// ── Display ─────────────────────────────────────────────
	UPROPERTY(meta = (BindWidgetOptional)) UTextBlock* Txt_WalletAddress;
	UPROPERTY(meta = (BindWidgetOptional)) UTextBlock* Txt_UserId;
	UPROPERTY(meta = (BindWidgetOptional)) UTextBlock* Txt_Status;
	UPROPERTY(meta = (BindWidgetOptional)) UTextBlock* Txt_Language;

	// ── Sections (panel visibility toggled by login state) ─
	UPROPERTY(meta = (BindWidgetOptional)) UPanelWidget* Panel_Login;
	UPROPERTY(meta = (BindWidgetOptional)) UPanelWidget* Panel_Wallet;
	UPROPERTY(meta = (BindWidgetOptional)) UPanelWidget* Panel_Features;
	UPROPERTY(meta = (BindWidgetOptional)) UPanelWidget* Panel_EditorTools;

	// Optional bind: name the root ScrollBox `Scroll_Root` in the WBP to opt
	// into the mobile drag-fallback. When unset the fallback is a no-op.
	UPROPERTY(meta = (BindWidgetOptional)) UScrollBox* Scroll_Root;

	// ═══════════════ Blueprint-callable hooks ═══════════════
	// These are exposed so that a BP subclass can override behavior (e.g.
	// when the designer wants custom visual feedback on each action).

	UFUNCTION(BlueprintCallable, Category = "Dapp|Panel")
	void RefreshLocalizedLabels();

	// Fired right after RefreshLocalizedLabels finishes its default (C++) pass.
	// BP subclasses can override this to push localized text into bespoke
	// widgets that the C++ base class can't see (e.g. tooltips, header bars).
	UFUNCTION(BlueprintImplementableEvent, Category = "Dapp|Panel")
	void OnRefreshLocalizedLabels(EDappLang NewLang);

	UFUNCTION(BlueprintImplementableEvent, Category = "Dapp|Panel")
	void OnLoginStateChanged(bool bLoggedIn);

	// ═══════════════ UI → logic click handlers ═══════════════
	UFUNCTION() void OnClickLogin();
	UFUNCTION() void OnClickLoginGoogle();
	UFUNCTION() void OnClickLoginApple();
	UFUNCTION() void OnClickUseWebkit();
	UFUNCTION() void OnClickUseCrossPay();
	UFUNCTION() void OnClickCreateWallet();
	UFUNCTION() void OnClickGetAddress();
	UFUNCTION() void OnClickGetAllAddresses();
	UFUNCTION() void OnClickSelectWallet();
	UFUNCTION() void OnClickGetNativeBalance();
	UFUNCTION() void OnClickSignTx();
	UFUNCTION() void OnClickSendTx();
	UFUNCTION() void OnClickGetTokenBalance();
	UFUNCTION() void OnClickSendToken();
	UFUNCTION() void OnClickSignPersonalMessage();
	UFUNCTION() void OnClickSignTypedData();
	UFUNCTION() void OnClickCheckTokenExpiry();
	UFUNCTION() void OnClickRefreshToken();
	UFUNCTION() void OnClickGetUserInfo();
	UFUNCTION() void OnClickSignOut();
	UFUNCTION() void OnClickToggleLanguage();
	UFUNCTION() void OnClickEditorSimulateDeepLink();

	// ═══════════════ DappActor / Subsystem callbacks ═══════════════
	UFUNCTION() void HandleSdkReady(bool bHasActiveSession);
	UFUNCTION() void HandleAuthChanged(bool bLoggedIn);
	UFUNCTION() void HandleLanguageChanged(EDappLang NewLanguage);

	// ═══════════════ SDK async callbacks ═══════════════
	UFUNCTION() void HandleSignInResult(const FCROSSxAuthResult& Result);
	UFUNCTION() void HandleSignOutResult(bool bSuccess);
	UFUNCTION() void HandleCreateWalletResult(const FCROSSxCreateWalletResult& Result);
	UFUNCTION() void HandleGetAddressResult(const FCROSSxGetAddressResponse& Result);
	UFUNCTION() void HandleGetAddressesResult(const FCROSSxGetAddressesResponse& Result);
	UFUNCTION() void HandleSelectWalletResult(const FCROSSxWalletSelectionResult& Result);
	UFUNCTION() void HandleSignTxResult(const FCROSSxSignTxResponse& Result);
	UFUNCTION() void HandleSendTxResult(const FCROSSxSendTxResponse& Result);
	UFUNCTION() void HandleSignMessageResult(const FCROSSxSignMessageResponse& Result);
	UFUNCTION() void HandleSignTypedDataResult(const FCROSSxSignTypedDataResponse& Result);
	UFUNCTION() void HandleNativeBalanceResult(const FString& HexBalance);
	UFUNCTION() void HandleTokenBalanceRpcResult(const FCROSSxJsonRpcResponse& Result);
	UFUNCTION() void HandleSendTokenTxResult(const FCROSSxSendTxResponse& Result);
	UFUNCTION() void HandleGetUserInfoResult(const FCROSSxSdkUserInfo& Info);
	UFUNCTION() void HandleRefreshTokenResult(const FCROSSxRefreshTokenResult& Result);
	UFUNCTION() void HandleWebViewResult(const FCROSSxWebViewResult& Result);
	UFUNCTION() void HandleCrossPayCheckoutUrlResult(const FCROSSxCrossPayCheckoutUrlResult& Result);
	UFUNCTION() void HandleCrossPayWebViewResult(const FCROSSxWebViewResult& Result);
	UFUNCTION() void HandleCrossPayPaymentResult(const FCROSSxCrossPayPaymentResult& Result);

private:
	// ── Resolvers ──────────────────────────────────────────
	ADappActor*                 ResolveActor() const;
	UCROSSxSdkSubsystem*        ResolveSdk() const;
	UDappLocalizationSubsystem* ResolveLoc() const;
	UDappNotificationSubsystem* ResolveNotif() const;

	// ── Helpers ────────────────────────────────────────────
	void BindAllClickHandlers();
	void ApplyLoginState(bool bLoggedIn);
	FString  ReadText(UEditableTextBox* Box, const FString& Fallback = FString()) const;
	FString  ReadText(UMultiLineEditableTextBox* Box, const FString& Fallback = FString()) const;
	void     WriteText(UEditableTextBox* Box, const FString& Text);
	void     WriteLabel(UTextBlock* Block, const FText& Text);
	void     SetStatus(FName Key);
	void     SetStatusArgs(FName Key, const TMap<FString, FString>& Args);
	void     SetStatusText(const FText& Text);
	FString  ResolveFromAddress() const;
	FString  ResolveChainId() const;
	int32    ResolveTokenDecimals() const;
	FString  ResolveDappName() const;
	// Mirrors CROSSxSdkTestPanelWidget's pattern of pulling the cached `Sub`
	// from FCROSSxWalletInfo (falling back to UserId). Passing an empty Sub
	// to SetupWalletWithUIAsync makes the SDK pick whichever value happens
	// to be in the auth cache at *that* moment, which can race the
	// migration flow and store the password under a different identity than
	// the one used at sign-time → the user sees "invalid password" with a
	// correct PIN. Resolving the Sub up-front avoids that race.
	FString  ResolveCachedSub() const;
	// Wallet-setup retry plumbing. EnsureWalletSetup() kicks
	// SetupWalletWithUIAsync (PIN modal). HandleCreateWalletResult() then
	// re-fires the action stashed in PendingWalletAction.
	void     EnsureWalletSetup(EPendingWalletAction NextAction);
	void     RetryPendingWalletAction();
	void     Notify(FName Key);
	void     NotifyArgs(FName Key, const TMap<FString, FString>& Args);
	void     NotifyError(FName Key, const TMap<FString, FString>& Args);
	static FString ShortenAddress(const FString& Address);
	static FString HexToDecimalString(const FString& HexWithOrWithoutPrefix);

	// Token-balance request bookkeeping (since HandleTokenBalanceRpcResult has
	// no context about which address / decimals were requested).
	UPROPERTY(Transient) FString PendingTokenBalanceHolder;
	UPROPERTY(Transient) int32   PendingTokenBalanceDecimals = 18;

	UPROPERTY(Transient) bool    bLastKnownLoggedIn = false;

	// What to do once SetupWalletWithUIAsync resolves. Defaults to None,
	// meaning HandleCreateWalletResult just closes the loop and reports
	// success/failure. See OnClickSignPersonalMessage etc. for callers.
	UPROPERTY(Transient) EPendingWalletAction PendingWalletAction = EPendingWalletAction::None;

	// Mobile drag-fallback bookkeeping (see NativeOnPreviewMouseButtonDown
	// / NativeOnTouch* implementations).
	bool       bMobileDragActive    = false;
	FVector2D  MobileDragLastScreen = FVector2D::ZeroVector;
	int32      MobileDragPointerIdx = INDEX_NONE;
};
