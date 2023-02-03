#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <cmath>

using namespace std;

/* Привет! Код удачно прошёл тесты в тренажёре, однако меня не покидает чувство того,
что осталось немало мест, где его можно улучшить. Возможно, вынести какие-то части
в отдельные методы класса SearchServer, чтобы упростить другие для понимания.
Но пока что такие вещи мне неочевидны — буду очень признателен за подобные замечания! */

//Задаём количество топ-документов
const int MAX_RESULT_DOCUMENT_COUNT = 5;

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
        } else {
            word += c;
        }
    }
    if (!word.empty()) {
        words.push_back(word);
    }

    return words;
}

//Структура для хранения документа
struct Document {
    int id;
    double relevance;
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
    void AddDocument(int document_id, const string& document) {
        ++document_count_;
        const vector<string> words = SplitIntoWordsNoStop(document);
        //Добавляем документ и вычисляем TF конкретного слова в нём 
        const double tf = 1.0 / words.size();
        for (const string& word : words) {
            word_to_document_freqs_[word][document_id] += tf;
        }
    }

    //Выбираем топ-документы
    vector<Document> FindTopDocuments(const string& raw_query) const {
        const Query query = ParseQuery(raw_query);
        auto matched_documents = FindAllDocuments(query);
        //Сортировка по невозрастанию реливантности
        sort(matched_documents.begin(), matched_documents.end(),
             [](const Document& lhs, const Document& rhs) {
                 return lhs.relevance > rhs.relevance;
             });
        if (matched_documents.size() > MAX_RESULT_DOCUMENT_COUNT) {
            matched_documents.resize(MAX_RESULT_DOCUMENT_COUNT);
        }
        return matched_documents;
    }

private:
    //Счётчик для подсчёта количества добавленных документов
    int document_count_ = 0;

    //Структура для хранения парсированного запроса        
    struct Query {
        set<string> plus_words;
        set<string> minus_words;
    };

    //Сущность для хранения документов - словарь <слово(ключ) <id(ключ) , TF>>
    map<string, map<int, double>> word_to_document_freqs_;
    
    //Множество стоп-слов
    set<string> stop_words_;

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

    //Парсинг строки запроса
    Query ParseQuery(const string& text) const {
        Query query;
        //Итерируемся по каждому слову запроса
        for (const string& word : SplitIntoWordsNoStop(text)) {
            //Это минус-слово?
            if (word[0] == '-') {
                //Это минус-слово совпало со стоп-словом?
                if (!IsStopWord(word.substr(1))) {
                    //Записать минус-слово в соответствующее множество
                    query.minus_words.insert(word.substr(1));
                }
            } else {
                //Записать плюс-слово в соответствующее множество
                query.plus_words.insert(word);
            }
        }
        return query;
    }
    
    //Найти все документы, подходящие под запрос
    vector<Document> FindAllDocuments(const Query& query) const {
        //Конечный вектор с отобранными документами
        vector<Document> matched_documents;
        //<id, relevance> (relevance = sum(tf * idf))
        map<int, double> document_to_relevance;
        double idf = 0.0;
        //формируем набор документов document_to_relevance с плюс-словами и их релевантностью
        for (string plus_word : query.plus_words) {
            if (word_to_document_freqs_.count(plus_word)) {
                //Вычисляем IDF конкретного слова из запроса
                idf = log((document_count_ * 1.0) / word_to_document_freqs_.at(plus_word).size());
                for (const auto& [id_document, tf] : word_to_document_freqs_.at(plus_word)) {
                    //Вычисляем релевантность документа с учётом вхождения каждого плюс-слова
                    document_to_relevance[id_document] += (tf * idf);
                }
            } else {
                continue;
            }
        }
        //Вычёркиваем из document_to_relevance документы с минус-словами
        for (const string& minus_word : query.minus_words) {
            if (word_to_document_freqs_.count(minus_word)) {
                for (const auto& [id_document, relevance] : word_to_document_freqs_.at(minus_word)){
                    document_to_relevance.erase(id_document);
                }
            } else {
                continue;
            }
        }
        //Формируем результурующий вектор структуры Document
        for (const auto& [id_document, relevance] : document_to_relevance) {
            matched_documents.push_back({id_document, relevance});
        }
        return matched_documents;
    }
};

SearchServer CreateSearchServer() {
    SearchServer search_server;
    search_server.SetStopWords(ReadLine());

    const int document_count = ReadLineWithNumber();
    for (int document_id = 0; document_id < document_count; ++document_id) {
        search_server.AddDocument(document_id, ReadLine());
    }

    return search_server;
}

int main() {
    const SearchServer search_server = CreateSearchServer();

    const string query = ReadLine();
    for (const auto& [document_id, relevance] : search_server.FindTopDocuments(query)) {
        cout << "{ document_id = "s << document_id << ", "
             << "relevance = "s << relevance << " }"s << endl;
    }
}