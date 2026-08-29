#include "stdafx.h"
#include "CrashHandler.h"

#include <DbgHelp.h>
#pragma comment(lib, "Dbghelp.lib")

namespace
{
	LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS* info)
	{
		HANDLE process = GetCurrentProcess();
		SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME);
		SymInitialize(process, nullptr, TRUE);

		fprintf(stderr, "\n=== CRASH code=0x%08X addr=%p tid=%lu ===\n",
			(unsigned)info->ExceptionRecord->ExceptionCode,
			info->ExceptionRecord->ExceptionAddress, GetCurrentThreadId());

		CONTEXT ctx = *info->ContextRecord;
		STACKFRAME64 frame{};
		frame.AddrPC.Offset = ctx.Rip;    frame.AddrPC.Mode = AddrModeFlat;
		frame.AddrFrame.Offset = ctx.Rbp; frame.AddrFrame.Mode = AddrModeFlat;
		frame.AddrStack.Offset = ctx.Rsp; frame.AddrStack.Mode = AddrModeFlat;

		for (int i = 0; i < 24; ++i)
		{
			if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(),
				&frame, &ctx, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
				break;
			if (frame.AddrPC.Offset == 0)
				break;

			char buffer[sizeof(SYMBOL_INFO) + 256]{};
			SYMBOL_INFO* symbol = (SYMBOL_INFO*)buffer;
			symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
			symbol->MaxNameLen = 255;
			DWORD64 displacement = 0;
			if (SymFromAddr(process, frame.AddrPC.Offset, &displacement, symbol))
			{
				IMAGEHLP_LINE64 line{};
				line.SizeOfStruct = sizeof(line);
				DWORD lineDisplacement = 0;
				if (SymGetLineFromAddr64(process, frame.AddrPC.Offset, &lineDisplacement, &line))
					fprintf(stderr, "  #%d %s +0x%llx (%s:%lu)\n",
						i, symbol->Name, displacement, line.FileName, line.LineNumber);
				else
					fprintf(stderr, "  #%d %s +0x%llx\n", i, symbol->Name, displacement);
			}
			else
			{
				fprintf(stderr, "  #%d 0x%llx\n", i, frame.AddrPC.Offset);
			}
		}
		fflush(stderr);
		return EXCEPTION_EXECUTE_HANDLER;
	}
}

void Core::InstallCrashHandler()
{
	SetUnhandledExceptionFilter(OnUnhandledException);
}
