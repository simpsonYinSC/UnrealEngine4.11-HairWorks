// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "PartyPrivatePCH.h"
#include "PartyGameState.h"
#include "PartyMemberState.h"
#include "Party.h"
#include "PartyBeaconClient.h"
#include "Online.h"
#include "OnlineSubsystemUtils.h"

namespace PartyConsoleVariables
{
	// CVars
	TAutoConsoleVariable<int32> CVarAcceptJoinsDuringLoad(
		TEXT("Party.CVarAcceptJoinsDuringLoad"),
		1,
		TEXT("Enables joins while leader is trying to load into a game\n")
		TEXT("1 Enables. 0 disables."),
		ECVF_Default);
}


UPartyGameState::UPartyGameState(const FObjectInitializer& ObjectInitializer) :
	Super(ObjectInitializer),
	OwningUserId(nullptr),
	bDebugAcceptingMembers(false),
	PartyInfo(nullptr),
	bPromotionLockoutState(false),
	bStayWithPartyOnDisconnect(false),
	ReservationBeaconClientClass(nullptr),
	ReservationBeaconClient(nullptr),
	PartyStateRef(nullptr),
	PartyStateRefDef(nullptr)
{
	PartyMemberStateClass = UPartyMemberState::StaticClass();
	ReservationBeaconClientClass = APartyBeaconClient::StaticClass();

	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
	}
}

void UPartyGameState::BeginDestroy()
{
	Super::BeginDestroy();
	OnShutdown();
}

void UPartyGameState::OnShutdown()
{
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		UnregisterFrontendDelegates();
	}

	PartyInfo.Reset();
	PartyMembersState.Empty();

	if (PartyStateRefDef)
	{
		if (PartyStateRefScratch)
		{
			PartyStateRefDef->DestroyStruct(PartyStateRefScratch);
			FMemory::Free(PartyStateRefScratch);
		}
		PartyStateRefDef = nullptr;
	}

	PartyStateRef = nullptr;
}

void UPartyGameState::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	Super::AddReferencedObjects(InThis, Collector);

	UPartyGameState* This = CastChecked<UPartyGameState>(InThis);
	check(This);

	TArray<UPartyMemberState*> PartyMembers;
	This->PartyMembersState.GenerateValueArray(PartyMembers);
	Collector.AddReferencedObjects(PartyMembers);
}

void UPartyGameState::RegisterFrontendDelegates()
{
	UWorld* World = GetWorld();

	UnregisterFrontendDelegates();
}

void UPartyGameState::UnregisterFrontendDelegates()
{
}

void UPartyGameState::ResetPartyState()
{
	if (PartyStateRef)
	{
		// Reset Party State
		PartyStateRef->Reset();
	}
}

void UPartyGameState::ResetPartySize()
{
	UParty* Party = GetPartyOuter();
	SetPartyMaxSize(Party->GetDefaultPartyMaxSize());
}

void UPartyGameState::ResetLocalPlayerState()
{
	UWorld* World = GetWorld();
	AGameState* GameState = World->GetGameState();
	check(GameState);

	for (const APlayerState* PlayerState : GameState->PlayerArray)
	{
		if (PlayerState && PlayerState->UniqueId.IsValid())
		{
			UPartyMemberState** LocalPartyMemberStatePtr = PartyMembersState.Find(PlayerState->UniqueId);
			UPartyMemberState* LocalPartyMemberState = LocalPartyMemberStatePtr ? *LocalPartyMemberStatePtr : nullptr;
			if (LocalPartyMemberState)
			{
				LocalPartyMemberState->Reset();
			}
		}
	}
}

void UPartyGameState::Init(const FUniqueNetId& LocalUserId, TSharedPtr<FOnlinePartyInfo>& InPartyInfo)
{
	OwningUserId.SetUniqueNetId(LocalUserId.AsShared());
	PartyInfo = InPartyInfo;
	CurrentConfig = *PartyInfo->Config;
	bDebugAcceptingMembers = CurrentConfig.bIsAcceptingMembers;

	// Last since it needs the party info/id set first
	RegisterFrontendDelegates();
}

void UPartyGameState::InitFromCreate(const FUniqueNetId& LocalUserId, TSharedPtr<FOnlinePartyInfo>& InPartyInfo)
{
	Init(LocalUserId, InPartyInfo);

	// Setup initial data for the party
	ResetPartyState();

	FUniqueNetIdRepl MemberId(LocalUserId.AsShared());
	UpdatePartyData(MemberId);

	// Broadcast join
	UParty* Party = GetPartyOuter();
	check(Party);
	Party->OnPartyJoined().Broadcast(this);

	// Update local player data and broadcast member data updates
	SendLocalPlayerPartyData();
}

void UPartyGameState::InitFromJoin(const FUniqueNetId& LocalUserId, TSharedPtr<FOnlinePartyInfo>& InPartyInfo)
{
	Init(LocalUserId, InPartyInfo);

	// Broadcast join
	UParty* Party = GetPartyOuter();
	check(Party);
	Party->OnPartyJoined().Broadcast(this);

	// Update local player data and broadcast member data updates
	SendLocalPlayerPartyData();
}

void UPartyGameState::PreClientTravel()
{
	if (!PartyConsoleVariables::CVarAcceptJoinsDuringLoad.GetValueOnGameThread())
	{
		// Possibly deal with pending approvals?
		RejectAllPendingJoinRequests();
	}
	CleanupReservationBeacon();

	UnregisterFrontendDelegates();
}

bool UPartyGameState::ResetForFrontend()
{
	UE_LOG(LogParty, Verbose, TEXT("Resetting parties for frontend"));

	bool bSuccess = false;

	CleanupReservationBeacon();

	if (PartyInfo.IsValid())
	{
		UWorld* World = GetWorld();
		IOnlinePartyPtr PartyInt = Online::GetPartyInterface(World);
		if (PartyInt.IsValid())
		{
			TSharedPtr<FOnlinePartyInfo> ExistingPartyInfo = PartyInt->GetPartyInfo(*OwningUserId, *PartyInfo->PartyId);
			if (ExistingPartyInfo.IsValid())
			{
				bSuccess = true;

				Init(*OwningUserId, ExistingPartyInfo);
				bStayWithPartyOnDisconnect = false;

				ResetPartyState();
				ResetLocalPlayerState();

				TSharedPtr<const FUniqueNetId> PartyLeaderId = GetPartyLeader();
				bool bIsPartyLeader = (PartyLeaderId.IsValid() && *PartyLeaderId == *OwningUserId);

				// Refresh local player data
				SendLocalPlayerPartyData();

				// Remove members we have, but lower level doesn't know about
				for (const auto& PartyMember : PartyMembersState)
				{
					const FUniqueNetIdRepl& MemberId = PartyMember.Key;
					if (MemberId.IsValid())
					{
						TSharedPtr<FOnlinePartyMember> CheckPartyMember = PartyInt->GetPartyMember(*OwningUserId, *PartyInfo->PartyId, *MemberId);
						if (!CheckPartyMember.IsValid())
						{
							UE_LOG(LogParty, Verbose, TEXT("[%s] Player %s left during fixup"), *PartyInfo->PartyId->ToString(), *MemberId.ToString());
							HandlePartyMemberLeft(*MemberId, EMemberExitedReason::Left);
						}
					}
				}

				// Add members we don't have, but lower level does
				TArray<TSharedRef<FOnlinePartyMember>> PartyMembers;
				PartyInt->GetPartyMembers(*OwningUserId, *PartyInfo->PartyId, PartyMembers);
				for (const auto& PartyMember : PartyMembers)
				{
					TSharedRef<const FUniqueNetId> MemberId = PartyMember->GetUserId();

					FUniqueNetIdRepl UniqueId(MemberId);
					UPartyMemberState** CurrentPartyMemberDataPtr = PartyMembersState.Find(UniqueId);
					UPartyMemberState* CurrentPartyMemberData = CurrentPartyMemberDataPtr ? *CurrentPartyMemberDataPtr : nullptr;
					if (!CurrentPartyMemberData)
					{
						TSharedPtr<FOnlinePartyData> PartyMemberData = PartyInt->GetPartyMemberData(*OwningUserId, *PartyInfo->PartyId, *MemberId);
						if (PartyMemberData.IsValid())
						{
							UE_LOG(LogParty, Verbose, TEXT("[%s] Player %s data received during fixup"), *PartyInfo->PartyId->ToString(), *UniqueId.ToString());
							HandlePartyMemberDataReceived(*MemberId, PartyMemberData.ToSharedRef());
						}
					}
				}

				if (bIsPartyLeader)
				{
					UpdatePartyData(OwningUserId);
					ResetPartySize();
					UpdateAcceptingMembers();
				}
			}
			else
			{
				UE_LOG(LogParty, Warning, TEXT("Party interface can't find party during reset!"));
			}
		}
		else
		{
			UE_LOG(LogParty, Warning, TEXT("Invalid party interface during reset!"));
		}
	}
	else
	{
		UE_LOG(LogParty, Warning, TEXT("Invalid party info during reset!"));
	}

	if (!bSuccess)
	{
		PartyInfo = nullptr;

		ResetPartyState();
		ResetLocalPlayerState();
		UnregisterFrontendDelegates();
	}

	return bSuccess;
}

UPartyMemberState* UPartyGameState::CreateNewPartyMember(const FUniqueNetId& InMemberId)
{
	UPartyMemberState* NewPartyMemberState = nullptr;

	if (InMemberId.IsValid())
	{
		UWorld* World = GetWorld();
		IOnlinePartyPtr PartyInt = Online::GetPartyInterface(World);
		if (PartyInt.IsValid())
		{
			TSharedPtr<FOnlinePartyMember> PartyMember = PartyInt->GetPartyMember(*OwningUserId, *PartyInfo->PartyId, InMemberId);
			if (PartyMember.IsValid())
			{
				NewPartyMemberState = NewObject<UPartyMemberState>(this, PartyMemberStateClass);
				NewPartyMemberState->UniqueId.SetUniqueNetId(PartyMember->GetUserId());
				NewPartyMemberState->DisplayName = FText::FromString(PartyMember->GetDisplayName());
			}
			else
			{
				UE_LOG(LogParty, Warning, TEXT("CreateNewPartyMember: Invalid party member %s"), *InMemberId.ToString());
			}
		}
		else
		{
			UE_LOG(LogParty, Warning, TEXT("CreateNewPartyMember: No party interface."));
		}
	}
	else
	{
		UE_LOG(LogParty, Warning, TEXT("CreateNewPartyMember: Invalid member id."));
	}

	return NewPartyMemberState;
}

void UPartyGameState::HandlePartyConfigChanged(const TSharedRef<FPartyConfiguration>& InPartyConfig)
{
	UE_LOG(LogParty, VeryVerbose, TEXT("[%s] HandlePartyConfigChanged"), PartyInfo.IsValid() ? *PartyInfo->PartyId->ToString() : TEXT("INVALID"));
	if (PartyInfo.IsValid())
	{
		CurrentConfig = *PartyInfo->Config;
		bDebugAcceptingMembers = CurrentConfig.bIsAcceptingMembers;
	}
}

void UPartyGameState::HandlePartyMemberJoined(const FUniqueNetId& InMemberId)
{
	UE_LOG(LogParty, VeryVerbose, TEXT("[%s] HandlePartyMemberJoined %s"), PartyInfo.IsValid() ? *PartyInfo->PartyId->ToString() : TEXT("INVALID"), *InMemberId.ToString());

 	TSharedRef<const FUniqueNetId> IdRef = InMemberId.AsShared();
 	FUniqueNetIdRepl MemberId(IdRef);
 	if (MemberId.IsValid())
	{
		UPartyMemberState* PartyMemberState = CreateNewPartyMember(InMemberId);
		if (ensure(PartyMemberState))
		{
			PartyMembersState.Add(MemberId, PartyMemberState);

			UParty* Party = GetPartyOuter();
			check(Party);
			Party->OnPartyMemberJoined().Broadcast(this, MemberId);
			PartyMemberState->bHasAnnouncedJoin = true;
			UpdateAcceptingMembers();
		}	
	}
}

void UPartyGameState::HandlePartyMemberLeft(const FUniqueNetId& InMemberId, EMemberExitedReason Reason)
{
	UE_LOG(LogParty, VeryVerbose, TEXT("[%s] HandlePartyMemberLeft %s"), PartyInfo.IsValid() ? *PartyInfo->PartyId->ToString() : TEXT("INVALID"), *InMemberId.ToString());

	if (InMemberId.IsValid())
	{
		TSharedRef<const FUniqueNetId> IdRef = InMemberId.AsShared();
		FUniqueNetIdRepl MemberId(IdRef);
		
		UParty* Party = GetPartyOuter();
		check(Party);
		Party->OnPartyMemberLeaving().Broadcast(this, MemberId, Reason);

		PartyMembersState.Remove(MemberId);
		Party->OnPartyMemberLeft().Broadcast(this, MemberId, Reason);

		// Update party join state, will cause a failure on leader promotion currently
		// because we can't tell the difference between "expected leader" and "actually the new leader"
		UpdateAcceptingMembers();
	}
}

void UPartyGameState::HandlePartyMemberPromoted(const FUniqueNetId& InMemberId)
{
	UE_LOG(LogParty, VeryVerbose, TEXT("[%s] HandlePartyMemberPromoted %s"), PartyInfo.IsValid() ? *PartyInfo->PartyId->ToString() : TEXT("INVALID"), *InMemberId.ToString());

	if (InMemberId.IsValid())
	{
		TSharedRef<const FUniqueNetId> IdRef = InMemberId.AsShared();
		FUniqueNetIdRepl MemberId(IdRef);

		UParty* Party = GetPartyOuter();
		check(Party);
		Party->OnPartyMemberPromoted().Broadcast(this, MemberId);
	}

	// Now that the leader is gone and a new leader established, make sure the accepting state is correct
	UpdateAcceptingMembers();
}

void UPartyGameState::ComparePartyData(const FPartyState& OldPartyData, const FPartyState& NewPartyData)
{
	TSharedPtr<const FUniqueNetId> PartyLeaderId = GetPartyLeader();

	// Client passenger view delegates, leader won't get these because they are driving
	if (PartyLeaderId.IsValid() && *PartyLeaderId != *OwningUserId)
	{
		if (OldPartyData.PartyType != NewPartyData.PartyType)
		{
			OnPartyTypeChanged().Broadcast(NewPartyData.PartyType);
		}

		if (OldPartyData.bLeaderFriendsOnly != NewPartyData.bLeaderFriendsOnly)
		{
			OnLeaderFriendsOnlyChanged().Broadcast(NewPartyData.bLeaderFriendsOnly);
		}

		if (OldPartyData.bLeaderInvitesOnly != NewPartyData.bLeaderInvitesOnly)
		{
			OnLeaderInvitesOnlyChanged().Broadcast(NewPartyData.bLeaderInvitesOnly);
		}
	}
}

void UPartyGameState::HandlePartyDataReceived(const TSharedRef<FOnlinePartyData>& InPartyData)
{
	UE_LOG(LogParty, VeryVerbose, TEXT("[%s] HandlePartyDataReceived"), PartyInfo.IsValid() ? *PartyInfo->PartyId->ToString() : TEXT("INVALID"));

	UWorld* World = GetWorld();
	IOnlinePartyPtr PartyInt = Online::GetPartyInterface(World);
	if (PartyInt.IsValid())
	{
		check(PartyStateRefDef && PartyStateRef);
		if (FVariantDataConverter::VariantMapToUStruct(InPartyData->KeyValAttrs, PartyStateRefDef, PartyStateRefScratch, 0, CPF_Transient | CPF_RepSkip))
		{
			ComparePartyData(*PartyStateRef, *PartyStateRefScratch);

			ensure(PartyStateRefDef->GetCppStructOps()->Copy(PartyStateRef, PartyStateRefScratch, 1));
			OnPartyDataChanged().Broadcast(*PartyStateRef);
		}
		else
		{
			UE_LOG(LogParty, Warning, TEXT("[%s] Failed to serialize party data!"));
		}
	}
}

void UPartyGameState::HandlePartyMemberDataReceived(const FUniqueNetId& InMemberId, const TSharedRef<FOnlinePartyData>& InPartyMemberData)
{
	UE_LOG(LogParty, VeryVerbose, TEXT("[%s] HandlePartyMemberDataReceived %s"), PartyInfo.IsValid() ? *PartyInfo->PartyId->ToString() : TEXT("INVALID"), *InMemberId.ToString());

	UWorld* World = GetWorld();
	IOnlinePartyPtr PartyInt = Online::GetPartyInterface(World);
	if (PartyInt.IsValid())
	{
		FUniqueNetIdRepl UniqueId(InMemberId.AsShared());
		UPartyMemberState** CurrentPartyMemberDataPtr = PartyMembersState.Find(UniqueId);
		UPartyMemberState* CurrentPartyMemberData = CurrentPartyMemberDataPtr ? *CurrentPartyMemberDataPtr : nullptr;
		if (!CurrentPartyMemberData)
		{
			CurrentPartyMemberData = CreateNewPartyMember(InMemberId);
			if (CurrentPartyMemberData)
			{
				PartyMembersState.Add(UniqueId, CurrentPartyMemberData);
			}
		}

		if (ensure(CurrentPartyMemberData))
		{
			if (!CurrentPartyMemberData->bHasAnnouncedJoin)
			{
				// Both local and remote players will announce joins
				UParty* Party = GetPartyOuter();
				check(Party);
				Party->OnPartyMemberJoined().Broadcast(this, UniqueId);
				CurrentPartyMemberData->bHasAnnouncedJoin = true;
			}

			check(CurrentPartyMemberData->MemberStateRefDef && CurrentPartyMemberData->MemberStateRefScratch);

			// Copy out the old data
			ensure(CurrentPartyMemberData->MemberStateRefDef->GetCppStructOps()->Copy(CurrentPartyMemberData->MemberStateRefScratch, CurrentPartyMemberData->MemberStateRef, 1));
			if (FVariantDataConverter::VariantMapToUStruct(InPartyMemberData->KeyValAttrs, CurrentPartyMemberData->MemberStateRefDef, CurrentPartyMemberData->MemberStateRef, 0, CPF_Transient | CPF_RepSkip))
			{
				// Broadcast property changes
				CurrentPartyMemberData->ComparePartyMemberData(*CurrentPartyMemberData->MemberStateRefScratch);
				OnPartyMemberDataChanged().Broadcast(CurrentPartyMemberData->UniqueId, CurrentPartyMemberData);
			}
			else
			{
				UE_LOG(LogParty, Warning, TEXT("[%s] Failed to serialize party member data!"), *InMemberId.ToString());
				ensure(CurrentPartyMemberData->MemberStateRefDef->GetCppStructOps()->Copy(CurrentPartyMemberData->MemberStateRef, CurrentPartyMemberData->MemberStateRefScratch, 1));
			}
		}

	}
}

void UPartyGameState::HandleLeavingParty()
{
	// Stop listening for delegates as we leave
	UnregisterFrontendDelegates();
}

void UPartyGameState::HandleRemovedFromParty(EMemberExitedReason Reason)
{
	// Trigger delegate first
	UParty* Party = GetPartyOuter();
	check(Party);
	Party->OnPartyLeft().Broadcast(this, Reason);
	// Cleanup
	OnShutdown();
}

void UPartyGameState::HandleLockoutPromotionStateChange(bool bNewLockoutState)
{
	bPromotionLockoutState = bNewLockoutState;
}

EApprovalAction UPartyGameState::ProcessJoinRequest(const FUniqueNetId& RecipientId, const FUniqueNetId& SenderId, EJoinPartyDenialReason& DenialReason)
{
	return EApprovalAction::Approve;
}

void UPartyGameState::HandlePartyJoinRequestReceived(const FUniqueNetId& RecipientId, const FUniqueNetId& SenderId)
{
#if 0 // auto approve requests for debugging error case
	UWorld* World = GetWorld();
	IOnlinePartyPtr PartyInt = Online::GetPartyInterface(World);
	if (PartyInt.IsValid())
	{
		PartyInt->ApproveJoinRequest(RecipientId, *PartyInfo->PartyId, SenderId, true);
	}
	return;
#endif

	EApprovalAction ApprovalAction = EApprovalAction::Deny;
	EJoinPartyDenialReason DenialReason = EJoinPartyDenialReason::Busy;

	if (PartyInfo.IsValid())
	{
		int32 NumPartyMembers = GetPartySize();
		int32 MaxPartyMembers = CurrentConfig.MaxMembers;
		if (NumPartyMembers < MaxPartyMembers)
		{
			if (IsInJoinableGameState())
			{
				TSharedPtr<const FUniqueNetId> PartyLeader = GetPartyLeader();
				if (PartyLeader.IsValid() && *OwningUserId == *PartyLeader)
				{
					ApprovalAction = ProcessJoinRequest(RecipientId, SenderId, DenialReason);
				}
				else
				{
					// Party leader has changed
					DenialReason = EJoinPartyDenialReason::NotPartyLeader;
				}
			}
			else
			{
				// Game is full
				DenialReason = EJoinPartyDenialReason::GameFull;
			}
		}
		else
		{
			DenialReason = EJoinPartyDenialReason::PartyFull;
		}
	}
	else
	{
		DenialReason = EJoinPartyDenialReason::Busy;
	}

	if (ApprovalAction == EApprovalAction::Enqueue ||
		ApprovalAction == EApprovalAction::EnqueueAndStartBeacon)
	{
		// Enqueue for a more opportune time
		UE_LOG(LogParty, Verbose, TEXT("[%s] Enqueuing approval request for %s"), PartyInfo.IsValid() ? *PartyInfo->PartyId->ToString() : TEXT("INVALID"), *SenderId.ToString());
		
		FPendingMemberApproval PendingApproval;
		PendingApproval.RecipientId.SetUniqueNetId(RecipientId.AsShared());
		PendingApproval.SenderId.SetUniqueNetId(SenderId.AsShared());
		PendingApprovals.Enqueue(PendingApproval);

		if (ReservationBeaconClient == nullptr &&
			ApprovalAction == EApprovalAction::EnqueueAndStartBeacon)
		{
			ConnectToReservationBeacon();
		}
	}
	else
	{
		bool bApproveRequest = (ApprovalAction == EApprovalAction::Approve);

		// Respond now
		UE_LOG(LogParty, Verbose, TEXT("[%s] Responding to approval request for %s with %s"), PartyInfo.IsValid() ? *PartyInfo->PartyId->ToString() : TEXT("INVALID"), *SenderId.ToString(), bApproveRequest ? TEXT("approved") : TEXT("denied"));
		
		UWorld* World = GetWorld();
		IOnlinePartyPtr PartyInt = Online::GetPartyInterface(World);
		if (PartyInt.IsValid())
		{
			PartyInt->ApproveJoinRequest(RecipientId, *PartyInfo->PartyId, SenderId, bApproveRequest, (int32)(!bApproveRequest ? DenialReason : EJoinPartyDenialReason::NoReason));
		}
	}
}

TSharedPtr<const FOnlinePartyId> UPartyGameState::GetPartyId() const
{
	if (PartyInfo.IsValid())
	{
		return PartyInfo->PartyId;
	}

	return nullptr;
}

void UPartyGameState::SetPartyType(EPartyType InPartyType, bool bLeaderFriendsOnly, bool bLeaderInvitesOnly)
{
	if (*OwningUserId == *GetPartyLeader())
	{
		check(PartyStateRef);
		if (PartyStateRef->PartyType != InPartyType ||
			PartyStateRef->bLeaderFriendsOnly != bLeaderFriendsOnly ||
			PartyStateRef->bLeaderInvitesOnly != bLeaderInvitesOnly)
		{
			bool bIsPrivate = (InPartyType == EPartyType::Private);

			CurrentConfig.Permissions = bIsPrivate ? EPartyPermissions::Private : EPartyPermissions::Public;
			CurrentConfig.bIsAcceptingMembers = bIsPrivate ? false : CurrentConfig.bIsAcceptingMembers;
			CurrentConfig.bShouldPublishToPresence = !bIsPrivate;
			CurrentConfig.bCanNonLeaderPublishToPresence = !bIsPrivate && !bLeaderFriendsOnly;
			CurrentConfig.bCanNonLeaderInviteToParty = !bLeaderInvitesOnly;
			UpdatePartyConfig(bIsPrivate);

			if (PartyStateRef->PartyType != InPartyType)
			{
				OnPartyTypeChanged().Broadcast(InPartyType);
			}

			if (PartyStateRef->bLeaderFriendsOnly != bLeaderFriendsOnly)
			{
				OnLeaderFriendsOnlyChanged().Broadcast(bLeaderFriendsOnly);
			}

			if (PartyStateRef->bLeaderInvitesOnly != bLeaderInvitesOnly)
			{
				OnLeaderInvitesOnlyChanged().Broadcast(bLeaderInvitesOnly);
			}

			PartyStateRef->PartyType = InPartyType;
			PartyStateRef->bLeaderFriendsOnly = bLeaderFriendsOnly;
			PartyStateRef->bLeaderInvitesOnly = bLeaderInvitesOnly;
			UpdatePartyData(OwningUserId);

			// Refresh accepting members, taking everything into account
			UpdateAcceptingMembers();
		}
	}
}

void UPartyGameState::StayWithPartyOnExit(bool bInStayWithParty)
{
	bStayWithPartyOnDisconnect = bInStayWithParty;
}

bool UPartyGameState::ShouldStayWithPartyOnExit() const
{
	return bStayWithPartyOnDisconnect;
}

int32 UPartyGameState::GetPartySize() const
{
	return PartyMembersState.Num();
}

void UPartyGameState::SetPartyMaxSize(int32 NewSize)
{
	if (PartyInfo.IsValid())
	{
		if (CurrentConfig.MaxMembers != NewSize)
		{
			UParty* Party = GetPartyOuter();
			CurrentConfig.MaxMembers = FMath::Clamp(NewSize, 1, Party->GetDefaultPartyMaxSize());
			UpdatePartyConfig();
		}
	}
	else
	{
		UE_LOG(LogParty, Warning, TEXT("Invalid party info updating party size!"));
	}
}

int32 UPartyGameState::GetPartyMaxSize() const
{
	if (PartyInfo.IsValid())
	{
		return PartyInfo->Config->MaxMembers;
	}

	// Invalid party info
	return INDEX_NONE;
}

void UPartyGameState::UpdateAcceptingMembers()
{
	EJoinPartyDenialReason DenialReason = EJoinPartyDenialReason::NoReason;

	TSharedPtr<const FUniqueNetId> PartyLeader = GetPartyLeader();
	if (PartyLeader.IsValid() && *OwningUserId == *PartyLeader)
	{
		bool bCurrentlyAcceptingMembers = false;

		// Look at game joinability (in game with permission or no game at all)
		if (IsInJoinableGameState())
		{
			// Make sure the party isn't full
			int32 NumPartyMembers = GetPartySize();
			int32 MaxPartyMembers = CurrentConfig.MaxMembers;
			if (NumPartyMembers < MaxPartyMembers)
			{
				// Look at party joinability
				switch (PartyStateRef->PartyType)
				{
				case EPartyType::Public:
				case EPartyType::FriendsOnly:
					bCurrentlyAcceptingMembers = true;
					break;
				case EPartyType::Private:
					// Party is private, invite required
					DenialReason = EJoinPartyDenialReason::PartyPrivate;
				default:
					break;
				}
			}
			else
			{
				// Party is full
				DenialReason = EJoinPartyDenialReason::PartyFull;
			}
		}
		else
		{
			DenialReason = EJoinPartyDenialReason::GameFull;
		}

		SetAcceptingMembers(bCurrentlyAcceptingMembers, DenialReason);
	}
}

void UPartyGameState::SetAcceptingMembers(bool bIsAcceptingMembers, EJoinPartyDenialReason DenialReason)
{
	if (PartyInfo.IsValid())
	{
		if (*OwningUserId == *GetPartyLeader())
		{
			if (CurrentConfig.bIsAcceptingMembers != bIsAcceptingMembers)
			{
				int32 NumPartyMembers = GetPartySize();
				int32 MaxPartyMembers = CurrentConfig.MaxMembers;
				bool bIsRoomInParty = (NumPartyMembers < MaxPartyMembers);

				bDebugAcceptingMembers = bIsAcceptingMembers && bIsRoomInParty;
				CurrentConfig.bIsAcceptingMembers = bDebugAcceptingMembers;
				CurrentConfig.NotAcceptingMembersReason = (int32)(bDebugAcceptingMembers ? EJoinPartyDenialReason::NoReason : DenialReason);
				UpdatePartyConfig();
			}
		}
	}
	else
	{
		UE_LOG(LogParty, Warning, TEXT("Invalid party info updating joinability!"));
	}
}

bool UPartyGameState::IsInJoinableGameState() const
{
	bool bInGame = false;
	bool bGameJoinable = false;

	UWorld* World = GetWorld();
	IOnlineSessionPtr SessionInt = Online::GetSessionInterface(World);
	if (ensure(SessionInt.IsValid()))
	{
		bool bGamePublicJoinable = false;
		bool bGameFriendJoinable = false;
		bool bGameInviteOnly = false;
		bool bGameAllowInvites = false;

		FNamedOnlineSession* GameSession = SessionInt->GetNamedSession(GameSessionName);
		if (GameSession != NULL &&
			GameSession->GetJoinability(bGamePublicJoinable, bGameFriendJoinable, bGameInviteOnly, bGameAllowInvites))
		{
			bInGame = true;

			TSharedPtr<FOnlineSessionInfo> UserSessionInfo = GameSession->SessionInfo;
			if (UserSessionInfo.IsValid())
			{
				// User's game is joinable in some way if any of this is true
				bGameJoinable = bGamePublicJoinable || bGameFriendJoinable || bGameInviteOnly;
			}
		}
	}

	return !bInGame || (bInGame && bGameJoinable);
}

bool UPartyGameState::GetJoinability(bool& bFriendJoinable, bool& bInviteOnly, bool& bAllowInvites) const
{
	bool bValidSettings = false;
	if (PartyInfo.IsValid())
	{
		bValidSettings = true;

		// In joinable game or no game at all
		bool bGameJoinable = IsInJoinableGameState();

		bFriendJoinable = false;
		bInviteOnly = false;
		bAllowInvites = false;

		// If the game is joinable (ie not full in general), then party rules apply
		if (bGameJoinable)
		{
			bool bPublicJoinable = false;
			bValidSettings = PartyInfo->GetJoinability(*OwningUserId, bPublicJoinable, bFriendJoinable, bInviteOnly, bAllowInvites);
		}
	}

	return bValidSettings;
}

void UPartyGameState::UpdatePartyConfig(bool bResetAccessKey)
{
	if (PartyInfo.IsValid())
	{
		if (*OwningUserId == *GetPartyLeader())
		{
			UWorld* World = GetWorld();
			IOnlinePartyPtr PartyInt = Online::GetPartyInterface(World);
			if (PartyInt.IsValid())
			{
				FOnUpdatePartyComplete CompletionDelegate;
				CompletionDelegate.BindUObject(this, &ThisClass::OnUpdatePartyConfigComplete);
				if (!PartyInt->UpdateParty(*OwningUserId, *PartyInfo->PartyId, CurrentConfig, bResetAccessKey, CompletionDelegate))
				{
					UE_LOG(LogParty, Warning, TEXT("[%s] Failed to update party"), PartyInfo.IsValid() ? *PartyInfo->PartyId->ToString() : TEXT("INVALID"));
				}
			}
			else
			{
				UE_LOG(LogParty, Warning, TEXT("[%s] Invalid party interface updating party size"), PartyInfo.IsValid() ? *PartyInfo->PartyId->ToString() : TEXT("INVALID"));
			}
		}
	}
	else
	{
		UE_LOG(LogParty, Warning, TEXT("Invalid party info updating party size!"));
	}
}

void UPartyGameState::OnUpdatePartyConfigComplete(const FUniqueNetId& LocalUserId, bool bWasSuccessful, const FOnlinePartyId& InPartyId, const FString& Error)
{
	UE_LOG(LogParty, Verbose, TEXT("[%s] Party config updated %d %s"), *InPartyId.ToString(), bWasSuccessful, *Error);

	if (PartyInfo.IsValid())
	{
		CurrentConfig = *PartyInfo->Config;
		bDebugAcceptingMembers = CurrentConfig.bIsAcceptingMembers;
	}
}

TSharedPtr<const FUniqueNetId> UPartyGameState::GetPartyLeader() const
{
	TSharedPtr<const FUniqueNetId> ReturnValue;

	if (PartyInfo.IsValid())
	{
		ReturnValue = PartyInfo->GetLeaderId();
	}

	return ReturnValue;
}

UPartyMemberState* UPartyGameState::GetPartyMember(const FUniqueNetIdRepl& InUniqueId) const
{
	if (InUniqueId.IsValid())
	{
		UPartyMemberState* const* PartyMemberPtr = PartyMembersState.Find(InUniqueId);
		return PartyMemberPtr ? *PartyMemberPtr : nullptr;
	}
	
	return nullptr;
}

void UPartyGameState::GetAllPartyMembers(TArray<UPartyMemberState*>& PartyMembers) const
{
	PartyMembersState.GenerateValueArray(PartyMembers);
}

void UPartyGameState::OnPartyMemberPromoted(const FUniqueNetId& LocalUserId, bool bWasSuccessful, const FOnlinePartyId& InPartyId, const FUniqueNetId& InMemberId, const FString& Error)
{
	UE_LOG(LogParty, Verbose, TEXT("[%s] Player %s promoted %s %d %s"), *InPartyId.ToString(), *LocalUserId.ToString(), *InMemberId.ToString(), bWasSuccessful, *Error);
}

void UPartyGameState::PromoteMember(const FUniqueNetIdRepl& NewPartyLeader)
{
	if (!bPromotionLockoutState)
	{
		if (*OwningUserId == *GetPartyLeader())
		{
			IOnlinePartyPtr PartyInt = Online::GetPartyInterface(GetWorld());
			if (PartyInt.IsValid() && ensure(OwningUserId.IsValid()) && ensure(NewPartyLeader.IsValid()))
			{
				FOnPromotePartyMemberComplete CompletionDelegate;
				CompletionDelegate.BindUObject(this, &ThisClass::OnPartyMemberPromoted);
				PartyInt->PromoteMember(*OwningUserId, *PartyInfo->PartyId, *NewPartyLeader, CompletionDelegate);
			}
		}
	}
	else
	{
		UE_LOG(LogParty, Verbose, TEXT("[%s] Promote member feature locked out."), PartyInfo.IsValid() ? *PartyInfo->PartyId->ToString() : TEXT("INVALID"));
	}
}

void UPartyGameState::OnPartyMemberKicked(const FUniqueNetId& LocalUserId, const FOnlinePartyId& InPartyId, const FUniqueNetId& InMemberId, const EKickMemberCompletionResult Result)
{
	bool bWasSuccessful = Result == EKickMemberCompletionResult::Succeeded;
	UE_LOG(LogParty, Verbose, TEXT("[%s] Player %s kicked %s %d %s"), *InPartyId.ToString(), *LocalUserId.ToString(), *InMemberId.ToString(), bWasSuccessful, ToString(Result));
}

void UPartyGameState::KickMember(const FUniqueNetIdRepl& PartyMemberToKick)
{
	IOnlinePartyPtr PartyInt = Online::GetPartyInterface(GetWorld());
	if (PartyInt.IsValid() && ensure(OwningUserId.IsValid()) && ensure(PartyMemberToKick.IsValid()))
	{
		if (*OwningUserId == *GetPartyLeader())
		{
			FOnKickPartyMemberComplete CompletionDelegate;
			CompletionDelegate.BindUObject(this, &ThisClass::OnPartyMemberKicked);
			PartyInt->KickMember(*OwningUserId, *PartyInfo->PartyId, *PartyMemberToKick, CompletionDelegate);
		}
	}
}

void UPartyGameState::InitLocalPlayerPartyData()
{
	UWorld* World = GetWorld();
	AGameState* GameState = World->GetGameState();
	check(GameState);

	for (const APlayerState* PlayerState : GameState->PlayerArray)
	{
		if (PlayerState && PlayerState->UniqueId.IsValid())
		{
			UPartyMemberState** LocalPartyMemberStatePtr = PartyMembersState.Find(PlayerState->UniqueId);
			UPartyMemberState* LocalPartyMemberState = LocalPartyMemberStatePtr ? *LocalPartyMemberStatePtr : nullptr;
			if (!LocalPartyMemberState)
			{
				LocalPartyMemberState = CreateNewPartyMember(*PlayerState->UniqueId);
				if (LocalPartyMemberState)
				{
					PartyMembersState.Add(PlayerState->UniqueId, LocalPartyMemberState);
				}
			}
		}
	}
}

void UPartyGameState::SendLocalPlayerPartyData()
{
	UWorld* World = GetWorld();

	InitLocalPlayerPartyData();

	IOnlinePartyPtr PartyInt = Online::GetPartyInterface(World);
	if (PartyInt.IsValid())
	{
		// Send local player data
		for (auto PartyMembers : PartyMembersState)
		{
			const FUniqueNetIdRepl& UniqueId = PartyMembers.Key;
			const UPartyMemberState* PartyMember = PartyMembers.Value;

			UpdatePartyMemberState(UniqueId, PartyMember);
		}
	}
}

void UPartyGameState::UpdatePartyData(const FUniqueNetIdRepl& InLocalUserId)
{
	UWorld* World = GetWorld();
	IOnlinePartyPtr PartyInt = Online::GetPartyInterface(World);
	if (PartyInt.IsValid())
	{
		FOnlinePartyAttrs PartyDataAttrs;
		if (FVariantDataConverter::UStructToVariantMap(PartyStateRefDef, PartyStateRef, PartyDataAttrs, 0, CPF_Transient | CPF_RepSkip))
		{
			PartyInt->UpdatePartyData(*InLocalUserId, *PartyInfo->PartyId, PartyDataAttrs);
		}
	}
}

void UPartyGameState::UpdatePartyMemberState(const FUniqueNetIdRepl& InLocalUserId, const UPartyMemberState* InPartyMemberState)
{
	UWorld* World = GetWorld();
	IOnlinePartyPtr PartyInt = Online::GetPartyInterface(World);
	if (PartyInt.IsValid())
	{
		FOnlinePartyAttrs PartyMemberStateAttrs;
		if (FVariantDataConverter::UStructToVariantMap(InPartyMemberState->MemberStateRefDef, InPartyMemberState->MemberStateRef, PartyMemberStateAttrs, 0, CPF_Transient | CPF_RepSkip))
		{
			PartyInt->UpdatePartyMemberData(*InLocalUserId, *PartyInfo->PartyId, PartyMemberStateAttrs);
		}
	}
}

void UPartyGameState::ConnectToReservationBeacon()
{
	UWorld* World = GetWorld();

	if (PartyInfo.IsValid())
	{
		FUniqueNetIdRepl PartyLeader(GetPartyLeader());
		if (PartyLeader.IsValid() && *OwningUserId == *PartyLeader)
		{
			FPendingMemberApproval NextApproval;
			if (PendingApprovals.Peek(NextApproval))
			{
				// Reconnect to the reservation beacon to maintain our place in the game (just until actual joined, holds place for all party members)
				ReservationBeaconClient = World->SpawnActor<APartyBeaconClient>(ReservationBeaconClientClass);
				if (ReservationBeaconClient)
				{
					UE_LOG(LogParty, Verbose, TEXT("Created party reservation beacon %s."), *ReservationBeaconClient->GetName());

					ReservationBeaconClient->OnHostConnectionFailure().BindUObject(this, &ThisClass::OnReservationBeaconUpdateConnectionFailure);
					ReservationBeaconClient->OnReservationRequestComplete().BindUObject(this, &ThisClass::OnReservationBeaconUpdateResponseReceived);
					ReservationBeaconClient->OnReservationCountUpdate().BindUObject(this, &ThisClass::OnReservationCountUpdate);

					FPlayerReservation NewPlayerRes;
					NewPlayerRes.UniqueId = NextApproval.SenderId;

					TArray<FPlayerReservation> PlayersToAdd;
					PlayersToAdd.Add(NewPlayerRes);

					ReservationBeaconClient->RequestReservationUpdate(CurrentSession, PartyLeader, PlayersToAdd);
				}
				else
				{
					OnReservationBeaconUpdateConnectionFailure();
				}
			}
		}
	}
}

void UPartyGameState::RejectAllPendingJoinRequests()
{
	UWorld* World = GetWorld();
	IOnlinePartyPtr PartyInt = Online::GetPartyInterface(World);
	bool bValidInterface = PartyInt.IsValid() && PartyInfo.IsValid();

	bool bEmpty = false;
	FPendingMemberApproval PendingApproval;

	do
	{
		bEmpty = !PendingApprovals.Dequeue(PendingApproval);
		if (!bEmpty && bValidInterface)
		{
			UE_LOG(LogParty, Verbose, TEXT("[%s] Responding to approval request for %s with denied"), PartyInfo.IsValid() ? *PartyInfo->PartyId->ToString() : TEXT("INVALID"), *PendingApproval.SenderId.ToString());
			PartyInt->ApproveJoinRequest(*PendingApproval.RecipientId, *PartyInfo->PartyId, *PendingApproval.SenderId, false, (int32)EJoinPartyDenialReason::Busy);
		}
	} while (!bEmpty);
}

void UPartyGameState::OnReservationBeaconUpdateConnectionFailure()
{
	UE_LOG(LogParty, Verbose, TEXT("Reservation update beacon failure %s."), ReservationBeaconClient ? *ReservationBeaconClient->GetName() : *FString(TEXT("")));

	// empty the queue, denying all requests
	RejectAllPendingJoinRequests();
	CleanupReservationBeacon();
}

void UPartyGameState::OnReservationBeaconUpdateResponseReceived(EPartyReservationResult::Type ReservationResponse)
{
	UE_LOG(LogParty, Verbose, TEXT("OnReservationBeaconUpdateResponseReceived %s"), EPartyReservationResult::ToString(ReservationResponse));

	if (ReservationResponse == EPartyReservationResult::ReservationAccepted ||
		ReservationResponse == EPartyReservationResult::ReservationDuplicate)
	{
		UWorld* World = GetWorld();
		IOnlinePartyPtr PartyInt = Online::GetPartyInterface(World);
		bool bValidInterface = PartyInt.IsValid() && PartyInfo.IsValid();

		// There should be at least the one
		FPendingMemberApproval PendingApproval;
		if (ensure(PendingApprovals.Dequeue(PendingApproval)))
		{
			if (bValidInterface)
			{
				UE_LOG(LogParty, Verbose, TEXT("[%s] Responding to approval request for %s with approved"), PartyInfo.IsValid() ? *PartyInfo->PartyId->ToString() : TEXT("INVALID"), *PendingApproval.SenderId.ToString());
				PartyInt->ApproveJoinRequest(*PendingApproval.RecipientId, *PartyInfo->PartyId, *PendingApproval.SenderId, true);
			}
		}

		// Check if there are any more while we are connected
		if (PendingApprovals.Dequeue(PendingApproval))
		{
			if (ensure(ReservationBeaconClient))
			{
				FUniqueNetIdRepl PartyLeader(GetPartyLeader());

				FPendingMemberApproval NextApproval;
				PendingApprovals.Peek(NextApproval);

				FPlayerReservation NewPlayerRes;
				NewPlayerRes.UniqueId = NextApproval.SenderId;

				TArray<FPlayerReservation> PlayersToAdd;
				PlayersToAdd.Add(NewPlayerRes);

				ReservationBeaconClient->RequestReservationUpdate(PartyLeader, PlayersToAdd);
			}
		}
		else
		{
			CleanupReservationBeacon();
		}
	}
	else
	{
		// empty the queue, denying all requests
		RejectAllPendingJoinRequests();
		CleanupReservationBeacon();
	}
}

void UPartyGameState::OnReservationCountUpdate(int32 NumRemaining)
{
}

void UPartyGameState::CleanupReservationBeacon()
{
	if (ReservationBeaconClient)
	{
		UE_LOG(LogParty, Verbose, TEXT("Party reservation beacon cleanup while in state %s, pending approvals: %s"), ToString(ReservationBeaconClient->GetConnectionState()), !PendingApprovals.IsEmpty() ? TEXT("true") : TEXT("false"));

		ReservationBeaconClient->OnHostConnectionFailure().Unbind();
		ReservationBeaconClient->OnReservationRequestComplete().Unbind();
		ReservationBeaconClient->OnReservationCountUpdate().Unbind();
		ReservationBeaconClient->DestroyBeacon();
		ReservationBeaconClient = nullptr;
	}
}

UWorld* UPartyGameState::GetWorld() const
{
	UParty* Party = GetPartyOuter();
	if (Party)
	{
		return Party->GetWorld();
	}
	
	return nullptr;
}

UParty* UPartyGameState::GetPartyOuter() const
{
	return GetTypedOuter<UParty>();
}