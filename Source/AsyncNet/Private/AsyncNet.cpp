// Copyright Epic Games, Inc. All Rights Reserved.

#include "AsyncNet.h"

#include "Networking.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

#define LOCTEXT_NAMESPACE "FAsyncNetModule"
FAsyncNetModule* AsyncNetModule;

uint32 FAsyncNetRunnable::Run()
{
	int64 MaximumSingleRecvBytes = 0;
	while (bRunThread)
	{
		MaximumSingleRecvBytes = 0;
		//TCPServerTo
		for (auto& [TCPServerName, TCPServerPtr] : AsyncNetModule->TCPServerMap)
		{
			auto& TCPListenerSocket = TCPServerPtr->ListenerSocket;
			if (TCPListenerSocket->GetConnectionState() == ESocketConnectionState::SCS_Connected)
			{
				bool bHasPendingConnection = true;
				if (TCPListenerSocket->HasPendingConnection(bHasPendingConnection))
				{
					if (bHasPendingConnection)
					{
						TSharedRef<FInternetAddr> ConnectedSocketInternetAddr = ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->CreateInternetAddr();
						static int32 NameCounter = 0;
						auto SocketName = ConnectedSocketInternetAddr->ToString(true);
						FSocket* ConnectedSocket = TCPListenerSocket->Accept(
							*ConnectedSocketInternetAddr,
							SocketName);

						if(ConnectedSocket != nullptr)
						{
							ConnectedSocket->SetNonBlocking();
							if (auto FindExistClient = TCPServerPtr->ConnectedSockets.Find(*SocketName))
							{
								auto ExistClient = *FindExistClient;
								auto ExistSocket = ExistClient.Socket;
								ExistSocket->Close();
								ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ExistSocket);
								//GameThreadTasksQueue.Enqueue([TCPServerComponent = TCPServerComponent , NetworkName]{ TCPServerComponent->OnDisconnected(NetworkName); });
							}
							TCPServerPtr->ConnectedSockets.FindOrAdd(*SocketName, FSocketClient(ConnectedSocket));
							//GameThreadTasksQueue.Enqueue([TCPServerComponent = TCPServerComponent , NetworkName]{ TCPServerComponent->OnConnected(NetworkName); });
						}
					}
				}
				TSet<FName> LostConnectionSocketSet;
				// 
				for (auto& [ConnectedSocketName, ConnectedClient] : TCPServerPtr->ConnectedSockets)
				{
					auto ConnectedSocket = ConnectedClient.Socket;
					if(ConnectedSocket->GetConnectionState() == ESocketConnectionState::SCS_Connected)
					{
						uint32 PendingDataSize;
						bool NonAnyRecvError = true;
						while (ConnectedSocket->HasPendingData(PendingDataSize))
						{
							TArray<uint8> Data;
							Data.SetNum(PendingDataSize);
							int32 BytesRead = 0;
							NonAnyRecvError = ConnectedSocket->Recv(Data.GetData(), Data.Num(), BytesRead);
							TCPServerPtr->ClientProxy->OnRecvData.ExecuteIfBound(TCPServerPtr->ClientProxy, ConnectedSocketName, ConnectedClient);
						}
						if(!NonAnyRecvError)
						{
							ConnectedSocket->Close();
							LostConnectionSocketSet.Add(ConnectedSocketName);
							//UE_LOG(LogRemoteController, Warning, TEXT("CLIENT <%s> RECV ERROR"), *Tuple.Key.ToString());
						}
					}
					else
					{
						ConnectedSocket->Close();
						LostConnectionSocketSet.Add(ConnectedSocketName);
					}
				}
			}
			else
			{
				
				TCPServerPtr->OnBeforeSocketClose.Broadcast(TCPServerPtr);
				TCPListenerSocket->Close();
				ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(TCPListenerSocket);
				TCPListenerSocket = nullptr;
				for (auto& [ConnectedSocketName, ConnectedClient] : TCPServerPtr->ConnectedSockets)
				{
					auto ConnectedSocket = ConnectedClient.Socket;
					if(ConnectedSocket)
					{
						ConnectedSocket->Close();
						ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ConnectedSocket);
						ConnectedSocket = nullptr;
					}
				}
			}
		}
		if (MaximumSingleRecvBytes <= 1024)
		{
			FPlatformProcess::Sleep(0.001);
		}
		else
		{
			FPlatformProcess::YieldThread();
		}
	}
	return 0;
}

void FAsyncNetModule::StartupModule()
{
	AsyncNetModule = this;
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	AsyncNetRunnable = new FAsyncNetRunnable();
}

void FAsyncNetModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
	delete AsyncNetRunnable;
}

TSharedPtr<FAsyncTCPServer> FAsyncNetModule::CreateTCPServer(FClientProxy* ClientProxy, const FString& IP, int32 Port, int32 RWBufferSize)
{
	FIPv4Address ServerAddr;
	FIPv4Address::Parse(IP.IsEmpty() ? "0.0.0.0" : IP, ServerAddr);
	FIPv4Endpoint Endpoint(ServerAddr, Port);
	FString SocketName = ServerAddr.ToString();
	auto ListenerSocket = FTcpSocketBuilder(SocketName)
		.AsReusable()
		.BoundToEndpoint(Endpoint)
		.Listening(8)
		.WithSendBufferSize(RWBufferSize)
		.WithReceiveBufferSize(RWBufferSize)
		.Build();
	if(ListenerSocket)
	{
		auto& TCPServer = TCPServerMap.Emplace(SocketName, MakeShared<FAsyncTCPServer>(ListenerSocket, ClientProxy, IP, Port, RWBufferSize));
		return TCPServer;
	}
	return nullptr;
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FAsyncNetModule, AsyncNet)