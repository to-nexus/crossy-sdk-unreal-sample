#include "Dapp/DappGameMode.h"

#include "Dapp/DappActor.h"

#include "Blueprint/UserWidget.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

DEFINE_LOG_CATEGORY_STATIC(LogDappGameMode, Log, All);

ADappGameMode::ADappGameMode()
{
	DappActorClass = ADappActor::StaticClass();

	// Conventional sample asset path. External projects relocating the
	// widget should override this in their BP_DappGameMode subclass instead
	// of changing C++.
	TestPanelWidgetClass = TSoftClassPtr<UUserWidget>(
		FSoftObjectPath(TEXT("/Game/UI/WBP_DappTestPanel.WBP_DappTestPanel_C")));
}

void ADappGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (bAutoSpawnDappActor)
	{
		EnsureDappActor();
	}

	if (bAutoCreateTestPanel)
	{
		CreateTestPanelWidget();
	}
}

void ADappGameMode::EnsureDappActor()
{
	UWorld* World = GetWorld();
	if (!World) { return; }

	if (ADappActor* Existing = ADappActor::Find(this))
	{
		UE_LOG(LogDappGameMode, Log,
			TEXT("ADappActor already placed in level — skipping auto-spawn."));
		(void)Existing;
		return;
	}

	UClass* SpawnClass = DappActorClass ? DappActorClass.Get() : ADappActor::StaticClass();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;

	ADappActor* Spawned = World->SpawnActor<ADappActor>(SpawnClass, FTransform::Identity, Params);
	if (!Spawned)
	{
		UE_LOG(LogDappGameMode, Error, TEXT("Failed to spawn ADappActor (class=%s)."),
			*GetNameSafe(SpawnClass));
		return;
	}

	UE_LOG(LogDappGameMode, Log, TEXT("Spawned ADappActor (%s) at level start."),
		*Spawned->GetName());
}

void ADappGameMode::CreateTestPanelWidget()
{
	UWorld* World = GetWorld();
	if (!World) { return; }

	APlayerController* PC = World->GetFirstPlayerController();
	if (!PC)
	{
		UE_LOG(LogDappGameMode, Warning,
			TEXT("No PlayerController available yet — skipping test panel creation."));
		return;
	}

	if (TestPanelWidgetClass.IsNull())
	{
		UE_LOG(LogDappGameMode, Warning,
			TEXT("TestPanelWidgetClass is unset — skipping test panel creation. "
			     "Set Project Settings > Maps & Modes > GameMode > Test Panel Widget Class, "
			     "or create /Game/UI/WBP_DappTestPanel reparented to UDappTestPanelBase."));
		return;
	}

	// Synchronous load is intentional here — at BeginPlay we are post-cook
	// and the asset (if it exists) is already mounted. If it is missing we
	// log instead of stalling the game thread.
	UClass* WidgetClass = TestPanelWidgetClass.LoadSynchronous();
	if (!WidgetClass)
	{
		UE_LOG(LogDappGameMode, Error,
			TEXT("Could not load TestPanelWidgetClass at '%s'. Did you create WBP_DappTestPanel?"),
			*TestPanelWidgetClass.ToString());
		return;
	}

	UUserWidget* Widget = CreateWidget<UUserWidget>(PC, WidgetClass);
	if (!Widget)
	{
		UE_LOG(LogDappGameMode, Error, TEXT("CreateWidget returned null for %s."),
			*GetNameSafe(WidgetClass));
		return;
	}

	Widget->AddToViewport();

	if (bAutoEnableUIInputMode)
	{
		FInputModeGameAndUI Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Mode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(Mode);
		PC->SetShowMouseCursor(true);
	}

	UE_LOG(LogDappGameMode, Log,
		TEXT("Test panel widget '%s' added to viewport."), *Widget->GetName());
}
