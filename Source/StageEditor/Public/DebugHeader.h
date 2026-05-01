#pragma once

#pragma region Imports
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Misc/ScopedSlowTask.h"
#include "Templates/UniquePtr.h"
#pragma endregion Imports

namespace DebugHeader
{
#pragma region Debug Utils
	/**
	 * @brief Prints a debug message to the screen.
	 * @param Message The message string to display.
	 * @param Color The color of the message (default is random).
	 */
	static void Print(const FString& Message, const FColor& Color = FColor::MakeRandomColor())
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 8.f, Color, Message);
		}
	}

	/**
	 * @brief Prints a message to the output log.
	 * @param Message The message string to log.
	 */
	static void PrintLog(const FString& Message)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s"), *Message);
	}
#pragma endregion Debug Utils

#pragma region Notification Utils
	/**
	 * @brief Shows a modal message dialog.
	 * @param MsgType The type of dialog (e.g., YesNo, Ok).
	 * @param Msg The message content.
	 * @param bShowMsgAsWarning If true, sets the title to "Warning".
	 * @return The user's response.
	 */
	static EAppReturnType::Type ShowMsgDialog(EAppMsgType::Type MsgType, const FString& Msg,
	                                          bool bShowMsgAsWarning = true)
	{
		EAppReturnType::Type UserResponse;
		if (bShowMsgAsWarning)
		{
			FText MsgTitle = FText::FromString(TEXT("Warning"));
			UserResponse = FMessageDialog::Open(
				MsgType,
				FText::FromString(Msg),
				MsgTitle
			);
		}
		else
		{
			UserResponse = FMessageDialog::Open(
				MsgType,
				FText::FromString(Msg)
			);
		}
		return UserResponse;
	}

	/**
	 * @brief Shows a non-intrusive notification info bubble.
	 * @param Message The message to display in the notification.
	 */
	static void ShowNotifyInfo(const FString& Message)
	{
		FNotificationInfo NotifyInfo(FText::FromString(Message));
		NotifyInfo.bUseLargeFont = true;
		NotifyInfo.FadeOutDuration = 7.f;

		FSlateNotificationManager::Get().AddNotification(NotifyInfo);
	}
#pragma endregion Notification Utils

#pragma region Progress Utils
	/**
	 * @brief Creates and displays a scoped slow task progress bar.
	 * @param TotalWork The total amount of work units.
	 * @param TaskTitle The title of the task.
	 * @param bShowCancelButton Whether to show a cancel button.
	 * @return A unique pointer to the scoped slow task.
	 */
	static TUniquePtr<FScopedSlowTask> CreateProgressTask(float TotalWork, const FText& TaskTitle, bool bShowCancelButton = true)
	{
		TUniquePtr<FScopedSlowTask> SlowTask = MakeUnique<FScopedSlowTask>(TotalWork, TaskTitle);
		SlowTask->MakeDialog(bShowCancelButton);
		return SlowTask;
	}
#pragma endregion Progress Utils
}
