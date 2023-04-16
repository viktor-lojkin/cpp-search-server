#pragma once

#include <string>
#include <iostream>

using namespace std::string_literals;

//Храним документ
struct Document {
    Document();

    Document(int id, double relevance, int rating);

    int id = 0;
    double relevance = 0.0;
    int rating = 0;
};

//Статусы документа
enum class DocumentStatus {
    ACTUAL,
    IRRELEVANT,
    BANNED,
    REMOVED,
};


std::ostream& operator<<(std::ostream& out, Document document);