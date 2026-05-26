#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/DappNotificationSubsystem.h"
#include "DappNotificationHostWidget.generated.h"

class UVerticalBox;
class UWidget;

/**
 * UDappNotificationHostWidget — minimal on-screen toast renderer for the
 * sample. Subscribes to `UDappNotificationSubsystem::OnNotification` and
 * stacks short-lived toast cards near the bottom of the viewport.
 *
 * Two integration modes:
 *
 *   1) Zero-config (the sample default). Instantiate this class directly
 *      (no WBP needed). On first construction the widget builds a small
 *      Overlay + VerticalBox tree programmatically so toasts work out of
 *      the box on mobile and desktop alike.
 *
 *   2) Custom presentation. Subclass in a WBP and bind a
 *      `UVerticalBox` named `Box_Toasts` via BindWidgetOptional. The C++
 *      base class will skip the auto-built layout and use your container
 *      instead. Color / duration / max-toast properties remain editable
 *      in the Class Defaults panel.
 *
 * The whole widget is hit-test invisible so it never intercepts input
 * destined for the dApp test panel underneath it.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Dapp Notification Host"))
class CROSSYSDKUNREALSAMP_API UDappNotificationHostWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UDappNotificationHostWidget(const FObjectInitializer& ObjectInitializer);

	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

protected:
	/** Optional WBP-bound container. Leave null in C++-only setups; the base
	 *  class will build a default Overlay/VerticalBox tree at initialization. */
	UPROPERTY(meta = (BindWidgetOptional))
	UVerticalBox* Box_Toasts = nullptr;

	/** Fallback duration when the broadcast payload doesn't carry one. Sized
	 *  generously since the CROSS Pay result toast is multi-line and users
	 *  often want to read it before it disappears. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dapp|Notification")
	float DefaultDurationSeconds = 6.f;

	/** Hard cap on simultaneously visible toasts. Oldest evicted first. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dapp|Notification",
		meta = (ClampMin = "1"))
	int32 MaxVisibleToasts = 5;

	/** Padding (px) applied to each toast card's content. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dapp|Notification")
	FMargin ToastPadding = FMargin(16.f, 12.f);

	/** Vertical gap (px) between consecutive toasts. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dapp|Notification",
		meta = (ClampMin = "0"))
	float ToastSpacing = 6.f;

	/** Font size used by the auto-built UTextBlock. WBP-overridden hosts ignore. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dapp|Notification",
		meta = (ClampMin = "8"))
	int32 ToastFontSize = 14;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dapp|Notification|Colors")
	FLinearColor ColorInfo    = FLinearColor(0.10f, 0.13f, 0.18f, 0.92f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dapp|Notification|Colors")
	FLinearColor ColorSuccess = FLinearColor(0.12f, 0.42f, 0.22f, 0.92f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dapp|Notification|Colors")
	FLinearColor ColorWarning = FLinearColor(0.55f, 0.40f, 0.10f, 0.92f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dapp|Notification|Colors")
	FLinearColor ColorError   = FLinearColor(0.55f, 0.18f, 0.18f, 0.92f);

	UFUNCTION()
	void HandleNotification(const FDappNotification& Notification);

private:
	void BuildAutoLayoutIfNeeded();
	void SpawnToast(const FDappNotification& Notification);
	void EvictOldestIfNeeded();
	void EnsureSubscribed();
	FLinearColor ResolveSeverityColor(EDappNotificationSeverity Severity) const;
	void StartAutoDismissTimer(UWidget* ToastWidget, float DurationSeconds);

	UPROPERTY(Transient)
	bool bSubscribed = false;

	UPROPERTY(Transient)
	bool bMissingContainerWarned = false;
};
