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

string Core::FileSystem::GetExtension(const string& path)
{
	auto dotPos = path.find_last_of('.');
	if (dotPos == string::npos)
	{
		throw runtime_error{ "Path has no extension" };
	}

	return path.substr(dotPos + 1);
}
