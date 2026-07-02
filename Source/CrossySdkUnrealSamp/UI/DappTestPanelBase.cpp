#include "UI/DappTestPanelBase.h"

#include "Async/Async.h"
#include "Components/Button.h"
#include "Components/ContentWidget.h"
#include "Components/EditableTextBox.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/PanelWidget.h"
#include "Components/ScrollBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "Dom/JsonObject.h"
#include "Misc/Guid.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Interfaces/IHttpResponse.h"
#include "Interfaces/IHttpRequest.h"
#include "HttpModule.h"

#include "Dapp/DappActor.h"
#include "UI/DappErc20Codec.h"
#include "UI/DappNotificationSubsystem.h"
#include "Localization/DappLocalizationSubsystem.h"

#include "SDK/CROSSxSdkSubsystem.h"
#include "Core/Types/CROSSxSdkSettings.h"
#include "Core/CROSSxEndpoints.h"
#include "Core/Utils/CROSSxChainUtils.h"


namespace
{
	/**
	 * Resolves the default CAIP-2 chainId based on the active CROSSx environment.
	 * - Dev / Stage → CROSS testnet (eip155:612044)
	 * - Prod        → CROSS mainnet (eip155:612055)
	 */
	static FString ResolveDefaultChainId()
	{
		const UCROSSxSdkSettings* Settings = GetDefault<UCROSSxSdkSettings>();
		const ECROSSxEnvironment Env = Settings ? Settings->Environment : ECROSSxEnvironment::Prod;
		return Env == ECROSSxEnvironment::Prod ? TEXT("eip155:612055") : TEXT("eip155:612044");
	}
}

DEFINE_LOG_CATEGORY_STATIC(LogDappPanel, Log, All);

// Forward-declared so the token-balance / send-token handlers (defined
// earlier in the file) can name their missing fields via the same lookup
// helper used by the label-injection code. The full definition lives
// alongside the other label utilities below.
namespace { FText InputLabelOrFallback(UDappLocalizationSubsystem* Loc, FName Key, const FString& FallbackEN); }

namespace
{
	FCROSSxCrossPayCreatePaymentRequest BuildCrossPayPaymentRequest()
	{
		FCROSSxCrossPayCreatePaymentRequest Request;
		Request.Currency = TEXT("USD");
		Request.Amount = TEXT("4.99");
		Request.PayerUid = TEXT("test-user");
		Request.Metadata.OrderId = FString::Printf(TEXT("demo-order-%s"), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
		Request.Metadata.Product.Name = TEXT("Legendary Axe");
		Request.Metadata.Product.Detail = TEXT("Epic weapon skin for warrior class");
		Request.Metadata.Product.Image = TEXT("https://dev-static.crossnft.io/assets/games/7e33c5becdc9aea95c09e9374e382e12fd7abb7c.png");
		Request.Metadata.Product.Quantity = 1;
		Request.Metadata.Product.Category = TEXT("IN_GAME_ITEM");
		Request.Metadata.Product.Vendor = TEXT("Seal M on CROSS");
		return Request;
	}

	static FString SerializeCrossPayRequest(const FCROSSxCrossPayCreatePaymentRequest& Request)
	{
		TSharedRef<FJsonObject> Product = MakeShared<FJsonObject>();
		Product->SetStringField(TEXT("name"), Request.Metadata.Product.Name);
		Product->SetStringField(TEXT("detail"), Request.Metadata.Product.Detail);
		Product->SetStringField(TEXT("image"), Request.Metadata.Product.Image);
		Product->SetNumberField(TEXT("quantity"), Request.Metadata.Product.Quantity);
		Product->SetStringField(TEXT("category"), Request.Metadata.Product.Category);
		Product->SetStringField(TEXT("vendor"), Request.Metadata.Product.Vendor);

		TSharedRef<FJsonObject> Metadata = MakeShared<FJsonObject>();
		Metadata->SetObjectField(TEXT("product"), Product);
		Metadata->SetStringField(TEXT("order_id"), Request.Metadata.OrderId);

		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("currency"), Request.Currency);
		Root->SetStringField(TEXT("amount"), Request.Amount);
		Root->SetStringField(TEXT("payer_uid"), Request.PayerUid);
		Root->SetObjectField(TEXT("metadata"), Metadata);

		FString Out;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Root, Writer);
		return Out;
	}
}

/**
 * CrossPayServerSimulator — 클라이언트 측 결제 생성 시뮬레이터.
 *
 * 실제 서비스에서는 게임사 서버에서 처리해야 합니다.
 * 클라이언트는 서버 API를 호출해 CheckoutUrl/CheckoutId를 받아야 합니다.
 * 이 네임스페이스는 샘플 앱 전용 시뮬레이터로, Merchant API Key가 포함된
 * 코드를 프로덕션 클라이언트에 배포하지 마세요.
 */
namespace CrossPayServerSimulator
{
	static const FString MerchantApiKey = TEXT("test-api-key-crosshub-001");

	static void CreateCheckout(
		const FCROSSxCrossPayCreatePaymentRequest& Request,
		TFunction<void(bool bSuccess, const FString& CheckoutUrl, const FString& CheckoutId, const FString& ErrorMessage)> OnComplete)
	{
		const UCROSSxSdkSettings* Settings = GetDefault<UCROSSxSdkSettings>();
		const ECROSSxEnvironment Env = Settings ? Settings->Environment : ECROSSxEnvironment::Prod;
		const FString PaymentsUrl = FCROSSxEndpoints::GetCrossPayApiUrl(Env) + TEXT("/v1/payments");

		const FString RequestJson = SerializeCrossPayRequest(Request);

		TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
		HttpRequest->SetURL(PaymentsUrl);
		HttpRequest->SetVerb(TEXT("POST"));
		HttpRequest->SetHeader(TEXT("Content-Type"), TEXT("application/json"));
		HttpRequest->SetHeader(TEXT("Accept"), TEXT("application/json"));
		HttpRequest->SetHeader(TEXT("Idempotency-Key"), FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens));
		HttpRequest->SetHeader(TEXT("Authorization"), FString::Printf(TEXT("Bearer %s"), *MerchantApiKey));
		HttpRequest->SetContentAsString(RequestJson);

		HttpRequest->OnProcessRequestComplete().BindLambda(
			[OnComplete](FHttpRequestPtr /*Req*/, FHttpResponsePtr Response, bool bSucceeded)
			{
				const int32 StatusCode    = Response.IsValid() ? Response->GetResponseCode() : 0;
				const FString ResponseBody = Response.IsValid() ? Response->GetContentAsString() : FString();

				if (!bSucceeded || !Response.IsValid() || StatusCode < 200 || StatusCode >= 300)
				{
					OnComplete(false, FString(), FString(),
						FString::Printf(TEXT("CROSS Pay failed HTTP %d: %s"), StatusCode, *ResponseBody));
					return;
				}

				TSharedPtr<FJsonObject> Root;
				const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ResponseBody);
				if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
				{
					OnComplete(false, FString(), FString(), TEXT("CROSS Pay response is not valid JSON."));
					return;
				}

				FString CheckoutUrl, CheckoutId;
				Root->TryGetStringField(TEXT("checkout_url"), CheckoutUrl);
				Root->TryGetStringField(TEXT("checkout_id"),  CheckoutId);

				if (CheckoutUrl.IsEmpty())
				{
					OnComplete(false, FString(), FString(),
						TEXT("CROSS Pay checkout_url is missing in the payment response."));
					return;
				}

				// checkout_id가 없으면 URL에서 추출합니다.
				if (CheckoutId.IsEmpty())
				{
					FString Left;
					CheckoutUrl.Split(TEXT("?"), &CheckoutId, &Left, ESearchCase::IgnoreCase, ESearchDir::FromStart);
					if (CheckoutId.IsEmpty()) { CheckoutId = CheckoutUrl; }
					int32 SlashIndex = INDEX_NONE;
					if (CheckoutId.FindLastChar(TEXT('/'), SlashIndex))
					{
						CheckoutId = CheckoutId.Mid(SlashIndex + 1);
					}
				}

				OnComplete(true, CheckoutUrl, CheckoutId, FString());
			});

		if (!HttpRequest->ProcessRequest())
		{
			OnComplete(false, FString(), FString(),
				TEXT("CROSS Pay failed: could not start payment request"));
		}
	}
}

// ─────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────

UDappTestPanelBase::UDappTestPanelBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DefaultTypedDataJson = TEXT(R"({
  "types": {
    "EIP712Domain": [
      { "name": "name",    "type": "string"  },
      { "name": "version", "type": "string"  },
      { "name": "chainId", "type": "uint256" }
    ],
    "Mail": [
      { "name": "from",    "type": "string" },
      { "name": "to",      "type": "string" },
      { "name": "contents","type": "string" }
    ]
  },
  "primaryType": "Mail",
  "domain": {
    "name": "CROSSx Unreal Sample",
    "version": "1",
    "chainId": 612044
  },
  "message": {
    "from": "Alice",
    "to": "Bob",
    "contents": "Hello from CROSSx Unreal Sample"
  }
})");
}

void UDappTestPanelBase::NativeConstruct()
{
	Super::NativeConstruct();

	// Mobile ScrollBox fix (UE-42440 official Epic comment): SScrollBox only
	// steals capture from a child Button after the user has dragged past
	// FSlateApplication::DragTriggerDistance (default 5px). On high-DPI
	// Android screens 5px is below the typical finger jitter, so the Button
	// permanently keeps capture and the ScrollBox never starts scrolling
	// when the touch begins on top of any interactive widget. Lowering the
	// threshold to ~2px lets the ScrollBox win the capture race almost
	// immediately while still allowing taps to register as taps.
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().SetDragTriggerDistance(2.0f);
	}

	BindAllClickHandlers();

	// Inject "Chain ID" / "To" / "Value" / ... titles above each Inp_*
	// widget so the dApp UI is self-describing without forcing every WBP
	// downstream of this base to hand-author label TextBlocks.
	EnsureInputLabelsBuilt();

	// Pre-fill text boxes with sensible defaults so the first-time user can
	// hit any button without typing anything. All values are harmless on
	// CROSS testnet.
	WriteText(Inp_ChainId,        (DefaultChainId == TEXT("eip155:612044") || DefaultChainId.IsEmpty()) ? ResolveDefaultChainId() : DefaultChainId);
	WriteText(Inp_Value,          DefaultTxValueWei);
	WriteText(Inp_SignMessage,    DefaultSignMessage);
	WriteText(Inp_TokenDecimals,  DefaultTokenDecimals);
	WriteText(Inp_TokenAmount,    DefaultTokenAmount);

	// Patch DefaultTypedDataJson so domain.chainId matches the active
	// environment. The constructor hard-codes 612044 (testnet) as a safe
	// placeholder; replace it with the resolved chain number so the SDK's
	// TypedData validation doesn't reject the default payload on mainnet.
	{
		const FString ActiveChain = ResolveDefaultChainId();
		static const FString Eip155Prefix = TEXT("eip155:");
		if (ActiveChain.StartsWith(Eip155Prefix))
		{
			const FString ActiveNumeric = ActiveChain.Mid(Eip155Prefix.Len());
			DefaultTypedDataJson = DefaultTypedDataJson.Replace(TEXT("612044"), *ActiveNumeric);
		}
	}
	WriteText(Inp_SignTypedData,  DefaultTypedDataJson);

	// Wire up DappActor signals — the actor itself is placed in the level,
	// so we do not assume any particular ordering. If it is not yet present
	// we fall back to the subsystems for read-only operations.
	if (ADappActor* Actor = ResolveActor())
	{
		Actor->OnAuthChanged.AddDynamic(this,     &UDappTestPanelBase::HandleAuthChanged);
		Actor->OnSdkReady.AddDynamic(this,        &UDappTestPanelBase::HandleSdkReady);
	}

	if (UDappLocalizationSubsystem* Loc = ResolveLoc())
	{
		Loc->OnLanguageChanged.AddDynamic(this, &UDappTestPanelBase::HandleLanguageChanged);
		// Seed both the SDK locale and the language indicator label from the
		// persisted dApp language so the very first paint matches the toggle.
		HandleLanguageChanged(Loc->GetLanguage());
	}
	else
	{
		RefreshLocalizedLabels();
	}
	ApplyLoginState(bLastKnownLoggedIn);
}

// ─────────────────────────────────────────────────────────────────────
// Mobile ScrollBox drag fallback
// ─────────────────────────────────────────────────────────────────────
//
// Why this exists:
//   On Android, when a touch starts on top of a UButton or UEditableTextBox
//   inside a UScrollBox, the child captures the pointer and the ScrollBox
//   never gets a chance to "steal" capture. NativeOnPreviewMouseButtonDown
//   would fire on the root before children, but on the mobile path it does
//   not run early enough (or at all on some devices), so the cleanest
//   solution is to bypass the event system entirely and just poll the global
//   cursor position every frame. With bUseMouseForTouch=True the touch is
//   already mirrored into FSlateApplication's cursor state, so polling
//   captures every drag — regardless of which widget owns capture.
//
// Trade-offs:
//   - A long press that drifts even slightly will both click the button AND
//     scroll the panel by the drift. Acceptable for a developer test sample
//     (matches Unity sample behaviour).
//   - bUseMouseForTouch=True is required (set in DefaultInput.ini).
//   - Multi-touch is not supported.

FReply UDappTestPanelBase::NativeOnPreviewMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// Best-effort: if the preview event reaches us before any child captures,
	// we get a head start on the polling loop with the exact down position.
	if (Scroll_Root && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bMobileDragActive    = true;
		MobileDragLastScreen = InMouseEvent.GetScreenSpacePosition();
	}
	return Super::NativeOnPreviewMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UDappTestPanelBase::NativeOnTouchStarted(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent)
{
	if (Scroll_Root && MobileDragPointerIdx == INDEX_NONE)
	{
		bMobileDragActive    = true;
		MobileDragLastScreen = InGestureEvent.GetScreenSpacePosition();
		MobileDragPointerIdx = InGestureEvent.GetPointerIndex();
	}
	return Super::NativeOnTouchStarted(InGeometry, InGestureEvent);
}

FReply UDappTestPanelBase::NativeOnTouchMoved(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent)
{
	if (Scroll_Root && bMobileDragActive && InGestureEvent.GetPointerIndex() == MobileDragPointerIdx)
	{
		const FVector2D Current = InGestureEvent.GetScreenSpacePosition();
		const FVector2D Delta   = Current - MobileDragLastScreen;
		MobileDragLastScreen    = Current;
		Scroll_Root->SetScrollOffset(Scroll_Root->GetScrollOffset() - Delta.Y);
	}
	return Super::NativeOnTouchMoved(InGeometry, InGestureEvent);
}

FReply UDappTestPanelBase::NativeOnTouchEnded(const FGeometry& InGeometry, const FPointerEvent& InGestureEvent)
{
	if (InGestureEvent.GetPointerIndex() == MobileDragPointerIdx)
	{
		bMobileDragActive    = false;
		MobileDragPointerIdx = INDEX_NONE;
	}
	return Super::NativeOnTouchEnded(InGeometry, InGestureEvent);
}

void UDappTestPanelBase::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (!Scroll_Root || !FSlateApplication::IsInitialized())
	{
		return;
	}

	FSlateApplication& Slate = FSlateApplication::Get();
	const bool bMouseDown = Slate.GetPressedMouseButtons().Contains(EKeys::LeftMouseButton);

	if (bMouseDown)
	{
		const FVector2D CursorPos = Slate.GetCursorPos();
		if (!bMobileDragActive)
		{
			bMobileDragActive    = true;
			MobileDragLastScreen = CursorPos;
		}
		else
		{
			const FVector2D Delta = CursorPos - MobileDragLastScreen;
			MobileDragLastScreen  = CursorPos;
			if (!Delta.IsNearlyZero())
			{
				Scroll_Root->SetScrollOffset(Scroll_Root->GetScrollOffset() - Delta.Y);
			}
		}
	}
	else if (bMobileDragActive && MobileDragPointerIdx == INDEX_NONE)
	{
		bMobileDragActive = false;
	}
}

void UDappTestPanelBase::NativeDestruct()
{
	if (ADappActor* Actor = ResolveActor())
	{
		Actor->OnAuthChanged.RemoveDynamic(this, &UDappTestPanelBase::HandleAuthChanged);
		Actor->OnSdkReady.RemoveDynamic(this,    &UDappTestPanelBase::HandleSdkReady);
	}
	if (UDappLocalizationSubsystem* Loc = ResolveLoc())
	{
		Loc->OnLanguageChanged.RemoveDynamic(this, &UDappTestPanelBase::HandleLanguageChanged);
	}
	Super::NativeDestruct();
}

void UDappTestPanelBase::BindAllClickHandlers()
{
	#define BIND_CLICK(ButtonPtr, Handler) \
		if (ButtonPtr) { ButtonPtr->OnClicked.AddUniqueDynamic(this, &UDappTestPanelBase::Handler); }

	BIND_CLICK(Btn_Login,                  OnClickLogin);
	BIND_CLICK(Btn_LoginGoogle,            OnClickLoginGoogle);
	BIND_CLICK(Btn_LoginApple,             OnClickLoginApple);
	BIND_CLICK(Btn_UseWebkit,                OnClickUseWebkit);
	BIND_CLICK(Btn_UseCrossPay,              OnClickUseCrossPay);
	BIND_CLICK(Btn_CreateWallet,           OnClickCreateWallet);
	BIND_CLICK(Btn_GetAddress,             OnClickGetAddress);
	BIND_CLICK(Btn_GetAllAddresses,        OnClickGetAllAddresses);
	BIND_CLICK(Btn_SelectWallet,           OnClickSelectWallet);
	BIND_CLICK(Btn_GetNativeBalance,       OnClickGetNativeBalance);
	BIND_CLICK(Btn_SignTx,                 OnClickSignTx);
	BIND_CLICK(Btn_SendTx,                 OnClickSendTx);
	BIND_CLICK(Btn_GetTokenBalance,        OnClickGetTokenBalance);
	BIND_CLICK(Btn_SendToken,              OnClickSendToken);
	BIND_CLICK(Btn_SignPersonalMessage,    OnClickSignPersonalMessage);
	BIND_CLICK(Btn_SignTypedData,          OnClickSignTypedData);
	BIND_CLICK(Btn_CheckTokenExpiry,       OnClickCheckTokenExpiry);
	BIND_CLICK(Btn_RefreshToken,           OnClickRefreshToken);
	BIND_CLICK(Btn_GetUserInfo,            OnClickGetUserInfo);
	BIND_CLICK(Btn_SignOut,                OnClickSignOut);
	BIND_CLICK(Btn_ToggleLanguage,         OnClickToggleLanguage);
	BIND_CLICK(Btn_EditorSimulateDeepLink, OnClickEditorSimulateDeepLink);

	#undef BIND_CLICK

	// Editor-only section: hide on non-editor builds.
	if (Panel_EditorTools)
	{
	#if WITH_EDITOR
		Panel_EditorTools->SetVisibility(ESlateVisibility::Visible);
	#else
		Panel_EditorTools->SetVisibility(ESlateVisibility::Collapsed);
	#endif
	}
}

// ─────────────────────────────────────────────────────────────────────
// Auth section
// ─────────────────────────────────────────────────────────────────────

void UDappTestPanelBase::OnClickLogin()
{
	UCROSSxSdkSubsystem* Sdk = ResolveSdk();
	if (!Sdk) { NotifyError(TEXT("sample.auth.failed"), { { TEXT("message"), TEXT("SDK unavailable") } }); return; }

	SetStatus(TEXT("sample.status.signingIn"));
	FCROSSxAuthResultDelegate Del;
	Del.BindDynamic(this, &UDappTestPanelBase::HandleSignInResult);
	Sdk->SignInWithUIAsync(Del);
}

void UDappTestPanelBase::OnClickLoginGoogle()
{
	UCROSSxSdkSubsystem* Sdk = ResolveSdk();
	if (!Sdk) { return; }
	SetStatus(TEXT("sample.status.signingIn"));
	FCROSSxAuthResultDelegate Del;
	Del.BindDynamic(this, &UDappTestPanelBase::HandleSignInResult);
	Sdk->SignInWithCreateAsync(ECROSSxLoginProvider::Google, Del);
}

void UDappTestPanelBase::OnClickLoginApple()
{
	UCROSSxSdkSubsystem* Sdk = ResolveSdk();
	if (!Sdk) { return; }
	SetStatus(TEXT("sample.status.signingIn"));
	FCROSSxAuthResultDelegate Del;
	Del.BindDynamic(this, &UDappTestPanelBase::HandleSignInResult);
	Sdk->SignInWithCreateAsync(ECROSSxLoginProvider::Apple, Del);
}

void UDappTestPanelBase::HandleSignInResult(const FCROSSxAuthResult& Result)
{
	if (!Result.bSuccess)
	{
		{
			const TMap<FString, FString> Args = { { TEXT("message"), Result.ErrorMessage.IsEmpty() ? TEXT("unknown") : Result.ErrorMessage } };
			NotifyError(TEXT("sample.auth.failed"), Args);
			SetStatusArgs(TEXT("sample.auth.failed"), Args);
		}
		return;
	}

	if (ADappActor* Actor = ResolveActor())
	{
		if (!Result.WalletAddress.IsEmpty())
		{
			Actor->SetSelectedWallet(Result.WalletAddress, 0);
		}
	}
	WriteLabel(Txt_UserId,        FText::FromString(Result.User.Id));
	WriteLabel(Txt_WalletAddress, FText::FromString(Result.WalletAddress));
	WriteText(Inp_From,           Result.WalletAddress);

	if (!Result.WalletAddress.IsEmpty())
	{
		NotifyArgs(TEXT("sample.auth.successWithWallet"),
			{ { TEXT("userId"),  Result.User.Id },
			  { TEXT("address"), ShortenAddress(Result.WalletAddress) } });
	}
	else
	{
		NotifyArgs(TEXT("sample.auth.successNoWallet"),
			{ { TEXT("userId"), Result.User.Id } });
	}
	SetStatus(TEXT("sample.auth.success"));
	ApplyLoginState(true);

	// Mirror the dev-panel UX: if sign-in succeeded but the backend reports no
	// wallet yet (or the account needs migration), auto-trigger the wallet
	// setup flow. The SDK's SetupWalletWithUIAsync handles both "create new
	// wallet" and "migrate existing v1 wallet" via its built-in PIN modal —
	// without this auto-trigger the user would only see the silent -10005
	// "user wallet not found" error on the next GetAddress call.
	//
	// Note on Google/Apple paths: OnClickLoginGoogle/Apple call
	// SignInWithCreateAsync, which already chains SetupWalletWithUIAsync
	// inside the SDK on success. In that case Result.WalletAddress will be
	// populated by the time we land here and this block becomes a no-op.
	// The check below also covers the OnClickLogin (provider-picker UI)
	// path, which uses the plain SignInWithUIAsync and *doesn't* auto-set
	// up a wallet — that path needs the explicit trigger below.
	if (Result.WalletAddress.IsEmpty())
	{
		// Reuses the same retry plumbing as the sign-message flow. Pending
		// action stays None — auto-setup right after sign-in is a one-shot,
		// the user re-clicks whatever feature they wanted next.
		EnsureWalletSetup(EPendingWalletAction::None);
	}
}

void UDappTestPanelBase::OnClickSignOut()
{
	UCROSSxSdkSubsystem* Sdk = ResolveSdk();
	if (!Sdk) { return; }
	SetStatus(TEXT("sample.status.signingOut"));
	FCROSSxBoolDelegate Del;
	Del.BindDynamic(this, &UDappTestPanelBase::HandleSignOutResult);
	Sdk->SignOutAsync(Del);
}

void UDappTestPanelBase::HandleSignOutResult(bool bSuccess)
{
	if (bSuccess)
	{
		Notify(TEXT("sample.common.signedOut"));
		if (ADappActor* Actor = ResolveActor()) { Actor->ClearSelectedWallet(); }
		WriteLabel(Txt_UserId,        FText::GetEmpty());
		WriteLabel(Txt_WalletAddress, FText::GetEmpty());
		WriteText(Inp_From,           FString());
		SetStatus(TEXT("sample.common.signedOut"));
		ApplyLoginState(false);
	}
	else
	{
		NotifyError(TEXT("sample.common.signOutFailed"), {});
	}
}

// ─────────────────────────────────────────────────────────────────────
// Wallet / Address section
// ─────────────────────────────────────────────────────────────────────

void UDappTestPanelBase::OnClickCreateWallet()
{
	// Single entry point, shared with auto-setup from HandleSignInResult and
	// the sign-message wallet-required branch. Passing the cached Sub
	// explicitly (vs FString()) avoids a race where SDK's internal fallback
	// reads a stale auth cache mid-migration and writes the wallet password
	// against a different identity than the one used at sign-time.
	EnsureWalletSetup(EPendingWalletAction::None);
}

void UDappTestPanelBase::HandleCreateWalletResult(const FCROSSxCreateWalletResult& Result)
{
	if (!Result.ErrorMessage.IsEmpty())
	{
		// Bail without retry — user typically cancels or hits a backend
		// error, replaying the sign-message would just spam the same modal.
		PendingWalletAction = EPendingWalletAction::None;
		{
			const TMap<FString, FString> Args = { { TEXT("message"), Result.ErrorMessage } };
			NotifyError(TEXT("sample.wallet.createFailed"), Args);
			SetStatusArgs(TEXT("sample.wallet.createFailed"), Args);
		}
		return;
	}

	if (ADappActor* Actor = ResolveActor())
	{
		Actor->SetSelectedWallet(Result.Address, 0);
	}
	WriteLabel(Txt_WalletAddress, FText::FromString(Result.Address));
	WriteText(Inp_From,           Result.Address);
	{
		const TMap<FString, FString> ToastArgs  = { { TEXT("address"), ShortenAddress(Result.Address) } };
		const TMap<FString, FString> StatusArgs = { { TEXT("address"), Result.Address } };
		NotifyArgs(TEXT("sample.wallet.createSuccess"), ToastArgs);
		SetStatusArgs(TEXT("sample.wallet.createSuccess"), StatusArgs);
	}

	// Replay whatever button kicked off the auto-setup so the user
	// experiences a single fluid flow (mirrors CROSSxSdkTestPanelWidget's
	// EnsureFromAddressThen lambda chain).
	RetryPendingWalletAction();
}

void UDappTestPanelBase::OnClickGetAddress()
{
	UCROSSxSdkSubsystem* Sdk = ResolveSdk();
	if (!Sdk) { return; }
	SetStatus(TEXT("sample.status.loading"));
	FCROSSxGetAddressDelegate Del;
	Del.BindDynamic(this, &UDappTestPanelBase::HandleGetAddressResult);
	Sdk->GetAddressWithUIAsync(0, Del);
}

void UDappTestPanelBase::HandleGetAddressResult(const FCROSSxGetAddressResponse& Result)
{
	if (!Result.ErrorMessage.IsEmpty())
	{
		const TMap<FString, FString> Args = { { TEXT("message"), Result.ErrorMessage } };
		NotifyError(TEXT("sample.address.fetchFailed"), Args);
		SetStatusArgs(TEXT("sample.address.fetchFailed"), Args);
		return;
	}
	if (ADappActor* Actor = ResolveActor())
	{
		Actor->SetSelectedWallet(Result.Address, Result.Index);
	}
	WriteLabel(Txt_WalletAddress, FText::FromString(Result.Address));
	WriteText(Inp_From,           Result.Address);
	{
		const TMap<FString, FString> ToastArgs  = { { TEXT("address"), ShortenAddress(Result.Address) } };
		const TMap<FString, FString> StatusArgs = { { TEXT("address"), Result.Address } };
		NotifyArgs(TEXT("sample.address.fetched"), ToastArgs);
		SetStatusArgs(TEXT("sample.address.fetched"), StatusArgs);
	}
}

void UDappTestPanelBase::OnClickGetAllAddresses()
{
	UCROSSxSdkSubsystem* Sdk = ResolveSdk();
	if (!Sdk) { return; }
	SetStatus(TEXT("sample.status.loading"));
	FCROSSxGetAddressesDelegate Del;
	Del.BindDynamic(this, &UDappTestPanelBase::HandleGetAddressesResult);
	Sdk->GetAddressesAsync(Del);
}

void UDappTestPanelBase::HandleGetAddressesResult(const FCROSSxGetAddressesResponse& Result)
{
	if (!Result.ErrorMessage.IsEmpty())
	{
		const TMap<FString, FString> Args = { { TEXT("message"), Result.ErrorMessage } };
		NotifyError(TEXT("sample.address.listFailed"), Args);
		SetStatusArgs(TEXT("sample.address.listFailed"), Args);
		return;
	}

	FString Summary;
	for (const FCROSSxAddressInfo& Info : Result.Addresses)
	{
		Summary += FString::Printf(TEXT("[%d] %s\n"), Info.Index, *Info.Address);
	}
	UE_LOG(LogDappPanel, Log, TEXT("Addresses:\n%s"), *Summary);

	{
		const TMap<FString, FString> Args = { { TEXT("count"), FString::FromInt(Result.Addresses.Num()) } };
		NotifyArgs(TEXT("sample.address.listOk"), Args);
		SetStatusArgs(TEXT("sample.address.listOk"), Args);
	}
}

void UDappTestPanelBase::OnClickSelectWallet()
{
	UCROSSxSdkSubsystem* Sdk = ResolveSdk();
	if (!Sdk) { return; }
	SetStatus(TEXT("sample.status.loading"));
	FCROSSxWalletSelectionDelegate Del;
	Del.BindDynamic(this, &UDappTestPanelBase::HandleSelectWalletResult);

	const FString Current = ResolveFromAddress();
	Sdk->SelectWalletWithUIAsync(Current, Del);
}

void UDappTestPanelBase::HandleSelectWalletResult(const FCROSSxWalletSelectionResult& Result)
{
	if (Result.Address.IsEmpty())
	{
		Notify(TEXT("sample.address.selectCancelled"));
		return;
	}
	if (ADappActor* Actor = ResolveActor()) { Actor->SetSelectedWallet(Result.Address, Result.Index); }
	WriteLabel(Txt_WalletAddress, FText::FromString(Result.Address));
	WriteText(Inp_From,           Result.Address);
	NotifyArgs(TEXT("sample.address.selected"),
		{ { TEXT("address"), ShortenAddress(Result.Address) } });
}

// ─────────────────────────────────────────────────────────────────────
// Transaction section
// ─────────────────────────────────────────────────────────────────────

void UDappTestPanelBase::OnClickGetNativeBalance()
{
	UCROSSxSdkSubsystem* Sdk = ResolveSdk();
	if (!Sdk) { return; }
	const FString From = ResolveFromAddress();
	if (From.IsEmpty())
	{
		// Native-balance read also needs an address; trigger the same
		// SetupWallet flow but don't auto-retry — balance lookup is cheap
		// enough to re-click manually after the wallet exists.
		EnsureWalletSetup(EPendingWalletAction::None);
		return;
	}

	SetStatus(TEXT("sample.status.loading"));
	FCROSSxStringDelegate Del;
	Del.BindDynamic(this, &UDappTestPanelBase::HandleNativeBalanceResult);
	Sdk->GetBalanceAsync(From, ResolveChainId(), Del, TEXT("latest"));
}

void UDappTestPanelBase::HandleNativeBalanceResult(const FString& HexBalance)
{
	const FString Dec = DappErc20Codec::DecodeUint256BalanceHex(HexBalance, 18);
	{
		const TMap<FString, FString> Args = { { TEXT("amount"), Dec } };
		NotifyArgs(TEXT("sample.tx.nativeBalance"), Args);
		SetStatusArgs(TEXT("sample.tx.nativeBalance"), Args);
	}
}

void UDappTestPanelBase::OnClickSignTx()
{
	UCROSSxSdkSubsystem* Sdk = ResolveSdk();
	if (!Sdk) { return; }

	FCROSSxUnsignedTx Tx;
	Tx.ChainId = ResolveChainId();
	Tx.From    = ResolveFromAddress();
	Tx.To      = ReadText(Inp_To);
	Tx.Value   = ResolveTxValueAsHexWei();
	Tx.Data    = ReadText(Inp_Data);
	// Empty data fields must be normalized to "0x" — some chains/RPCs
	// reject the unsigned tx envelope when `data` is the empty string.
	// Mirrors CROSSxSdkTestPanelWidget::BuildUnsignedTxFromInputs.
	if (Tx.Data.IsEmpty()) { Tx.Data = TEXT("0x"); }

	if (Tx.From.IsEmpty())
	{
		EnsureWalletSetup(EPendingWalletAction::SignTx);
		return;
	}
	if (Tx.To.IsEmpty())
	{
		NotifyError(TEXT("sample.tx.invalidInput"), {});
		return;
	}

	SetStatus(TEXT("sample.status.signing"));
	FCROSSxSignTxDelegate Del;
	Del.BindDynamic(this, &UDappTestPanelBase::HandleSignTxResult);
	Sdk->SignTransactionWithUIAsync(Tx, Tx.ChainId, Del, ResolveDappName());
}

void UDappTestPanelBase::HandleSignTxResult(const FCROSSxSignTxResponse& Result)
{
	if (!Result.ErrorMessage.IsEmpty())
	{
		const TMap<FString, FString> Args = { { TEXT("message"), Result.ErrorMessage } };
		NotifyError(TEXT("sample.tx.signFailed"), Args);
		SetStatusArgs(TEXT("sample.tx.signFailed"), Args);
		return;
	}
	{
		const TMap<FString, FString> Args = { { TEXT("hash"), Result.TxHash.IsEmpty() ? TEXT("(local)") : Result.TxHash } };
		NotifyArgs(TEXT("sample.tx.signOk"), Args);
		SetStatusArgs(TEXT("sample.tx.signOk"), Args);
	}
}

void UDappTestPanelBase::OnClickSendTx()
{
	UCROSSxSdkSubsystem* Sdk = ResolveSdk();
	if (!Sdk) { return; }

	FCROSSxUnsignedTx Tx;
	Tx.ChainId = ResolveChainId();
	Tx.From    = ResolveFromAddress();
	Tx.To      = ReadText(Inp_To);
	Tx.Value   = ResolveTxValueAsHexWei();
	Tx.Data    = ReadText(Inp_Data);
	if (Tx.Data.IsEmpty()) { Tx.Data = TEXT("0x"); }

	if (Tx.From.IsEmpty())
	{
		EnsureWalletSetup(EPendingWalletAction::SendTx);
		return;
	}
	if (Tx.To.IsEmpty())
	{
		NotifyError(TEXT("sample.tx.invalidInput"), {});
		return;
	}

	SetStatus(TEXT("sample.status.sending"));
	FCROSSxSendTxDelegate Del;
	Del.BindDynamic(this, &UDappTestPanelBase::HandleSendTxResult);
	Sdk->SendTransactionWithUIAsync(Tx, Tx.ChainId, Del, ResolveDappName());
}

void UDappTestPanelBase::HandleSendTxResult(const FCROSSxSendTxResponse& Result)
{
	if (!Result.ErrorMessage.IsEmpty())
	{
		const TMap<FString, FString> Args = { { TEXT("message"), Result.ErrorMessage } };
		NotifyError(TEXT("sample.tx.sendFailed"), Args);
		SetStatusArgs(TEXT("sample.tx.sendFailed"), Args);
		return;
	}
	{
		const TMap<FString, FString> Args = { { TEXT("hash"), Result.TxHash } };
		NotifyArgs(TEXT("sample.tx.sendOk"), Args);
		SetStatusArgs(TEXT("sample.tx.sendOk"), Args);
	}
}

// ─────────────────────────────────────────────────────────────────────
// Token (ERC-20) section
// ─────────────────────────────────────────────────────────────────────

void UDappTestPanelBase::OnClickGetTokenBalance()
{
	UCROSSxSdkSubsystem* Sdk = ResolveSdk();
	if (!Sdk) { return; }

	const FString From     = ResolveFromAddress();
	const FString Contract = ReadText(Inp_TokenContract);
	if (From.IsEmpty())
	{
		// Same fluid retry as Sign* handlers — surface the SDK setup modal
		// on first use, then re-fire this button so the user gets their
		// balance without a second click.
		EnsureWalletSetup(EPendingWalletAction::GetTokenBalance);
		return;
	}
	if (Contract.IsEmpty())
	{
		// Surface the exact missing field name so the developer knows what
		// to type into the panel — a generic "input invalid" toast forced
		// them to re-read the C++ to figure out which slot was blank.
		const TMap<FString, FString> Args = {
			{ TEXT("fields"), InputLabelOrFallback(ResolveLoc(), TEXT("sample.input.tokenContract"), TEXT("Token Contract")).ToString() }
		};
		NotifyError(TEXT("sample.token.invalidInput"), Args);
		return;
	}

	const FString CallData = DappErc20Codec::EncodeBalanceOf(From);
	if (CallData.IsEmpty())
	{
		NotifyError(TEXT("sample.token.encodingFailed"), {});
		return;
	}

	// eth_call params: [{ to, data }, blockTag]
	const FString CallJson = FString::Printf(TEXT("{\"to\":\"%s\",\"data\":\"%s\"}"),
		*Contract, *CallData);

	FCROSSxJsonRpcRequest Req;
	Req.Id     = TEXT("1");
	Req.Method = TEXT("eth_call");
	Req.ParamsJson = { CallJson, TEXT("\"latest\"") };

	PendingTokenBalanceHolder   = From;
	PendingTokenBalanceDecimals = ResolveTokenDecimals();

	SetStatus(TEXT("sample.status.loading"));
	FCROSSxJsonRpcDelegate Del;
	Del.BindDynamic(this, &UDappTestPanelBase::HandleTokenBalanceRpcResult);
	Sdk->WalletRpcAsync(Req, ResolveChainId(), Del);
}

void UDappTestPanelBase::HandleTokenBalanceRpcResult(const FCROSSxJsonRpcResponse& Result)
{
	if (Result.HasError())
	{
		NotifyError(TEXT("sample.token.balanceFailed"),
			{ { TEXT("message"), Result.Error.Message } });
		return;
	}
	// ResultJson is a JSON-encoded string like "\"0x...\"". Strip quotes.
	FString Hex = Result.ResultJson;
	Hex.TrimStartAndEndInline();
	Hex.RemoveFromStart(TEXT("\""));
	Hex.RemoveFromEnd(TEXT("\""));
	const FString Dec = DappErc20Codec::DecodeUint256BalanceHex(Hex, PendingTokenBalanceDecimals);

	{
		const TMap<FString, FString> Args = { { TEXT("amount"), Dec } };
		NotifyArgs(TEXT("sample.token.balance"), Args);
		SetStatusArgs(TEXT("sample.token.balance"), Args);
	}
}

void UDappTestPanelBase::OnClickSendToken()
{
	UCROSSxSdkSubsystem* Sdk = ResolveSdk();
	if (!Sdk) { return; }

	const FString From      = ResolveFromAddress();
	const FString Contract  = ReadText(Inp_TokenContract);
	const FString To        = ReadText(Inp_TokenTo);
	const FString Amount    = ReadText(Inp_TokenAmount);
	const int32   Decimals  = ResolveTokenDecimals();

	if (From.IsEmpty())
	{
		EnsureWalletSetup(EPendingWalletAction::SendToken);
		return;
	}
	if (Contract.IsEmpty() || To.IsEmpty() || Amount.IsEmpty())
	{
		// Collect *which* slots are blank — without this the toast just
		// says "input invalid" and a first-time dev has no way to tell
		// whether they forgot the contract, the recipient, or the amount.
		UDappLocalizationSubsystem* Loc = ResolveLoc();
		TArray<FString> Missing;
		if (Contract.IsEmpty()) { Missing.Add(InputLabelOrFallback(Loc, TEXT("sample.input.tokenContract"), TEXT("Token Contract")).ToString()); }
		if (To.IsEmpty())       { Missing.Add(InputLabelOrFallback(Loc, TEXT("sample.input.tokenTo"),       TEXT("Token Recipient")).ToString()); }
		if (Amount.IsEmpty())   { Missing.Add(InputLabelOrFallback(Loc, TEXT("sample.input.tokenAmount"),   TEXT("Token Amount")).ToString()); }
		const TMap<FString, FString> Args = {
			{ TEXT("fields"), FString::Join(Missing, TEXT(", ")) }
		};
		NotifyError(TEXT("sample.token.invalidInput"), Args);
		return;
	}

	const FString CallData = DappErc20Codec::EncodeTransfer(To, Amount, Decimals);
	if (CallData.IsEmpty())
	{
		NotifyError(TEXT("sample.token.encodingFailed"), {});
		return;
	}

	FCROSSxUnsignedTx Tx;
	Tx.ChainId = ResolveChainId();
	Tx.From    = From;
	Tx.To      = Contract;   // ERC-20 transfers target the token contract.
	Tx.Value   = TEXT("0x0");
	Tx.Data    = CallData;

	SetStatus(TEXT("sample.status.sending"));
	FCROSSxSendTxDelegate Del;
	Del.BindDynamic(this, &UDappTestPanelBase::HandleSendTokenTxResult);
	// Demo passes Amount = "-" for ERC-20 transfers because the on-chain
	// `value` is always 0x0 (the real amount lives in the `data` payload).
	// Surfacing the dash hides a misleading "0 native" line in the modal.
	Sdk->SendTransactionWithUIAsync(Tx, Tx.ChainId, Del, ResolveDappName(), TEXT("-"));
}

void UDappTestPanelBase::HandleSendTokenTxResult(const FCROSSxSendTxResponse& Result)
{
	if (!Result.ErrorMessage.IsEmpty())
	{
		const TMap<FString, FString> Args = { { TEXT("message"), Result.ErrorMessage } };
		NotifyError(TEXT("sample.token.sendFailed"), Args);
		SetStatusArgs(TEXT("sample.token.sendFailed"), Args);
		return;
	}
	{
		const TMap<FString, FString> Args = { { TEXT("hash"), Result.TxHash } };
		NotifyArgs(TEXT("sample.token.sendOk"), Args);
		SetStatusArgs(TEXT("sample.token.sendOk"), Args);
	}
}

// ─────────────────────────────────────────────────────────────────────
// Message signing section
// ─────────────────────────────────────────────────────────────────────

void UDappTestPanelBase::OnClickSignPersonalMessage()
{
	UCROSSxSdkSubsystem* Sdk = ResolveSdk();
	if (!Sdk) { return; }
	const FString From = ResolveFromAddress();
	if (From.IsEmpty())
	{
		// Mirror CROSSxSdkTestPanelWidget::HandleSignMessageClicked → the
		// dev-panel demo auto-runs SetupWalletWithUIAsync (which surfaces
		// the SDK's "create wallet" or "migrate v1 wallet" modal) and
		// retries the original button on success. Without this branch the
		// button feels dead to first-time users that haven't migrated.
		EnsureWalletSetup(EPendingWalletAction::SignPersonalMessage);
		return;
	}

	const FString Message = ReadText(Inp_SignMessage, DefaultSignMessage);
	SetStatus(TEXT("sample.status.signing"));
	FCROSSxSignMessageDelegate Del;
	Del.BindDynamic(this, &UDappTestPanelBase::HandleSignMessageResult);
	// Pass DappName so the SDK's sign-confirmation modal can render the
	// correct dApp identity (matches CROSSxSdkTestPanelWidget's call site).
	Sdk->SignMessageWithUIAsync(Message, ResolveChainId(), From, Del, ResolveDappName());
}

void UDappTestPanelBase::HandleSignMessageResult(const FCROSSxSignMessageResponse& Result)
{
	if (!Result.ErrorMessage.IsEmpty())
	{
		const TMap<FString, FString> Args = { { TEXT("message"), Result.ErrorMessage } };
		NotifyError(TEXT("sample.message.signFailed"), Args);
		SetStatusArgs(TEXT("sample.message.signFailed"), Args);
		return;
	}
	UE_LOG(LogDappPanel, Log, TEXT("Personal signature: %s"), *Result.Signature);
	{
		// Show the full signature in the status banner so QA can copy/verify it
		// against the wallet output. Truncation belongs in the toast (NotifyArgs)
		// where vertical space is limited, not in the on-screen status row.
		const TMap<FString, FString> ToastArgs  = { { TEXT("sig"), Result.Signature.Len() > 16 ? Result.Signature.Left(16) + TEXT("…") : Result.Signature } };
		const TMap<FString, FString> StatusArgs = { { TEXT("sig"), Result.Signature } };
		NotifyArgs(TEXT("sample.message.signOk"), ToastArgs);
		SetStatusArgs(TEXT("sample.message.signOk"), StatusArgs);
	}
}

void UDappTestPanelBase::OnClickSignTypedData()
{
	UCROSSxSdkSubsystem* Sdk = ResolveSdk();
	if (!Sdk) { return; }
	const FString From = ResolveFromAddress();
	if (From.IsEmpty())
	{
		EnsureWalletSetup(EPendingWalletAction::SignTypedData);
		return;
	}

	const FString TypedJson = ReadText(Inp_SignTypedData, DefaultTypedDataJson);
	if (TypedJson.TrimStartAndEnd().IsEmpty())
	{
		NotifyError(TEXT("sample.message.invalidTypedData"), {});
		return;
	}

	SetStatus(TEXT("sample.status.signing"));
	FCROSSxSignTypedDataDelegate Del;
	Del.BindDynamic(this, &UDappTestPanelBase::HandleSignTypedDataResult);
	// DappName populates the EIP-712 confirmation modal title (matches
	// CROSSxSdkTestPanelWidget). Without it the SDK shows an empty header
	// and external dApps can't be visually distinguished.
	Sdk->SignTypedDataWithUIAsync(From, TypedJson, ResolveChainId(), Del, ResolveDappName());
}

void UDappTestPanelBase::HandleSignTypedDataResult(const FCROSSxSignTypedDataResponse& Result)
{
	if (!Result.ErrorMessage.IsEmpty())
	{
		const TMap<FString, FString> Args = { { TEXT("message"), Result.ErrorMessage } };
		NotifyError(TEXT("sample.message.typedFailed"), Args);
		SetStatusArgs(TEXT("sample.message.typedFailed"), Args);
		return;
	}
	UE_LOG(LogDappPanel, Log, TEXT("Typed-data signature: %s"), *Result.Signature);
	{
		const TMap<FString, FString> ToastArgs  = { { TEXT("sig"), Result.Signature.Len() > 16 ? Result.Signature.Left(16) + TEXT("…") : Result.Signature } };
		const TMap<FString, FString> StatusArgs = { { TEXT("sig"), Result.Signature } };
		NotifyArgs(TEXT("sample.message.typedOk"), ToastArgs);
		SetStatusArgs(TEXT("sample.message.typedOk"), StatusArgs);
	}
}

// ─────────────────────────────────────────────────────────────────────
// Session / common
// ─────────────────────────────────────────────────────────────────────

void UDappTestPanelBase::OnClickCheckTokenExpiry()
{
	UCROSSxSdkSubsystem* Sdk = ResolveSdk();
	if (!Sdk) { return; }
	const bool bExpired = Sdk->IsTokenExpired();
	Notify(bExpired ? TEXT("sample.session.expiredNow")
	                : TEXT("sample.session.valid"));
}

void UDappTestPanelBase::OnClickRefreshToken()
{
	UCROSSxSdkSubsystem* Sdk = ResolveSdk();
	if (!Sdk) { return; }
	SetStatus(TEXT("sample.status.refreshing"));
	FCROSSxRefreshTokenDelegate Del;
	Del.BindDynamic(this, &UDappTestPanelBase::HandleRefreshTokenResult);
	Sdk->RefreshTokenAsync(Del);
}

void UDappTestPanelBase::HandleRefreshTokenResult(const FCROSSxRefreshTokenResult& Result)
{
	// FCROSSxRefreshTokenResult::IsError() checks ErrorCode != 0 — relying
	// on ErrorMessage alone misclassifies failures whose message field
	// happens to be empty (e.g. transport timeouts surfacing only a code).
	if (Result.IsError())
	{
		NotifyError(TEXT("sample.session.refreshFailed"),
			{ { TEXT("message"), Result.ErrorMessage.IsEmpty()
				? FString::Printf(TEXT("code=%d"), Result.ErrorCode)
				: Result.ErrorMessage } });
		return;
	}
	Notify(TEXT("sample.session.refreshOk"));
	SetStatus(TEXT("sample.session.refreshOk"));
}

void UDappTestPanelBase::OnClickGetUserInfo()
{
	UCROSSxSdkSubsystem* Sdk = ResolveSdk();
	if (!Sdk) { return; }
	SetStatus(TEXT("sample.status.loading"));
	FCROSSxUserInfoDelegate Del;
	Del.BindDynamic(this, &UDappTestPanelBase::HandleGetUserInfoResult);
	Sdk->GetUserInfoAsync(Del);
}

void UDappTestPanelBase::HandleGetUserInfoResult(const FCROSSxSdkUserInfo& Info)
{
	if (Info.Id.IsEmpty())
	{
		NotifyError(TEXT("sample.common.userInfoFailed"), {});
		return;
	}
	WriteLabel(Txt_UserId, FText::FromString(Info.Id));
	{
		const TMap<FString, FString> Args = {
			{ TEXT("userId"), Info.Id },
			{ TEXT("email"),  Info.Email },
			{ TEXT("type"),   Info.LoginType }
		};
		NotifyArgs(TEXT("sample.common.userInfoOk"), Args);
		SetStatusArgs(TEXT("sample.common.userInfoOk"), Args);
	}
}

// ─────────────────────────────────────────────────────────────────────
// Webkit section
// ─────────────────────────────────────────────────────────────────────

void UDappTestPanelBase::OnClickUseWebkit()
{
	UCROSSxSdkSubsystem* Sdk = ResolveSdk();
	if (!Sdk)
	{
		NotifyError(TEXT("sample.sdk.notReady"), {});
		return;
	}

	// Mirrors CROSSxSdkTestPanelWidget::BuildSampleWebkitUrl — when the user
	// leaves the URL input blank, fall back to the SDK demo's stage webkit
	// URL with placeholders the SDK substitutes (dappSessionId,
	// dappAccessToken). External teams overriding for production should
	// set Inp_WebkitUrl in the WBP defaults or hard-code their own here.
	FString Url = ReadText(Inp_WebkitUrl).TrimStartAndEnd();
	if (Url.IsEmpty())
	{
		const FString BaseUrl     = TEXT("https://stg-ramp.crosstoken.io/exchange");
		const FString Uuid        = FGenericPlatformHttp::UrlEncode(TEXT("sonny"));
		const FString SessionId   = FGenericPlatformHttp::UrlEncode(TEXT("{{dappSessionId}}"));
		const FString AccessToken = FGenericPlatformHttp::UrlEncode(TEXT("{{dappAccessToken}}"));
		const FString Network     = FGenericPlatformHttp::UrlEncode(TEXT("testnet"));
		Url = FString::Printf(TEXT("%s?uuid=%s&sessionId=%s&accessToken=%s&network=%s"),
			*BaseUrl, *Uuid, *SessionId, *AccessToken, *Network);
	}

	SetStatus(TEXT("sample.status.openingWebkit"));
	FCROSSxWebViewDelegate Delegate;
	Delegate.BindDynamic(this, &UDappTestPanelBase::HandleWebViewResult);
	Sdk->OpenWebView(Url, Delegate);
}

// Each click intentionally starts an independent CROSS Pay flow. The
// SDK routes callbacks per-state via FCROSSxNativeDeepLinkAdapter::
// CrossPayWaiters, so N concurrent clicks resolve into N independent
// payment results — each checkout id surfaces via its own
// HandleCrossPayPaymentResult call. There is no re-entry guard here on
// purpose; gating this at the dApp would make the SDK's multi-waiter
// routing untestable and contradicts the expectation that every
// checkout returns its own callback.
void UDappTestPanelBase::OnClickUseCrossPay()
{
	UCROSSxSdkSubsystem* Sdk = ResolveSdk();
	if (!Sdk)
	{
		NotifyError(TEXT("sample.sdk.notReady"), {});
		return;
	}

	SetStatus(TEXT("sample.status.openingCrossPay"));

	// Step 1: 서버 시뮬레이터로 checkout 생성.
	// 실제 서비스에서는 게임사 서버 API를 호출해 CheckoutUrl/CheckoutId를 받아야 합니다.
	TWeakObjectPtr<UDappTestPanelBase> WeakThis(this);
	CrossPayServerSimulator::CreateCheckout(BuildCrossPayPaymentRequest(),
		[WeakThis](bool bSuccess, const FString& CheckoutUrl, const FString& CheckoutId, const FString& ErrorMessage)
		{
			UDappTestPanelBase* Self = WeakThis.Get();
			if (!Self) { return; }

			if (!bSuccess)
			{
				Self->NotifyArgs(TEXT("sample.crossPay.failed"), { { TEXT("message"), ErrorMessage } });
				return;
			}

			UCROSSxSdkSubsystem* Sdk = Self->ResolveSdk();
			if (!Sdk)
			{
				Self->NotifyError(TEXT("sample.sdk.notReady"), {});
				return;
			}

			// Step 2: SDK로 결제 프로세스 진행 (WebView 열기 + 딥링크 대기).
			FCROSSxCrossPayPaymentDelegate Delegate;
			Delegate.BindDynamic(Self, &UDappTestPanelBase::HandleCrossPayPaymentResult);
			Sdk->OpenCrossPayAndWaitResultAsync(CheckoutUrl, CheckoutId, Delegate);
		});
}

void UDappTestPanelBase::HandleWebViewResult(const FCROSSxWebViewResult& Result)
{
	if (Result.bSuccess)
	{
		Notify(TEXT("sample.webkit.closed"));
	}
	else
	{
		NotifyArgs(TEXT("sample.webkit.failed"), { { TEXT("message"), Result.ErrorMessage } });
	}
}

void UDappTestPanelBase::HandleCrossPayCheckoutUrlResult(const FCROSSxCrossPayCheckoutUrlResult& Result)
{
	if (!Result.bSuccess)
	{
		NotifyArgs(TEXT("sample.crossPay.failed"), { { TEXT("message"), Result.ErrorMessage } });
		return;
	}

	UCROSSxSdkSubsystem* Sdk = ResolveSdk();
	if (!Sdk)
	{
		NotifyError(TEXT("sample.sdk.notReady"), {});
		return;
	}

	FCROSSxWebViewDelegate Delegate;
	Delegate.BindDynamic(this, &UDappTestPanelBase::HandleCrossPayWebViewResult);
	Sdk->OpenWebView(Result.CheckoutUrl, Delegate);
}


void UDappTestPanelBase::HandleCrossPayPaymentResult(const FCROSSxCrossPayPaymentResult& Result)
{
	if (!Result.bSuccess)
	{
		NotifyArgs(TEXT("sample.crossPay.failed"), { { TEXT("message"), Result.ErrorMessage } });
		return;
	}

	NotifyArgs(TEXT("sample.crossPay.result"),
	{
		{ TEXT("status"), Result.StatusText },
		{ TEXT("paymentId"), Result.Id.IsEmpty() ? TEXT("-") : Result.Id },
		{ TEXT("checkoutId"), Result.CheckoutId.IsEmpty() ? TEXT("-") : Result.CheckoutId },
		{ TEXT("amount"), FString::Printf(TEXT("%s %s"), *Result.Amount, *Result.Currency).TrimStartAndEnd() },
		{ TEXT("provider"), Result.ProviderText.IsEmpty() ? TEXT("-") : Result.ProviderText },
		{ TEXT("orderId"), Result.Metadata.OrderId.IsEmpty() ? TEXT("-") : Result.Metadata.OrderId },
	});
}

void UDappTestPanelBase::HandleCrossPayWebViewResult(const FCROSSxWebViewResult& Result)
{
	if (Result.bSuccess)
	{
		Notify(TEXT("sample.crossPay.closed"));
	}
	else
	{
		NotifyArgs(TEXT("sample.crossPay.failed"), { { TEXT("message"), Result.ErrorMessage } });
	}
}

// ─────────────────────────────────────────────────────────────────────
// Utilities / locale / state
// ─────────────────────────────────────────────────────────────────────

void UDappTestPanelBase::OnClickToggleLanguage()
{
	if (UDappLocalizationSubsystem* Loc = ResolveLoc())
	{
		Loc->ToggleLanguage();
	}
}

void UDappTestPanelBase::HandleLanguageChanged(EDappLang NewLanguage)
{
	// Mirror the dApp-level language toggle into the SDK's own locale store
	// via the public subsystem API. The SDK ships its own modals (PIN, wallet
	// setup, sign confirmation, …) localized through FCROSSxSdkLocalization;
	// without this round-trip the dApp's KO/EN button would only retitle
	// sample labels while every SDK modal stayed in English.
	//
	// Architecture: dApp keys (sample.*) live in DT_DappStrings and are
	// resolved by UDappLocalizationSubsystem; SDK keys (sdk.*) live in the
	// plugin's Resources/Localization JSON and are resolved by
	// FCROSSxSdkLocalization. The dApp owns the language state and pushes it
	// down via UCROSSxSdkSubsystem::SetLocale on every toggle.
	if (UCROSSxSdkSubsystem* Sdk = ResolveSdk())
	{
		Sdk->SetLocale(NewLanguage == EDappLang::EN ? TEXT("en") : TEXT("ko"));
	}

	RefreshLocalizedLabels();
	if (Txt_Language)
	{
		Txt_Language->SetText(FText::FromString(NewLanguage == EDappLang::EN ? TEXT("EN") : TEXT("KO")));
	}
}

void UDappTestPanelBase::OnClickEditorSimulateDeepLink()
{
#if WITH_EDITOR
	// The SDK auto-registers crossx-{ProjectId} / webkit-{ProjectId} schemes on
	// mobile. Desktop editors do not receive real deep links, so this button
	// is a placeholder for a per-dApp simulation — external teams typically
	// replace its body with something that matches their OAuth mock server.
	Notify(TEXT("sample.editor.deepLinkNoop"));
#endif
}

void UDappTestPanelBase::HandleSdkReady(bool bHasActiveSession)
{
	// Same defensive marshal as HandleAuthChanged — the SDK initialize
	// callback may fire from a worker thread depending on platform/adapter.
	if (!IsInGameThread())
	{
		TWeakObjectPtr<UDappTestPanelBase> WeakThis(this);
		AsyncTask(ENamedThreads::GameThread, [WeakThis, bHasActiveSession]()
		{
			if (UDappTestPanelBase* Strong = WeakThis.Get())
			{
				Strong->HandleSdkReady(bHasActiveSession);
			}
		});
		return;
	}
	ApplyLoginState(bHasActiveSession);
	Notify(bHasActiveSession ? TEXT("sample.status.resumedSession")
	                         : TEXT("sample.status.sdkReady"));

	// Mirror the dev-panel demo's HandleInitializeCompleted: when a cached
	// session is restored but the wallet was never created (or only exists
	// as a v1 record that needs migration), surface the SDK's setup modal
	// immediately. Without this, users on second launch see the resumed-
	// session banner but no migration prompt — they have to click any
	// wallet-needing button (e.g. Sign Message) before EnsureWalletSetup
	// fires from the per-action guard.
	if (bHasActiveSession && ResolveFromAddress().IsEmpty())
	{
		EnsureWalletSetup(EPendingWalletAction::None);
	}
}

void UDappTestPanelBase::HandleAuthChanged(bool bLoggedIn)
{
	// SDK currently broadcasts OnAuthChanged from a worker thread (e.g.
	// SignOutAsync's task callback). ApplyLoginState() touches Slate
	// (SetVisibility), which asserts unless executed on the game thread.
	// Marshal to the game thread defensively so the sample is robust against
	// any SDK callback site that forgets to dispatch back to the main loop.
	if (!IsInGameThread())
	{
		TWeakObjectPtr<UDappTestPanelBase> WeakThis(this);
		AsyncTask(ENamedThreads::GameThread, [WeakThis, bLoggedIn]()
		{
			if (UDappTestPanelBase* Strong = WeakThis.Get())
			{
				Strong->ApplyLoginState(bLoggedIn);
			}
		});
		return;
	}
	ApplyLoginState(bLoggedIn);
}

namespace
{
	// Recursively descend the widget tree until we find the first TextBlock.
	// UMG buttons are usually `UButton > UTextBlock` (single-text label) but
	// designers occasionally wrap content in HBox/SizeBox/etc. — we still
	// want the label to follow the language toggle in those cases.
	UTextBlock* FindFirstTextBlockIn(UWidget* Root)
	{
		if (!Root) { return nullptr; }
		if (UTextBlock* Direct = Cast<UTextBlock>(Root)) { return Direct; }
		if (UPanelWidget* Panel = Cast<UPanelWidget>(Root))
		{
			for (int32 i = 0; i < Panel->GetChildrenCount(); ++i)
			{
				if (UTextBlock* Found = FindFirstTextBlockIn(Panel->GetChildAt(i)))
				{
					return Found;
				}
			}
		}
		else if (UContentWidget* Content = Cast<UContentWidget>(Root))
		{
			return FindFirstTextBlockIn(Content->GetContent());
		}
		return nullptr;
	}

	// Push a localized label into the first text block under a button. No-op
	// when either the button or the text block is missing — this keeps the
	// helper safe to call even on WBPs that omit some buttons (BindWidgetOptional).
	void ApplyButtonLabel(UButton* Btn, UDappLocalizationSubsystem* Loc, FName Key)
	{
		if (!Btn || !Loc) { return; }
		if (UTextBlock* Label = FindFirstTextBlockIn(Btn))
		{
			Label->SetText(Loc->GetText(Key));
		}
	}
}

void UDappTestPanelBase::RefreshLocalizedLabels()
{
	UDappLocalizationSubsystem* Loc = ResolveLoc();
	if (Loc)
	{
		// Button labels — keys live in DT_DappStrings under sample.button.*.
		// Add new rows there to localize additional buttons; widget-name based
		// lookup means we don't need a per-button BP override.
		ApplyButtonLabel(Btn_Login,                  Loc, TEXT("sample.button.login"));
		ApplyButtonLabel(Btn_LoginGoogle,            Loc, TEXT("sample.button.loginGoogle"));
		ApplyButtonLabel(Btn_LoginApple,             Loc, TEXT("sample.button.loginApple"));
		ApplyButtonLabel(Btn_UseWebkit,                Loc, TEXT("sample.button.useWebkit"));
		ApplyButtonLabel(Btn_UseCrossPay,              Loc, TEXT("sample.button.useCrossPay"));
		ApplyButtonLabel(Btn_CreateWallet,           Loc, TEXT("sample.button.createWallet"));
		ApplyButtonLabel(Btn_GetAddress,             Loc, TEXT("sample.button.getAddress"));
		ApplyButtonLabel(Btn_GetAllAddresses,        Loc, TEXT("sample.button.getAllAddresses"));
		ApplyButtonLabel(Btn_SelectWallet,           Loc, TEXT("sample.button.selectWallet"));
		ApplyButtonLabel(Btn_GetNativeBalance,       Loc, TEXT("sample.button.getNativeBalance"));
		ApplyButtonLabel(Btn_SignTx,                 Loc, TEXT("sample.button.signTx"));
		ApplyButtonLabel(Btn_SendTx,                 Loc, TEXT("sample.button.sendTx"));
		ApplyButtonLabel(Btn_GetTokenBalance,        Loc, TEXT("sample.button.getTokenBalance"));
		ApplyButtonLabel(Btn_SendToken,              Loc, TEXT("sample.button.sendToken"));
		ApplyButtonLabel(Btn_SignPersonalMessage,    Loc, TEXT("sample.button.signPersonalMessage"));
		ApplyButtonLabel(Btn_SignTypedData,          Loc, TEXT("sample.button.signTypedData"));
		ApplyButtonLabel(Btn_CheckTokenExpiry,       Loc, TEXT("sample.button.checkTokenExpiry"));
		ApplyButtonLabel(Btn_RefreshToken,           Loc, TEXT("sample.button.refreshToken"));
		ApplyButtonLabel(Btn_GetUserInfo,            Loc, TEXT("sample.button.getUserInfo"));
		ApplyButtonLabel(Btn_SignOut,                Loc, TEXT("sample.button.signOut"));
		ApplyButtonLabel(Btn_EditorSimulateDeepLink, Loc, TEXT("sample.button.editorSimulateDeepLink"));
		// Btn_ToggleLanguage label is handled by HandleLanguageChanged
		// (always shows the *current* language code, not a translated word).

		// Input titles ("Chain ID", "Value", "Data (hex)", ...) follow the
		// same language toggle as the buttons. We refresh by FName so the
		// C++ fallback strings stay in lockstep with whatever the user (or
		// translator) wires into DT_DappStrings.
		RefreshInputLabels();

		// Hand off to BP — designers can override OnRefreshLocalizedLabels to
		// localize bespoke widgets the C++ base can't see.
		OnRefreshLocalizedLabels(Loc->GetLanguage());
	}
}

// ─────────────────────────────────────────────────────────────────────
// Private helpers
// ─────────────────────────────────────────────────────────────────────

ADappActor* UDappTestPanelBase::ResolveActor() const
{
	return ADappActor::Find(this);
}

UCROSSxSdkSubsystem* UDappTestPanelBase::ResolveSdk() const
{
	if (const ADappActor* Actor = ResolveActor())
	{
		return Actor->GetSdk();
	}
	if (const UWorld* World = GetWorld())
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			return GI->GetSubsystem<UCROSSxSdkSubsystem>();
		}
	}
	return nullptr;
}


UDappLocalizationSubsystem* UDappTestPanelBase::ResolveLoc() const
{
	return UDappLocalizationSubsystem::Get(this);
}

UDappNotificationSubsystem* UDappTestPanelBase::ResolveNotif() const
{
	return UDappNotificationSubsystem::Get(this);
}

void UDappTestPanelBase::ApplyLoginState(bool bLoggedIn)
{
	bLastKnownLoggedIn = bLoggedIn;
	if (Panel_Login)    { Panel_Login->SetVisibility(bLoggedIn ? ESlateVisibility::Collapsed : ESlateVisibility::Visible); }
	if (Panel_Wallet)   { Panel_Wallet->SetVisibility(bLoggedIn ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }
	if (Panel_Features) { Panel_Features->SetVisibility(bLoggedIn ? ESlateVisibility::Visible : ESlateVisibility::Collapsed); }

	OnLoginStateChanged(bLoggedIn);
}

FString UDappTestPanelBase::ReadText(UEditableTextBox* Box, const FString& Fallback) const
{
	if (!Box) { return Fallback; }
	FString Val = Box->GetText().ToString();
	Val.TrimStartAndEndInline();
	return Val.IsEmpty() ? Fallback : Val;
}

FString UDappTestPanelBase::ReadText(UMultiLineEditableTextBox* Box, const FString& Fallback) const
{
	if (!Box) { return Fallback; }
	FString Val = Box->GetText().ToString();
	Val.TrimStartAndEndInline();
	return Val.IsEmpty() ? Fallback : Val;
}

void UDappTestPanelBase::WriteText(UEditableTextBox* Box, const FString& Text)
{
	if (!Box) { return; }
	Box->SetText(FText::FromString(Text));
}

void UDappTestPanelBase::WriteText(UMultiLineEditableTextBox* Box, const FString& Text)
{
	if (!Box) { return; }
	Box->SetText(FText::FromString(Text));
}

void UDappTestPanelBase::WriteLabel(UTextBlock* Block, const FText& Text)
{
	if (Block) { Block->SetText(Text); }
}

FString UDappTestPanelBase::ResolveTxValueAsHexWei() const
{
	const FString Raw = ReadText(Inp_Value, DefaultTxValueWei);

	bool bOk = false;
	const FString HexWei = FCROSSxChainUtils::DecimalToHexWei(Raw, 18, &bOk);

	if (bOk && !HexWei.IsEmpty())
	{
		return HexWei;
	}

	// Falling back here means the user typed something we couldn't parse
	// (negative, letters, garbage). Forward the raw string so the SDK's
	// own normalize-quantity pass still produces a clear "invalid prepare
	// params" / warning trail rather than silently zero-ing the tx.
	UE_LOG(LogDappPanel, Warning,
		TEXT("[Dapp] Inp_Value=\"%s\" is not a valid decimal or hex amount; forwarding as-is."),
		*Raw);
	return Raw;
}

namespace
{
	FText InputLabelOrFallback(UDappLocalizationSubsystem* Loc, FName Key, const FString& FallbackEN)
	{
		if (Loc)
		{
			const FText Translated = Loc->GetText(Key);
			// UDappLocalizationSubsystem::GetText returns "<missing:key>"
			// when the row is absent (e.g. CSV not re-imported yet). Detect
			// that marker so we can show readable copy until the asset is
			// updated.
			if (!Translated.ToString().StartsWith(TEXT("<missing:")))
			{
				return Translated;
			}
		}
		return FText::FromString(FallbackEN);
	}
}

void UDappTestPanelBase::InsertInputLabel(UWidget* Anchor, FName LocKey, const FString& FallbackEN)
{
	if (!Anchor) { return; }
	if (!Anchor->GetParent()) { return; }

	UTextBlock* Label = NewObject<UTextBlock>(this);
	if (!Label) { return; }

	const FText LabelText = InputLabelOrFallback(ResolveLoc(), LocKey, FallbackEN);
	Label->SetText(LabelText);

	FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 11);
	Label->SetFont(Font);
	Label->SetColorAndOpacity(FSlateColor(FLinearColor(0.55f, 0.85f, 0.85f, 1.f)));

	// Defer actual insertion — RebuildParentVerticalBoxes() reorders
	// all children at once after every label is registered. Using
	// InsertChildAt() per-label causes UMG to append instead of insert.
	FInputLabelBinding Binding;
	Binding.Anchor      = Anchor;
	Binding.Label       = Label;
	Binding.LocKey      = LocKey;
	Binding.FallbackEN  = FallbackEN;
	InputLabels.Add(Binding);
}

void UDappTestPanelBase::EnsureInputLabelsBuilt()
{
	if (bInputLabelsBuilt) { return; }
	bInputLabelsBuilt = true;

	// One entry per Inp_* widget the C++ base knows about. WBPs that omit
	// an input simply skip its label too (InsertInputLabel early-outs on
	// null anchors).
	InsertInputLabel(Inp_ChainId,        TEXT("sample.input.chainId"),        TEXT("Chain ID"));
	InsertInputLabel(Inp_From,           TEXT("sample.input.from"),           TEXT("From"));
	InsertInputLabel(Inp_To,             TEXT("sample.input.to"),             TEXT("To"));
	InsertInputLabel(Inp_Value,          TEXT("sample.input.value"),          TEXT("Value (decimal, e.g. 1 = 1 CROSS)"));
	InsertInputLabel(Inp_Data,           TEXT("sample.input.data"),           TEXT("Data (hex)"));
	InsertInputLabel(Inp_TokenContract,  TEXT("sample.input.tokenContract"),  TEXT("Token Contract"));
	InsertInputLabel(Inp_TokenTo,        TEXT("sample.input.tokenTo"),        TEXT("Token Recipient"));
	InsertInputLabel(Inp_TokenAmount,    TEXT("sample.input.tokenAmount"),    TEXT("Token Amount (decimal)"));
	InsertInputLabel(Inp_TokenDecimals,  TEXT("sample.input.tokenDecimals"),  TEXT("Token Decimals"));
	InsertInputLabel(Inp_SignMessage,    TEXT("sample.input.signMessage"),    TEXT("Message to Sign"));
	InsertInputLabel(Inp_SignTypedData,  TEXT("sample.input.signTypedData"),  TEXT("EIP-712 Typed Data (JSON)"));
	InsertInputLabel(Inp_WebkitUrl,      TEXT("sample.input.webkitUrl"),      TEXT("Webkit URL"));

	// Actually place every label before its anchor in the parent VerticalBox.
	// InsertChildAt() appends rather than inserting in UMG, so we rebuild
	// each affected VerticalBox's child order in one pass here.
	RebuildParentVerticalBoxes();
}

void UDappTestPanelBase::RebuildParentVerticalBoxes()
{
	// Build a map: VerticalBox → { anchor → label } so we process each VBox once.
	TMap<UVerticalBox*, TMap<UWidget*, UTextBlock*>> VBoxMap;
	for (const FInputLabelBinding& B : InputLabels)
	{
		UWidget*    Anchor = B.Anchor.Get();
		UTextBlock* Label  = B.Label.Get();
		if (!Anchor || !Label) { continue; }
		UVerticalBox* VBox = Cast<UVerticalBox>(Anchor->GetParent());
		if (!VBox) { continue; }
		VBoxMap.FindOrAdd(VBox).Add(Anchor, Label);
	}

	for (auto& [VBox, AnchorToLabel] : VBoxMap)
	{
		// Snapshot every child with its slot properties before clearing.
		struct FChildSnap
		{
			UWidget*             Widget;
			FMargin              Padding;
			EHorizontalAlignment HAlign  = HAlign_Fill;
			EVerticalAlignment   VAlign  = VAlign_Fill;
			FSlateChildSize      Size    = FSlateChildSize(ESlateSizeRule::Automatic);
			bool                 bHasSlot = false;
		};
		TArray<FChildSnap> Snaps;
		for (int32 i = 0; i < VBox->GetChildrenCount(); i++)
		{
			UWidget* Child = VBox->GetChildAt(i);
			FChildSnap S;
			S.Widget = Child;
			if (UVerticalBoxSlot* VBoxSlot = Cast<UVerticalBoxSlot>(Child->Slot))
			{
				S.Padding   = VBoxSlot->GetPadding();
				S.HAlign    = VBoxSlot->GetHorizontalAlignment();
				S.VAlign    = VBoxSlot->GetVerticalAlignment();
				S.Size      = VBoxSlot->GetSize();
				S.bHasSlot  = true;
			}
			Snaps.Add(MoveTemp(S));
		}

		VBox->ClearChildren();

		for (const FChildSnap& S : Snaps)
		{
			// If this child is an anchor, prepend its label first.
			if (UTextBlock** LabelPtr = AnchorToLabel.Find(S.Widget))
			{
				if (UVerticalBoxSlot* LSlot = VBox->AddChildToVerticalBox(*LabelPtr))
					LSlot->SetPadding(FMargin(2.f, 6.f, 2.f, 2.f));
			}

			if (UVerticalBoxSlot* NewSlot = VBox->AddChildToVerticalBox(S.Widget))
			{
				if (S.bHasSlot)
				{
					NewSlot->SetPadding(S.Padding);
					NewSlot->SetHorizontalAlignment(S.HAlign);
					NewSlot->SetVerticalAlignment(S.VAlign);
					NewSlot->SetSize(S.Size);
				}
			}
		}
	}
}

void UDappTestPanelBase::RefreshInputLabels()
{
	UDappLocalizationSubsystem* Loc = ResolveLoc();
	for (const FInputLabelBinding& Binding : InputLabels)
	{
		if (UTextBlock* Block = Binding.Label.Get())
		{
			Block->SetText(InputLabelOrFallback(Loc, Binding.LocKey, Binding.FallbackEN));
		}
	}
}

void UDappTestPanelBase::SetStatus(FName Key)
{
	if (UDappLocalizationSubsystem* Loc = ResolveLoc())
	{
		SetStatusText(Loc->GetText(Key));
	}
	else
	{
		SetStatusText(FText::FromName(Key));
	}
}

void UDappTestPanelBase::SetStatusArgs(FName Key, const TMap<FString, FString>& Args)
{
	if (UDappLocalizationSubsystem* Loc = ResolveLoc())
	{
		SetStatusText(Loc->Format(Key, Args));
	}
	else
	{
		SetStatusText(FText::FromName(Key));
	}
}

void UDappTestPanelBase::SetStatusText(const FText& Text)
{
	WriteLabel(Txt_Status, Text);
}

FString UDappTestPanelBase::ResolveDappName() const
{
	if (const ADappActor* Actor = ResolveActor())
	{
		if (!Actor->AppName.IsEmpty())
		{
			return Actor->AppName;
		}
	}
	return TEXT("CROSSx Unreal Sample");
}

FString UDappTestPanelBase::ResolveCachedSub() const
{
	if (UCROSSxSdkSubsystem* Sdk = ResolveSdk())
	{
		const FCROSSxWalletInfo Info = Sdk->GetCachedWalletInfo();
		return Info.Sub.IsEmpty() ? Info.UserId : Info.Sub;
	}
	return FString();
}

void UDappTestPanelBase::EnsureWalletSetup(EPendingWalletAction NextAction)
{
	UCROSSxSdkSubsystem* Sdk = ResolveSdk();
	if (!Sdk) { return; }

	// Stash the in-flight intent so HandleCreateWalletResult can retry it
	// once the user finishes the SDK-side migration / create-wallet modal.
	PendingWalletAction = NextAction;

	SetStatus(TEXT("sample.status.creatingWallet"));
	FCROSSxCreateWalletDelegate Del;
	Del.BindDynamic(this, &UDappTestPanelBase::HandleCreateWalletResult);
	Sdk->SetupWalletWithUIAsync(ResolveCachedSub(), Del);
}

void UDappTestPanelBase::RetryPendingWalletAction()
{
	const EPendingWalletAction Action = PendingWalletAction;
	PendingWalletAction = EPendingWalletAction::None;
	switch (Action)
	{
		case EPendingWalletAction::SignPersonalMessage: OnClickSignPersonalMessage(); break;
		case EPendingWalletAction::SignTypedData:       OnClickSignTypedData();       break;
		case EPendingWalletAction::SignTx:              OnClickSignTx();              break;
		case EPendingWalletAction::SendTx:              OnClickSendTx();              break;
		case EPendingWalletAction::GetTokenBalance:     OnClickGetTokenBalance();     break;
		case EPendingWalletAction::SendToken:           OnClickSendToken();           break;
		default: break;
	}
}

FString UDappTestPanelBase::ResolveFromAddress() const
{
	if (const ADappActor* Actor = ResolveActor())
	{
		const FString Selected = Actor->GetSelectedWalletAddress();
		if (!Selected.IsEmpty()) { return Selected; }
	}
	return ReadText(Inp_From);
}

FString UDappTestPanelBase::ResolveChainId() const
{
	FString ChainId = ReadText(Inp_ChainId, (DefaultChainId == TEXT("eip155:612044") || DefaultChainId.IsEmpty()) ? ResolveDefaultChainId() : DefaultChainId);
	return ChainId;
}

int32 UDappTestPanelBase::ResolveTokenDecimals() const
{
	const FString Str = ReadText(Inp_TokenDecimals, DefaultTokenDecimals);
	const int32 Parsed = FCString::Atoi(*Str);
	return (Parsed > 0 && Parsed <= 36) ? Parsed : 18;
}

void UDappTestPanelBase::Notify(FName Key)
{
	UDappNotificationSubsystem* Notif = ResolveNotif();
	UDappLocalizationSubsystem* Loc   = ResolveLoc();
	if (!Notif) { return; }
	const FText Msg = Loc ? Loc->GetText(Key) : FText::FromName(Key);
	Notif->Show(Msg);
}

void UDappTestPanelBase::NotifyArgs(FName Key, const TMap<FString, FString>& Args)
{
	UDappNotificationSubsystem* Notif = ResolveNotif();
	UDappLocalizationSubsystem* Loc   = ResolveLoc();
	if (!Notif) { return; }
	const FText Msg = Loc ? Loc->Format(Key, Args) : FText::FromName(Key);
	Notif->Show(Msg);
}

void UDappTestPanelBase::NotifyError(FName Key, const TMap<FString, FString>& Args)
{
	UDappNotificationSubsystem* Notif = ResolveNotif();
	UDappLocalizationSubsystem* Loc   = ResolveLoc();
	if (!Notif) { return; }
	const FText Msg = Loc ? Loc->Format(Key, Args) : FText::FromName(Key);
	Notif->ShowError(Msg);
}

FString UDappTestPanelBase::ShortenAddress(const FString& Address)
{
	if (Address.Len() <= 12) { return Address; }
	return Address.Left(6) + TEXT("…") + Address.Right(4);
}

FString UDappTestPanelBase::HexToDecimalString(const FString& HexWithOrWithoutPrefix)
{
	return DappErc20Codec::DecodeUint256BalanceHex(HexWithOrWithoutPrefix, 0);
}
