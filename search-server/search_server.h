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


//���������� ���-����������
const int MAX_RESULT_DOCUMENT_COUNT = 5;
//���������� ���������� �� �������������
const double EPSILON = 1e-6;

class SearchServer {

private:

    //������ ������������� ������
    struct Query {
        std::vector<std::string_view> plus_words;
        std::vector<std::string_view> minus_words;
    };

    //������ ������������� ����� �������
    struct QueryWord {
        bool is_minus;
        bool is_stop;
        std::string_view word;
    };

    //������ ���� � ����������
    struct DocumentData {
        int rating;
        DocumentStatus document_status;
    };

    std::set<int> ids_;

    //<id ���������, ���� � ���������>
    std::map<int, DocumentData> documents_;

    std::set<std::string, std::less<>> stop_words_;
    std::set<std::string, std::less<>> all_words_;

    //������ ���������
    //      < �����(����)   <  id(����),  TF  >>
    std::map<std::string, std::map<int, double>, std::less<>> word_to_document_freqs_;
    //    < id(����)    <  �����(����),   TF  >>
    std::map<int, std::map<std::string_view, double>> document_to_word_freqs_;

    bool IsStopWord(std::string_view word) const;
    static bool IsValidWord(std::string_view word);
    void LonelyMinusTerminator(std::string_view word) const;

    //��������� ������ �� ������ � ��������� � ����������� ����-�����
    std::vector<std::string_view> SplitIntoWordsNoStop(std::string_view text) const;

    //��������� ������� ������� ���������
    static int ComputeAverageRating(const std::vector<int>& ratings);

    //������� ����� ������ �������
    QueryWord ParseQueryWord(std::string_view word) const;

    //������� ������ �������
    Query ParseQuery(std::string_view text) const;
    Query ParseQuerySeq(std::string_view text) const;

    //��������� IDF ����������� ����� �� �������
    double CalculateIDF(std::string_view plus_word) const;

    //����� ��� ���������, ���������� ��� ������
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

    //��������� ��������� ����-���� � ������������:
    template <typename StringContainer>
    explicit SearchServer(const StringContainer& stop_words);
    explicit SearchServer(const std::string& stop_words_text);
    explicit SearchServer(std::string_view stop_words_view);

    //����� ���������� ����������
    int GetDocumentCount() const;

    //�������� ������� ���� � ������ ���������
    const std::map<std::string_view, double>& GetWordFrequencies(int document_id) const;

    //��������� �������� � ����
    void AddDocument(int id_document, std::string_view document, DocumentStatus status, const std::vector<int>& ratings);

    //�������� ���-���������
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

    //������� ����������
    std::tuple<std::vector<std::string_view>, DocumentStatus> MatchDocument(std::execution::sequenced_policy, std::string_view raw_query, int document_id) const;
    std::tuple<std::vector<std::string_view>, DocumentStatus> MatchDocument(std::execution::parallel_policy, std::string_view raw_query, int document_id) const;
    std::tuple<std::vector<std::string_view>, DocumentStatus> MatchDocument(std::string_view raw_query, int document_id) const;

    //������� ��������
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
    //�������� ������ � ����������� �����������
    std::vector<Document> matched_documents;
    //<id, relevance> (relevance = sum(tf * idf))
    std::map<int, double> document_to_relevance;

    //��������� ����� ���������� document_to_relevance � ����-������� � �� ��������������
    for (std::string_view plus_word : query.plus_words) {
        if (word_to_document_freqs_.count(plus_word)) {
            //��������� IDF ����������� ����� �� �������
            const double idf = CalculateIDF(plus_word);
            for (const auto& [id_document, tf] : word_to_document_freqs_.find(plus_word)->second) {
                const auto& document_data = documents_.at(id_document);
                if (predicate(id_document, document_data.document_status, document_data.rating)) {
                    //��������� ������������� ��������� � ������ ��������� ������� ����-�����
                    document_to_relevance[id_document] += tf * idf;
                }
            }
        }
        else { continue; }
    }

    //����������� �� document_to_relevance ��������� � �����-�������
    for (std::string_view minus_word : query.minus_words) {
        if (word_to_document_freqs_.count(minus_word)) {
            for (const auto& [id_document, relevance] : word_to_document_freqs_.find(minus_word)->second) {
                document_to_relevance.erase(id_document);
            }
        }
        else { continue; }
    }

    //��������� �������������� ������ ��������� Document
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
    //�������� ������ � ����������� �����������
    std::vector<Document> matched_documents;
    //<id, relevance> (relevance = sum(tf * idf))
    ConcurrentMap<int, double> document_to_relevance_par(100);

    std::for_each(
        std::execution::par,
        query.plus_words.begin(), query.plus_words.end(),
        [this, predicate, &document_to_relevance_par](auto& plus_word) {
            if (word_to_document_freqs_.count(plus_word)) {
                //��������� IDF ����������� ����� �� �������
                const double idf = CalculateIDF(plus_word);
                for (const auto& [id_document, tf] : word_to_document_freqs_.find(plus_word)->second) {
                    const auto& document_data = documents_.at(id_document);
                    if (predicate(id_document, document_data.document_status, document_data.rating)) {
                        //��������� ������������� ��������� � ������ ��������� ������� ����-�����
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

    //��������� �������������� ������ ��������� Document
    for (const auto& [id_document, relevance] : document_to_relevance) {
        matched_documents.push_back({ id_document, relevance, documents_.at(id_document).rating });
    }

    return matched_documents;
}


template <typename ExecutionPolicy, typename Predicate>
std::vector<Document> SearchServer::FindTopDocuments(const ExecutionPolicy& policy, std::string_view raw_query, Predicate predicate) const {
    //������ ���������� ���������� ������� ����������� ������ ParseQuery (ParseQueryWord)
    const Query query = ParseQuerySeq(raw_query);
    auto matched_documents = FindAllDocuments(policy, query, predicate);
    //���������� �� ������������� ...
    std::sort(
        policy,
        matched_documents.begin(), matched_documents.end(),
        [](const Document& lhs, const Document& rhs) {
            //... ��������, ���� ������������� ����������
            if (std::abs(lhs.relevance - rhs.relevance) < EPSILON) {
                return lhs.rating > rhs.rating;
            }
            //... �������������
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
    //������ ���������� ���������� ������� ����������� ������ ParseQuery (ParseQueryWord)
    const Query query = ParseQuerySeq(raw_query);
    auto matched_documents = FindAllDocuments(query, predicate);
    //���������� �� ������������� ...
    std::sort(matched_documents.begin(), matched_documents.end(),
        [](const Document& lhs, const Document& rhs) {
            //... ��������, ���� ������������� ����������
            if (std::abs(lhs.relevance - rhs.relevance) < EPSILON) {
                return lhs.rating > rhs.rating;
            }
            //... �������������
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