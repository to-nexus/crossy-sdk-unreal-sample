#include "UI/DappNotificationHostWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Engine/World.h"
#include "Fonts/SlateFontInfo.h"
#include "Layout/Margin.h"
#include "Styling/SlateColor.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogDappNotificationHost, Log, All);

UDappNotificationHostWidget::UDappNotificationHostWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// The toast layer must never intercept clicks intended for the test
	// panel (or any other gameplay UI) sitting underneath it.
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);
}

void UDappNotificationHostWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BuildAutoLayoutIfNeeded();
}

void UDappNotificationHostWidget::NativeConstruct()
{
	Super::NativeConstruct();
	EnsureSubscribed();
}

void UDappNotificationHostWidget::NativeDestruct()
{
	if (UDappNotificationSubsystem* Notif = UDappNotificationSubsystem::Get(this))
	{
		Notif->OnNotification.RemoveDynamic(this, &UDappNotificationHostWidget::HandleNotification);
	}
	bSubscribed = false;
	Super::NativeDestruct();
}

void UDappNotificationHostWidget::EnsureSubscribed()
{
	if (bSubscribed) { return; }
	UDappNotificationSubsystem* Notif = UDappNotificationSubsystem::Get(this);
	if (!Notif)
	{
		UE_LOG(LogDappNotificationHost, Warning,
			TEXT("UDappNotificationSubsystem unavailable — toasts will not be shown."));
		return;
	}
	Notif->OnNotification.AddDynamic(this, &UDappNotificationHostWidget::HandleNotification);
	bSubscribed = true;
}

void UDappNotificationHostWidget::BuildAutoLayoutIfNeeded()
{
	if (Box_Toasts) { return; }
	if (!WidgetTree) { return; }

	// Respect any WBP-authored layout that didn't bind Box_Toasts: do not
	// stomp the existing widget tree. We log the misconfiguration lazily
	// in SpawnToast so it only fires when something is actually broadcast.
	if (WidgetTree->RootWidget) { return; }

	UOverlay* RootOverlay = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("Root_NotificationOverlay"));
	WidgetTree->RootWidget = RootOverlay;

	UVerticalBox* Stack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("Box_Toasts"));
	Stack->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	if (UOverlaySlot* OverlaySlot = RootOverlay->AddChildToOverlay(Stack))
	{
		OverlaySlot->SetHorizontalAlignment(HAlign_Fill);
		OverlaySlot->SetVerticalAlignment(VAlign_Bottom);
		// Leave clearance for the mobile gesture / safe-area row at the bottom.
		OverlaySlot->SetPadding(FMargin(16.f, 0.f, 16.f, 40.f));
	}

	Box_Toasts = Stack;
}

void UDappNotificationHostWidget::HandleNotification(const FDappNotification& Notification)
{
	SpawnToast(Notification);
}

void UDappNotificationHostWidget::SpawnToast(const FDappNotification& Notification)
{
	if (!WidgetTree) { return; }

	if (!Box_Toasts)
	{
		BuildAutoLayoutIfNeeded();
		if (!Box_Toasts)
		{
			if (!bMissingContainerWarned)
			{
				UE_LOG(LogDappNotificationHost, Warning,
					TEXT("Notification host has no Box_Toasts container — toasts will be ignored. "
					     "Either leave the WidgetTree empty (so the C++ base class can auto-build a "
					     "layout), or bind a UVerticalBox named 'Box_Toasts' in the WBP override."));
				bMissingContainerWarned = true;
			}
			return;
		}
	}

	EvictOldestIfNeeded();

	UBorder* Card = WidgetTree->ConstructWidget<UBorder>();
	Card->SetBrushColor(ResolveSeverityColor(Notification.Severity));
	Card->SetPadding(ToastPadding);
	Card->SetVisibility(ESlateVisibility::HitTestInvisible);

	UTextBlock* Text = WidgetTree->ConstructWidget<UTextBlock>();
	Text->SetText(Notification.Message);
	Text->SetAutoWrapText(true);
	Text->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	FSlateFontInfo Font = Text->GetFont();
	Font.Size = ToastFontSize;
	Text->SetFont(Font);

	Card->SetContent(Text);

	if (UVerticalBoxSlot* VBoxSlot = Box_Toasts->AddChildToVerticalBox(Card))
	{
		VBoxSlot->SetPadding(FMargin(0.f, ToastSpacing * 0.5f));
		VBoxSlot->SetHorizontalAlignment(HAlign_Fill);
	}

	const float Duration = Notification.DurationSeconds > 0.f
		? Notification.DurationSeconds
		: DefaultDurationSeconds;
	StartAutoDismissTimer(Card, Duration);
}

void UDappNotificationHostWidget::EvictOldestIfNeeded()
{
	if (!Box_Toasts) { return; }
	while (Box_Toasts->GetChildrenCount() >= MaxVisibleToasts)
	{
		Box_Toasts->RemoveChildAt(0);
	}
}

void UDappNotificationHostWidget::StartAutoDismissTimer(UWidget* ToastWidget, float DurationSeconds)
{
	if (!ToastWidget) { return; }
	UWorld* World = GetWorld();
	if (!World) { return; }

	TWeakObjectPtr<UWidget> WeakToast(ToastWidget);
	TWeakObjectPtr<UVerticalBox> WeakBox(Box_Toasts);

	FTimerHandle Handle;
	World->GetTimerManager().SetTimer(
		Handle,
		FTimerDelegate::CreateWeakLambda(this, [WeakToast, WeakBox]()
		{
			UWidget* Toast = WeakToast.Get();
			UVerticalBox* Box = WeakBox.Get();
			if (Toast && Box)
			{
				Box->RemoveChild(Toast);
			}
		}),
		FMath::Max(DurationSeconds, 0.5f),
		/*bLoop=*/false);
}

FLinearColor UDappNotificationHostWidget::ResolveSeverityColor(EDappNotificationSeverity Severity) const
{
	switch (Severity)
	{
	case EDappNotificationSeverity::Success: return ColorSuccess;
	case EDappNotificationSeverity::Warning: return ColorWarning;
	case EDappNotificationSeverity::Error:   return ColorError;
	case EDappNotificationSeverity::Info:
	default:                                 return ColorInfo;
	}
}
