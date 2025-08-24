#include "EndianConverter.h"

// 判断当前系统是否为小端字节序
bool FEndianConverter::IsLittleEndian()
{
    // 通过联合体判断字节序
    union
    {
        uint32_t IntValue;
        uint8_t Bytes[4];
    } TestUnion;

    TestUnion.IntValue = 0x01020304;
    
    // 小端系统中，低地址存储低字节，所以Bytes[0]会是0x04
    return (TestUnion.Bytes[0] == 0x04);
}

// 32位整数：小端转大端
uint32_t FEndianConverter::LittleToBigEndian(uint32_t Value)
{
    // 拆分字节并重新排列
    return (
        ((Value & 0x000000FF) << 24) |  // 低字节移到高字节位
        ((Value & 0x0000FF00) << 8)  |  // 次低字节移到次高字节位
        ((Value & 0x00FF0000) >> 8)  |  // 次高字节移到次低字节位
        ((Value & 0xFF000000) >> 24)    // 高字节移到低字节位
    );
}

// 16位整数：小端转大端
uint16_t FEndianConverter::LittleToBigEndian16(uint16_t Value)
{
    return (
        ((Value & 0x00FF) << 8) |  // 低字节移到高字节位
        ((Value & 0xFF00) >> 8)    // 高字节移到低字节位
    );
}

// 主机字节序转网络字节序（网络字节序为大端）
uint32_t FEndianConverter::HostToNetwork32(uint32_t HostValue)
{
    // 如果是小端系统，需要转换；大端系统直接返回原值
    if (IsLittleEndian())
    {
        return LittleToBigEndian(HostValue);
    }
    return HostValue;
}

// 网络字节序转主机字节序
uint32_t FEndianConverter::NetworkToHost32(uint32_t NetworkValue)
{
    // 与主机转网络的逻辑相同，因为网络字节序固定为大端
    return HostToNetwork32(NetworkValue);
}
