#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <utility>
#include <map>
#include <set>
#include <tuple>
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <execution>
#include <type_traits>


#include "document.h"
#include "string_processing.h"
#include "concurrent_map.h"


using namespace std::string_literals;


//Количество топ-документов
const int MAX_RESULT_DOCUMENT_COUNT = 5;
//Допустимая погрешнось по релевантности
const double EPSILON = 1e-6;

class SearchServer {

private:

    //Храним парсированный запрос
    struct Query {
        std::vector<std::string_view> plus_words;
        std::vector<std::string_view> minus_words;
    };

    //Храним парсированное слово запроса
    struct QueryWord {
        bool is_minus;
        bool is_stop;
        std::string_view word;
    };

    //Храним инфу о документах
    struct DocumentData {
        int rating;
        DocumentStatus document_status;
    };

    std::set<int> ids_;

    //<id документа, инфа о документе>
    std::map<int, DocumentData> documents_;

    std::set<std::string, std::less<>> stop_words_;
    std::set<std::string, std::less<>> all_words_;

    //Храним документы
    //      < слово(ключ)   <  id(ключ),  TF  >>
    std::map<std::string, std::map<int, double>, std::less<>> word_to_document_freqs_;
    //    < id(ключ)    <  слово(ключ),   TF  >>
    std::map<int, std::map<std::string_view, double>> document_to_word_freqs_;

    bool IsStopWord(std::string_view word) const;
    static bool IsValidWord(std::string_view word);
    void LonelyMinusTerminator(std::string_view word) const;

    //Формируем вектор из строки с пробелами и вычёркиваем стоп-слова
    std::vector<std::string_view> SplitIntoWordsNoStop(std::string_view text) const;

    //Вычисляем средний рейтинг документа
    static int ComputeAverageRating(const std::vector<int>& ratings);

    //Парсинг слова строки запроса
    QueryWord ParseQueryWord(std::string_view word) const;

    //Парсинг строки запроса
    Query ParseQuery(std::string_view text) const;
    Query ParseQuerySeq(std::string_view text) const;

    //Вычисляем IDF конкретного слова из запроса
    double CalculateIDF(std::string_view plus_word) const;

    //Найти все документы, подходящие под запрос
    template <typename Predicate>
    std::vector<Document> FindAllDocuments(const std::execution::sequenced_policy&, const Query& query, Predicate predicate) const;
    template <typename Predicate>
    std::vector<Document> FindAllDocuments(const std::execution::parallel_policy&, const Query& query, Predicate predicate) const;
    template <typename Predicate>
    std::vector<Document> FindAllDocuments(const Query& query, Predicate predicate) const;


public:

    typedef typename std::set<int>::const_iterator id_const_iterator;
    id_const_iterator begin();
    id_const_iterator end();

    //Формируем множество стоп-слов — конструкторы:
    template <typename StringContainer>
    explicit SearchServer(const StringContainer& stop_words);
    explicit SearchServer(const std::string& stop_words_text);
    explicit SearchServer(std::string_view stop_words_view);

    //Узнаём количество документов
    int GetDocumentCount() const;

    //Получаем частоту слов в нужном документе
    const std::map<std::string_view, double>& GetWordFrequencies(int document_id) const;

    //Добавляем документ в базу
    void AddDocument(int id_document, std::string_view document, DocumentStatus status, const std::vector<int>& ratings);

    //Выбираем ТОП-документы
    template <typename ExecutionPolicy, typename Predicate>
    std::vector<Document> FindTopDocuments(const ExecutionPolicy& policy, std::string_view raw_query, Predicate predicate) const;
    template <typename Predicate>
    std::vector<Document> FindTopDocuments(std::string_view raw_query, Predicate predicate) const;
    template <typename ExecutionPolicy>
    std::vector<Document> FindTopDocuments(const ExecutionPolicy& policy, std::string_view raw_query, DocumentStatus status) const;
    std::vector<Document> FindTopDocuments(std::string_view raw_query, DocumentStatus status) const;
    template <typename ExecutionPolicy>
    std::vector<Document> FindTopDocuments(const ExecutionPolicy& policy, std::string_view raw_query) const;
    std::vector<Document> FindTopDocuments(std::string_view raw_query) const;

    //Матчинг документов
    std::tuple<std::vector<std::string_view>, DocumentStatus> MatchDocument(std::execution::sequenced_policy, std::string_view raw_query, int document_id) const;
    std::tuple<std::vector<std::string_view>, DocumentStatus> MatchDocument(std::execution::parallel_policy, std::string_view raw_query, int document_id) const;
    std::tuple<std::vector<std::string_view>, DocumentStatus> MatchDocument(std::string_view raw_query, int document_id) const;

    //Удаляем документ
    template <class ExecutionPolicy>
    void RemoveDocument(ExecutionPolicy&& policy, int document_id);
    void RemoveDocument(int document_id);
};

template <typename StringContainer>
SearchServer::SearchServer(const StringContainer& stop_words)
    : stop_words_(MakeUniqueNonEmptyStrings(stop_words)) {

    if (any_of(stop_words.begin(), stop_words.end(),
        [](const auto& stop_word) { return !IsValidWord(stop_word); })) {
        throw std::invalid_argument("One of stop-words contains special characters!"s);
    }
}


template <typename Predicate>
std::vector<Document> SearchServer::FindAllDocuments(const Query& query, Predicate predicate) const {
    //Конечный вектор с отобранными документами
    std::vector<Document> matched_documents;
    //<id, relevance> (relevance = sum(tf * idf))
    std::map<int, double> document_to_relevance;

    //формируем набор документов document_to_relevance с плюс-словами и их релевантностью
    for (std::string_view plus_word : query.plus_words) {
        if (word_to_document_freqs_.count(plus_word)) {
            //Вычисляем IDF конкретного слова из запроса
            const double idf = CalculateIDF(plus_word);
            for (const auto& [id_document, tf] : word_to_document_freqs_.find(plus_word)->second) {
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
    for (std::string_view minus_word : query.minus_words) {
        if (word_to_document_freqs_.count(minus_word)) {
            for (const auto& [id_document, relevance] : word_to_document_freqs_.find(minus_word)->second) {
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

template <typename Predicate>
std::vector<Document> SearchServer::FindAllDocuments(const std::execution::sequenced_policy&, const Query& query, Predicate predicate) const {
    return FindAllDocuments(query, predicate);
}

template <typename Predicate>
std::vector<Document> SearchServer::FindAllDocuments(const std::execution::parallel_policy&, const Query& query, Predicate predicate) const {
    //Конечный вектор с отобранными документами
    std::vector<Document> matched_documents;
    //<id, relevance> (relevance = sum(tf * idf))
    ConcurrentMap<int, double> document_to_relevance_par(100);

    std::for_each(
        std::execution::par,
        query.plus_words.begin(), query.plus_words.end(),
        [this, predicate, &document_to_relevance_par](auto& plus_word) {
            if (word_to_document_freqs_.count(plus_word)) {
                //Вычисляем IDF конкретного слова из запроса
                const double idf = CalculateIDF(plus_word);
                for (const auto& [id_document, tf] : word_to_document_freqs_.find(plus_word)->second) {
                    const auto& document_data = documents_.at(id_document);
                    if (predicate(id_document, document_data.document_status, document_data.rating)) {
                        //Вычисляем релевантность документа с учётом вхождения каждого плюс-слова
                        document_to_relevance_par[id_document].ref_to_value += tf * idf;
                    }
                }
            }
        }
    );

    std::map<int, double> document_to_relevance = document_to_relevance_par.BuildOrdinaryMap();

    std::for_each(
        std::execution::par,
        query.minus_words.begin(), query.minus_words.end(),
        [this, predicate, &document_to_relevance](auto& minus_word) {

            if (word_to_document_freqs_.count(minus_word)) {
                for (const auto& [id_document, relevance] : word_to_document_freqs_.find(minus_word)->second) {
                    document_to_relevance.erase(id_document);
                }
            }
        }
    );

    matched_documents.reserve(document_to_relevance.size());

    //Формируем результурующий вектор структуры Document
    for (const auto& [id_document, relevance] : document_to_relevance) {
        matched_documents.push_back({ id_document, relevance, documents_.at(id_document).rating });
    }

    return matched_documents;
}


template <typename ExecutionPolicy, typename Predicate>
std::vector<Document> SearchServer::FindTopDocuments(const ExecutionPolicy& policy, std::string_view raw_query, Predicate predicate) const {
    //Теперь валидность поискового запроса проверяется внутри ParseQuery (ParseQueryWord)
    const Query query = ParseQuerySeq(raw_query);
    auto matched_documents = FindAllDocuments(policy, query, predicate);
    //Сортировка по невозрастанию ...
    std::sort(
        policy,
        matched_documents.begin(), matched_documents.end(),
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

    if (matched_documents.size() > MAX_RESULT_DOCUMENT_COUNT) {
        matched_documents.resize(MAX_RESULT_DOCUMENT_COUNT);
    }
    return matched_documents;
}

template <typename Predicate>
std::vector<Document> SearchServer::FindTopDocuments(std::string_view raw_query, Predicate predicate) const {
    //Теперь валидность поискового запроса проверяется внутри ParseQuery (ParseQueryWord)
    const Query query = ParseQuerySeq(raw_query);
    auto matched_documents = FindAllDocuments(query, predicate);
    //Сортировка по невозрастанию ...
    std::sort(matched_documents.begin(), matched_documents.end(),
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

    if (matched_documents.size() > MAX_RESULT_DOCUMENT_COUNT) {
        matched_documents.resize(MAX_RESULT_DOCUMENT_COUNT);
    }
    return matched_documents;
}

template <typename ExecutionPolicy>
std::vector<Document> SearchServer::FindTopDocuments(const ExecutionPolicy& policy, std::string_view raw_query, DocumentStatus status) const {
    return FindTopDocuments(
        policy, raw_query,
        [status](int id_document, DocumentStatus document_status, int rating) {
            return document_status == status;
        }
    );
}


template <typename ExecutionPolicy>
std::vector<Document> SearchServer::FindTopDocuments(const ExecutionPolicy& policy, std::string_view raw_query) const {
    return FindTopDocuments(policy, raw_query, DocumentStatus::ACTUAL);
}


template <class ExecutionPolicy>
void SearchServer::RemoveDocument(ExecutionPolicy&& policy, int document_id) {
    if (!ids_.count(document_id)) {
        return;
    }

    std::vector<std::string_view> words_to_delete(document_to_word_freqs_.at(document_id).size());

    std::transform(
        policy,
        document_to_word_freqs_.at(document_id).begin(), document_to_word_freqs_.at(document_id).end(),
        words_to_delete.begin(),
        [](auto& word) {
            return word.first;
        }
    );

    std::for_each(
        policy,
        words_to_delete.begin(), words_to_delete.end(),
        [this, document_id](auto& word_to_delete) {
            word_to_document_freqs_.find(word_to_delete)->second.erase(document_id);
        }
    );

    document_to_word_freqs_.erase(document_id);
    documents_.erase(document_id);
    ids_.erase(document_id);
}