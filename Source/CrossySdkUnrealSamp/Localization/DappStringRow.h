#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "DappStringRow.generated.h"

/**
 * A single localized string entry used by UDappLocalizationSubsystem.
 *
 * The DataTable's RowName is the lookup key (e.g. "sample.auth.google").
 * KO/EN are authored directly in the DataTable editor. Adding a new
 * supported language is a two-step operation:
 *   1) add a new FText UPROPERTY here (e.g. JA)
 *   2) extend EDappLang + UDappLocalizationSubsystem::ResolveText()
 *
 * No Localization Dashboard is used; FText values are stored verbatim,
 * which keeps the host project free of any .po/Gather pipeline work.
 */
USTRUCT(BlueprintType)
struct FDappStringRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dapp|Localization")
	FText KO;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dapp|Localization")
	FText EN;
};
