#pragma once

#include <cstdint>

class FEndianConverter
{
public:
    // 判断当前系统是否为小端字节序
    static bool IsLittleEndian();

    // 32位整数：小端转大端
    static uint32_t LittleToBigEndian(uint32_t Value);

    // 16位整数：小端转大端
    static uint16_t LittleToBigEndian16(uint16_t Value);

    // 主机字节序转网络字节序（网络字节序为大端）
    static uint32_t HostToNetwork32(uint32_t HostValue);

    // 网络字节序转主机字节序
    static uint32_t NetworkToHost32(uint32_t NetworkValue);

};
