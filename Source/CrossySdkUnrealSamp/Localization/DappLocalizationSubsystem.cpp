#include "Localization/DappLocalizationSubsystem.h"

#include "Engine/DataTable.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/PlatformFileManager.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Misc/ConfigCacheIni.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// Matches the asset that ships with the sample (see P3.8 content guide).
	// External teams can override via SetStringTable() before first use.
	constexpr const TCHAR* DefaultStringTablePath =
		TEXT("/Game/Localization/DT_DappStrings.DT_DappStrings");

	const FText& ResolveText(const FDappStringRow& Row, EDappLang Lang)
	{
		switch (Lang)
		{
		case EDappLang::EN: return Row.EN;
		case EDappLang::KO:
		default:            return Row.KO;
		}
	}
}

UDappLocalizationSubsystem* UDappLocalizationSubsystem::Get(const UObject* WorldContext)
{
	if (!WorldContext) { return nullptr; }
	if (const UWorld* World = WorldContext->GetWorld())
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			return GI->GetSubsystem<UDappLocalizationSubsystem>();
		}
	}
	return nullptr;
}

void UDappLocalizationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadPersistedLanguage();
	ResolveDefaultTableIfNeeded();
}

void UDappLocalizationSubsystem::SetLanguage(EDappLang InLang)
{
	if (InLang == CurrentLang) { return; }
	CurrentLang = InLang;
	PersistLanguage();
	OnLanguageChanged.Broadcast(CurrentLang);
}

void UDappLocalizationSubsystem::ToggleLanguage()
{
	SetLanguage(CurrentLang == EDappLang::KO ? EDappLang::EN : EDappLang::KO);
}

void UDappLocalizationSubsystem::SetStringTable(UDataTable* InTable)
{
	StringTable = InTable;
}

FText UDappLocalizationSubsystem::GetText(FName Key) const
{
	ResolveDefaultTableIfNeeded();

	if (StringTable)
	{
		if (const FDappStringRow* Row = StringTable->FindRow<FDappStringRow>(Key, TEXT("UDappLocalizationSubsystem::GetText"), /*bWarnIfRowMissing=*/false))
		{
			const FText& Text = ResolveText(*Row, CurrentLang);
			if (!Text.IsEmpty())
			{
				return Text;
			}
		}
	}

	return FText::FromString(FString::Printf(TEXT("<missing:%s>"), *Key.ToString()));
}

FText UDappLocalizationSubsystem::Format(FName Key, const TMap<FString, FString>& Args) const
{
	FFormatNamedArguments FormatArgs;
	FormatArgs.Reserve(Args.Num());
	for (const TPair<FString, FString>& Pair : Args)
	{
		FormatArgs.Add(Pair.Key, FText::FromString(Pair.Value));
	}
	return FText::Format(GetText(Key), FormatArgs);
}

void UDappLocalizationSubsystem::LoadPersistedLanguage()
{
	FString Saved;
	if (GConfig && GConfig->GetString(PrefsSection, PrefsKeyLang, Saved, GGameUserSettingsIni))
	{
		if (Saved.Equals(TEXT("EN"), ESearchCase::IgnoreCase))
		{
			CurrentLang = EDappLang::EN;
			return;
		}
		if (Saved.Equals(TEXT("KO"), ESearchCase::IgnoreCase))
		{
			CurrentLang = EDappLang::KO;
			return;
		}
	}

	// Fall back to engine culture on first launch, defaulting to Korean.
	const FString Culture = FInternationalization::Get().GetCurrentLanguage()->GetName();
	CurrentLang = Culture.StartsWith(TEXT("en")) ? EDappLang::EN : EDappLang::KO;
}

void UDappLocalizationSubsystem::PersistLanguage() const
{
	if (!GConfig) { return; }
	const TCHAR* Value = (CurrentLang == EDappLang::EN) ? TEXT("EN") : TEXT("KO");
	GConfig->SetString(PrefsSection, PrefsKeyLang, Value, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

void UDappLocalizationSubsystem::ResolveDefaultTableIfNeeded() const
{
	if (StringTable) { return; }

	// LoadObject is fine from Subsystem Initialize / first GetText; the content
	// asset is created via the editor (see P3.9 DT_DappStrings.csv import step).
	UDataTable* Loaded = LoadObject<UDataTable>(nullptr, DefaultStringTablePath);
	if (Loaded)
	{
		StringTable = Loaded;
	}
}
