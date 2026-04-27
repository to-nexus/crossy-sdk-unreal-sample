#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "DappNotificationSubsystem.generated.h"

UENUM(BlueprintType)
enum class EDappNotificationSeverity : uint8
{
	Info    UMETA(DisplayName = "Info"),
	Success UMETA(DisplayName = "Success"),
	Warning UMETA(DisplayName = "Warning"),
	Error   UMETA(DisplayName = "Error")
};

USTRUCT(BlueprintType)
struct FDappNotification
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Dapp|Notification")
	FText Message;

	UPROPERTY(BlueprintReadOnly, Category = "Dapp|Notification")
	EDappNotificationSeverity Severity = EDappNotificationSeverity::Info;

	/** Seconds to show before the toast fades out. 0 means "let the widget decide". */
	UPROPERTY(BlueprintReadOnly, Category = "Dapp|Notification")
	float DurationSeconds = 0.f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDappNotification, const FDappNotification&, Notification);

/**
 * Broadcast-only toast bus. Equivalent to Unity sample's Notification.Show().
 *
 * This subsystem does not draw anything itself — UI widgets (e.g.
 * UDappNotificationWidget or a BP override) subscribe to OnNotification and
 * choose how to render the message. Keeping presentation out of the subsystem
 * means the same API is usable from headless tests, CLI-style logging and
 * in-game HUD toasts alike.
 */
UCLASS()
class CROSSYSDKUNREALSAMP_API UDappNotificationSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Dapp|Notification",
		meta = (WorldContext = "WorldContext", DisplayName = "Get Dapp Notifications"))
	static UDappNotificationSubsystem* Get(const UObject* WorldContext);

	UFUNCTION(BlueprintCallable, Category = "Dapp|Notification")
	void Show(const FText& Message,
	          EDappNotificationSeverity Severity = EDappNotificationSeverity::Info,
	          float DurationSeconds = 3.f);

	UFUNCTION(BlueprintCallable, Category = "Dapp|Notification")
	void ShowInfo(const FText& Message)    { Show(Message, EDappNotificationSeverity::Info); }

	UFUNCTION(BlueprintCallable, Category = "Dapp|Notification")
	void ShowSuccess(const FText& Message) { Show(Message, EDappNotificationSeverity::Success); }

	UFUNCTION(BlueprintCallable, Category = "Dapp|Notification")
	void ShowWarning(const FText& Message) { Show(Message, EDappNotificationSeverity::Warning); }

	UFUNCTION(BlueprintCallable, Category = "Dapp|Notification")
	void ShowError(const FText& Message)   { Show(Message, EDappNotificationSeverity::Error); }

	UPROPERTY(BlueprintAssignable, Category = "Dapp|Notification")
	FOnDappNotification OnNotification;
};
