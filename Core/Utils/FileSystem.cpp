#include "stdafx.h"
#include "FileSystem.h"

vector<uint8_t> Core::FileSystem::Read(const string& fileName, const uint32_t count)
{
	vector<uint8_t> data;

	ifstream file;

	file.open(fileName, ios::in | ios::binary);

	if (!file.is_open())
	{
		throw runtime_error("Failed to open file: " + fileName);
	}

	uint64_t readCount = count;
	if (count == 0)
	{
		file.seekg(0, ios::end);
		readCount = static_cast<uint64_t>(file.tellg());
		file.seekg(0, ios::beg);
	}

	data.resize(static_cast<size_t>(readCount));
	file.read(reinterpret_cast<char*>(data.data()), readCount);
	file.close();

	return data;
}

vector<uint32_t> Core::FileSystem::Read32Fast(const string& fileName)
{
	std::ifstream file(fileName, std::ios::binary | std::ios::ate);

	if (!file) {
		throw std::runtime_error("Failed to open file: " + fileName);
	}

	std::streamsize size = file.tellg();
	if (size % 4 != 0) {
		throw std::runtime_error("File size is not aligned to 4 bytes.");
	}

	file.seekg(0, std::ios::beg);

	std::vector<uint32_t> result(size / 4);
	if (!file.read(reinterpret_cast<char*>(result.data()), size)) {
		throw std::runtime_error("Failed to read file: " + fileName);
	}

	return result;
}

vector<uint32_t> Core::FileSystem::Read32(const string& fileName)
{
	vector<uint32_t> data;

	ifstream file;

	file.open(fileName, ios::in | ios::binary);

	if (!file.is_open())
	{
		throw runtime_error("Failed to open file: " + fileName);
	}

	file.seekg(0, ios::end);
	uint64_t readCount = static_cast<uint64_t>(file.tellg());
	file.seekg(0, ios::beg);

	data.resize(static_cast<size_t>(readCount));
	file.read(reinterpret_cast<char*>(data.data()), readCount);
	file.close();

	return data;
}

vector<char> Core::FileSystem::ReadChar(const string& fileName)
{
	ifstream file{ fileName, ios::ate | ios::binary };

	if (!file.is_open())
		throw runtime_error{ "failed to open file" };

	size_t fileSize{ static_cast<size_t>(file.tellg()) };
	vector<char> buffer(fileSize);

	file.seekg(0);
	file.read(buffer.data(), fileSize);
	file.close();

	return buffer;
}

void Core::FileSystem::Write(const string& fileName, const void* data, size_t size)
{
	// Ensure the destination directory exists before writing
	std::filesystem::path path(fileName);
	if (path.has_parent_path())
	{
		std::filesystem::create_directories(path.parent_path());
	}

	ofstream file;
	file.open(fileName, ios::out | ios::binary | ios::trunc);

	if (!file.is_open())
	{
		throw runtime_error("Failed to open file for writing: " + fileName);
	}

	file.write(reinterpret_cast<const char*>(data), static_cast<streamsize>(size));
	file.close();

	if (!file.good())
	{
		throw runtime_error("Failed to write file: " + fileName);
	}
}

bool Core::FileSystem::Exists(const string& fileName)
{
	return std::filesystem::exists(fileName);
}

string Core::FileSystem::GetExtension(const string& path)
{
	auto dotPos = path.find_last_of('.');
	if (dotPos == string::npos)
	{
		throw runtime_error{ "Path has no extension" };
	}

	return path.substr(dotPos + 1);
}
