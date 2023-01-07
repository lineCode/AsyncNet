// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

struct FAsyncMsg;

typedef TSharedPtr<FAsyncMsg, ESPMode::ThreadSafe> FSharedAsyncMsg;

USTRUCT()
struct FAsyncMsg
{
	GENERATED_BODY()
	FName SocketName;
	TArray<uint8> Data;
};

class FAsyncNetRunnable : public FRunnable
{
public:
	FAsyncNetRunnable()
	{
		Thread = FRunnableThread::Create(this, TEXT("AsyncNetThread"));
	}
	~FAsyncNetRunnable()
	{
		if (Thread)
		{
			Thread->Kill();
			delete Thread;
		}
	}
	virtual bool Init() override
	{
		//auto CurrentThreadId = FPlatformTLS::GetCurrentThreadId();
		check(!IsInGameThread());
		return true;
	}
	
	virtual uint32 Run() override;

	virtual void Stop() override
	{
		bRunThread = false;
	}
	
	virtual void Exit() override
	{
		check(!IsInGameThread());
	}

	FRunnableThread* Thread = nullptr;
	bool bRunThread = true;
};

struct FSocketClient
{
	FSocketClient(FSocket* InSocket) : Socket(InSocket) {}
	FSocket* Socket;
	TArray<uint8> RecvBuffer;
	TArray<uint8> SendBuffer;
};

struct FClientProxy
{
	void* ProxyData;
	TDelegate<void(FClientProxy* ClientProxy, const FName& SocketName, FSocketClient& SocketClient)> OnClientConnected;
	TDelegate<void(FClientProxy* ClientProxy, const FName& SocketName, FSocketClient& SocketClient)> OnRecvData;
	TDelegate<void(FClientProxy* ClientProxy, const FName& SocketName, FSocketClient& SocketClient)> OnClientDisconnected;
};


class FAsyncTCPServer
{
public:
	FAsyncTCPServer(FSocket* InListenerSocket, FClientProxy* InClientProxy, const FString InIP, int32 InPort, int32 InRWBufferSize = 64 * 1024)
		: ListenerSocket(InListenerSocket)
		, ClientProxy(InClientProxy)
		, IP(InIP)
		, Port(InPort)
		, RWBufferSize(InRWBufferSize)
	{
		check(InListenerSocket);
	}
	~FAsyncTCPServer()
	{
		
	}
	FClientProxy* ClientProxy;
	TMap<FName, FSocketClient> ConnectedSockets;
	TQueue<FAsyncMsg> MsgRecvQueue;
	TQueue<FAsyncMsg> MsgSendQueue;
	// Begin Connection Info
	FSocket* ListenerSocket;
	FString IP;
	int32 Port;
	int32 RWBufferSize;
	// End Connection Info

	TMulticastDelegate<void(TSharedPtr<FAsyncTCPServer>&)> OnBeforeSocketClose;

};

class FAsyncNetModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	TSharedPtr<FAsyncTCPServer> CreateTCPServer(FClientProxy* InClientProxy, const FString& IP, int32 Port, int32 RWBufferSize = 64 * 1024);

	TMap<FName, TSharedPtr<FAsyncTCPServer>> TCPServerMap;
	FAsyncNetRunnable* AsyncNetRunnable;
};
