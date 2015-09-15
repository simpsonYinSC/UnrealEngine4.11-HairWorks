// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "PortalRpcPrivatePCH.h"
#include "PortalRpcResponder.h"
#include "IPortalRpcResponder.h"
#include "IMessageRpcServer.h"
#include "IPortalRpcServer.h"
#include "PortalRpcMessages.h"
#include "ModuleManager.h"

class FPortalRpcResponderImpl
	: public IPortalRpcResponder
{
public:

	virtual ~FPortalRpcResponderImpl() { }

public:

	// IPortalRpcResponder interface
	virtual FOnPortalRpcLookup& OnLookup() override
	{
		return LookupDelegate;
	}

private:

	FPortalRpcResponderImpl()
	{
		MessageEndpoint = FMessageEndpoint::Builder("FPortalRpcResponder")
			.Handling<FPortalRpcLocateServer>(this, &FPortalRpcResponderImpl::HandleMessage);

		if (MessageEndpoint.IsValid())
		{
			MessageEndpoint->Subscribe<FPortalRpcLocateServer>();
		}
	}

private:

	void HandleMessage(const FPortalRpcLocateServer& Message, const TSharedRef<IMessageContext, ESPMode::ThreadSafe>& Context)
	{
		if (!LookupDelegate.IsBound())
		{
			return;
		}

		const FString ProductKey = Message.ProductId.ToString() + Message.ProductVersion;
		TSharedPtr<IPortalRpcServer> Server = Servers.FindRef(ProductKey);

		if (!Server.IsValid())
		{
			Server = LookupDelegate.Execute(ProductKey);
		}

		if (Server.IsValid())
		{
			Server->ConnectTo(Context->GetSender());
		}
	}


private:

	/** A delegate that is executed when a look-up for an RPC server occurs. */
	FOnPortalRpcLookup LookupDelegate;

	/** Message endpoint. */
	TSharedPtr<FMessageEndpoint, ESPMode::ThreadSafe> MessageEndpoint;

	/** Holds the existing RPC servers. */
	TMap<FString, TSharedPtr<IPortalRpcServer>> Servers;

	friend FPortalRpcResponderFactory;
};

TSharedRef<IPortalRpcResponder> FPortalRpcResponderFactory::Create()
{
	return MakeShareable(new FPortalRpcResponderImpl());
}
