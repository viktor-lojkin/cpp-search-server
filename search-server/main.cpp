#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <cmath>
#include <numeric>

using namespace std;

//Количество топ-документов
const int MAX_RESULT_DOCUMENT_COUNT = 5;
//Допустимая погрешнось по релевантности
const double EPSILON = 1e-6;

string ReadLine() {
    string s;
    getline(cin, s);
    return s;
}

int ReadLineWithNumber() {
    int result = 0;
    cin >> result;
    ReadLine();
    return result;
}

//Формируем вектор из строки с пробелами
vector<string> SplitIntoWords(const string& text) {
    vector<string> words;
    string word;
    for (const char c : text) {
        if (c == ' ') {
            if (!word.empty()) {
                words.push_back(word);
                word.clear();
            }
        }
        else {
            word += c;
        }
    }
    if (!word.empty()) {
        words.push_back(word);
    }

    return words;
}

//Храним документ
struct Document {
    int id;
    double relevance;
    int rating;
};

//Статусы документа
enum class DocumentStatus {
    ACTUAL,
    IRRELEVANT,
    BANNED,
    REMOVED,
};

class SearchServer {
public:
    //Формируем множество стоп-слов из строки
    void SetStopWords(const string& text) {
        for (const string& word : SplitIntoWords(text)) {
            stop_words_.insert(word);
        }
    }

    //Добавляем документ
    void AddDocument(int id_document, const string& document, DocumentStatus status,
        const vector<int>& ratings) {
        const vector<string> words = SplitIntoWordsNoStop(document);
        //Добавляем документ и вычисляем TF конкретного слова в нём 
        const double tf = 1.0 / static_cast<double>(words.size());
        for (const string& word : words) {
            word_to_document_freqs_[word][id_document] += tf;
        }
        documents_.emplace(id_document, DocumentData{ ComputeAverageRating(ratings), status });
    }

    //Выбираем ТОП-документы (с дополнительными критериями сортировки)
    template <typename Predicate>
    vector<Document> FindTopDocuments(const string& raw_query, Predicate predicate) const {
        const Query query = ParseQuery(raw_query);
        auto matched_documents = FindAllDocuments(query, predicate);
        //Сортировка по невозрастанию ...
        sort(matched_documents.begin(), matched_documents.end(),
            [](const Document& lhs, const Document& rhs) {
                //... рейтинга, если релевантность одинаковая
                if (abs(lhs.relevance - rhs.relevance) < EPSILON) {
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

    //Выбираем ТОП-документы (по статусу)
    vector<Document> FindTopDocuments(const string& raw_query, DocumentStatus status) const {
        return FindTopDocuments(raw_query, [status](int id_document, DocumentStatus document_status, int rating)
            { return document_status == status; });
    }

    //Выбираем ТОП-документы (если хочется вызвать с одним аргументом)
    vector<Document> FindTopDocuments(const string& raw_query) const {
        //Тогда будем показывать только АКТУАЛЬНЫЕ
        return FindTopDocuments(raw_query, DocumentStatus::ACTUAL);
    }

    //Узнать сколько у нас документов
    int GetDocumentCount() const {
        return documents_.size();
    }

    tuple<vector<string>, DocumentStatus> MatchDocument(const string& raw_query,
        int document_id) const {
        const Query query = ParseQuery(raw_query);
        vector<string> matched_words;
        for (const string& word : query.plus_words) {
            if (word_to_document_freqs_.count(word) == 0) {
                continue;
            }
            if (word_to_document_freqs_.at(word).count(document_id)) {
                matched_words.push_back(word);
            }
        }
        for (const string& word : query.minus_words) {
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

private:
    //Храним инфу о документах
    struct DocumentData {
        int rating;
        DocumentStatus document_status;
    };
    //<id документа, инфа о документе>
    map<int, DocumentData> documents_;

    //Храним ножество стоп-слов
    set<string> stop_words_;

    //Храним парсированный запрос    
    struct Query {
        set<string> plus_words;
        set<string> minus_words;
    };

    //Храним парсированное слово запроса
    struct QueryWord {
        bool is_minus;
        bool is_stop;
        string word;
    };

    //Храним документы <слово(ключ) <id(ключ) , TF>>
    map<string, map<int, double>> word_to_document_freqs_;

    //Проверка - "это стоп-слово?"
    bool IsStopWord(const string& word) const {
        return stop_words_.count(word) > 0;
    }

    //Формируем вектор из строки с пробелами и вычёркиваем стоп-слова  
    vector<string> SplitIntoWordsNoStop(const string& text) const {
        vector<string> words;
        for (const string& word : SplitIntoWords(text)) {
            if (!IsStopWord(word)) {
                words.push_back(word);
            }
        }
        return words;
    }

    //Вычисляем средний рейтинг документа
    static int ComputeAverageRating(const vector<int>& ratings) {
        if (ratings.empty()) {
            return 0;
        }
        return accumulate(ratings.begin(), ratings.end(), 0) /
            static_cast<int>(ratings.size());
    }

    //Парсинг слова строки запроса
    QueryWord ParseQueryWord(string word) const {
        bool is_minus = false;
        //Это минус-слово?
        if (word[0] == '-') {
            //Да! Теперь уберём "-" впереди.
            is_minus = true;
            word = word.substr(1);
        }
        return { is_minus, IsStopWord(word), word };
    }

    //Парсинг строки запроса
    Query ParseQuery(const string& text) const {
        Query query;
        //Итерируемся по каждому слову запроса
        for (const string& word : SplitIntoWords(text)) {
            const QueryWord query_word = ParseQueryWord(word);
            if (!query_word.is_stop) {
                if (query_word.is_minus) {
                    //Записать минус-слово в соответствующее множество
                    query.minus_words.insert(query_word.word);
                }
                else {
                    //Записать плюс-слово в соответствующее множество
                    query.plus_words.insert(query_word.word);
                }
            }
        }
        return query;
    }

    //Вычилсить IDF конкретного слова из запроса
    double CalculateIDF(const string& plus_word) const {
        return log(GetDocumentCount() * 1.0 / word_to_document_freqs_.at(plus_word).size());
    }

    //Найти все документы, подходящие под запрос
    template <typename Predicate>
    vector<Document> FindAllDocuments(const Query& query, Predicate predicate) const {
        //Конечный вектор с отобранными документами
        vector<Document> matched_documents;
        //<id, relevance> (relevance = sum(tf * idf))
        map<int, double> document_to_relevance;

        //формируем набор документов document_to_relevance с плюс-словами и их релевантностью
        for (const string& plus_word : query.plus_words) {
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
        for (const string& minus_word : query.minus_words) {
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
};

// ==================== для примера =========================

void PrintDocument(const Document& document) {
    cout << "{ "s
        << "document_id = "s << document.id << ", "s
        << "relevance = "s << document.relevance << ", "s
        << "rating = "s << document.rating << " }"s << endl;
}

int main() {
    SearchServer search_server;
    search_server.SetStopWords("и в на"s);

    search_server.AddDocument(0, "белый кот и модный ошейник"s, DocumentStatus::ACTUAL, { 8, -3 });
    search_server.AddDocument(1, "пушистый кот пушистый хвост"s, DocumentStatus::ACTUAL, { 7, 2, 7 });
    search_server.AddDocument(2, "ухоженный пёс выразительные глаза"s, DocumentStatus::ACTUAL,
        { 5, -12, 2, 1 });

    for (const Document& document : search_server.FindTopDocuments("ухоженный кот"s)) {
        PrintDocument(document);
    }

    return 0;
}