#include "UI/DappNotificationSubsystem.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogDappNotification, Log, All);

UDappNotificationSubsystem* UDappNotificationSubsystem::Get(const UObject* WorldContext)
{
	if (!WorldContext) { return nullptr; }
	if (const UWorld* World = WorldContext->GetWorld())
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			return GI->GetSubsystem<UDappNotificationSubsystem>();
		}
	}
	return nullptr;
}

void UDappNotificationSubsystem::Show(const FText& Message,
                                      EDappNotificationSeverity Severity,
                                      float DurationSeconds)
{
	FDappNotification Payload;
	Payload.Message         = Message;
	Payload.Severity        = Severity;
	Payload.DurationSeconds = DurationSeconds;

	// Always mirror to the output log so headless / editor-only paths still
	// surface the message even when no toast widget is attached.
	switch (Severity)
	{
	case EDappNotificationSeverity::Error:
		UE_LOG(LogDappNotification, Error,   TEXT("%s"), *Message.ToString()); break;
	case EDappNotificationSeverity::Warning:
		UE_LOG(LogDappNotification, Warning, TEXT("%s"), *Message.ToString()); break;
	default:
		UE_LOG(LogDappNotification, Log,     TEXT("%s"), *Message.ToString()); break;
	}

	OnNotification.Broadcast(Payload);
}
