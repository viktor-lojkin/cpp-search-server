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


//���������� ���-����������
const int MAX_RESULT_DOCUMENT_COUNT = 5;
//���������� ���������� �� �������������
const double EPSILON = 1e-6;

class SearchServer {
public:

    //��������� ��������� ����-���� �� ������ � ������������:
    template <typename StringContainer>
    explicit SearchServer(const StringContainer& stop_words);
    explicit SearchServer(const std::string& stop_words_text);

    //��������� �������� � ����
    void AddDocument(int id_document, const std::string& document, DocumentStatus status, const std::vector<int>& ratings);

    //�������� ���-���������
    template <typename Predicate>
    std::vector<Document> FindTopDocuments(const std::string& raw_query, Predicate predicate) const;
    std::vector<Document> FindTopDocuments(const std::string& raw_query, DocumentStatus status) const;
    std::vector<Document> FindTopDocuments(const std::string& raw_query) const;

    //������� ����������
    std::tuple<std::vector<std::string>, DocumentStatus> MatchDocument(const std::string& raw_query, int document_id) const;

    //����� ���������� � id ���������
    int GetDocumentCount() const;

    //������ GetDocumentId
    typedef typename std::set<int>::const_iterator id_const_iterator;
    id_const_iterator begin();
    id_const_iterator end();

    //�������� ������� ���� � ������ ���������
    const std::map<std::string, double>& GetWordFrequencies(int document_id) const;

    //������� ��������
    void RemoveDocument(int document_id);


private:

    //������ ���� � ����������
    struct DocumentData {
        int rating;
        DocumentStatus document_status;
    };
    //<id ���������, ���� � ���������>
    std::map<int, DocumentData> documents_;

    //������ �������� ����-����
    std::set<std::string> stop_words_;

    //������ ������������� ������
    struct Query {
        std::set<std::string> plus_words;
        std::set<std::string> minus_words;
    };

    //������ ������������� ����� �������
    struct QueryWord {
        bool is_minus;
        bool is_stop;
        std::string word;
    };

    //������ ��������� < �����(����) <id(����), TF> >
    std::map<std::string, std::map<int, double>> word_to_document_freqs_;
    //      < id(����)    <  �����(����), TF  >>
    std::map<int, std::map<std::string, double>> document_to_word_freqs_;

    //������ id ���������� � ������� �� ���������� � ����
    std::set<int> ids_;

    //��������
    bool IsStopWord(const std::string& word) const;
    static bool IsValidWord(const std::string& word);
    void LonelyMinusTerminator(const std::string& word) const;

    //��������� ������ �� ������ � ��������� � ����������� ����-�����
    std::vector<std::string> SplitIntoWordsNoStop(const std::string& text) const;

    //��������� ������� ������� ���������
    static int ComputeAverageRating(const std::vector<int>& ratings);

    //������� ����� ������ �������
    QueryWord ParseQueryWord(std::string word) const;

    //������� ������ �������
    Query ParseQuery(const std::string& text) const;

    //��������� IDF ����������� ����� �� �������
    double CalculateIDF(const std::string& plus_word) const;

    //����� ��� ���������, ���������� ��� ������
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
// SearchServer::SearchServer(const string& stop_words_text) ����� � search_server.cpp

//�������� ���-��������� (� ��������������� ���������� ����������)
template <typename Predicate>
std::vector<Document> SearchServer::FindTopDocuments(const std::string& raw_query, Predicate predicate) const {
    //������ ���������� ���������� ������� ����������� ������ ParseQuery (ParseQueryWord)
    const Query query = ParseQuery(raw_query);
    auto matched_documents = FindAllDocuments(query, predicate);
    //���������� �� ������������� ...
    sort(matched_documents.begin(), matched_documents.end(),
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
    //��������� ������ ��� ����������� ���
    if (matched_documents.size() > MAX_RESULT_DOCUMENT_COUNT) {
        matched_documents.resize(MAX_RESULT_DOCUMENT_COUNT);
    }
    return matched_documents;
}


//����� ��� ���������, ���������� ��� ������
template <typename Predicate>
std::vector<Document> SearchServer::FindAllDocuments(const Query& query, Predicate predicate) const {
    //�������� ������ � ����������� �����������
    std::vector<Document> matched_documents;
    //<id, relevance> (relevance = sum(tf * idf))
    std::map<int, double> document_to_relevance;

    //��������� ����� ���������� document_to_relevance � ����-������� � �� ��������������
    for (const std::string& plus_word : query.plus_words) {
        if (word_to_document_freqs_.count(plus_word)) {
            //��������� IDF ����������� ����� �� �������
            const double idf = CalculateIDF(plus_word);
            for (const auto& [id_document, tf] : word_to_document_freqs_.at(plus_word)) {
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
    for (const std::string& minus_word : query.minus_words) {
        if (word_to_document_freqs_.count(minus_word)) {
            for (const auto& [id_document, relevance] : word_to_document_freqs_.at(minus_word)) {
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