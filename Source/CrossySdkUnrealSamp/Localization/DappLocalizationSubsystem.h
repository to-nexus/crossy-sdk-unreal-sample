#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DappStringRow.h"
#include "DappLocalizationSubsystem.generated.h"

UENUM(BlueprintType)
enum class EDappLang : uint8
{
	KO UMETA(DisplayName = "Korean"),
	EN UMETA(DisplayName = "English")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDappLanguageChanged, EDappLang, NewLanguage);

/**
 * Lightweight runtime-only localization for the sample Dapp.
 *
 * Why a DataTable instead of the Unreal Localization Dashboard?
 *   • External dApp teams only need to edit one asset (DT_DappStrings) to
 *     add/modify entries — no .po files, no Gather/Compile step, no build
 *     pipeline changes.
 *   • Stays isolated from the host project's own localization setup.
 *
 * Mirrors the mental model of Unity sample's DappLocalization
 * (Dictionary<string,string> keyed by dotted strings like "sample.auth.google").
 *
 * The selected language persists in GGameUserSettingsIni under [DappLocalization]
 * so it survives editor PIE sessions and packaged runs.
 */
UCLASS(Config = Game)
class CROSSYSDKUNREALSAMP_API UDappLocalizationSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	UFUNCTION(BlueprintPure, Category = "Dapp|Localization",
		meta = (WorldContext = "WorldContext", DisplayName = "Get Dapp Localization"))
	static UDappLocalizationSubsystem* Get(const UObject* WorldContext);

	UFUNCTION(BlueprintCallable, Category = "Dapp|Localization")
	void SetLanguage(EDappLang InLang);

	UFUNCTION(BlueprintPure, Category = "Dapp|Localization")
	EDappLang GetLanguage() const { return CurrentLang; }

	/** Toggles between KO and EN (only two languages are currently supported). */
	UFUNCTION(BlueprintCallable, Category = "Dapp|Localization")
	void ToggleLanguage();

	/**
	 * Looks up Key in the string table. If missing, returns the key wrapped in
	 * <missing:Key> so it is visually obvious in the UI (matches Unity sample).
	 */
	UFUNCTION(BlueprintPure, Category = "Dapp|Localization")
	FText GetText(FName Key) const;

	/**
	 * GetText + FText::Format with named arguments. Same {name} placeholder
	 * convention as Unity sample's DappLocalization.TByKey.
	 */
	UFUNCTION(BlueprintPure, Category = "Dapp|Localization")
	FText Format(FName Key, const TMap<FString, FString>& Args) const;

	/** Fires whenever SetLanguage / ToggleLanguage changes the active language. */
	UPROPERTY(BlueprintAssignable, Category = "Dapp|Localization")
	FOnDappLanguageChanged OnLanguageChanged;

	/**
	 * DataTable asset holding all FDappStringRow entries. External teams can
	 * swap it from the Game Instance's config or set it at runtime before the
	 * first GetText call.
	 */
	UFUNCTION(BlueprintCallable, Category = "Dapp|Localization")
	void SetStringTable(UDataTable* InTable);

private:
	static constexpr const TCHAR* PrefsSection = TEXT("DappLocalization");
	static constexpr const TCHAR* PrefsKeyLang = TEXT("Lang");

	void LoadPersistedLanguage();
	void PersistLanguage() const;
	void ResolveDefaultTableIfNeeded();

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> StringTable = nullptr;

	UPROPERTY(Transient)
	EDappLang CurrentLang = EDappLang::KO;
};
