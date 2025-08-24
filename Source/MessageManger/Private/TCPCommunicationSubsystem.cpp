#include "TCPCommunicationSubsystem.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformProcess.h"
#include "TimerManager.h"
#include "EndianConverter.h"


void UTCPCommunicationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    bIsConnected = false;
    Socket = nullptr;
    ReceiveTask = nullptr;
    SendTask = nullptr;
}

void UTCPCommunicationSubsystem::Deinitialize()
{
    Super::Deinitialize();
    Disconnect();
}

bool UTCPCommunicationSubsystem::Connect(const FString& InIPAddress, int32 InPort)
{
    // 如果已经连接，先断开
    if (bIsConnected)
    {
        Disconnect();
    }
    ISocketSubsystem* SocketSubsystem = ISocketSubsystem::Get();
    // 创建Socket
    Socket = MakeShareable(SocketSubsystem->CreateSocket(NAME_Stream, TEXT("Default"), false));
    if (!Socket.IsValid())
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to create socket"));
        return false;
    }

    // 设置Socket选项
    Socket->SetNonBlocking(true);
    int32 SendBufferSize = 2 * 1024 * 1024;
    int32 RecvBufferSize = 2 * 1024 * 1024;
    Socket->SetSendBufferSize(SendBufferSize, SendBufferSize);
    Socket->SetReceiveBufferSize(RecvBufferSize, RecvBufferSize);

    // 解析IP地址
    FIPv4Address IPAddress;
    if (!FIPv4Address::Parse(InIPAddress, IPAddress))
    {
        UE_LOG(LogTemp, Error, TEXT("Invalid IP address: %s"), *InIPAddress);
        return false;
    }

    // 连接到服务器
    TSharedRef<FInternetAddr> ServerAddress = FIPv4Endpoint(IPAddress, InPort).ToInternetAddr();
    bool bConnected = Socket->Connect(*ServerAddress);

    if (bConnected)
    {
        bIsConnected = true;
        UE_LOG(LogTemp, Log, TEXT("Connected to server: %s:%d"), *InIPAddress, InPort);

        // 启动接收和发送线程
        //ReceiveTask = new FAsyncTask<FReceiveWorker>(this, Socket);
        //ReceiveTask->StartBackgroundTask();

        SendTask = new FAsyncTask<FSendWorker>(this, Socket, SendQueue);
        SendTask->StartBackgroundTask();

        // 启动心跳机制
        LastHeartbeatTime = FDateTime::UtcNow();
        GetWorld()->GetTimerManager().SetTimer(HeartbeatTimer, this, &UTCPCommunicationSubsystem::SendHeartbeat, 5.0f, true);
        
        // 通知连接状态变化
        NotifyConnectionStatusChanged(true);
        
        return true;
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to connect to server: %s:%d"), *InIPAddress, InPort);
        Socket.Reset();
        return false;
    }
}

void UTCPCommunicationSubsystem::Disconnect()
{
    if (bIsConnected && Socket.IsValid())
    {
        // 停止心跳
        GetWorld()->GetTimerManager().ClearTimer(HeartbeatTimer);
        
        // 关闭Socket
        Socket->Close();
        Socket.Reset();

        // 等待线程结束
        if (ReceiveTask)
        {
            ReceiveTask->EnsureCompletion();
            delete ReceiveTask;
            ReceiveTask = nullptr;
        }

        if (SendTask)
        {
            SendTask->EnsureCompletion();
            delete SendTask;
            SendTask = nullptr;
        }

        // 清空队列
        FNetworkMessage Dummy;
        while (SendQueue.Dequeue(Dummy)) {}

        bIsConnected = false;
        UE_LOG(LogTemp, Log, TEXT("Disconnected from server"));
        
        // 通知连接状态变化
        NotifyConnectionStatusChanged(false);
    }
}

bool UTCPCommunicationSubsystem::SendMessage(const FNetworkMessage& Message)
{
    if (!bIsConnected || !Socket.IsValid())
    {
        UE_LOG(LogTemp, Warning, TEXT("Not connected to server, cannot send message"));
        return false;
    }

    // 将消息加入发送队列
    SendQueue.Enqueue(Message);
    return true;
}

void UTCPCommunicationSubsystem::RegisterMessageHandler(FOnMessageReceived InHandler)
{
    MessageReceivedDelegate = InHandler;
}

void UTCPCommunicationSubsystem::RegisterConnectionStatusHandler(FOnConnectionStatusChanged InHandler)
{
    ConnectionStatusDelegate = InHandler;
}

void UTCPCommunicationSubsystem::SendHeartbeat()
{
    if (!bIsConnected) return;
    
    // 发送心跳消息
    FNetworkMessage HeartbeatMsg(TEXT("Heartbeat"), TEXT("{}"));
    SendMessage(HeartbeatMsg);
    
    // 检查是否超时
    CheckHeartbeatTimeout();
}

void UTCPCommunicationSubsystem::CheckHeartbeatTimeout()
{
  /*  if (!bIsConnected) return;

    FTimespan TimeSinceLastHeartbeat = FDateTime::UtcNow() - LastHeartbeatTime;
    if (TimeSinceLastHeartbeat.GetTotalSeconds() > HeartbeatTimeout)
    {
        UE_LOG(LogTemp, Error, TEXT("Heartbeat timeout, disconnecting..."));
        Disconnect();
    }*/
    
}

void UTCPCommunicationSubsystem::HandleHeartbeat()
{
    LastHeartbeatTime = FDateTime::UtcNow();
    UE_LOG(LogTemp, Verbose, TEXT("Received heartbeat from server"));
}

FString UTCPCommunicationSubsystem::SerializeMessage(const FNetworkMessage& Message)
{
    TSharedPtr<FJsonObject> JsonObject = MakeShareable(new FJsonObject());
    JsonObject->SetStringField(TEXT("Type"), Message.MessageType);
    JsonObject->SetStringField(TEXT("Data"), Message.JsonData);

    FString JsonString;
    TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&JsonString);
    FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);

    return JsonString;
}

bool UTCPCommunicationSubsystem::DeserializeMessage(const FString& JsonString, FNetworkMessage& OutMessage)
{
    TSharedPtr<FJsonObject> JsonObject;
    TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

    if (FJsonSerializer::Deserialize(Reader, JsonObject) && JsonObject.IsValid())
    {
        OutMessage.MessageType = JsonObject->GetStringField(TEXT("Type"));
        OutMessage.JsonData = JsonObject->GetStringField(TEXT("Data"));
        return true;
    }

    UE_LOG(LogTemp, Error, TEXT("Failed to deserialize message: %s"), *JsonString);
    return false;
}

void UTCPCommunicationSubsystem::ProcessReceivedData(const TArray<uint8>& Data)
{
    // 将新数据添加到缓冲区
    ReceiveBuffer.Append(Data);
    
    // 处理缓冲区中的完整消息
    while (true)
    {
        // 消息格式：4字节长度前缀（大端序） + 消息内容
        if (ReceiveBuffer.Num() < 4)
        {
            // 数据不足，等待更多数据
            break;
        }
        
        // 解析消息长度
        uint32 MessageLength = 0;
        memcpy(&MessageLength, ReceiveBuffer.GetData(), 4);
        // 转换为大端序
        //MessageLength = FPlatformMisc::NetworkToHostLong(MessageLength);
        
        // 检查是否有完整的消息
        if (ReceiveBuffer.Num() < (int32)(4 + MessageLength))
        {
            // 消息不完整，等待更多数据
            break;
        }
        
        // 提取消息内容
        TArray<uint8> MessageData;
        MessageData.Append(ReceiveBuffer.GetData() + 4, MessageLength);
        
        // 从缓冲区移除已处理的消息
        ReceiveBuffer.RemoveAt(0, 4 + MessageLength);
        
        // 将字节转换为字符串
        FString MessageString = FString(MessageData.Num(), (TCHAR*)MessageData.GetData());
        
        // 反序列化消息
        FNetworkMessage NetworkMessage;
        if (DeserializeMessage(MessageString, NetworkMessage))
        {
            // 处理心跳消息
            if (NetworkMessage.MessageType == TEXT("Heartbeat"))
            {
                HandleHeartbeat();
                continue;
            }
            
            // 触发消息接收回调（在游戏线程中执行）
            AsyncTask(ENamedThreads::GameThread, [this, NetworkMessage]()
            {
                if (MessageReceivedDelegate.IsBound())
                {
                    MessageReceivedDelegate.Execute(NetworkMessage);
                }
            });
        }
    }
}

void UTCPCommunicationSubsystem::NotifyConnectionStatusChanged(bool bNewConnected)
{
    AsyncTask(ENamedThreads::GameThread, [this, bNewConnected]()
    {
        if (ConnectionStatusDelegate.IsBound())
        {
            ConnectionStatusDelegate.Execute(bNewConnected);
        }
    });
}

// 接收线程实现
void FReceiveWorker::DoWork()
{
    if (!Socket.IsValid() || !Subsystem)
    {
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("Receive worker started"));

    while (Subsystem->IsConnected() && Socket.IsValid())
    {
        // 读取数据
        //TArray<uint8> ReceiveData;

        FArrayReader ReceiveData = FArrayReader(true);
        
        uint32 Size;
        if (Socket->HasPendingData(Size))
        {
            //ReceiveData.SetNumUninitialized(FMath::Min(Size, 65536u)); // 一次最多读取64KB
            ReceiveData.SetNumUninitialized(FMath::Min(Size, 65536u));

            int32 Read = 0;
            if (Socket->Recv(ReceiveData.GetData(), ReceiveData.Num(), Read))
            {
                if (Read > 0)
                {
                    ReceiveData.SetNum(Read);
                    Subsystem->ProcessReceivedData(ReceiveData);
                }
            }
            else
            {
                // 接收失败，断开连接
                UE_LOG(LogTemp, Error, TEXT("Failed to receive data"));
                Subsystem->Disconnect();
                break;
            }
        }

        // 短暂休眠，减少CPU占用
        FPlatformProcess::Sleep(0.001f);
    }

    UE_LOG(LogTemp, Log, TEXT("Receive worker stopped"));
}

// 发送线程实现
void FSendWorker::DoWork()
{
    if (!Socket.IsValid() || !Subsystem)
    {
        return;
    }

    UE_LOG(LogTemp, Log, TEXT("Send worker started"));

    while (Subsystem->IsConnected() && Socket.IsValid())
    {
        // 从队列中获取消息
        FNetworkMessage Message;
        while (SendQueue.Dequeue(Message))
        {
            // 序列化消息
            FString JsonString = Subsystem->SerializeMessage(Message);
            
            // 转换为字节数组
            TArray<uint8> Data;
            Data.Append((uint8*)TCHAR_TO_UTF8(*JsonString), JsonString.Len());

            // 添加长度前缀（大端序）
            uint32 Length = Data.Num() + sizeof(uint32);
            FArrayWriter MessageData = FArrayWriter(true);
            MessageData << Length;
            MessageData << JsonString;


            uint32 MessageLength1 = 0;
            memcpy(&MessageLength1, MessageData.GetData(), sizeof(uint32));

            UE_LOG(LogTemp, Log, TEXT("FSendWorker::send data MessageLength1 = %d"), MessageLength1);
            // 发送数据
            int32 BytesSent = 0;
            if (!Socket->Send(MessageData.GetData(), MessageData.Num(), BytesSent) || BytesSent != MessageData.Num())
            {
                UE_LOG(LogTemp, Error, TEXT("Failed to send data. Sent %d of %d bytes"), BytesSent, MessageData.Num());
                Subsystem->Disconnect();
                break;
            }
        }

        // 短暂休眠，减少CPU占用
        FPlatformProcess::Sleep(0.001f);
    }

    UE_LOG(LogTemp, Log, TEXT("Send worker stopped"));
}
