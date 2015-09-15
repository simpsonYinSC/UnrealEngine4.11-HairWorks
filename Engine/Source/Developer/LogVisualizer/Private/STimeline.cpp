// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "LogVisualizer.h"
#include "STimeline.h"
#include "STimelineBar.h"
#include "TimeSliderController.h"
#include "STimelinesContainer.h"
#include "LogVisualizerSettings.h"

class STimelineLabelAnchor : public SMenuAnchor
{
public:
	void Construct(const FArguments& InArgs, TSharedPtr<class STimeline> InTimelineOwner)
	{
		SMenuAnchor::Construct(InArgs);
		TimelineOwner = InTimelineOwner;
	}

private:
	/** SWidget Interface */
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	TWeakPtr<class STimeline> TimelineOwner;
};

FReply STimelineLabelAnchor::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton && TimelineOwner.IsValid() && TimelineOwner.Pin()->IsSelected())
	{
		SetIsOpen(!IsOpen());
		return FReply::Handled();
	}
	return FReply::Unhandled();
}

FReply STimeline::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	Owner->ChangeSelection(SharedThis(this), MouseEvent);

	return FReply::Unhandled();
}

FReply STimeline::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	FReply ReturnValue = FReply::Unhandled();
	if (IsSelected() == false)
	{
		return ReturnValue;
	}

	return FLogVisualizer::Get().GetEvents().OnKeyboardEvent.Execute(MyGeometry, InKeyEvent);
}

bool STimeline::IsSelected() const
{
	// Ask the tree if we are selected
	return FVisualLoggerDatabase::Get().IsRowSelected(GetName());
}

void STimeline::UpdateVisibility()
{
	ULogVisualizerSettings* Settings = ULogVisualizerSettings::StaticClass()->GetDefaultObject<ULogVisualizerSettings>();

	FVisualLoggerDBRow& DataRow = FVisualLoggerDatabase::Get().GetRowByName(GetName());
	const TArray<FVisualLogDevice::FVisualLogEntryItem>& Entries = DataRow.GetItems();

	const bool bJustIgnore = Settings->bIgnoreTrivialLogs && Entries.Num() <= Settings->TrivialLogsThreshold;
	const bool bVisibleByOwnerClasses = FVisualLoggerFilters::Get().MatchObjectName(OwnerClassName.ToString());
	const bool bIsCollapsed = bJustIgnore || DataRow.GetNumberOfHiddenItems() == Entries.Num() || (SearchFilter.Len() > 0 && OwnerName.ToString().Find(SearchFilter) == INDEX_NONE);

	const bool bIsHidden = bIsCollapsed || !bVisibleByOwnerClasses;
	SetVisibility(bIsHidden ? EVisibility::Collapsed : EVisibility::Visible);
	if (bIsCollapsed)
	{
		Owner->SetSelectionState(SharedThis(this), false, false);
	}

	FVisualLoggerDatabase::Get().SetRowVisibility(GetName(), !bIsHidden);
}

void STimeline::OnFiltersSearchChanged(const FText& Filter)
{
	OnFiltersChanged();
}

void STimeline::OnFiltersChanged()
{
	UpdateVisibility();
}

void STimeline::OnSearchChanged(const FText& Filter)
{
	SearchFilter = Filter.ToString();
	UpdateVisibility();
}

void STimeline::HandleLogVisualizerSettingChanged(FName InName)
{
	UpdateVisibility();
}

const TArray<FVisualLogDevice::FVisualLogEntryItem>& STimeline::GetEntries()
{
	FVisualLoggerDBRow& DataRow = FVisualLoggerDatabase::Get().GetRowByName(GetName());
	return DataRow.GetItems();
}

void STimeline::AddEntry(const FVisualLogDevice::FVisualLogEntryItem& Entry) 
{ 
	//Entries.Add(Entry); 

	ULogVisualizerSettings* Settings = ULogVisualizerSettings::StaticClass()->GetDefaultObject<ULogVisualizerSettings>();
	
	FVisualLoggerDBRow& DataRow = FVisualLoggerDatabase::Get().GetRowByName(GetName());
	const TArray<FVisualLogDevice::FVisualLogEntryItem>& Entries = DataRow.GetItems();

	UpdateVisibility();

}

void STimeline::Construct(const FArguments& InArgs, TSharedPtr<FVisualLoggerTimeSliderController> TimeSliderController, TSharedPtr<STimelinesContainer> InContainer, FName InName, FName InOwnerClassName)
{
	OnGetMenuContent = InArgs._OnGetMenuContent;

	Owner = InContainer;
	OwnerName = InName;
	OwnerClassName = InOwnerClassName;
	
	ChildSlot
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(FMargin(0, 4, 0, 0))
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			.FillWidth(TAttribute<float>::Create(TAttribute<float>::FGetter::CreateLambda([=] { return FLogVisualizer::Get().GetAnimationOutlinerFillPercentage(); })))
			[
				SAssignNew(PopupAnchor, STimelineLabelAnchor, SharedThis(this))
				.OnGetMenuContent(OnGetMenuContent)
				[
					SNew(SBorder)
					.HAlign(HAlign_Fill)
					.Padding(FMargin(0, 0, 4, 0))
					.BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))
					[
						SNew(SBorder)
						.VAlign(VAlign_Center)
						.BorderImage(this, &STimeline::GetBorder)
						.Padding(FMargin(4, 0, 2, 0))
						[
							// Search box for searching through the outliner
							SNew(SVerticalBox)
							+ SVerticalBox::Slot()
							.Padding(FMargin(0, 0, 0, 0))
							.HAlign(HAlign_Fill)
							.VAlign(VAlign_Fill)
							[
								SNew(STextBlock)
								.Text(FText::FromName(OwnerName))
								.ShadowOffset(FVector2D(1.f, 1.f))
							]
							+ SVerticalBox::Slot()
							.Padding(FMargin(0, 0, 0, 0))
							.HAlign(HAlign_Fill)
							.VAlign(VAlign_Fill)
							[
								SNew(STextBlock)
								.Text(FText::FromName(OwnerClassName))
								.TextStyle(FLogVisualizerStyle::Get(), TEXT("Sequencer.ClassNAme"))
							]
						]
					]
				]
			]
			+ SHorizontalBox::Slot()
			.Padding(FMargin(0, 4, 0, 0))
			.HAlign(HAlign_Left)
			[
				SNew(SBox)
				.Padding(FMargin(0, 0, 0, 0))
				.HAlign(HAlign_Left)
				[
					// Search box for searching through the outliner
					SAssignNew(TimelineBar, STimelineBar, TimeSliderController, SharedThis(this))
				]
			]
		];

	ULogVisualizerSettings::StaticClass()->GetDefaultObject<ULogVisualizerSettings>()->OnSettingChanged().AddRaw(this, &STimeline::HandleLogVisualizerSettingChanged);
	FVisualLoggerDatabase::Get().GetEvents().OnNewItem.AddRaw(this, &STimeline::OnNewItemHandler);
	FVisualLoggerDatabase::Get().GetEvents().OnRowSelectionChanged.AddRaw(this, &STimeline::OnRowSelectionChanged);
}

STimeline::~STimeline()
{
	if (ULogVisualizerSettings::StaticClass() && ULogVisualizerSettings::StaticClass()->GetDefaultObject<ULogVisualizerSettings>())
	{
		ULogVisualizerSettings::StaticClass()->GetDefaultObject<ULogVisualizerSettings>()->OnSettingChanged().RemoveAll(this);
	}
	FVisualLoggerDatabase::Get().GetEvents().OnNewItem.RemoveAll(this);
	FVisualLoggerDatabase::Get().GetEvents().OnRowSelectionChanged.RemoveAll(this);
}

void STimeline::OnRowSelectionChanged(const TArray<FName>& RowNames)
{
	if (RowNames.Num() > 0)
	{
		const FName RowName = RowNames[RowNames.Num() - 1];
	}
}

void STimeline::OnNewItemHandler(const FVisualLoggerDBRow& BDRow, int32 ItemIndex)
{
	const FVisualLogDevice::FVisualLogEntryItem& Entry = BDRow.GetItems()[ItemIndex];
	if (GetName() == Entry.OwnerName)
	{
		AddEntry(Entry);
	}
}

const FSlateBrush* STimeline::GetBorder() const
{
	if (IsSelected())
	{
		return FLogVisualizerStyle::Get().GetBrush("ToolBar.Button.Hovered");
	}
	else
	{
		return FLogVisualizerStyle::Get().GetBrush("ToolBar.Button.Normal");
	}
}

void STimeline::Goto(float ScrubPosition) 
{ 

}

void STimeline::GotoNextItem() 
{ 
	FLogVisualizer::Get().GotoNextItem(GetName());
}

void STimeline::GotoPreviousItem() 
{ 
	FLogVisualizer::Get().GotoPreviousItem(GetName());
}

void STimeline::MoveCursorByDistance(int32 Distance)
{
	if (Distance > 0)
	{
		FLogVisualizer::Get().GotoNextItem(GetName(), FMath::Abs(Distance));
	}
	else
	{
		FLogVisualizer::Get().GotoPreviousItem(GetName(), FMath::Abs(Distance));
	}
	
}
