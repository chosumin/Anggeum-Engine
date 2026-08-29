#pragma once

namespace Core
{
	// Installs a process-wide unhandled-exception filter that prints a
	// symbolized stack trace (via DbgHelp + the PDB next to the exe) to
	// stderr - for whichever thread faulted. Call once at startup.
	void InstallCrashHandler();
}
