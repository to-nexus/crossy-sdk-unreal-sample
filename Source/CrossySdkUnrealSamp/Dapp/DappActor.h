#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Core/Types/CROSSxSdkConfig.h"
#include "Core/Types/CROSSxSdkTypes.h"
#include "DappActor.generated.h"

class UCROSSxSdkSubsystem;
class UCROSSxWebkitSdkSubsystem;
class UDappLocalizationSubsystem;
class UDappNotificationSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDappSdkReady, bool, bHasActiveSession);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDappSdkSessionExpired);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDappSdkAuthChanged, bool, bLoggedIn);

/**
 * ADappActor — the "Dapp" bootstrap Actor for the sample project.
 *
 * Analog of Unity sample's `Dapp.Awake() / Dapp.Start()` methods. Dropping a
 * single instance of this actor into the startup map is enough for the whole
 * sample app to come alive:
 *   1) Resolves UCROSSxSdkSettings::GetProjectId() (set via Project Settings).
 *   2) Builds FCROSSxSdkConfig with platform-appropriate AppId, Theme, Locale.
 *   3) Calls CROSSxSdkSubsystem::Configure() + InitializeSdkAsync().
 *   4) Initializes CROSSxWebkitSdkSubsystem with the same ProjectId.
 *   5) Bridges SDK events (sign-in, sign-out, session-expired) to Blueprint
 *      multicast delegates that UDappTestPanelBase listens to.
 *   6) Keeps the SDK locale in sync with UDappLocalizationSubsystem.
 *
 * Intentionally does NOT touch UI. The test panel widget is created and added
 * to the viewport by the Player Controller's HUD/BeginPlay (see P3.8 guide)
 * or by the game-mode's `TestPanelWidgetClass`.
 */
UCLASS(Blueprintable, BlueprintType, meta = (DisplayName = "Dapp Actor"))
class CROSSYSDKUNREALSAMP_API ADappActor : public AActor
{
	GENERATED_BODY()

public:
	ADappActor();

	// ─── Public state (read-only from UI) ───

	UFUNCTION(BlueprintPure, Category = "Dapp") UCROSSxSdkSubsystem*     GetSdk() const;
	UFUNCTION(BlueprintPure, Category = "Dapp") UCROSSxWebkitSdkSubsystem* GetWebkitSdk() const;

	UFUNCTION(BlueprintPure, Category = "Dapp")
	bool IsSdkInitialized() const;

	UFUNCTION(BlueprintPure, Category = "Dapp")
	bool IsLoggedIn() const;

	UFUNCTION(BlueprintPure, Category = "Dapp")
	FString GetCurrentUserId() const { return CurrentUserId; }

	UFUNCTION(BlueprintPure, Category = "Dapp")
	FString GetSelectedWalletAddress() const { return SelectedWalletAddress; }

	UFUNCTION(BlueprintCallable, Category = "Dapp")
	void SetSelectedWallet(const FString& Address, int32 Index);

	UFUNCTION(BlueprintCallable, Category = "Dapp")
	void ClearSelectedWallet();

	/**
	 * Looks up ADappActor in the current world. Intended to be called from
	 * UUserWidget subclasses via `GetOwningPlayer()->GetWorld()`.
	 * Returns nullptr if no Dapp actor was placed in the level.
	 */
	UFUNCTION(BlueprintPure, Category = "Dapp", meta = (WorldContext = "WorldContext"))
	static ADappActor* Find(const UObject* WorldContext);

	// ─── Events for the UI layer ───

	UPROPERTY(BlueprintAssignable, Category = "Dapp|Events")
	FOnDappSdkReady OnSdkReady;

	UPROPERTY(BlueprintAssignable, Category = "Dapp|Events")
	FOnDappSdkAuthChanged OnAuthChanged;

	UPROPERTY(BlueprintAssignable, Category = "Dapp|Events")
	FOnDappSdkSessionExpired OnSessionExpired;

	// ─── Tweakables (editable on the actor in the level) ───

	/** "CROSSx Unreal Sample" by default. Shown in SDK modals. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dapp|Config")
	FString AppName = TEXT("CROSSx Unreal Sample");

	/** CAIP-2 chainId used by default for test transactions. Defaults to CROSS testnet. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dapp|Config")
	FString DefaultChainId = TEXT("eip155:612044");

	/** When true, SDK debug logs are enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dapp|Config")
	bool bEnableDebugLogs = true;

	/**
	 * Optional override for FCROSSxSdkConfig::AppId. When empty, the actor
	 * derives a platform-specific AppId from the project name, matching the
	 * Unity sample convention (.android / .ios / .windows).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dapp|Config")
	FString AppIdOverride;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION() void HandleSignInComplete(const FCROSSxAuthResult& Result);
	UFUNCTION() void HandleSignOutComplete();
	UFUNCTION() void HandleSessionExpired();
	UFUNCTION() void HandleSdkInitialized(const FCROSSxAuthResult& Result);
	UFUNCTION() void HandleLanguageChanged(EDappLang NewLanguage);

	void BuildSdkConfig(FCROSSxSdkConfig& OutConfig) const;
	FString ResolveAppId() const;
	FString LocaleCodeForLanguage(EDappLang Lang) const;

	UCROSSxSdkSubsystem*        ResolveSdk() const;
	UCROSSxWebkitSdkSubsystem*    ResolveWebkitSdk() const;
	UDappLocalizationSubsystem* ResolveLocalization() const;
	UDappNotificationSubsystem* ResolveNotifications() const;

	UPROPERTY(Transient) FString CurrentUserId;
	UPROPERTY(Transient) FString SelectedWalletAddress;
	UPROPERTY(Transient) int32   SelectedWalletIndex = INDEX_NONE;
};
