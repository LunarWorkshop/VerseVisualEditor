#pragma once

#include "Slate/SVerseGraphSurface.h"
#include "Widgets/SCompoundWidget.h"

/** Function-specific wrapper around the shared Verse graph surface. */
class SVerseFunctionCanvas final : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SVerseFunctionCanvas) {}
		SLATE_ARGUMENT(TSharedPtr<SWidget>, InitialAnchor)
		SLATE_ARGUMENT(TArray<FVerseGraphConnection>, Connections)
		SLATE_ARGUMENT(TSharedPtr<FVerseGraphMotionController>, MotionController)
		SLATE_ARGUMENT(TSharedPtr<FVerseGraphEndpointRegistry>, EndpointRegistry)
		SLATE_EVENT(FOnVerseGraphConnectionTargetDropped, OnConnectionDropped)
		SLATE_EVENT(FSimpleDelegate, OnConnectionCancelled)
		SLATE_EVENT(FSimpleDelegate, OnBackgroundClicked)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, FVerseCanvasViewState InitialViewState, bool bCenterInitially);
	FVerseCanvasViewState GetViewState() const;
	void RefreshContent(
		TSharedRef<SWidget> InContent,
		TArray<FVerseGraphConnection> InConnections,
		TSharedPtr<SWidget> InInitialAnchor,
		TSharedPtr<FVerseGraphEndpointRegistry> InEndpointRegistry);
	FReply BeginConnectionDrag(
		const FVerseSocketDragStart& DragStart,
		TMap<FVerseVisualSocketEndpoint, EVerseSocketDragVisualState> DragStates);
	void EndConnectionPreview();
	TSharedRef<FVerseGraphMotionController> GetMotionController() const
	{
		return Surface->GetMotionController();
	}

private:
	TSharedPtr<SVerseGraphSurface> Surface;
};
