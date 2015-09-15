// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.
#include "BlueprintEditorPrivatePCH.h"
#include "ImaginaryBlueprintData.h"
#include "Json.h"
#include "FindInBlueprints.h"
#include "FindInBlueprintManager.h"

#include "TextFilter.h"

#define LOCTEXT_NAMESPACE "FindInBlueprints"

///////////////////////
// FSearchableValueInfo

FText FSearchableValueInfo::GetDisplayText(const TMap<int32, FText>& InLookupTable) const
{
	if (!DisplayText.IsEmpty() || LookupTableKey == -1)
	{
		return DisplayText;
	}
	return FindInBlueprintsHelpers::AsFText(LookupTableKey, InLookupTable);
}

////////////////////////////
// FComponentUniqueDisplay

bool FComponentUniqueDisplay::operator==(const FComponentUniqueDisplay& Other)
{
	// Two search results in the same object/sub-object should never have the same display string ({Key}: {Value} pairing)
	return SearchResult.IsValid() && Other.SearchResult.IsValid() && SearchResult->GetDisplayString().CompareTo(Other.SearchResult->GetDisplayString()) == 0;
}

///////////////////////
// FImaginaryFiBData

FImaginaryFiBData::FImaginaryFiBData() : LookupTablePtr(nullptr)
{
}

FImaginaryFiBData::FImaginaryFiBData(TSharedPtr< FJsonObject > InUnparsedJsonObject, TMap<int32, FText>* InLookupTablePtr)
	: UnparsedJsonObject(InUnparsedJsonObject)
	, LookupTablePtr(InLookupTablePtr)
{
}

FSearchResult FImaginaryFiBData::CreateSearchResult(FSearchResult InParent)
{
	FSearchResult ReturnSearchResult = CreateSearchResult_Internal(InParent);

	FText DisplayName;
	for( auto TagsAndValues : ParsedTagsAndValues )
	{
		if (TagsAndValues.Value.IsCoreDisplay() || !TagsAndValues.Value.IsSearchable())
		{
			FText Value = TagsAndValues.Value.GetDisplayText(*LookupTablePtr);
			ReturnSearchResult->ParseSearchInfo(FText::FromString(TagsAndValues.Key), Value);
		}
	}
	return ReturnSearchResult;
}

FSearchResult FImaginaryFiBData::CreateSearchTree(FSearchResult InParentSearchResult, TWeakPtr< FImaginaryFiBData > InCurrentPointer, TArray< const FImaginaryFiBData* >& InValidSearchResults, TMultiMap< const FImaginaryFiBData*, FComponentUniqueDisplay >& InMatchingSearchComponents)
{
	FSearchResult CurrentSearchResult = InCurrentPointer.Pin()->CreateSearchResult(InParentSearchResult);
	bool bValidSearchResults = false;

	// Check all children first, to see if they are valid in the search results
	for (TSharedPtr<FImaginaryFiBData> ChildData : InCurrentPointer.Pin()->ParsedChildData)
	{
		FSearchResult Result = CreateSearchTree(CurrentSearchResult, ChildData, InValidSearchResults, InMatchingSearchComponents);
		if (Result.IsValid())
		{
			bValidSearchResults = true;
			CurrentSearchResult->Children.Add(Result);
		}
	}

	// If the children did not match the search results but this item does, then we will want to return true
	if (!bValidSearchResults && (InValidSearchResults.Find(InCurrentPointer.Pin().Get()) != INDEX_NONE || InMatchingSearchComponents.Find(InCurrentPointer.Pin().Get())))
	{
		bValidSearchResults = true;
	}

	if (bValidSearchResults)
	{
		TArray< FComponentUniqueDisplay > SearchResultList;
		InMatchingSearchComponents.MultiFind(InCurrentPointer.Pin().Get(), SearchResultList, true);
		CurrentSearchResult->Children.Reserve(CurrentSearchResult->Children.Num() + SearchResultList.Num());

		// Add any data that matched the search results as a child of our search result
		for (FComponentUniqueDisplay& SearchResultWrapper : SearchResultList)
		{
			SearchResultWrapper.SearchResult->Parent = CurrentSearchResult;
			CurrentSearchResult->Children.Add(SearchResultWrapper.SearchResult);
		}
		return CurrentSearchResult;
	}
	return nullptr;
}

bool FImaginaryFiBData::IsCompatibleWithFilter(ESearchQueryFilter InSearchQueryFilter)
{
	return true;
}

bool FImaginaryFiBData::CanCallFilter(ESearchQueryFilter InSearchQueryFilter)
{
	// Always compatible with the AllFilter
	return InSearchQueryFilter == ESearchQueryFilter::AllFilter;
}

void FImaginaryFiBData::ParseAllChildData()
{
	if (UnparsedJsonObject.IsValid())
	{
		for( auto MapValues : UnparsedJsonObject->Values )
		{
			TSharedPtr< FJsonValue > JsonValue = MapValues.Value;

			if (!TrySpecialHandleJsonValue(MapValues.Key, JsonValue))
			{
				FText Key = FText::FromString(MapValues.Key);
				ParseJsonValue(Key, Key, JsonValue);
			}
		}
	}

	UnparsedJsonObject.Reset();
}

void FImaginaryFiBData::ParseJsonValue(FText InKey, FText InDisplayKey, TSharedPtr< FJsonValue > InJsonValue)
{
	if( InJsonValue->Type == EJson::String)
	{
		ParsedTagsAndValues.Add(InKey.ToString(), FSearchableValueInfo(InDisplayKey, FCString::Atoi(*InJsonValue->AsString()), GetSearchabilityStatus(InKey.ToString())));
	}
	else if (InJsonValue->Type == EJson::Boolean)
	{
		ParsedTagsAndValues.Add(InKey.ToString(), FSearchableValueInfo(InDisplayKey, FText::FromString(InJsonValue->AsString()), GetSearchabilityStatus(InKey.ToString())));
	}
	else if( InJsonValue->Type == EJson::Array)
	{
		TSharedPtr< FCategorySectionHelper > ArrayCategory = MakeShareable(new FCategorySectionHelper(LookupTablePtr, InKey, true));
		ParsedChildData.Add(ArrayCategory);
		TArray<TSharedPtr< FJsonValue > > ArrayList = InJsonValue->AsArray();
		for( int32 ArrayIdx = 0; ArrayIdx < ArrayList.Num(); ++ArrayIdx)
		{
			TSharedPtr< FJsonValue > ArrayValue = ArrayList[ArrayIdx];
			ArrayCategory->ParseJsonValue(InKey, FText::FromString(FString::FromInt(ArrayIdx)), ArrayValue);
		}		
	}
	else if (InJsonValue->Type == EJson::Object)
	{
		TSharedPtr< FCategorySectionHelper > SubObjectCategory = MakeShareable(new FCategorySectionHelper(InJsonValue->AsObject(), LookupTablePtr, InDisplayKey, false));
		SubObjectCategory->ParseAllChildData();
		ParsedChildData.Add(SubObjectCategory);
	}
	else
	{
		// For everything else, there's this. Numbers come here and will be treated as strings
		ParsedTagsAndValues.Add(InKey.ToString(), FSearchableValueInfo(InDisplayKey, FText::FromString(InJsonValue->AsString()), GetSearchabilityStatus(InKey.ToString())));
	}
}

FText FImaginaryFiBData::CreateSearchComponentDisplayText(FText InKey, FText InValue) const
{
	FFormatNamedArguments Args;
	Args.Add(TEXT("Key"), InKey);
	Args.Add(TEXT("Value"), InValue);
	return FText::Format(LOCTEXT("ExtraSearchInfo", "{Key}: {Value}"), Args);
}

bool FImaginaryFiBData::TestBasicStringExpression(const FTextFilterString& InValue, const ETextFilterTextComparisonMode InTextComparisonMode, TMultiMap< const FImaginaryFiBData*, FComponentUniqueDisplay >& InOutMatchingSearchComponents) const
{
	bool bMatchesSearchQuery = false;
	for( auto ParsedValues : ParsedTagsAndValues )
	{
		if (ParsedValues.Value.IsSearchable() && !ParsedValues.Value.IsExplicitSearchable())
		{
			FText Value = ParsedValues.Value.GetDisplayText(*LookupTablePtr);
			FString ValueAsString = Value.ToString();
			ValueAsString.ReplaceInline(TEXT(" "), TEXT(""));
			bool bMatchesSearch = TextFilterUtils::TestBasicStringExpression(ValueAsString, InValue, InTextComparisonMode) || TextFilterUtils::TestBasicStringExpression(Value.BuildSourceString(), InValue, InTextComparisonMode);
			
			if (bMatchesSearch && !ParsedValues.Value.IsCoreDisplay())
			{
				FSearchResult SearchResult(new FFindInBlueprintsResult(CreateSearchComponentDisplayText(ParsedValues.Value.GetDisplayKey(), Value), nullptr ));
				InOutMatchingSearchComponents.Add(this, FComponentUniqueDisplay(SearchResult));
			}

			bMatchesSearchQuery |= bMatchesSearch;
		}
	}
	// Any children that are treated as a TagAndValue Category should be added for independent searching
	for (const TSharedPtr< FImaginaryFiBData > Child : ParsedChildData)
	{
		if (Child->IsTagAndValueCategory())
		{
			bMatchesSearchQuery |= Child->TestBasicStringExpression(InValue, InTextComparisonMode, InOutMatchingSearchComponents);
		}
	}

	return bMatchesSearchQuery;
}

bool FImaginaryFiBData::TestComplexExpression(const FName& InKey, const FTextFilterString& InValue, const ETextFilterComparisonOperation InComparisonOperation, const ETextFilterTextComparisonMode InTextComparisonMode, TMultiMap< const FImaginaryFiBData*, FComponentUniqueDisplay >& InOutMatchingSearchComponents) const
{
	bool bMatchesSearchQuery = false;

	TArray<FSearchableValueInfo> ValueList;
	ParsedTagsAndValues.MultiFind(InKey.ToString(), ValueList, true);
	for (const FSearchableValueInfo& ItemValue : ValueList)
	{
		if (ItemValue.IsSearchable())
		{
			FText Value = ItemValue.GetDisplayText(*LookupTablePtr);
			FString ValueAsString = Value.ToString();
			ValueAsString.ReplaceInline(TEXT(" "), TEXT(""));
			bool bMatchesSearch = TextFilterUtils::TestComplexExpression(ValueAsString, InValue, InComparisonOperation, InTextComparisonMode) || TextFilterUtils::TestBasicStringExpression(Value.BuildSourceString(), InValue, InTextComparisonMode);

			if (bMatchesSearch && !ItemValue.IsCoreDisplay())
			{
				FSearchResult SearchResult(new FFindInBlueprintsResult(CreateSearchComponentDisplayText(ItemValue.GetDisplayKey(), Value), nullptr ));
				InOutMatchingSearchComponents.Add(this, FComponentUniqueDisplay(SearchResult));
			}
			bMatchesSearchQuery |= bMatchesSearch;
		}
	}

	// Any children that are treated as a TagAndValue Category should be added for independent searching
	for (const TSharedPtr< FImaginaryFiBData > Child : ParsedChildData)
	{
		if (Child->IsTagAndValueCategory())
		{
			bMatchesSearchQuery |= Child->TestComplexExpression(InKey, InValue, InComparisonOperation, InTextComparisonMode, InOutMatchingSearchComponents);
		}
	}
	return bMatchesSearchQuery;
}

///////////////////////////
// FCategorySectionHelper

FCategorySectionHelper::FCategorySectionHelper(TMap<int32, FText>* InLookupTablePtr, FText InCategoryName, bool bInTagAndValueCategory)
	: FImaginaryFiBData(nullptr, InLookupTablePtr)
	, CategoryName(InCategoryName)
	, bIsTagAndValue(bInTagAndValueCategory)
{
}

FCategorySectionHelper::FCategorySectionHelper(TSharedPtr< FJsonObject > InUnparsedJsonObject, TMap<int32, FText>* InLookupTablePtr, FText InCategoryName, bool bInTagAndValueCategory)
	: FImaginaryFiBData(InUnparsedJsonObject, InLookupTablePtr)
	, CategoryName(InCategoryName)
	, bIsTagAndValue(bInTagAndValueCategory)
{
}

FCategorySectionHelper::FCategorySectionHelper(TSharedPtr< FJsonObject > InUnparsedJsonObject, TMap<int32, FText>* InLookupTablePtr, FText InCategoryName, bool bInTagAndValueCategory, FCategorySectionHelperCallback InSpecialHandlingCallback)
	: FImaginaryFiBData(InUnparsedJsonObject, InLookupTablePtr)
	, SpecialHandlingCallback(InSpecialHandlingCallback)
	, CategoryName(InCategoryName)
	, bIsTagAndValue(bInTagAndValueCategory)
{

}

bool FCategorySectionHelper::CanCallFilter(ESearchQueryFilter InSearchQueryFilter)
{
	return true;
}

FSearchResult FCategorySectionHelper::CreateSearchResult_Internal(FSearchResult InParent) const
{
	return FSearchResult(new FFindInBlueprintsResult(CategoryName, InParent));
}

void FCategorySectionHelper::ParseAllChildData()
{
	if (UnparsedJsonObject.IsValid() && SpecialHandlingCallback.IsBound())
	{
		SpecialHandlingCallback.Execute(UnparsedJsonObject, ParsedChildData);
		UnparsedJsonObject.Reset();
	}
	else
	{
		FImaginaryFiBData::ParseAllChildData();
	}
}

//////////////////////////////////////////
// FImaginaryBlueprint

FImaginaryBlueprint::FImaginaryBlueprint(FString InBlueprintName, FString InBlueprintPath, FString InBlueprintParentClass, TArray<FString>& InInterfaces, FString InUnparsedStringData)
	: BlueprintPath(InBlueprintPath)
	, UnparsedStringData(InUnparsedStringData)
{
	ParseToJson();
	ParsedTagsAndValues.Add(FFindInBlueprintSearchTags::FiB_Name.ToString(), FSearchableValueInfo(FFindInBlueprintSearchTags::FiB_Name, FText::FromString(InBlueprintName), ESearchableValueStatus::ExplicitySearchable));
	ParsedTagsAndValues.Add(FFindInBlueprintSearchTags::FiB_Path.ToString(), FSearchableValueInfo(FFindInBlueprintSearchTags::FiB_Path, FText::FromString(InBlueprintPath), ESearchableValueStatus::ExplicitySearchable));
	ParsedTagsAndValues.Add(FFindInBlueprintSearchTags::FiB_ParentClass.ToString(), FSearchableValueInfo(FFindInBlueprintSearchTags::FiB_ParentClass, FText::FromString(InBlueprintParentClass), ESearchableValueStatus::ExplicitySearchable));

	TSharedPtr< FCategorySectionHelper > InterfaceCategory = MakeShareable(new FCategorySectionHelper(nullptr, &LookupTable, FFindInBlueprintSearchTags::FiB_Interfaces, true));
	for( int32 InterfaceIdx = 0; InterfaceIdx < InInterfaces.Num(); ++InterfaceIdx)
	{
		FString& Interface = InInterfaces[InterfaceIdx];
		FText Key = FText::FromString(FString::FromInt(InterfaceIdx));
		FSearchableValueInfo Value(Key, FText::FromString(Interface), ESearchableValueStatus::ExplicitySearchable);
		InterfaceCategory->AddKeyValuePair(FFindInBlueprintSearchTags::FiB_Interfaces, Value);
	}		
	ParsedChildData.Add(InterfaceCategory);
}

FSearchResult FImaginaryBlueprint::CreateSearchResult_Internal(FSearchResult InParent) const
{
	return FSearchResult(new FFindInBlueprintsResult(ParsedTagsAndValues.Find(FFindInBlueprintSearchTags::FiB_Path.ToString())->GetDisplayText(LookupTable)));
}

bool FImaginaryBlueprint::IsCompatibleWithFilter(ESearchQueryFilter InSearchQueryFilter)
{
	return InSearchQueryFilter == ESearchQueryFilter::AllFilter || InSearchQueryFilter == ESearchQueryFilter::BlueprintFilter;
}

bool FImaginaryBlueprint::CanCallFilter(ESearchQueryFilter InSearchQueryFilter)
{
	return InSearchQueryFilter == ESearchQueryFilter::NodesFilter ||
		InSearchQueryFilter == ESearchQueryFilter::PinsFilter ||
		InSearchQueryFilter == ESearchQueryFilter::GraphsFilter ||
		InSearchQueryFilter == ESearchQueryFilter::UberGraphsFilter ||
		InSearchQueryFilter == ESearchQueryFilter::FunctionsFilter ||
		InSearchQueryFilter == ESearchQueryFilter::MacrosFilter ||
		InSearchQueryFilter == ESearchQueryFilter::PropertiesFilter ||
		InSearchQueryFilter == ESearchQueryFilter::VariablesFilter ||
		InSearchQueryFilter == ESearchQueryFilter::ComponentsFilter ||
		FImaginaryFiBData::CanCallFilter(InSearchQueryFilter);
}

void FImaginaryBlueprint::ParseToJson()
{
	UnparsedJsonObject = FFindInBlueprintSearchManager::ConvertJsonStringToObject(UnparsedStringData, LookupTable);
}

bool FImaginaryBlueprint::TrySpecialHandleJsonValue(FString InKey, TSharedPtr< FJsonValue > InJsonValue)
{
	bool bResult = false;

	if(InKey == FFindInBlueprintSearchTags::FiB_Properties.ToString())
	{
		// Pulls out all properties (variables) for this Blueprint
		TArray<TSharedPtr< FJsonValue > > PropertyList = InJsonValue->AsArray();
		for( TSharedPtr< FJsonValue > PropertyValue : PropertyList )
		{
			ParsedChildData.Add(MakeShareable(new FImaginaryProperty(PropertyValue->AsObject(), &LookupTable)));
		}
		bResult = true;
	}
	else if (InKey == FFindInBlueprintSearchTags::FiB_Functions.ToString())
	{
		ParseGraph(InJsonValue, FFindInBlueprintSearchTags::FiB_Functions.ToString(), GT_Function);
		bResult = true;
	}
	else if (InKey == FFindInBlueprintSearchTags::FiB_Macros.ToString())
	{
		ParseGraph(InJsonValue, FFindInBlueprintSearchTags::FiB_Macros.ToString(), GT_Macro);
		bResult = true;
	}
	else if (InKey == FFindInBlueprintSearchTags::FiB_UberGraphs.ToString())
	{
		ParseGraph(InJsonValue, FFindInBlueprintSearchTags::FiB_UberGraphs.ToString(), GT_Ubergraph);
		bResult = true;
	}
	else if (InKey == FFindInBlueprintSearchTags::FiB_SubGraphs.ToString())
	{
		ParseGraph(InJsonValue, FFindInBlueprintSearchTags::FiB_SubGraphs.ToString(), GT_Ubergraph);
		bResult = true;
	}
	else if(InKey == FFindInBlueprintSearchTags::FiB_Components.ToString())
	{
		TArray<TSharedPtr< FJsonValue > > ComponentList = InJsonValue->AsArray();
		TSharedPtr< FJsonObject > ComponentsWrapperObject(new FJsonObject);
		ComponentsWrapperObject->Values.Add(FFindInBlueprintSearchTags::FiB_Components.ToString(), InJsonValue);
		ParsedChildData.Add(MakeShareable(new FCategorySectionHelper(ComponentsWrapperObject, &LookupTable, FFindInBlueprintSearchTags::FiB_Components, false, FCategorySectionHelper::FCategorySectionHelperCallback::CreateRaw(this, &FImaginaryBlueprint::ParseComponents))));
		bResult = true;
	}

	if (!bResult)
	{
		bResult = FImaginaryFiBData::TrySpecialHandleJsonValue(InKey, InJsonValue);
	}
	return bResult;
}

void FImaginaryBlueprint::ParseGraph( TSharedPtr< FJsonValue > InJsonValue, FString InCategoryTitle, EGraphType InGraphType )
{
	TArray<TSharedPtr< FJsonValue > > GraphList = InJsonValue->AsArray();
	for( TSharedPtr< FJsonValue > GraphValue : GraphList )
	{
		ParsedChildData.Add(MakeShareable(new FImaginaryGraph(GraphValue->AsObject(), &LookupTable, InGraphType)));
	}
}

void FImaginaryBlueprint::ParseComponents(TSharedPtr< FJsonObject > InJsonObject, TArray< TSharedPtr<FImaginaryFiBData> >& OutParsedChildData)
{
	// Pulls out all properties (variables) for this Blueprint
	TArray<TSharedPtr< FJsonValue > > ComponentList = InJsonObject->GetArrayField(FFindInBlueprintSearchTags::FiB_Components.ToString());
	for( TSharedPtr< FJsonValue > ComponentValue : ComponentList )
	{
		OutParsedChildData.Add(MakeShareable(new FImaginaryComponent(ComponentValue->AsObject(), &LookupTable)));
	}
}

//////////////////////////
// FImaginaryGraph

FImaginaryGraph::FImaginaryGraph(TSharedPtr< FJsonObject > InUnparsedJsonObject, TMap<int32, FText>* InLookupTablePtr, EGraphType InGraphType)
	: FImaginaryFiBData(InUnparsedJsonObject, InLookupTablePtr)
	, GraphType(InGraphType)
{
}

FSearchResult FImaginaryGraph::CreateSearchResult_Internal(FSearchResult InParent) const
{
	return FSearchResult(new FFindInBlueprintsGraph(FText::GetEmpty(), InParent, GraphType));
}

bool FImaginaryGraph::IsCompatibleWithFilter(ESearchQueryFilter InSearchQueryFilter)
{
	return InSearchQueryFilter == ESearchQueryFilter::AllFilter || 
		InSearchQueryFilter == ESearchQueryFilter::GraphsFilter ||
		(GraphType == GT_Ubergraph && InSearchQueryFilter == ESearchQueryFilter::UberGraphsFilter) ||
		(GraphType == GT_Function && InSearchQueryFilter == ESearchQueryFilter::FunctionsFilter) ||
		(GraphType == GT_Macro && InSearchQueryFilter == ESearchQueryFilter::MacrosFilter);
}

bool FImaginaryGraph::CanCallFilter(ESearchQueryFilter InSearchQueryFilter)
{
	return InSearchQueryFilter == ESearchQueryFilter::PinsFilter ||
		InSearchQueryFilter == ESearchQueryFilter::NodesFilter ||
		(GraphType == GT_Function && InSearchQueryFilter == ESearchQueryFilter::PropertiesFilter) ||
		(GraphType == GT_Function && InSearchQueryFilter == ESearchQueryFilter::VariablesFilter) ||
		FImaginaryFiBData::CanCallFilter(InSearchQueryFilter);
}

ESearchableValueStatus FImaginaryGraph::GetSearchabilityStatus(FString InKey)
{
	// This is a non-ideal way to assign searchability vs being a core display item and will be resolved in future versions of the FiB data in the AR
	if (FindInBlueprintsHelpers::IsTextEqualToString(FFindInBlueprintSearchTags::FiB_Name, InKey)
		|| FindInBlueprintsHelpers::IsTextEqualToString(FFindInBlueprintSearchTags::FiB_NativeName, InKey)
		)
	{
		return ESearchableValueStatus::CoreDisplayItem;
	}

	return ESearchableValueStatus::Searchable;
}

bool FImaginaryGraph::TrySpecialHandleJsonValue(FString InKey, TSharedPtr< FJsonValue > InJsonValue)
{
	if (FindInBlueprintsHelpers::IsTextEqualToString(FFindInBlueprintSearchTags::FiB_Nodes, InKey))
	{
		TArray< TSharedPtr< FJsonValue > > NodeList = InJsonValue->AsArray();

		for( TSharedPtr< FJsonValue > NodeValue : NodeList )
		{
			ParsedChildData.Add(MakeShareable(new FImaginaryGraphNode(NodeValue->AsObject(), LookupTablePtr)));
		}
		return true;
	}
	else if (FindInBlueprintsHelpers::IsTextEqualToString(FFindInBlueprintSearchTags::FiB_Properties, InKey))
	{
		// Pulls out all properties (local variables) for this graph
		TArray<TSharedPtr< FJsonValue > > PropertyList = InJsonValue->AsArray();
		for( TSharedPtr< FJsonValue > PropertyValue : PropertyList )
		{
			ParsedChildData.Add(MakeShareable(new FImaginaryProperty(PropertyValue->AsObject(), LookupTablePtr)));
		}
		return true;
	}
	return false;
}

//////////////////////////////////////
// FImaginaryGraphNode

FImaginaryGraphNode::FImaginaryGraphNode(TSharedPtr< FJsonObject > InUnparsedJsonObject, TMap<int32, FText>* InLookupTablePtr)
	: FImaginaryFiBData(InUnparsedJsonObject, InLookupTablePtr)
{
}

FSearchResult FImaginaryGraphNode::CreateSearchResult_Internal(FSearchResult InParent) const
{
	return FSearchResult(new FFindInBlueprintsGraphNode(FText::GetEmpty(), InParent));
}

bool FImaginaryGraphNode::IsCompatibleWithFilter(ESearchQueryFilter InSearchQueryFilter)
{
	return InSearchQueryFilter == ESearchQueryFilter::AllFilter || InSearchQueryFilter == ESearchQueryFilter::NodesFilter;
}

bool FImaginaryGraphNode::CanCallFilter(ESearchQueryFilter InSearchQueryFilter)
{
	return InSearchQueryFilter == ESearchQueryFilter::PinsFilter ||
		FImaginaryFiBData::CanCallFilter(InSearchQueryFilter);
}

ESearchableValueStatus FImaginaryGraphNode::GetSearchabilityStatus(FString InKey)
{
	// This is a non-ideal way to assign searchability vs being a core display item and will be resolved in future versions of the FiB data in the AR
	if (FindInBlueprintsHelpers::IsTextEqualToString(FFindInBlueprintSearchTags::FiB_Name, InKey)
		|| FindInBlueprintsHelpers::IsTextEqualToString(FFindInBlueprintSearchTags::FiB_NativeName, InKey)
		|| FindInBlueprintsHelpers::IsTextEqualToString(FFindInBlueprintSearchTags::FiB_Comment, InKey)
		)
	{
		return ESearchableValueStatus::CoreDisplayItem;
	}
	else if (FindInBlueprintsHelpers::IsTextEqualToString(FFindInBlueprintSearchTags::FiB_Glyph, InKey)
		|| FindInBlueprintsHelpers::IsTextEqualToString(FFindInBlueprintSearchTags::FiB_GlyphColor, InKey)
		|| FindInBlueprintsHelpers::IsTextEqualToString(FFindInBlueprintSearchTags::FiB_NodeGuid, InKey)
		)
	{
		return ESearchableValueStatus::NotSearchable;
	}
	else if (FindInBlueprintsHelpers::IsTextEqualToString(FFindInBlueprintSearchTags::FiB_ClassName, InKey))
	{
		return ESearchableValueStatus::ExplicitySearchable;
	}
	return ESearchableValueStatus::Searchable;
}

bool FImaginaryGraphNode::TrySpecialHandleJsonValue(FString InKey, TSharedPtr< FJsonValue > InJsonValue)
{
	if (FindInBlueprintsHelpers::IsTextEqualToString(FFindInBlueprintSearchTags::FiB_Pins, InKey))
	{
		TArray< TSharedPtr< FJsonValue > > PinsList = InJsonValue->AsArray();

		for( TSharedPtr< FJsonValue > Pin : PinsList )
		{
			ParsedChildData.Add(MakeShareable(new FImaginaryPin(Pin->AsObject(), LookupTablePtr, SchemaName)));
		}
		return true;
	}
	else if (FindInBlueprintsHelpers::IsTextEqualToString(FFindInBlueprintSearchTags::FiB_SchemaName, InKey))
	{
		// Previously extracted
		return true;
	}

	return false;
}

void FImaginaryGraphNode::ParseAllChildData()
{
	if (UnparsedJsonObject.IsValid())
	{
		TSharedPtr< FJsonObject > JsonObject = UnparsedJsonObject;
		// Very important to get the schema first, other bits of data depend on it
		TSharedPtr< FJsonValue > SchemaNameValue = JsonObject->GetField< EJson::String >(FFindInBlueprintSearchTags::FiB_SchemaName.ToString());
		if(SchemaNameValue.IsValid())
		{
			SchemaName = FindInBlueprintsHelpers::AsFText(SchemaNameValue, *LookupTablePtr).ToString();
		}

		FImaginaryFiBData::ParseAllChildData();
	}
}

///////////////////////////////////////////
// FImaginaryProperty

FImaginaryProperty::FImaginaryProperty(TSharedPtr< FJsonObject > InUnparsedJsonObject, TMap<int32, FText>* InLookupTablePtr)
	: FImaginaryFiBData(InUnparsedJsonObject, InLookupTablePtr)
{
}

bool FImaginaryProperty::IsCompatibleWithFilter(ESearchQueryFilter InSearchQueryFilter)
{
	return InSearchQueryFilter == ESearchQueryFilter::AllFilter || 
		InSearchQueryFilter == ESearchQueryFilter::PropertiesFilter || 
		InSearchQueryFilter == ESearchQueryFilter::VariablesFilter;
}

FSearchResult FImaginaryProperty::CreateSearchResult_Internal(FSearchResult InParent) const
{
	return FSearchResult(new FFindInBlueprintsProperty(FText::GetEmpty(), InParent));
}

ESearchableValueStatus FImaginaryProperty::GetSearchabilityStatus(FString InKey)
{
	// This is a non-ideal way to assign searchability vs being a core display item and will be resolved in future versions of the FiB data in the AR
	if (FindInBlueprintsHelpers::IsTextEqualToString(FFindInBlueprintSearchTags::FiB_Name, InKey)
		|| FindInBlueprintsHelpers::IsTextEqualToString(FFindInBlueprintSearchTags::FiB_NativeName, InKey)
		)
	{
		return ESearchableValueStatus::CoreDisplayItem;
	}
	else if (FindInBlueprintsHelpers::IsTextEqualToString(FFindInBlueprintSearchTags::FiB_PinCategory, InKey)
		|| FindInBlueprintsHelpers::IsTextEqualToString(FFindInBlueprintSearchTags::FiB_PinSubCategory, InKey)
		|| FindInBlueprintsHelpers::IsTextEqualToString(FFindInBlueprintSearchTags::FiB_ObjectClass, InKey)
		|| FindInBlueprintsHelpers::IsTextEqualToString(FFindInBlueprintSearchTags::FiB_IsArray, InKey)
		|| FindInBlueprintsHelpers::IsTextEqualToString(FFindInBlueprintSearchTags::FiB_IsReference, InKey)
		|| FindInBlueprintsHelpers::IsTextEqualToString(FFindInBlueprintSearchTags::FiB_IsSCSComponent, InKey)
		)
	{
		return ESearchableValueStatus::ExplicitySearchableHidden;
	}
	return ESearchableValueStatus::Searchable;
}

//////////////////////////////
// FImaginaryComponent

FImaginaryComponent::FImaginaryComponent(TSharedPtr< FJsonObject > InUnparsedJsonObject, TMap<int32, FText>* InLookupTablePtr)
	: FImaginaryProperty(InUnparsedJsonObject, InLookupTablePtr)
{
}

bool FImaginaryComponent::IsCompatibleWithFilter(ESearchQueryFilter InSearchQueryFilter)
{
	return FImaginaryProperty::IsCompatibleWithFilter(InSearchQueryFilter) || InSearchQueryFilter == ESearchQueryFilter::ComponentsFilter;
}

//////////////////////////////
// FImaginaryPin

FImaginaryPin::FImaginaryPin(TSharedPtr< FJsonObject > InUnparsedJsonObject, TMap<int32, FText>* InLookupTablePtr, FString InSchemaName)
	: FImaginaryFiBData(InUnparsedJsonObject, InLookupTablePtr)
	, SchemaName(InSchemaName)
{
}

bool FImaginaryPin::IsCompatibleWithFilter(ESearchQueryFilter InSearchQueryFilter)
{
	return InSearchQueryFilter == ESearchQueryFilter::AllFilter || InSearchQueryFilter == ESearchQueryFilter::PinsFilter;
}

FSearchResult FImaginaryPin::CreateSearchResult_Internal(FSearchResult InParent) const
{
	return FSearchResult(new FFindInBlueprintsPin(FText::GetEmpty(), InParent, SchemaName));
}

ESearchableValueStatus FImaginaryPin::GetSearchabilityStatus(FString InKey)
{
	if (FindInBlueprintsHelpers::IsTextEqualToString(FFindInBlueprintSearchTags::FiB_Name, InKey)
		|| FindInBlueprintsHelpers::IsTextEqualToString(FFindInBlueprintSearchTags::FiB_NativeName, InKey)
		)
	{
		return ESearchableValueStatus::CoreDisplayItem;
	}
	else if (FindInBlueprintsHelpers::IsTextEqualToString(FFindInBlueprintSearchTags::FiB_PinCategory, InKey)
		|| FindInBlueprintsHelpers::IsTextEqualToString(FFindInBlueprintSearchTags::FiB_PinSubCategory, InKey)
		|| FindInBlueprintsHelpers::IsTextEqualToString(FFindInBlueprintSearchTags::FiB_ObjectClass, InKey)
		|| FindInBlueprintsHelpers::IsTextEqualToString(FFindInBlueprintSearchTags::FiB_IsArray, InKey)
		|| FindInBlueprintsHelpers::IsTextEqualToString(FFindInBlueprintSearchTags::FiB_IsReference, InKey)
		|| FindInBlueprintsHelpers::IsTextEqualToString(FFindInBlueprintSearchTags::FiB_IsSCSComponent, InKey)
		)
	{
		return ESearchableValueStatus::ExplicitySearchableHidden;
	}
	return ESearchableValueStatus::Searchable;
}

bool FImaginaryPin::TrySpecialHandleJsonValue(FString InKey, TSharedPtr< FJsonValue > InJsonValue)
{
	return false;
}

#undef LOCTEXT_NAMESPACE