#include <cmath>
#include <numeric>

#include "search_server.h"


// ��������� ��������� ����-���� �� ������ � ������������:
SearchServer::SearchServer(const std::string& stop_words_text)
    : SearchServer(SplitIntoWords(stop_words_text)) {
}

// ��������� �������� � ����������� ����������, ���� �������� ����������(��.���� � private),
// ��� id ������������� ��� ����������� � ����
void SearchServer::AddDocument(int id_document, const std::string& document, DocumentStatus status,
    const std::vector<int>& ratings) {
    //������ ��� ���������� ��������� ���� ��������� ������ ����������� ��� ID
    if (id_document < 0 || documents_.count(id_document) > 0) {
        throw std::invalid_argument("Something wrong with ID!"s);
    }
    //������ �������� ���������� ������� ����� ��������� � SplitIntoWordsNoStop  
    const std::vector<std::string> words = SplitIntoWordsNoStop(document);
    //��������� �������� � ��������� TF ����������� ����� � ��
    const double tf = 1.0 / static_cast<double>(words.size());
    for (const std::string& word : words) {
        word_to_document_freqs_[word][id_document] += tf;
        document_to_word_freqs_[id_document][word] += tf;
    }
    documents_.emplace(id_document, DocumentData{ ComputeAverageRating(ratings), status });
    ids_.insert(id_document);
}

// �������� ���-��������� (� ��������������� ���������� ����������)
// template <typename Predicate>
// vector<Document> FindTopDocuments(const string& raw_query, Predicate predicate) const

// �������� ���-��������� (�� �������)
std::vector<Document> SearchServer::FindTopDocuments(const std::string& raw_query, DocumentStatus status) const {
    return FindTopDocuments(raw_query, [status](int id_document, DocumentStatus document_status, int rating)
        { return document_status == status; });
}

// �������� ���-��������� (���� ������� ������� � ����� ����������)
std::vector<Document> SearchServer::FindTopDocuments(const std::string& raw_query) const {
    // ����� ����� ���������� ������ ����������
    return FindTopDocuments(raw_query, DocumentStatus::ACTUAL);
}

// ������� ���������� - ���������� ��� ����� �� ���������� �������, �������������� � ���������
std::tuple<std::vector<std::string>, DocumentStatus> SearchServer::MatchDocument(const std::string& raw_query,
    int document_id) const {
    // ������ ���������� ���������� ������� ����������� ������ ParseQuery (ParseQueryWord)
    const Query query = ParseQuery(raw_query);
    std::vector<std::string> matched_words;
    for (const std::string& word : query.plus_words) {
        if (word_to_document_freqs_.count(word) == 0) {
            continue;
        }
        if (word_to_document_freqs_.at(word).count(document_id)) {
            matched_words.push_back(word);
        }
    }
    for (const std::string& word : query.minus_words) {
        if (word_to_document_freqs_.count(word) == 0) {
            continue;
        }
        if (word_to_document_freqs_.at(word).count(document_id)) {
            matched_words.clear();
            break;
        }
    }
    return { matched_words, documents_.at(document_id).document_status };
}

// ����� ������� � ��� ����������
int SearchServer::GetDocumentCount() const {
    return documents_.size();
}

// 
SearchServer::id_const_iterator SearchServer::begin() {
    return ids_.begin();
}
SearchServer::id_const_iterator SearchServer::end() {
    return ids_.end();
}

const std::map<std::string, double>& SearchServer::GetWordFrequencies(int document_id) const {
    static const std::map<std::string, double> empty_map{};
    if (document_to_word_freqs_.empty()) {
        return empty_map;
    } else {
        return document_to_word_freqs_.at(document_id);
    }
}

void SearchServer::RemoveDocument(int document_id) {
    if (!documents_.count(document_id)) {
        return;
    }

    documents_.erase(document_id);
    ids_.erase(document_id);

    for (auto& [word, tf] : document_to_word_freqs_.at(document_id)) {
        word_to_document_freqs_.at(word).erase(document_id);
    }

    document_to_word_freqs_.erase(document_id);
}

// �������� - "��� ����-�����?"
bool SearchServer::IsStopWord(const std::string& word) const {
    return stop_words_.count(word) > 0;
}

// �������� �� ����-�������
bool SearchServer::IsValidWord(const std::string& word) {
    // �������� ������� �� �����, ������� �� �������� ����-�������
    return none_of(word.begin(), word.end(),
        [](char c) { return c >= '\0' && c < ' '; });
}

// ����������� ����������, ���� ������� �������� ����� (' - ')
void SearchServer::LonelyMinusTerminator(const std::string& word) const {
    if (word.size() == 1 && word[0] == '-') {
        throw std::invalid_argument("This word contains only \'-\' and nothing else"s);
    }
}

//��������� ������ �� ������ � ��������� � ����������� ����-�����
std::vector<std::string> SearchServer::SplitIntoWordsNoStop(const std::string& text) const {
    std::vector<std::string> words;
    for (const std::string& word : SplitIntoWords(text)) {
        if (!IsValidWord(word)) {
            throw std::invalid_argument("Invalid word(s) in the adding doccument!"s);
        }
        if (!IsStopWord(word)) {
            words.push_back(word);
        }
    }
    return words;
}

// ��������� ������� ������� ���������
int SearchServer::ComputeAverageRating(const std::vector<int>& ratings) {
    if (ratings.empty()) {
        return 0;
    }
    return accumulate(ratings.begin(), ratings.end(), 0) /
        static_cast<int>(ratings.size());
}

// ������� ����� ������ �������
SearchServer::QueryWord SearchServer::ParseQueryWord(std::string word) const {
    //��� �������� �����?
    if (!IsValidWord(word)) {
        throw std::invalid_argument("Your word has a special character!"s);
    }
    //�������� �� ���������� ��������� ������
    LonelyMinusTerminator(word);

    bool is_minus = false;
    //��� �����-�����?
    if (word[0] == '-') {
        //��� ����� � ��������� �� ���� �������?
        if (word[1] == '-') {
            //��� ����� �� ��������!
            throw std::invalid_argument("Trying to set minus-minus word!"s);
        }
        //��� ������ �����-�����! ������ ����� "-" �������.
        is_minus = true;
        word = word.substr(1);
    }
    return { is_minus, IsStopWord(word), word };
}

// ������� ������ �������
SearchServer::Query SearchServer::ParseQuery(const std::string& text) const {
    Query query;
    // ����������� �� ������� ����� �������
    for (const std::string& word : SplitIntoWords(text)) {
        const QueryWord query_word = ParseQueryWord(word);
        if (!query_word.is_stop) {
            if (query_word.is_minus) {
                // �������� �����-����� � ��������������� ���������
                query.minus_words.insert(query_word.word);
            }
            else {
                // �������� ����-����� � ��������������� ���������
                query.plus_words.insert(query_word.word);
            }
        }
    }
    return query;
}

// ��������� IDF ����������� ����� �� �������
double SearchServer::CalculateIDF(const std::string& plus_word) const {
    return std::log(static_cast<double>(GetDocumentCount()) / static_cast<double>(word_to_document_freqs_.at(plus_word).size()));
}