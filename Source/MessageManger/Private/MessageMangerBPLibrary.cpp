// Copyright Epic Games, Inc. All Rights Reserved.

#include "MessageMangerBPLibrary.h"
#include "MessageManger.h"

UMessageMangerBPLibrary::UMessageMangerBPLibrary(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{

}

float UMessageMangerBPLibrary::MessageMangerSampleFunction(float Param)
{
	return -1;
}

FString UMessageMangerBPLibrary::ConvertUtf8BinaryToString(const TArray<uint8>& BinaryData)
{
    if (BinaryData.Num() == 0)
    {
        return FString();
    }

    // 确保数据以 null 结尾（避免解析越界）
    TArray<uint8> NullTerminatedBytes = BinaryData;
    NullTerminatedBytes.Add(0); // 添加 UTF-8 终止符 '\0'

    // 将字节数组转换为 UTF-8 字符串，再转为 FString
    const char* Utf8Data = reinterpret_cast<const char*>(NullTerminatedBytes.GetData());
    return FString(UTF8_TO_TCHAR(Utf8Data));
}

// 将 const wchar_t* 转换为二进制字节数组
void UMessageMangerBPLibrary::ConvertWCharToBinary(const wchar_t* WideStr, TArray<uint8>& OutBinaryData)
{
    if (!WideStr)
    {
        OutBinaryData.Empty();
        return;
    }

    // 1. 计算宽字符串长度（包含结尾的 '\0' 终止符）
    int32 WideStrLength = 0;
    while (WideStr[WideStrLength] != L'\0')
    {
        WideStrLength++;
    }
    WideStrLength++;  // 包含最后的 '\0'

    // 2. 计算二进制数据总字节数（每个 wchar_t 占 sizeof(wchar_t) 字节）
    int32 TotalBytes = WideStrLength * sizeof(wchar_t);

    // 3. 分配内存并复制数据
    OutBinaryData.SetNumUninitialized(TotalBytes);
    FMemory::Memcpy(OutBinaryData.GetData(), WideStr, TotalBytes);
}

void UMessageMangerBPLibrary::ConvertFStringToBinary(FString Str, TArray<uint8>& outBinaryData)
{
    const wchar_t* Utf8Data = *Str;
    UMessageMangerBPLibrary::ConvertWCharToBinary(Utf8Data, outBinaryData);
}