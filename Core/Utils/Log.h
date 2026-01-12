#pragma once
#include <stdio.h>
#include <stdarg.h>

#define LOG(fmt, ...) PrintLog(__FILE__, __FUNCTION__, __LINE__, fmt, ##__VA_ARGS__)

void PrintLog(const char* file, const char* func, int line, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    printf("%s/%s(%d): ", file, func, line);
    vprintf(fmt, ap);

    va_end(ap);

    printf("\n");
}