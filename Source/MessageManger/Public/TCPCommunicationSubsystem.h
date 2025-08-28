#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Async/AsyncWork.h"
#include "Runtime/Networking/Public/Networking.h"
#include "Runtime/Json/Public/Serialization/JsonSerializer.h"
#include "TCPCommunicationSubsystem.generated.h"

// 消息结构体
USTRUCT(BlueprintType)
struct FNetworkMessage
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "Network")
    FString MessageType;
    
    UPROPERTY(BlueprintReadWrite, Category = "Network")
    FString JsonData;
    
    FNetworkMessage() {}
    FNetworkMessage(const FString& InType, const FString& InData) 
        : MessageType(InType), JsonData(InData) {}
};

// 消息处理委托
DECLARE_DELEGATE_OneParam(FOnMessageReceived, const FNetworkMessage&);
DECLARE_DELEGATE_OneParam(FOnConnectionStatusChanged, bool /*bConnected*/);

UCLASS()
class MESSAGEMANGER_API UTCPCommunicationSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    // 子系统初始化和关闭
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // 连接到服务器
    UFUNCTION(BlueprintCallable, Category = "Network|TCP")
    bool Connect(const FString& InIPAddress, int32 InPort);

    // 断开连接
    UFUNCTION(BlueprintCallable, Category = "Network|TCP")
    void Disconnect();

    // 发送消息
    UFUNCTION(BlueprintCallable, Category = "Network|TCP")
    bool SendMessage(const FNetworkMessage& Message);

    // 注册消息处理回调
    void RegisterMessageHandler(FOnMessageReceived InHandler);
    
    // 注册连接状态变化回调
    void RegisterConnectionStatusHandler(FOnConnectionStatusChanged InHandler);

    // 检查是否连接
    UFUNCTION(BlueprintCallable, Category = "Network|TCP")
    bool IsConnected() const { return bIsConnected; }

        // 序列化消息为JSON
    FString SerializeMessage(const FNetworkMessage& Message);
    
    // 反序列化JSON为消息
    bool DeserializeMessage(const FString& JsonString, FNetworkMessage& OutMessage);
    
    // 处理接收到的原始数据
    void ProcessReceivedData(const TArray<uint8>& Data);

    // 广播消息
	void BroadcastMessage(const FNetworkMessage& Message);
private:
    // TCP套接字
    TSharedPtr<FSocket> Socket;
    
    // 连接状态
    bool bIsConnected;
    
    // 消息接收线程
    FAsyncTask<class FReceiveWorker>* ReceiveTask;
    
    // 消息发送队列
    TQueue<FNetworkMessage, EQueueMode::Mpsc> SendQueue;
    
    // 消息发送线程
    FAsyncTask<class FSendWorker>* SendTask;
    
    // 消息处理回调
    FOnMessageReceived MessageReceivedDelegate;
    
    // 连接状态变化回调
    FOnConnectionStatusChanged ConnectionStatusDelegate;
    
    // 心跳定时器
    FTimerHandle HeartbeatTimer;
    
    // 最后一次收到心跳的时间
    FDateTime LastHeartbeatTime;
    
    // 心跳超时时间(秒)
    const float HeartbeatTimeout = 30.0f;
    
    // 发送心跳包
    void SendHeartbeat();
    
    // 检查心跳超时
    void CheckHeartbeatTimeout();
    
    // 处理接收到的心跳包
    void HandleHeartbeat();
    
    // 粘包处理缓冲区
    TArray<uint8> ReceiveBuffer;
    
    // 通知连接状态变化
    void NotifyConnectionStatusChanged(bool bNewConnected);
};

// 接收消息的异步任务
class FReceiveWorker : public FNonAbandonableTask
{
public:
    FReceiveWorker(UTCPCommunicationSubsystem* InSubsystem, TSharedPtr<FSocket> InSocket)
        : Subsystem(InSubsystem), Socket(InSocket) {}

    ~FReceiveWorker() {}

    // 执行任务
    void DoWork();

    // 获得当前任务的统计信息
    FORCEINLINE TStatId GetStatId() const
    {
        RETURN_QUICK_DECLARE_CYCLE_STAT(FReceiveWorker, STATGROUP_ThreadPoolAsyncTasks);
    }

private:
    UTCPCommunicationSubsystem* Subsystem;
    TSharedPtr<FSocket> Socket;
};

// 发送消息的异步任务
class FSendWorker : public FNonAbandonableTask
{
public:
    FSendWorker(UTCPCommunicationSubsystem* InSubsystem, TSharedPtr<FSocket> InSocket, TQueue<FNetworkMessage, EQueueMode::Mpsc>& InSendQueue)
        : Subsystem(InSubsystem), Socket(InSocket), SendQueue(InSendQueue) {}

    ~FSendWorker() {}

    // 执行任务
    void DoWork();

    // 获得当前任务的统计信息
    FORCEINLINE TStatId GetStatId() const
    {
        RETURN_QUICK_DECLARE_CYCLE_STAT(FSendWorker, STATGROUP_ThreadPoolAsyncTasks);
    }

private:
    UTCPCommunicationSubsystem* Subsystem;
    TSharedPtr<FSocket> Socket;
    TQueue<FNetworkMessage, EQueueMode::Mpsc>& SendQueue;
};
