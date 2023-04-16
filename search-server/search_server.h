#pragma once

#include <string>
#include <vector>
#include <utility>
#include <map>
#include <set>
#include <tuple>
#include <cmath>
#include <algorithm>
#include <stdexcept>


#include "document.h"
#include "string_processing.h"


using namespace std::string_literals;


//Количество топ-документов
const int MAX_RESULT_DOCUMENT_COUNT = 5;
//Допустимая погрешнось по релевантности
const double EPSILON = 1e-6;

class SearchServer {
public:

    //Формируем множество стоп-слов из строки — конструкторы:
    template <typename StringContainer>
    explicit SearchServer(const StringContainer& stop_words);
    explicit SearchServer(const std::string& stop_words_text);

    //Добавляем документ в базу
    void AddDocument(int id_document, const std::string& document, DocumentStatus status, const std::vector<int>& ratings);

    //Выбираем ТОП-документы
    template <typename Predicate>
    std::vector<Document> FindTopDocuments(const std::string& raw_query, Predicate predicate) const;
    std::vector<Document> FindTopDocuments(const std::string& raw_query, DocumentStatus status) const;
    std::vector<Document> FindTopDocuments(const std::string& raw_query) const;

    //Матчинг документов
    std::tuple<std::vector<std::string>, DocumentStatus> MatchDocument(const std::string& raw_query, int document_id) const;

    //Узнаём количество и id документа
    int GetDocumentCount() const;

    //Вместо GetDocumentId
    typedef typename std::set<int>::const_iterator id_const_iterator;
    id_const_iterator begin();
    id_const_iterator end();

    //Получаем частоту слов в нужном документе
    const std::map<std::string, double>& GetWordFrequencies(int document_id) const;

    //Удаляем документ
    void RemoveDocument(int document_id);


private:

    //Храним инфу о документах
    struct DocumentData {
        int rating;
        DocumentStatus document_status;
    };
    //<id документа, инфа о документе>
    std::map<int, DocumentData> documents_;

    //Храним ножество стоп-слов
    std::set<std::string> stop_words_;

    //Храним парсированный запрос
    struct Query {
        std::set<std::string> plus_words;
        std::set<std::string> minus_words;
    };

    //Храним парсированное слово запроса
    struct QueryWord {
        bool is_minus;
        bool is_stop;
        std::string word;
    };

    //Храним документы < слово(ключ) <id(ключ), TF> >
    std::map<std::string, std::map<int, double>> word_to_document_freqs_;
    //      < id(ключ)    <  слово(ключ), TF  >>
    std::map<int, std::map<std::string, double>> document_to_word_freqs_;

    //Храним id документов в порядке их добавления в базу
    std::set<int> ids_;

    //Проверки
    bool IsStopWord(const std::string& word) const;
    static bool IsValidWord(const std::string& word);
    void LonelyMinusTerminator(const std::string& word) const;

    //Формируем вектор из строки с пробелами и вычёркиваем стоп-слова
    std::vector<std::string> SplitIntoWordsNoStop(const std::string& text) const;

    //Вычисляем средний рейтинг документа
    static int ComputeAverageRating(const std::vector<int>& ratings);

    //Парсинг слова строки запроса
    QueryWord ParseQueryWord(std::string word) const;

    //Парсинг строки запроса
    Query ParseQuery(const std::string& text) const;

    //Вычисляем IDF конкретного слова из запроса
    double CalculateIDF(const std::string& plus_word) const;

    //Найти все документы, подходящие под запрос
    template <typename Predicate>
    std::vector<Document> FindAllDocuments(const Query& query, Predicate predicate) const;
};

template <typename StringContainer>
SearchServer::SearchServer(const StringContainer& stop_words)
    : stop_words_(MakeUniqueNonEmptyStrings(stop_words)) {

    if (any_of(stop_words.begin(), stop_words.end(),
        [](const auto& stop_word) { return !IsValidWord(stop_word); })) {
        throw std::invalid_argument("One of stop-words contains special characters!"s);
    }
}
// SearchServer::SearchServer(const string& stop_words_text) лежит в search_server.cpp

//Выбираем ТОП-документы (с дополнительными критериями сортировки)
template <typename Predicate>
std::vector<Document> SearchServer::FindTopDocuments(const std::string& raw_query, Predicate predicate) const {
    //Теперь валидность поискового запроса проверяется внутри ParseQuery (ParseQueryWord)
    const Query query = ParseQuery(raw_query);
    auto matched_documents = FindAllDocuments(query, predicate);
    //Сортировка по невозрастанию ...
    sort(matched_documents.begin(), matched_documents.end(),
        [](const Document& lhs, const Document& rhs) {
            //... рейтинга, если релевантность одинаковая
            if (std::abs(lhs.relevance - rhs.relevance) < EPSILON) {
                return lhs.rating > rhs.rating;
            }
            //... релевантности
            else {
                return lhs.relevance > rhs.relevance;
            }
        });
    //Уменьшаем вектор под объявленный ТОП
    if (matched_documents.size() > MAX_RESULT_DOCUMENT_COUNT) {
        matched_documents.resize(MAX_RESULT_DOCUMENT_COUNT);
    }
    return matched_documents;
}


//Найти все документы, подходящие под запрос
template <typename Predicate>
std::vector<Document> SearchServer::FindAllDocuments(const Query& query, Predicate predicate) const {
    //Конечный вектор с отобранными документами
    std::vector<Document> matched_documents;
    //<id, relevance> (relevance = sum(tf * idf))
    std::map<int, double> document_to_relevance;

    //формируем набор документов document_to_relevance с плюс-словами и их релевантностью
    for (const std::string& plus_word : query.plus_words) {
        if (word_to_document_freqs_.count(plus_word)) {
            //Вычисляем IDF конкретного слова из запроса
            const double idf = CalculateIDF(plus_word);
            for (const auto& [id_document, tf] : word_to_document_freqs_.at(plus_word)) {
                const auto& document_data = documents_.at(id_document);
                if (predicate(id_document, document_data.document_status, document_data.rating)) {
                    //Вычисляем релевантность документа с учётом вхождения каждого плюс-слова
                    document_to_relevance[id_document] += tf * idf;
                }
            }
        }
        else { continue; }
    }

    //Вычёркиваем из document_to_relevance документы с минус-словами
    for (const std::string& minus_word : query.minus_words) {
        if (word_to_document_freqs_.count(minus_word)) {
            for (const auto& [id_document, relevance] : word_to_document_freqs_.at(minus_word)) {
                document_to_relevance.erase(id_document);
            }
        }
        else { continue; }
    }

    //Формируем результурующий вектор структуры Document
    for (const auto& [id_document, relevance] : document_to_relevance) {
        matched_documents.push_back({ id_document, relevance, documents_.at(id_document).rating });
    }
    return matched_documents;
}