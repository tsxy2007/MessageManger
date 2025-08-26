#include "TCPCommunicationSubsystem.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformProcess.h"
#include "TimerManager.h"
#include "EndianConverter.h"
#include <MessageMangerBPLibrary.h>


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
    Disconnect();
    Super::Deinitialize();
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
        ReceiveTask = new FAsyncTask<FReceiveWorker>(this, Socket);
        ReceiveTask->StartBackgroundTask();

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
        UWorld* World = GetWorld();
        if (World)
        {
            World->GetTimerManager().ClearTimer(HeartbeatTimer);
        }
        
        // 关闭Socket
        Socket->Close();
        Socket.Reset();
        // 清空队列
        FNetworkMessage Dummy;
        while (SendQueue.Dequeue(Dummy)) {}

        bIsConnected = false;
        UE_LOG(LogTemp, Log, TEXT("Disconnected from server"));

        // 通知连接状态变化
        NotifyConnectionStatusChanged(false);

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
    if (!bIsConnected) return;

    FTimespan TimeSinceLastHeartbeat = FDateTime::UtcNow() - LastHeartbeatTime;
    if (TimeSinceLastHeartbeat.GetTotalSeconds() > HeartbeatTimeout)
    {
        UE_LOG(LogTemp, Error, TEXT("Heartbeat timeout, disconnecting..."));
        Disconnect();
    }
    
}

void UTCPCommunicationSubsystem::HandleHeartbeat()
{
    LastHeartbeatTime = FDateTime::UtcNow();
    UE_LOG(LogTemp, Log, TEXT("Received heartbeat from server"));
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
    // 消息格式：4字节长度前缀（大端序） + 消息内容
    if (ReceiveBuffer.Num() < 4)
    {
        // 数据不足，等待更多数据
        return;
    }
    // 将字节转换为字符串
    FString MessageString = UMessageMangerBPLibrary::ConvertUtf8BinaryToString(ReceiveBuffer);
    ReceiveBuffer.Empty();
    // 反序列化消息
    FNetworkMessage NetworkMessage;
    if (DeserializeMessage(MessageString, NetworkMessage))
    {
        // 处理心跳消息
        if (NetworkMessage.MessageType == TEXT("Heartbeat"))
        {
            HandleHeartbeat();
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

    // 定义最大接收分片大小为64KB (65536字节)
    const int32 MAX_CHUNK_SIZE = 65536;
    // 分片头部结构 (与发送端对应)
    struct FChunkHeader
    {
        uint32 MessageId = 0;
        uint32 TotalLength = 0;
        uint32 ChunkIndex = 0;
        uint8 IsLastChunk = 0;
    };
    const int32 HEADER_SIZE = sizeof(FChunkHeader);

    // 用于缓存分片数据的结构
    struct FPartialMessage
    {
        TArray<uint8> Data;          // 完整数据缓冲区
        int32 ReceivedChunks;       // 已接收的分片数
        int32 TotalChunks;          // 总分片数
        FDateTime LastActivityTime; // 最后活动时间，用于超时处理
    };

    // 存储所有部分接收的消息 (MessageId -> 部分消息)
    TMap<uint32, FPartialMessage> PartialMessages;

    UE_LOG(LogTemp, Log, TEXT("Receive worker started with chunking (max %d bytes per chunk)"), MAX_CHUNK_SIZE);

    while (Subsystem->IsConnected() && Socket.IsValid())
    {
        uint32 PendingDataSize;
        if (Socket->HasPendingData(PendingDataSize))
        {
            // 一次最多读取64KB数据
            int32 ReadSize = FMath::Min(PendingDataSize, (uint32)MAX_CHUNK_SIZE);
            FArrayReader ReceiveData = FArrayReader(true);
            ReceiveData.SetNumUninitialized(ReadSize);

            int32 BytesRead = 0;
            if (Socket->Recv(ReceiveData.GetData(), ReadSize, BytesRead) && BytesRead > 0)
            {
                ReceiveData.SetNum(BytesRead);

                // 检查是否有足够的数据容纳头部
                if (BytesRead < HEADER_SIZE)
                {
                    UE_LOG(LogTemp, Error, TEXT("Received data too small for header (size: %d)"), BytesRead);
                    break;
                }

                // 解析头部信息
                FChunkHeader Header;
                FMemory::Memcpy(&Header, ReceiveData.GetData(), HEADER_SIZE);

                // 提取实际数据部分
                TArray<uint8> ChunkData;
                ChunkData.Append(ReceiveData.GetData() + HEADER_SIZE, BytesRead - HEADER_SIZE);

                UE_LOG(LogTemp, Log, TEXT("Received chunk %d (MessageId: %u, size: %d bytes)"),
                    Header.ChunkIndex, Header.MessageId, ChunkData.Num());

                // 检查是否是新消息
                if (!PartialMessages.Contains(Header.MessageId))
                {
                    // 计算总分片数
                    int32 TotalChunks = FMath::CeilToInt((float)Header.TotalLength / MAX_CHUNK_SIZE);

                    // 初始化新的部分消息
                    FPartialMessage NewMessage;
                    NewMessage.Data.SetNumUninitialized(Header.TotalLength);
                    NewMessage.ReceivedChunks = 0;
                    NewMessage.TotalChunks = TotalChunks;
                    NewMessage.LastActivityTime = FDateTime::UtcNow();

                    PartialMessages.Add(Header.MessageId, NewMessage);
                }

                // 获取当前消息的缓存
                FPartialMessage& CurrentMessage = PartialMessages[Header.MessageId];

                // 验证分片有效性
                if (Header.ChunkIndex >= (uint32)CurrentMessage.TotalChunks)
                {
                    UE_LOG(LogTemp, Error, TEXT("Invalid chunk index %d for message %u (total chunks: %d)"),
                        Header.ChunkIndex, Header.MessageId, CurrentMessage.TotalChunks);
                    PartialMessages.Remove(Header.MessageId);
                    continue;
                }

                // 计算当前分片在完整数据中的偏移量
                int32 ChunkOffset = Header.ChunkIndex * MAX_CHUNK_SIZE;

                // 确保偏移量不超过总长度
                if (ChunkOffset + ChunkData.Num() > CurrentMessage.Data.Num())
                {
                    UE_LOG(LogTemp, Error, TEXT("Chunk data exceeds message size for message %u"), Header.MessageId);
                    PartialMessages.Remove(Header.MessageId);
                    continue;
                }

                // 将分片数据复制到完整缓冲区
                FMemory::Memcpy(CurrentMessage.Data.GetData() + ChunkOffset, ChunkData.GetData(), ChunkData.Num());
                CurrentMessage.ReceivedChunks++;
                CurrentMessage.LastActivityTime = FDateTime::UtcNow();

                // 检查是否接收完所有分片
                if (Header.IsLastChunk && CurrentMessage.ReceivedChunks == CurrentMessage.TotalChunks)
                {
                    UE_LOG(LogTemp, Log, TEXT("Message %u fully received (%d bytes)"),
                        Header.MessageId, CurrentMessage.Data.Num());

                    // 将完整数据传递给处理函数
                    FArrayReader CompleteData = FArrayReader(true);
                    CompleteData.Append(CurrentMessage.Data);
                    Subsystem->ProcessReceivedData(CompleteData);

                    // 从缓存中移除
                    PartialMessages.Remove(Header.MessageId);
                }
            }
            else
            {
                // 接收失败，断开连接
                UE_LOG(LogTemp, Error, TEXT("Failed to receive data"));
                
                AsyncTask(ENamedThreads::GameThread, [this]()
                {
                        Subsystem->Disconnect();
                });
                break;
            }
        }
        

        // 清理超时的部分消息（5秒超时）
        TArray<uint32> ExpiredMessages;
        for (const auto& Pair : PartialMessages)
        {
            if (FDateTime::UtcNow() - Pair.Value.LastActivityTime > FTimespan::FromSeconds(5))
            {
                ExpiredMessages.Add(Pair.Key);
            }
        }
        for (uint32 MsgId : ExpiredMessages)
        {
            UE_LOG(LogTemp, Warning, TEXT("Message %u expired (incomplete chunks)"), MsgId);
            PartialMessages.Remove(MsgId);
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

    // 定义最大分片大小为64KB (65536字节)
    const int32 MAX_CHUNK_SIZE = 65536;

    UE_LOG(LogTemp, Log, TEXT("Send worker started with chunking (max %d bytes per chunk)"), MAX_CHUNK_SIZE);

    while (Subsystem->IsConnected() && Socket.IsValid())
    {
        // 从队列中获取消息
        FNetworkMessage Message;
        while (SendQueue.Dequeue(Message))
        {
            // 序列化消息
            FString JsonString = Subsystem->SerializeMessage(Message);

            // 转换为UTF-8字节流
            TArray<uint8> OutMsgData;
            UMessageMangerBPLibrary::ConvertFStringToBinary(JsonString, OutMsgData);
            int32 TotalDataLength = OutMsgData.Num();
            // 如果数据为空则跳过
            if (TotalDataLength <= 0)
            {
                UE_LOG(LogTemp, Warning, TEXT("Skipping empty message"));
                continue;
            }

            // 计算总分片数
            int32 TotalChunks = FMath::CeilToInt((float)TotalDataLength / MAX_CHUNK_SIZE);
            UE_LOG(LogTemp, Log, TEXT("Sending message as %d chunks (total %d bytes)"), TotalChunks, TotalDataLength);

            // 生成消息唯一ID，用于接收端重组
            uint32 MessageId = FMath::Rand(); // 实际项目中应使用更可靠的ID生成方式

            // 分片发送
            for (int32 ChunkIndex = 0; ChunkIndex < TotalChunks; ChunkIndex++)
            {
                // 计算当前分片的偏移量和大小
                int32 ChunkOffset = ChunkIndex * MAX_CHUNK_SIZE;
                int32 ChunkSize = FMath::Min(MAX_CHUNK_SIZE, TotalDataLength - ChunkOffset);

                // 构建分片头部 (共13字节: 4字节消息ID + 4字节总长度 + 4字节分片索引 + 1字节是否最后分片)
                struct FChunkHeader
                {
                    uint32 MessageId;
                    uint32 TotalLength;
                    uint32 ChunkIndex;
                    uint8 IsLastChunk;
                };

                FChunkHeader Header;
                Header.MessageId = MessageId;
                Header.TotalLength = TotalDataLength;
                Header.ChunkIndex = ChunkIndex;
                Header.IsLastChunk = (ChunkIndex == TotalChunks - 1) ? 1 : 0;

                // 准备发送缓冲区
                FArrayWriter ChunkData = FArrayWriter(true);

                // 写入头部
                ChunkData.Serialize(&Header, sizeof(FChunkHeader));

                // 写入当前分片的数据
                ChunkData.Serialize((void*)(OutMsgData.GetData() + ChunkOffset), ChunkSize);

                // 发送当前分片
                int32 BytesSent = 0;
                bool bSuccess = Socket->Send(ChunkData.GetData(), ChunkData.Num(), BytesSent);

                if (!bSuccess || BytesSent != ChunkData.Num())
                {
                    UE_LOG(LogTemp, Error, TEXT("Failed to send chunk %d. Sent %d of %d bytes"),
                        ChunkIndex, BytesSent, ChunkData.Num());
                    AsyncTask(ENamedThreads::GameThread, [this]()
                    {
                            Subsystem->Disconnect();
					});
                    
                    break;
                }
                else
                {
                    UE_LOG(LogTemp, Log, TEXT("Send chunk success %d send %d of %d bytes!"), ChunkIndex, ChunkData.Num(), BytesSent);
                }

                UE_LOG(LogTemp, Log, TEXT("Sent chunk %d/%d (size: %d bytes)"),
                    ChunkIndex + 1, TotalChunks, ChunkSize);
            }
        }

        // 短暂休眠，减少CPU占用
        FPlatformProcess::Sleep(0.001f);
    }

    UE_LOG(LogTemp, Log, TEXT("Send worker stopped"));
}
