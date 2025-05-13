#pragma once

namespace Core
{
	class FileSystem
	{
	public:
		static vector<uint8_t> Read(const string& fileName, const uint32_t count = 0);
		static vector<uint32_t> Read32(const string& fileName, const uint32_t count = 0);

		static string GetExtension(const string& path);
	};
}