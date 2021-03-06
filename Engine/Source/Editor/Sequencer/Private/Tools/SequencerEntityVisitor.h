// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

#pragma once

class FSequencerDisplayNode;
class UMovieSceneSection;
class IKeyArea;
class FSequencerSectionKeyAreaNode;
class ISequencerSection;

/** Enum of different types of entities that are available in the sequencer */
namespace ESequencerEntity
{
	enum Type
	{
		Key			= 1<<0,
		Section		= 1<<1,
	};

	static const uint32 Everything = (uint32)-1;
};

/** Visitor class used to handle specific sequencer entities */
struct ISequencerEntityVisitor
{
	ISequencerEntityVisitor(uint32 InEntityMask = ESequencerEntity::Everything) : EntityMask(InEntityMask) {}

	virtual void VisitKey(FKeyHandle KeyHandle, float KeyTime, const TSharedPtr<IKeyArea>& KeyArea, UMovieSceneSection* Section) const { }
	virtual void VisitSection(UMovieSceneSection* Section) const { }
	
	/** Check if the specified type of entity is applicable to this visitor */
	bool CheckEntityMask(ESequencerEntity::Type Type) const { return (EntityMask & Type) != 0; }

protected:

	/** Bitmask of allowable entities */
	uint32 EntityMask;
};

/** A range specifying time (and possibly vertical) bounds in the sequencer */
struct FSequencerEntityRange
{
	FSequencerEntityRange(const TRange<float>& InRange);
	FSequencerEntityRange(FVector2D TopLeft, FVector2D BottomRight);

	/** Check whether the specified section intersects this range */
	bool IntersectSection(const UMovieSceneSection* InSection) const;

	/** Check whether the specified node intersects this range */
	bool IntersectNode(const FSequencerDisplayNode& InNode) const;

	/** Check whether the specified node's key area intersects this range */
	bool IntersectKeyArea(const FSequencerDisplayNode& InNode, float VirtualKeyHeight) const;

	/** Start/end times */
	float StartTime, EndTime;

	/** Optional vertical bounds */
	TOptional<float> VerticalTop, VerticalBottom;
};

/** Struct used to iterate a two dimensional *visible* range with a user-supplied visitor */
struct FSequencerEntityWalker
{
	/** Construction from the range itself, and an optional virtual key size, where key bounds must be taken into consideration */
	FSequencerEntityWalker(const FSequencerEntityRange& InRange, FVector2D InVirtualKeySize = FVector2D());

	/** Visit the specified nodes (recursively) with this range and a user-supplied visitor */
	void Traverse(const ISequencerEntityVisitor& Visitor, const TArray< TSharedRef<FSequencerDisplayNode> >& Nodes);

private:

	/** Handle visitation of a particular node */
	void HandleNode(const ISequencerEntityVisitor& Visitor, FSequencerDisplayNode& InNode);
	/** Handle visitation of a particular node, along with a set of sections */
	void HandleNode(const ISequencerEntityVisitor& Visitor, FSequencerDisplayNode& InNode, TArray<TSharedRef<ISequencerSection>> InSections);
	/** Handle visitation of a key area node */
	void HandleKeyAreaNode(const ISequencerEntityVisitor& Visitor, FSequencerSectionKeyAreaNode& InKeyAreaNode, FSequencerDisplayNode& InOwnerNode, const TArray<TSharedRef<ISequencerSection>>& InSections);
	/** Handle visitation of a key area */
	void HandleKeyArea(const ISequencerEntityVisitor& Visitor, const TSharedPtr<IKeyArea>& KeyArea, UMovieSceneSection* Section);

	/** The bounds of the range */
	FSequencerEntityRange Range;

	/** Key size in virtual space */
	FVector2D VirtualKeySize;
};