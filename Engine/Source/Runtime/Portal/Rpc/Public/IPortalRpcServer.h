// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.
#pragma once

struct FMessageAddress;
class IMessageRpcServer;

/**
 * Interface for Portal RPC server
 */
class IPortalRpcServer
{
public:

	/**
	 * Sends a FPortalRpcServer response to the specified address, effectively
	 * allowing the two services to now communicate with each other directly.
	 */
	virtual void ConnectTo(const FMessageAddress& Address) const = 0;

	virtual IMessageRpcServer* GetMessageServer() = 0;

public:

	/** Virtual destructor. */
	~IPortalRpcServer() { }
};
