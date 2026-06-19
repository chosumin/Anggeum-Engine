#pragma once

namespace Core
{
	class FileSystem
	{
	public:
		static vector<uint8_t> Read(const string& fileName, const uint32_t count = 0);
		static vector<uint32_t> Read32Fast(const string& fileName);
		static vector<uint32_t> Read32(const string& fileName);
		static vector<char> ReadChar(const string& fileName);
		static void Write(const string& fileName, const void* data, size_t size);
		static bool Exists(const string& fileName);
		static string GetExtension(const string& path);
	};
}