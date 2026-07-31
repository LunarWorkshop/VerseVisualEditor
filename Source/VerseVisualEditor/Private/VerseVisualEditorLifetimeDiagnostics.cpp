#include "VerseVisualEditorLifetimeDiagnostics.h"

#include "Containers/Array.h"
#include "Containers/ArrayView.h"
#include "HAL/CriticalSection.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformStackWalk.h"
#include "HAL/PlatformTLS.h"
#include "Misc/CString.h"
#include "Misc/ScopeLock.h"

namespace VerseVisualEditorLifetimeDiagnostics
{
	namespace
	{
		constexpr uint32 MaxStackDepth = 32;
		constexpr int32 MaxKindLength = 48;
		constexpr int32 MaxLabelLength = 256;

		struct FTrackedLifetime
		{
			const void* Instance = nullptr;
			uint64 Sequence = 0;
			uint32 ThreadId = 0;
			uint32 StackDepth = 0;
			uint64 Stack[MaxStackDepth] = {};
			TCHAR Kind[MaxKindLength] = {};
			TCHAR Label[MaxLabelLength] = {};
		};

		struct FTracker
		{
			FCriticalSection Mutex;
			TArray<FTrackedLifetime> Live;
			uint64 NextSequence = 1;
		};

		FTracker& GetTracker()
		{
			// Deliberately leaked. Module and process shutdown ordering is the
			// subject of this diagnostic, so its storage must outlive both.
			static FTracker* Tracker = new FTracker();
			return *Tracker;
		}

		int32 FindTrackedIndex(
			TConstArrayView<FTrackedLifetime> Live,
			const void* Instance,
			const TCHAR* Kind)
		{
			return Live.IndexOfByPredicate(
				[Instance, Kind](const FTrackedLifetime& Item)
				{
					return Item.Instance == Instance
						&& FCString::Strcmp(Item.Kind, Kind) == 0;
				});
		}

		void CopyText(TCHAR* Destination, int32 DestinationCount, const TCHAR* Source)
		{
			FCString::Strncpy(
				Destination,
				Source != nullptr ? Source : TEXT(""),
				DestinationCount);
			Destination[DestinationCount - 1] = TEXT('\0');
		}

		void PrintRawStack(const FTrackedLifetime& Item)
		{
			for (uint32 FrameIndex = 0; FrameIndex < Item.StackDepth; ++FrameIndex)
			{
				FPlatformMisc::LowLevelOutputDebugStringf(
					TEXT("[VVE Lifetime]     raw[%02u]=0x%016llx\n"),
					FrameIndex,
					static_cast<unsigned long long>(Item.Stack[FrameIndex]));
			}
		}
	}

	void Track(const void* Instance, const TCHAR* Kind, const TCHAR* Label)
	{
		if (Instance == nullptr)
		{
			return;
		}

		FTrackedLifetime Item;
		Item.Instance = Instance;
		Item.ThreadId = FPlatformTLS::GetCurrentThreadId();
		Item.StackDepth = FPlatformStackWalk::CaptureStackBackTrace(
			Item.Stack,
			UE_ARRAY_COUNT(Item.Stack));
		CopyText(Item.Kind, UE_ARRAY_COUNT(Item.Kind), Kind);
		CopyText(Item.Label, UE_ARRAY_COUNT(Item.Label), Label);

		FTracker& Tracker = GetTracker();
		FScopeLock Lock(&Tracker.Mutex);
		Item.Sequence = Tracker.NextSequence++;
		const int32 ExistingIndex = FindTrackedIndex(Tracker.Live, Instance, Kind);
		if (ExistingIndex != INDEX_NONE)
		{
			FPlatformMisc::LowLevelOutputDebugStringf(
				TEXT("[VVE Lifetime] DUPLICATE kind=%s ptr=%p old-seq=%llu\n"),
				Kind,
				Instance,
				static_cast<unsigned long long>(Tracker.Live[ExistingIndex].Sequence));
			Tracker.Live.RemoveAtSwap(ExistingIndex, EAllowShrinking::No);
		}
		Tracker.Live.Add(Item);
		FPlatformMisc::LowLevelOutputDebugStringf(
			TEXT("[VVE Lifetime] + kind=%s ptr=%p seq=%llu thread=%u label=%s\n"),
			Item.Kind,
			Item.Instance,
			static_cast<unsigned long long>(Item.Sequence),
			Item.ThreadId,
			Item.Label);
	}

	void Update(const void* Instance, const TCHAR* Kind, const TCHAR* Label)
	{
		FTracker& Tracker = GetTracker();
		FScopeLock Lock(&Tracker.Mutex);
		const int32 Index = FindTrackedIndex(Tracker.Live, Instance, Kind);
		if (Index == INDEX_NONE)
		{
			FPlatformMisc::LowLevelOutputDebugStringf(
				TEXT("[VVE Lifetime] UPDATE-MISSING kind=%s ptr=%p label=%s\n"),
				Kind,
				Instance,
				Label != nullptr ? Label : TEXT(""));
			return;
		}
		CopyText(Tracker.Live[Index].Label, MaxLabelLength, Label);
		FPlatformMisc::LowLevelOutputDebugStringf(
			TEXT("[VVE Lifetime] ~ kind=%s ptr=%p seq=%llu label=%s\n"),
			Tracker.Live[Index].Kind,
			Tracker.Live[Index].Instance,
			static_cast<unsigned long long>(Tracker.Live[Index].Sequence),
			Tracker.Live[Index].Label);
	}

	void Untrack(const void* Instance, const TCHAR* Kind)
	{
		if (Instance == nullptr)
		{
			return;
		}

		FTracker& Tracker = GetTracker();
		FScopeLock Lock(&Tracker.Mutex);
		const int32 Index = FindTrackedIndex(Tracker.Live, Instance, Kind);
		if (Index == INDEX_NONE)
		{
			FPlatformMisc::LowLevelOutputDebugStringf(
				TEXT("[VVE Lifetime] DESTROY-MISSING kind=%s ptr=%p\n"),
				Kind,
				Instance);
			return;
		}
		const FTrackedLifetime Item = Tracker.Live[Index];
		Tracker.Live.RemoveAtSwap(Index, EAllowShrinking::No);
		FPlatformMisc::LowLevelOutputDebugStringf(
			TEXT("[VVE Lifetime] - kind=%s ptr=%p seq=%llu thread=%u label=%s\n"),
			Item.Kind,
			Item.Instance,
			static_cast<unsigned long long>(Item.Sequence),
			FPlatformTLS::GetCurrentThreadId(),
			Item.Label);
	}

	void Event(const TCHAR* Name, const void* Owner, const void* Related)
	{
		FPlatformMisc::LowLevelOutputDebugStringf(
			TEXT("[VVE Lifetime] EVENT name=%s owner=%p related=%p thread=%u\n"),
			Name != nullptr ? Name : TEXT(""),
			Owner,
			Related,
			FPlatformTLS::GetCurrentThreadId());
	}

	void Dump(const TCHAR* Phase)
	{
		FTracker& Tracker = GetTracker();
		FScopeLock Lock(&Tracker.Mutex);
		FPlatformMisc::LowLevelOutputDebugStringf(
			TEXT("\n[VVE Lifetime] DUMP phase=%s live=%d\n"),
			Phase != nullptr ? Phase : TEXT(""),
			Tracker.Live.Num());
		for (const FTrackedLifetime& Item : Tracker.Live)
		{
			FPlatformMisc::LowLevelOutputDebugStringf(
				TEXT("[VVE Lifetime]   kind=%s ptr=%p seq=%llu created-thread=%u label=%s depth=%u\n"),
				Item.Kind,
				Item.Instance,
				static_cast<unsigned long long>(Item.Sequence),
				Item.ThreadId,
				Item.Label,
				Item.StackDepth);
			PrintRawStack(Item);
		}
		FPlatformMisc::LowLevelOutputDebugString(
			TEXT("[VVE Lifetime] END DUMP\n\n"));
	}
}
