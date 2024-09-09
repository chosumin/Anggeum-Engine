#pragma once

class JsonParser
{
public:
    static unique_ptr<Document> LoadDocument(string path);
};

