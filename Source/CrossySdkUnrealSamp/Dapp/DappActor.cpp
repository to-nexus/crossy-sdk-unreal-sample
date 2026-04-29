#include "Dapp/DappActor.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/App.h"

#include "SDK/CROSSxSdkSubsystem.h"
#include "Core/Types/CROSSxSdkSettings.h"
#include "CROSSxRampSdkSubsystem.h"
#include "CROSSxRampTypes.h"

#include "Localization/DappLocalizationSubsystem.h"
#include "UI/DappNotificationSubsystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogDappActor, Log, All);

ADappActor::ADappActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
}

ADappActor* ADappActor::Find(const UObject* WorldContext)
{
	if (!WorldContext) { return nullptr; }
	const UWorld* World = WorldContext->GetWorld();
	if (!World) { return nullptr; }
	for (TActorIterator<ADappActor> It(const_cast<UWorld*>(World)); It; ++It)
	{
		return *It;
	}
	return nullptr;
}

UCROSSxSdkSubsystem* ADappActor::GetSdk() const           { return ResolveSdk(); }
UCROSSxRampSdkSubsystem* ADappActor::GetRampSdk() const   { return ResolveRampSdk(); }

bool ADappActor::IsSdkInitialized() const
{
	UCROSSxSdkSubsystem* Sdk = ResolveSdk();
	return Sdk && Sdk->IsInitialized();
}

bool ADappActor::IsLoggedIn() const
{
	UCROSSxSdkSubsystem* Sdk = ResolveSdk();
	return Sdk && Sdk->IsLoggedIn();
}

void ADappActor::SetSelectedWallet(const FString& Address, int32 Index)
{
	if (Address.IsEmpty()) { return; }
	SelectedWalletAddress = Address;
	SelectedWalletIndex   = Index;
}

void ADappActor::ClearSelectedWallet()
{
	SelectedWalletAddress.Reset();
	SelectedWalletIndex = INDEX_NONE;
}

void ADappActor::BeginPlay()
{
	Super::BeginPlay();

	UCROSSxSdkSubsystem* Sdk = ResolveSdk();
	UCROSSxRampSdkSubsystem* Ramp = ResolveRampSdk();

	if (!Sdk)
	{
		UE_LOG(LogDappActor, Error, TEXT("CROSSxSdkSubsystem is unavailable — is the plugin enabled?"));
		return;
	}

	// Build + validate config.
	FCROSSxSdkConfig Config;
	BuildSdkConfig(Config);

	FString ValidationError;
	if (!Config.Validate(ValidationError))
	{
		UE_LOG(LogDappActor, Warning, TEXT("CROSSx SDK config invalid: %s"), *ValidationError);
		if (UDappNotificationSubsystem* Notif = ResolveNotifications())
		{
			Notif->ShowError(FText::Format(
				NSLOCTEXT("Dapp", "SdkConfigInvalid", "CROSSx SDK config invalid: {0}"),
				FText::FromString(ValidationError)));
		}
		// We still subscribe to events in case the dApp fixes the config later.
	}

	Sdk->Configure(Config);

	// Enable the SDK-provided "review-and-confirm" sheet for personal_sign /
	// signTypedData / sign+send transaction flows. The third arg keeps the dApp
	// theme as-is (no override). Beyond UX, this also activates the SDK's
	// invalid-password retry path so a wrong PIN automatically re-prompts via
	// the verify-PIN modal (mirrors the in-SDK dApp test panel).
	Sdk->EnableSignConfirmation(Config.AppName, Config.Theme, false);

	// SDK locale mirrors UI locale. Initialize from the currently active Dapp
	// language so the very first SDK modal (e.g. sign-in) is already localized.
	if (UDappLocalizationSubsystem* Loc = ResolveLocalization())
	{
		Sdk->SetLocale(LocaleCodeForLanguage(Loc->GetLanguage()));
		Loc->OnLanguageChanged.AddDynamic(this, &ADappActor::HandleLanguageChanged);
	}

	// Event wiring (Unity counterpart: event + async callbacks in Dapp.cs).
	Sdk->OnSignInComplete.AddDynamic(this,  &ADappActor::HandleSignInComplete);
	Sdk->OnSignOutComplete.AddDynamic(this, &ADappActor::HandleSignOutComplete);
	Sdk->OnSessionExpired.AddDynamic(this,  &ADappActor::HandleSessionExpired);

	// Kick off async initialize — may pick up a cached session and auto-login.
	FCROSSxAuthResultDelegate InitDelegate;
	InitDelegate.BindDynamic(this, &ADappActor::HandleSdkInitialized);
	Sdk->InitializeSdkAsync(InitDelegate);

	// Ramp is a simple companion SDK — InitRamp just stages browser adapter +
	// freezes the ProjectId. Opening the ramp page is a dApp-call later.
	if (Ramp)
	{
		FCROSSxRampConfig RampCfg;
		RampCfg.ProjectId = Config.ProjectId;
		RampCfg.bDebug    = bEnableDebugLogs;
		Ramp->InitRamp(RampCfg);
	}
}

void ADappActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UCROSSxSdkSubsystem* Sdk = ResolveSdk())
	{
		Sdk->OnSignInComplete.RemoveDynamic(this,  &ADappActor::HandleSignInComplete);
		Sdk->OnSignOutComplete.RemoveDynamic(this, &ADappActor::HandleSignOutComplete);
		Sdk->OnSessionExpired.RemoveDynamic(this,  &ADappActor::HandleSessionExpired);
	}
	if (UDappLocalizationSubsystem* Loc = ResolveLocalization())
	{
		Loc->OnLanguageChanged.RemoveDynamic(this, &ADappActor::HandleLanguageChanged);
	}
	Super::EndPlay(EndPlayReason);
}

// ─── SDK event handlers ────────────────────────────────────────────────

void ADappActor::HandleSignInComplete(const FCROSSxAuthResult& Result)
{
	if (Result.bSuccess)
	{
		CurrentUserId = Result.User.Id;
		if (!Result.WalletAddress.IsEmpty())
		{
			SetSelectedWallet(Result.WalletAddress, 0);
		}
	}
	OnAuthChanged.Broadcast(Result.bSuccess);
}

void ADappActor::HandleSignOutComplete()
{
	CurrentUserId.Reset();
	ClearSelectedWallet();
	OnAuthChanged.Broadcast(false);
}

void ADappActor::HandleSessionExpired()
{
	// Mirror sign-out UX: clear local state, let the UI prompt for re-login.
	CurrentUserId.Reset();
	ClearSelectedWallet();
	OnSessionExpired.Broadcast();
	OnAuthChanged.Broadcast(false);

	if (UDappNotificationSubsystem* Notif = ResolveNotifications())
	{
		if (UDappLocalizationSubsystem* Loc = ResolveLocalization())
		{
			Notif->ShowWarning(Loc->GetText(TEXT("sample.session.expired")));
		}
		else
		{
			Notif->ShowWarning(NSLOCTEXT("Dapp", "SessionExpired", "Session expired. Please sign in again."));
		}
	}
}

void ADappActor::HandleSdkInitialized(const FCROSSxAuthResult& Result)
{
	const bool bHasSession = Result.bSuccess;
	UE_LOG(LogDappActor, Log, TEXT("CROSSx SDK initialized. HasSession=%d"),
		bHasSession ? 1 : 0);

	if (bHasSession)
	{
		CurrentUserId = Result.User.Id;
		if (!Result.WalletAddress.IsEmpty())
		{
			SetSelectedWallet(Result.WalletAddress, 0);
		}
	}

	OnSdkReady.Broadcast(bHasSession);
}

void ADappActor::HandleLanguageChanged(EDappLang NewLanguage)
{
	if (UCROSSxSdkSubsystem* Sdk = ResolveSdk())
	{
		Sdk->SetLocale(LocaleCodeForLanguage(NewLanguage));
	}
}

// ─── Config builders ───────────────────────────────────────────────────

void ADappActor::BuildSdkConfig(FCROSSxSdkConfig& OutConfig) const
{
	OutConfig.ProjectId       = UCROSSxSdkSettings::GetProjectId();
	OutConfig.AppName         = AppName;
	OutConfig.AppId           = ResolveAppId();
	OutConfig.DefaultChainId  = DefaultChainId;
	OutConfig.bEnableDebugLogs= bEnableDebugLogs;
	OutConfig.Theme           = ECROSSxThemeMode::Dark;
	OutConfig.LoginProvider   = ECROSSxLoginProvider::All;

	if (const UDappLocalizationSubsystem* Loc = ResolveLocalization())
	{
		OutConfig.Locale = LocaleCodeForLanguage(Loc->GetLanguage());
	}
}

FString ADappActor::ResolveAppId() const
{
	if (!AppIdOverride.IsEmpty())
	{
		return AppIdOverride;
	}

	// Mirrors the Unity sample's platform split:
	//   Android / iOS / everything else → .windows (console whitelist uses it
	//   as the fallback for desktop + editor).
	const FString Base = TEXT("com.nexus.crossx.sdk.unrealsample");
#if PLATFORM_ANDROID
	return Base + TEXT(".android");
#elif PLATFORM_IOS
	return Base + TEXT(".ios");
#else
	return Base + TEXT(".windows");
#endif
}

FString ADappActor::LocaleCodeForLanguage(EDappLang Lang) const
{
	switch (Lang)
	{
	case EDappLang::EN: return TEXT("en");
	case EDappLang::KO:
	default:            return TEXT("ko");
	}
}

// ─── Subsystem resolvers ───────────────────────────────────────────────

UCROSSxSdkSubsystem* ADappActor::ResolveSdk() const
{
	if (const UWorld* World = GetWorld())
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			return GI->GetSubsystem<UCROSSxSdkSubsystem>();
		}
	}
	return nullptr;
}

UCROSSxRampSdkSubsystem* ADappActor::ResolveRampSdk() const
{
	if (const UWorld* World = GetWorld())
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			return GI->GetSubsystem<UCROSSxRampSdkSubsystem>();
		}
	}
	return nullptr;
}

UDappLocalizationSubsystem* ADappActor::ResolveLocalization() const
{
	return UDappLocalizationSubsystem::Get(this);
}

UDappNotificationSubsystem* ADappActor::ResolveNotifications() const
{
	return UDappNotificationSubsystem::Get(this);
}
