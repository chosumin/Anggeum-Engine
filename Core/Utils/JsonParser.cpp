#include "stdafx.h"
#include "JsonParser.h"

unique_ptr<Document> JsonParser::LoadDocument(string path)
{
    FILE* fp = nullptr;
    fopen_s(&fp, path.c_str(), "rb");

    if (fp == nullptr) {
        std::cout << "file not exist : default param will be used" << std::endl;
        return nullptr;
    }

    //hack : length hardcoding.
    char readbuffer[65536];
    FileReadStream is(fp, readbuffer, sizeof(readbuffer));

    auto document = make_unique<Document>();
    document->ParseStream(is);

    return document;
}
