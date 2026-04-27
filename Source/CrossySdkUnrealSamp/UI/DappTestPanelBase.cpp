#include "UI/DappTestPanelBase.h"

#include "Components/Button.h"
#include "Components/EditableTextBox.h"
#include "Components/MultiLineEditableTextBox.h"
#include "Components/PanelWidget.h"
#include "Components/TextBlock.h"
#include "Kismet/KismetSystemLibrary.h"

#include "Dapp/DappActor.h"
#include "UI/DappErc20Codec.h"
#include "UI/DappNotificationSubsystem.h"
#include "Localization/DappLocalizationSubsystem.h"

#include "SDK/CROSSxSdkSubsystem.h"
#include "CROSSxRampSdkSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogDappPanel, Log, All);

// ─────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────

void UDappTestPanelBase::NativeConstruct()
{
	Super::NativeConstruct();

	BindAllClickHandlers();

	// Pre-fill text boxes with sensible defaults so the first-time user can
	// hit any button without typing anything. All values are harmless on
	// CROSS testnet.
	WriteText(Inp_ChainId,        DefaultChainId);
	WriteText(Inp_Value,          DefaultTxValueWei);
	WriteText(Inp_SignMessage,    DefaultSignMessage);
	WriteText(Inp_TokenDecimals,  DefaultTokenDecimals);

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
	}

	RefreshLocalizedLabels();
	ApplyLoginState(bLastKnownLoggedIn);
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
	BIND_CLICK(Btn_UseRamp,                OnClickUseRamp);
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
		NotifyError(TEXT("sample.auth.failed"),
			{ { TEXT("message"), Result.ErrorMessage.IsEmpty() ? TEXT("unknown") : Result.ErrorMessage } });
		SetStatus(TEXT("sample.auth.failed"));
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
	UCROSSxSdkSubsystem* Sdk = ResolveSdk();
	if (!Sdk) { return; }
	SetStatus(TEXT("sample.status.creatingWallet"));
	FCROSSxCreateWalletDelegate Del;
	Del.BindDynamic(this, &UDappTestPanelBase::HandleCreateWalletResult);

	// SetupWalletWithUIAsync uses the SDK's own PIN modal flow — no custom UI
	// needed here. Passing empty Sub makes the SDK fall back to the cached
	// user sub from the last sign-in.
	Sdk->SetupWalletWithUIAsync(FString(), Del);
}

void UDappTestPanelBase::HandleCreateWalletResult(const FCROSSxCreateWalletResult& Result)
{
	if (!Result.ErrorMessage.IsEmpty())
	{
		NotifyError(TEXT("sample.wallet.createFailed"), { { TEXT("message"), Result.ErrorMessage } });
		SetStatus(TEXT("sample.wallet.createFailed"));
		return;
	}

	if (ADappActor* Actor = ResolveActor())
	{
		Actor->SetSelectedWallet(Result.Address, 0);
	}
	WriteLabel(Txt_WalletAddress, FText::FromString(Result.Address));
	WriteText(Inp_From,           Result.Address);
	NotifyArgs(TEXT("sample.wallet.createSuccess"),
		{ { TEXT("address"), ShortenAddress(Result.Address) } });
	SetStatus(TEXT("sample.wallet.createSuccess"));
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
		NotifyError(TEXT("sample.address.fetchFailed"), { { TEXT("message"), Result.ErrorMessage } });
		return;
	}
	if (ADappActor* Actor = ResolveActor())
	{
		Actor->SetSelectedWallet(Result.Address, Result.Index);
	}
	WriteLabel(Txt_WalletAddress, FText::FromString(Result.Address));
	WriteText(Inp_From,           Result.Address);
	NotifyArgs(TEXT("sample.address.fetched"),
		{ { TEXT("address"), ShortenAddress(Result.Address) } });
	SetStatus(TEXT("sample.address.fetched"));
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
		NotifyError(TEXT("sample.address.listFailed"), { { TEXT("message"), Result.ErrorMessage } });
		return;
	}

	FString Summary;
	for (const FCROSSxAddressInfo& Info : Result.Addresses)
	{
		Summary += FString::Printf(TEXT("[%d] %s\n"), Info.Index, *Info.Address);
	}
	UE_LOG(LogDappPanel, Log, TEXT("Addresses:\n%s"), *Summary);

	NotifyArgs(TEXT("sample.address.listOk"),
		{ { TEXT("count"), FString::FromInt(Result.Addresses.Num()) } });
	SetStatus(TEXT("sample.address.listOk"));
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
	if (From.IsEmpty()) { NotifyError(TEXT("sample.tx.noWallet"), {}); return; }

	SetStatus(TEXT("sample.status.loading"));
	FCROSSxStringDelegate Del;
	Del.BindDynamic(this, &UDappTestPanelBase::HandleNativeBalanceResult);
	Sdk->GetBalanceAsync(From, ResolveChainId(), Del, TEXT("latest"));
}

void UDappTestPanelBase::HandleNativeBalanceResult(const FString& HexBalance)
{
	const FString Dec = DappErc20Codec::DecodeUint256BalanceHex(HexBalance, 18);
	NotifyArgs(TEXT("sample.tx.nativeBalance"), { { TEXT("amount"), Dec } });
	SetStatus(TEXT("sample.tx.nativeBalance"));
}

void UDappTestPanelBase::OnClickSignTx()
{
	UCROSSxSdkSubsystem* Sdk = ResolveSdk();
	if (!Sdk) { return; }

	FCROSSxUnsignedTx Tx;
	Tx.ChainId = ResolveChainId();
	Tx.From    = ResolveFromAddress();
	Tx.To      = ReadText(Inp_To);
	Tx.Value   = ReadText(Inp_Value, DefaultTxValueWei);
	Tx.Data    = ReadText(Inp_Data);

	if (Tx.From.IsEmpty() || Tx.To.IsEmpty())
	{
		NotifyError(TEXT("sample.tx.invalidInput"), {});
		return;
	}

	SetStatus(TEXT("sample.status.signing"));
	FCROSSxSignTxDelegate Del;
	Del.BindDynamic(this, &UDappTestPanelBase::HandleSignTxResult);
	Sdk->SignTransactionWithUIAsync(Tx, Tx.ChainId, Del);
}

void UDappTestPanelBase::HandleSignTxResult(const FCROSSxSignTxResponse& Result)
{
	if (!Result.ErrorMessage.IsEmpty())
	{
		NotifyError(TEXT("sample.tx.signFailed"), { { TEXT("message"), Result.ErrorMessage } });
		return;
	}
	NotifyArgs(TEXT("sample.tx.signOk"),
		{ { TEXT("hash"), Result.TxHash.IsEmpty() ? TEXT("(local)") : Result.TxHash } });
	SetStatus(TEXT("sample.tx.signOk"));
}

void UDappTestPanelBase::OnClickSendTx()
{
	UCROSSxSdkSubsystem* Sdk = ResolveSdk();
	if (!Sdk) { return; }

	FCROSSxUnsignedTx Tx;
	Tx.ChainId = ResolveChainId();
	Tx.From    = ResolveFromAddress();
	Tx.To      = ReadText(Inp_To);
	Tx.Value   = ReadText(Inp_Value, DefaultTxValueWei);
	Tx.Data    = ReadText(Inp_Data);

	if (Tx.From.IsEmpty() || Tx.To.IsEmpty())
	{
		NotifyError(TEXT("sample.tx.invalidInput"), {});
		return;
	}

	SetStatus(TEXT("sample.status.sending"));
	FCROSSxSendTxDelegate Del;
	Del.BindDynamic(this, &UDappTestPanelBase::HandleSendTxResult);
	Sdk->SendTransactionWithUIAsync(Tx, Tx.ChainId, Del);
}

void UDappTestPanelBase::HandleSendTxResult(const FCROSSxSendTxResponse& Result)
{
	if (!Result.ErrorMessage.IsEmpty())
	{
		NotifyError(TEXT("sample.tx.sendFailed"), { { TEXT("message"), Result.ErrorMessage } });
		return;
	}
	NotifyArgs(TEXT("sample.tx.sendOk"), { { TEXT("hash"), Result.TxHash } });
	SetStatus(TEXT("sample.tx.sendOk"));
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
	if (From.IsEmpty() || Contract.IsEmpty())
	{
		NotifyError(TEXT("sample.token.invalidInput"), {});
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

	NotifyArgs(TEXT("sample.token.balance"), { { TEXT("amount"), Dec } });
	SetStatus(TEXT("sample.token.balance"));
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

	if (From.IsEmpty() || Contract.IsEmpty() || To.IsEmpty() || Amount.IsEmpty())
	{
		NotifyError(TEXT("sample.token.invalidInput"), {});
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
	Sdk->SendTransactionWithUIAsync(Tx, Tx.ChainId, Del);
}

void UDappTestPanelBase::HandleSendTokenTxResult(const FCROSSxSendTxResponse& Result)
{
	if (!Result.ErrorMessage.IsEmpty())
	{
		NotifyError(TEXT("sample.token.sendFailed"), { { TEXT("message"), Result.ErrorMessage } });
		return;
	}
	NotifyArgs(TEXT("sample.token.sendOk"), { { TEXT("hash"), Result.TxHash } });
	SetStatus(TEXT("sample.token.sendOk"));
}

// ─────────────────────────────────────────────────────────────────────
// Message signing section
// ─────────────────────────────────────────────────────────────────────

void UDappTestPanelBase::OnClickSignPersonalMessage()
{
	UCROSSxSdkSubsystem* Sdk = ResolveSdk();
	if (!Sdk) { return; }
	const FString From = ResolveFromAddress();
	if (From.IsEmpty()) { NotifyError(TEXT("sample.tx.noWallet"), {}); return; }

	const FString Message = ReadText(Inp_SignMessage, DefaultSignMessage);
	SetStatus(TEXT("sample.status.signing"));
	FCROSSxSignMessageDelegate Del;
	Del.BindDynamic(this, &UDappTestPanelBase::HandleSignMessageResult);
	Sdk->SignMessageWithUIAsync(Message, ResolveChainId(), From, Del);
}

void UDappTestPanelBase::HandleSignMessageResult(const FCROSSxSignMessageResponse& Result)
{
	if (!Result.ErrorMessage.IsEmpty())
	{
		NotifyError(TEXT("sample.message.signFailed"), { { TEXT("message"), Result.ErrorMessage } });
		return;
	}
	UE_LOG(LogDappPanel, Log, TEXT("Personal signature: %s"), *Result.Signature);
	NotifyArgs(TEXT("sample.message.signOk"),
		{ { TEXT("sig"), Result.Signature.Len() > 16 ? Result.Signature.Left(16) + TEXT("…") : Result.Signature } });
	SetStatus(TEXT("sample.message.signOk"));
}

void UDappTestPanelBase::OnClickSignTypedData()
{
	UCROSSxSdkSubsystem* Sdk = ResolveSdk();
	if (!Sdk) { return; }
	const FString From = ResolveFromAddress();
	if (From.IsEmpty()) { NotifyError(TEXT("sample.tx.noWallet"), {}); return; }

	const FString TypedJson = ReadText(Inp_SignTypedData, DefaultTypedDataJson);
	if (TypedJson.TrimStartAndEnd().IsEmpty())
	{
		NotifyError(TEXT("sample.message.invalidTypedData"), {});
		return;
	}

	SetStatus(TEXT("sample.status.signing"));
	FCROSSxSignTypedDataDelegate Del;
	Del.BindDynamic(this, &UDappTestPanelBase::HandleSignTypedDataResult);
	Sdk->SignTypedDataWithUIAsync(From, TypedJson, ResolveChainId(), Del);
}

void UDappTestPanelBase::HandleSignTypedDataResult(const FCROSSxSignTypedDataResponse& Result)
{
	if (!Result.ErrorMessage.IsEmpty())
	{
		NotifyError(TEXT("sample.message.typedFailed"), { { TEXT("message"), Result.ErrorMessage } });
		return;
	}
	UE_LOG(LogDappPanel, Log, TEXT("Typed-data signature: %s"), *Result.Signature);
	NotifyArgs(TEXT("sample.message.typedOk"),
		{ { TEXT("sig"), Result.Signature.Len() > 16 ? Result.Signature.Left(16) + TEXT("…") : Result.Signature } });
	SetStatus(TEXT("sample.message.typedOk"));
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
	// FCROSSxRefreshTokenResult's exact shape is SDK-defined; we rely on its
	// string representation for logging and a binary success for the user.
	if (Result.ErrorMessage.IsEmpty())
	{
		Notify(TEXT("sample.session.refreshOk"));
		SetStatus(TEXT("sample.session.refreshOk"));
	}
	else
	{
		NotifyError(TEXT("sample.session.refreshFailed"),
			{ { TEXT("message"), Result.ErrorMessage } });
	}
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
	NotifyArgs(TEXT("sample.common.userInfoOk"),
		{ { TEXT("userId"), Info.Id },
		  { TEXT("email"),  Info.Email },
		  { TEXT("type"),   Info.LoginType } });
	SetStatus(TEXT("sample.common.userInfoOk"));
}

// ─────────────────────────────────────────────────────────────────────
// Ramp section
// ─────────────────────────────────────────────────────────────────────

void UDappTestPanelBase::OnClickUseRamp()
{
	UCROSSxRampSdkSubsystem* Ramp = ResolveRampSdk();
	if (!Ramp || !Ramp->IsInitialized())
	{
		NotifyError(TEXT("sample.ramp.notReady"), {});
		return;
	}

	const FString Url = ReadText(Inp_RampUrl);
	if (Url.IsEmpty())
	{
		NotifyError(TEXT("sample.ramp.noUrl"), {});
		return;
	}

	SetStatus(TEXT("sample.status.openingRamp"));

	TWeakObjectPtr<UDappTestPanelBase> Weak(this);
	Ramp->OpenRamp(Url, FOnRampComplete::CreateLambda(
		[Weak](FCROSSxRampResult Result)
		{
			if (UDappTestPanelBase* Self = Weak.Get())
			{
				if (Result.bSuccess)
				{
					Self->Notify(TEXT("sample.ramp.closed"));
				}
				else
				{
					Self->NotifyArgs(TEXT("sample.ramp.failed"),
						{ { TEXT("message"), Result.ErrorMessage } });
				}
			}
		}));
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
	RefreshLocalizedLabels();
	if (Txt_Language)
	{
		Txt_Language->SetText(FText::FromString(NewLanguage == EDappLang::EN ? TEXT("EN") : TEXT("KO")));
	}
}

void UDappTestPanelBase::OnClickEditorSimulateDeepLink()
{
#if WITH_EDITOR
	// The SDK auto-registers crossx-{ProjectId} / ramp-{ProjectId} schemes on
	// mobile. Desktop editors do not receive real deep links, so this button
	// is a placeholder for a per-dApp simulation — external teams typically
	// replace its body with something that matches their OAuth mock server.
	Notify(TEXT("sample.editor.deepLinkNoop"));
#endif
}

void UDappTestPanelBase::HandleSdkReady(bool bHasActiveSession)
{
	ApplyLoginState(bHasActiveSession);
	Notify(bHasActiveSession ? TEXT("sample.status.resumedSession")
	                         : TEXT("sample.status.sdkReady"));
}

void UDappTestPanelBase::HandleAuthChanged(bool bLoggedIn)
{
	ApplyLoginState(bLoggedIn);
}

void UDappTestPanelBase::RefreshLocalizedLabels()
{
	// Bound widgets' labels are authored in the WBP. External teams can either:
	//   (a) leave the BP text static and only localize via Txt_* display blocks, or
	//   (b) override this function in a BP subclass to push localized text into
	//       specific buttons.
	// Intentionally no-op in C++ to avoid dictating WBP text-binding choices.
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

UCROSSxRampSdkSubsystem* UDappTestPanelBase::ResolveRampSdk() const
{
	if (const ADappActor* Actor = ResolveActor())
	{
		return Actor->GetRampSdk();
	}
	if (const UWorld* World = GetWorld())
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			return GI->GetSubsystem<UCROSSxRampSdkSubsystem>();
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

void UDappTestPanelBase::WriteLabel(UTextBlock* Block, const FText& Text)
{
	if (Block) { Block->SetText(Text); }
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

void UDappTestPanelBase::SetStatusText(const FText& Text)
{
	WriteLabel(Txt_Status, Text);
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
	FString ChainId = ReadText(Inp_ChainId, DefaultChainId);
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
