// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Animation/AnimNodeBase.h"
#include "AnimNode_AssetPlayerBase.h"
#include "AnimNode_SequencePlayer.generated.h"

#pragma once

// Sequence player node
USTRUCT()
struct ENGINE_API FAnimNode_SequencePlayer : public FAnimNode_AssetPlayerBase
{
	GENERATED_USTRUCT_BODY()
public:
	// The animation sequence asset to play
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PinHiddenByDefault))
	UAnimSequenceBase* Sequence;

	// Should the animation continue looping when it reaches the end?
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PinHiddenByDefault))
	mutable bool bLoopAnimation;

	// The play rate multiplier. Can be negative, which will cause the animation to play in reverse.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PinHiddenByDefault))
	mutable float PlayRate;

	// The start up position, it only applies when reinitialized
	// if you loop, it will still start from 0.f after finishing the round
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category=Settings, meta=(PinHiddenByDefault))
	mutable float StartPosition;

	// The group index, assigned at compile time based on the editoronly GroupName (or INDEX_NONE if it is not part of any group)
	UPROPERTY()
	int32 GroupIndex;

	// The role this player can assume within the group (ignored if GroupIndex is INDEX_NONE)
	UPROPERTY()
	TEnumAsByte<EAnimGroupRole::Type> GroupRole;
public:	
	FAnimNode_SequencePlayer()
		: Sequence(NULL)
		, bLoopAnimation(true)
		, PlayRate(1.0f)
		, StartPosition(0.f)
		, GroupIndex(INDEX_NONE)
	{
	}

	// FAnimNode_Base interface
	virtual void Initialize(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones(const FAnimationCacheBonesContext& Context) override;
	virtual void UpdateAssetPlayer(const FAnimationUpdateContext& Context) override;
	virtual void Evaluate(FPoseContext& Output) override;
	virtual void OverrideAsset(UAnimationAsset* NewAsset) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;
	// End of FAnimNode_Base interface

	UAnimationAsset* GetAnimAsset() {return Sequence;}

	float GetTimeFromEnd(float CurrentNodeTime);
};
