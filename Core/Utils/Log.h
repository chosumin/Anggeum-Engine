#pragma once
#include <stdio.h>
#include <stdarg.h>

#define LOG(fmt, ...) PrintLog(__FILE__, __FUNCTION__, __LINE__, fmt, ##__VA_ARGS__)

void PrintLog(const char* file, const char* func, int line, const char* fmt, ...);