#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "DappGameMode.generated.h"

class ADappActor;
class UDappNotificationHostWidget;
class UUserWidget;

/**
 * ADappGameMode — turnkey game mode for the CROSSx Unreal sample.
 *
 * Drop this class onto any map (or set it as `GlobalDefaultGameMode`) and the
 * sample comes alive without further setup:
 *   1) Spawns an ADappActor (unless one is already in the level), which in
 *      turn configures + initializes the CROSSx SDK and Webkit subsystems.
 *   2) Creates the WBP_DappTestPanel widget and adds it to the local
 *      player's viewport, with input mode = Game and UI + cursor visible.
 *
 * Both behaviors can be turned off with bAutoSpawnDappActor /
 * bAutoCreateTestPanel if a project wants to take over composition manually
 * (e.g. nesting the panel inside a larger HUD).
 *
 * The widget class is intentionally a TSoftClassPtr so that:
 *   • the C++ module does not depend on a yet-to-be-authored UMG asset, and
 *   • external teams can swap WBP_DappTestPanel for their own re-skinned
 *     subclass via Project Settings > Maps & Modes (or DefaultGame.ini).
 */
UCLASS(BlueprintType, meta = (DisplayName = "Dapp Game Mode"))
class CROSSYSDKUNREALSAMP_API ADappGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ADappGameMode();

	/** Class to spawn for the bootstrap actor. Override in a BP subclass to
	 *  use a project-specific ADappActor variant (e.g. with custom AppId). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dapp")
	TSubclassOf<ADappActor> DappActorClass;

	/**
	 * Soft reference to the test-panel UMG widget. Default points at the
	 * conventional asset path (/Game/UI/WBP_DappTestPanel) — see
	 * Documentation/SAMPLE_WIDGET_GUIDE.md. Empty value disables the
	 * auto-create step (you can still construct the widget manually from BP).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dapp",
		meta = (MetaClass = "/Script/UMG.UserWidget", AllowedClasses = "/Script/UMG.UserWidget"))
	TSoftClassPtr<UUserWidget> TestPanelWidgetClass;

	/**
	 * On-screen notification host. Defaults to the C++ base class which builds
	 * a programmatic toast layout — no WBP authoring required. Override with a
	 * BP subclass to customise visuals (see UDappNotificationHostWidget).
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dapp")
	TSubclassOf<UDappNotificationHostWidget> NotificationHostWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dapp")
	bool bAutoSpawnDappActor = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dapp")
	bool bAutoCreateTestPanel = true;

	/** When true, instantiates NotificationHostWidgetClass and adds it to the
	 *  viewport above the test panel so UDappNotificationSubsystem broadcasts
	 *  surface as on-screen toasts. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dapp")
	bool bAutoCreateNotificationHost = true;

	/** When true, switches input mode to Game-and-UI and shows the cursor. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Dapp")
	bool bAutoEnableUIInputMode = true;

protected:
	virtual void BeginPlay() override;

private:
	void EnsureDappActor();
	void CreateTestPanelWidget();
	void CreateNotificationHostWidget();
};
