#pragma once

// 包一下Core的预编译头
#include <CoreSharedPCH.h>

// 需要导出一些静态变量 windows特有dllimport
#define LIBPROTOBUF_EXPORTS

#define GOOGLE_PROTOBUF_NO_RTTI 1

#if _WIN32
#pragma warning(disable : 4018 4065 4125 4310 4506 4661 4703 4800) // 错误告警
#pragma warning(disable : 4668 4701 4996) // 告警
#else
#define HAVE_PTHREAD
#endif