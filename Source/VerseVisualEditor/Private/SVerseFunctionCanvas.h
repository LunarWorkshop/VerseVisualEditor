#pragma once

#include "SVerseGraphSurface.h"
#include "Widgets/SCompoundWidget.h"

/** Function-specific wrapper around the shared Verse graph surface. */
class SVerseFunctionCanvas final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SVerseFunctionCanvas) {}
		SLATE_ARGUMENT(TSharedPtr<SWidget>, InitialAnchor)
		SLATE_ARGUMENT(TArray<FVerseGraphConnection>, Connections)
		SLATE_EVENT(FOnVerseGraphConnectionDropped, OnConnectionDropped)
		SLATE_EVENT(FSimpleDelegate, OnConnectionCancelled)
		SLATE_EVENT(FSimpleDelegate, OnBackgroundClicked)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, FVerseCanvasViewState InitialViewState, bool bCenterInitially);
	FVerseCanvasViewState GetViewState() const;
	void RefreshContent(
		TSharedRef<SWidget> InContent,
		TArray<FVerseGraphConnection> InConnections,
		TSharedPtr<SWidget> InInitialAnchor);
	FReply BeginConnectionDrag(const FVerseSocketDragStart& DragStart);
	void EndConnectionPreview();

private:
	TSharedPtr<SVerseGraphSurface> Surface;
};
