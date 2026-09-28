#include "stdafx.h"
#include "Log.h"

void PrintLog(const char* file, const char* func, int line, const char* fmt, ...)
{
	// One write per line: stdout is unbuffered, so every printf piece would
	// be its own write.
	char buffer[1024];
	int length = snprintf(buffer, sizeof(buffer), "%s/%s(%d): ", file, func, line);
	if (length < 0 || length >= static_cast<int>(sizeof(buffer)))
		length = 0;

	va_list ap;
	va_start(ap, fmt);
	int written = vsnprintf(buffer + length, sizeof(buffer) - length, fmt, ap);
	va_end(ap);

	if (written > 0)
		length = std::min(length + written, static_cast<int>(sizeof(buffer)) - 1);

	buffer[length++] = '\n';
	fwrite(buffer, 1, length, stdout);
}
