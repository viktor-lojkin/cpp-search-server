#include <iostream>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <cstdlib>
#include <iomanip>
#include <numeric>
#include <stdexcept>

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
    Document() = default;

    Document(int id, double relevance, int rating)
        : id(id)
        , relevance(relevance)
        , rating(rating) {
    }

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

template <typename StringContainer>
set<string> MakeUniqueNonEmptyStrings(const StringContainer& strings) {
    set<string> non_empty_strings;
    for (const string& str : strings) {
        if (!str.empty()) {
            non_empty_strings.insert(str);
        }
    }
    return non_empty_strings;
}

class SearchServer {
public:
    //Формируем множество стоп-слов из строки — конструкторы:
    template <typename StringContainer>
    explicit SearchServer(const StringContainer& stop_words)
        : stop_words_(MakeUniqueNonEmptyStrings(stop_words)) {
        
        if (any_of(stop_words.begin(), stop_words.end(),
            [](const auto& stop_word) { return !IsValidWord(stop_word); } )) {
            throw invalid_argument("One of stop-words contains special characters!"s);
        }
    }
    explicit SearchServer(const string& stop_words_text)
        : SearchServer(SplitIntoWords(stop_words_text)) {
    }

    //Добавляем документ и выбрасываем исключение, если документ невалидный(см.ниже в private),
    //его id отрицательный или повторяется в базе
    void AddDocument(int id_document, const string& document, DocumentStatus status,
        const vector<int>& ratings) {
        //Теперь при добавлении документа явно проверяем только коректность его ID
        if (id_document < 0 || documents_.count(id_document) > 0) {
            throw invalid_argument("Something wrong with ID!"s);
        }
        //Теперь проверка валидности каждого слова добавлена в SplitIntoWordsNoStop  
        const vector<string> words = SplitIntoWordsNoStop(document);
        //Добавляем документ и вычисляем TF конкретного слова в нём
        const double tf = 1.0 / static_cast<double>(words.size());
        for (const string& word : words) {
            word_to_document_freqs_[word][id_document] += tf;
        }
        documents_.emplace(id_document, DocumentData{ ComputeAverageRating(ratings), status });
        index_id_.push_back(id_document);
    }

    //Выбираем ТОП-документы (с дополнительными критериями сортировки)
    template <typename Predicate>
    vector<Document> FindTopDocuments(const string& raw_query, Predicate predicate) const {
        //Теперь валидность поискового запроса проверяется внутри ParseQuery (ParseQueryWord)
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

    //Матчинг документов - возвращаем все слова из поискового запроса, присутствующие в документе
    tuple<vector<string>, DocumentStatus> MatchDocument(const string& raw_query,
        int document_id) const {
        //Теперь валидность поискового запроса проверяется внутри ParseQuery (ParseQueryWord)
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

    //Узнаём сколько у нас документов
    int GetDocumentCount() const {
        return documents_.size();
    }

    //Узнаём id документа по порядку его добавления
    //(будем считать, что пользователь знает, что нумерация начинается с 0)
    int GetDocumentId(int index) const {
        return index_id_.at(index);
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

    //Храним id документов в порядке их добавления в базу
    vector<int> index_id_;

    //Проверка - "это стоп-слово?"
    bool IsStopWord(const string& word) const {
        return stop_words_.count(word) > 0;
    }

    //Проверка на спец-символы
    static bool IsValidWord(const string& word) {
        //Валидным считаем то слово, которое не содержит спец-символы
        return none_of(word.begin(), word.end(), [](char c) {
            return c >= '\0' && c < ' ';
            });
    }

    //Выбрасываем исключение, если находим одинокий минус (' - ')
    void LonelyMinusTerminator(const string& word) const {
        if (word.size() == 1 && word[0] == '-') {
            throw invalid_argument("This word contains only \'-\' and nothing else"s);
        }
    }

    //Формируем вектор из строки с пробелами и вычёркиваем стоп-слова
    vector<string> SplitIntoWordsNoStop(const string& text) const {
        vector<string> words;
        for (const string& word : SplitIntoWords(text)) {
            if (!IsValidWord(word)) {
                throw invalid_argument("Invalid word(s) in the adding doccument!"s);
            }
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
        //Это валидное слово?
        if (!IsValidWord(word)) {
            throw invalid_argument("Your word has a special character!"s);
        }
        //Проверка на отсутствие одинокого минуса
        LonelyMinusTerminator(word);
        
        bool is_minus = false;
        //Это минус-слово?
        if (word[0] == '-') {
            //Это слово с префиксом из двух минусов?
            if (word[1] == '-') {
                //Нам такое не подходит!
                throw invalid_argument("Trying to set minus-minus word!"s);
            }
            //Это просто минус-слово! Теперь уберём "-" впереди.
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
        return log(static_cast<double>(GetDocumentCount()) / static_cast<double>(word_to_document_freqs_.at(plus_word).size()));
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

/*
//Реализация макросов для тестов
template <typename T, typename U>
void AssertEqualImpl(const T& t, const U& u, const string& t_str, const string& u_str, const string& file,
    const string& func, unsigned line, const string& hint) {
    if (t != u) {
        cerr << boolalpha;
        cerr << file << "("s << line << "): "s << func << ": "s;
        cerr << "ASSERT_EQUAL("s << t_str << ", "s << u_str << ") failed: "s;
        cerr << t << " != "s << u << "."s;
        if (!hint.empty()) {
            cerr << " Hint: "s << hint;
        }
        cerr << endl;
        abort();
    }
}

#define ASSERT_EQUAL(a, b) AssertEqualImpl((a), (b), #a, #b, __FILE__, __FUNCTION__, __LINE__, ""s)

#define ASSERT_EQUAL_HINT(a, b, hint) AssertEqualImpl((a), (b), #a, #b, __FILE__, __FUNCTION__, __LINE__, (hint))

void AssertImpl(bool value, const string& expr_str, const string& file,
    const string& func, unsigned line, const string& hint) {
    if (!value) {
        cerr << file << "("s << line << "): "s << func << ": "s;
        cerr << "ASSERT("s << expr_str << ") failed."s;
        if (!hint.empty()) {
            cerr << " Hint: "s << hint;
        }
        cerr << endl;
        abort();
    }
}

#define ASSERT(expr) AssertImpl((expr), #expr, __FILE__, __FUNCTION__, __LINE__, ""s)

#define ASSERT_HINT(expr, hint) AssertImpl((expr), #expr, __FILE__, __FUNCTION__, __LINE__, (hint))

template <typename Func>
void RunTestImpl(Func func, const string& function) {
    func();
    cerr << function << " OK"s << endl;
}

#define RUN_TEST(func) RunTestImpl((func) , #func)

//Распаковка контейнеров
template <typename Left, typename Right>
ostream& operator<<(ostream& out, const pair<Left, Right>& pair) {
    return out << pair.first << ": "s << pair.second;
}

template <typename Container>
void Print(ostream& out, const Container& container) {
    bool is_first = true;
    for (const auto& element : container) {
        if (!is_first) {
            out << ", "s;
        }
        is_first = false;
        out << element;
    }
}

template <typename Term>
ostream& operator<<(ostream& out, const vector<Term> container) {
    out << "["s;
    Print(out, container);
    out << "]"s;
    return out;
}

template <typename Term>
ostream& operator<<(ostream& out, const set<Term> container) {
    out << "{"s;
    Print(out, container);
    out << "}"s;
    return out;
}

template <typename Key, typename Value>
ostream& operator<<(ostream& out, const map<Key, Value> container) {
    out << "{"s;
    Print(out, container);
    out << "}"s;
    return out;
}

// -------- Начало модульных тестов поисковой системы ----------

// Тест проверяет, что поисковая система исключает стоп-слова при добавлении документов
void TestExcludeStopWordsFromAddedDocumentContent() {
    const int doc_id = 42;
    const string content = "cat in the city"s;
    const vector<int> ratings = { 1, 2, 3 };
    {
        SearchServer server;
        server.AddDocument(doc_id, content, DocumentStatus::ACTUAL, ratings);
        const auto found_docs = server.FindTopDocuments("in"s);
        ASSERT_EQUAL(found_docs.size(), 1u);
        const Document& doc0 = found_docs[0];
        ASSERT_EQUAL(doc0.id, doc_id);
    }

    {
        SearchServer server;
        server.SetStopWords("in the"s);
        server.AddDocument(doc_id, content, DocumentStatus::ACTUAL, ratings);
        ASSERT_HINT(server.FindTopDocuments("in"s).empty(),
            "Stop words must be excluded from documents"s);
    }
}

void TestExcludeMinusWords() {
    //Поддержка минус-слов. Документы, содержащие минус-слова поискового запроса, не должны включаться в результаты поиска.
    SearchServer server;

    const int doc_id_1 = 42;
    const string content_1 = "cat in the city"s;
    const vector<int> ratings_1 = { 1, 2, 3 };

    const int doc_id_2 = 43;
    const string content_2 = "cat in the town"s;
    const vector<int> ratings_2 = { 1, 2, 3 };

    server.AddDocument(doc_id_1, content_1, DocumentStatus::ACTUAL, ratings_1);
    server.AddDocument(doc_id_2, content_2, DocumentStatus::ACTUAL, ratings_2);

    const auto found_docs = server.FindTopDocuments("cat -city"s);
    ASSERT_EQUAL(found_docs.size(), 1u);
    ASSERT_EQUAL_HINT(found_docs[0].id, doc_id_2, "Your document contains minus-word!"s);
}

// Добавление документов. Добавленный документ должен находиться по поисковому запросу, который содержит слова из документа.
void TestAddAndFind() {
    SearchServer server;

    const int doc_id_1 = 42;
    const string content_1 = "cat in the city"s;
    const vector<int> ratings_1 = { 1, 2, 3 };

    const int doc_id_2 = 43;
    const string content_2 = "dog in the city"s;
    const vector<int> ratings_2 = { 1, 2, 3 };

    server.AddDocument(doc_id_1, content_1, DocumentStatus::ACTUAL, ratings_1);
    server.AddDocument(doc_id_2, content_2, DocumentStatus::ACTUAL, ratings_2);

    const auto found_docs = server.FindTopDocuments("cat"s);
    ASSERT_EQUAL(found_docs.size(), 1u);
    ASSERT_EQUAL_HINT(found_docs[0].id, doc_id_1, "You found a wrong document :("s);
}

// Матчинг документов. При матчинге документа по поисковому запросу должны быть возвращены все слова из поискового запроса, присутствующие в документе. Если есть соответствие хотя бы по одному минус-слову, должен возвращаться пустой список слов.
void TestMatchDocuments() {
    SearchServer server;

    const int doc_id_1 = 42;
    const string content_1 = "cat in the city"s;
    const vector<int> ratings_1 = { 1, 2, 3 };

    const int doc_id_2 = 43;
    const string content_2 = "big cat in the town"s;
    const vector<int> ratings_2 = { 1, 2, 3 };

    server.SetStopWords("in the"s);
    server.AddDocument(doc_id_1, content_1, DocumentStatus::ACTUAL, ratings_1);
    server.AddDocument(doc_id_2, content_2, DocumentStatus::ACTUAL, ratings_2);

    const auto [words_1, status_1] = server.MatchDocument("cat -city"s, doc_id_1);
    ASSERT(words_1.empty());

    const auto [words_2, status_2] = server.MatchDocument("cat town"s, doc_id_2);
    ASSERT_EQUAL(words_2.size(), 2u);
    ASSERT_EQUAL_HINT(words_2[0], "cat"s, "There should be another word here..."s);
    ASSERT_EQUAL_HINT(words_2[1], "town"s, "There should be another word here..."s);
}

//Сортировка найденных документов по релевантности. Возвращаемые при поиске документов результаты должны быть отсортированы в порядке убывания релевантности.
void TestRelevanceSort() {
    SearchServer server;

    const int doc_id_1 = 42;
    const string content_1 = "cat in the city"s;
    const vector<int> ratings_1 = { 1, 2, 3 };

    const int doc_id_2 = 43;
    const string content_2 = "cat in the town"s;
    const vector<int> ratings_2 = { 1, 2, 3 };

    const int doc_id_3 = 44;
    const string content_3 = "big cat in the town"s;
    const vector<int> ratings_3 = { 1, 2, 3 };

    server.SetStopWords("in the"s);
    server.AddDocument(doc_id_1, content_1, DocumentStatus::ACTUAL, ratings_1);
    server.AddDocument(doc_id_2, content_2, DocumentStatus::ACTUAL, ratings_2);
    server.AddDocument(doc_id_3, content_3, DocumentStatus::ACTUAL, ratings_3);

    const auto found_docs = server.FindTopDocuments("big cat town"s);
    ASSERT_EQUAL_HINT(found_docs[0].id, doc_id_3, "I feel out of place"s);
    ASSERT_EQUAL_HINT(found_docs[1].id, doc_id_2, "I feel out of place"s);
    ASSERT_EQUAL_HINT(found_docs[2].id, doc_id_1, "I feel out of place"s);
}

// Вычисление рейтинга документов. Рейтинг добавленного документа равен среднему арифметическому оценок документа.
void TestRating() {
    SearchServer server;

    const int doc_id_1 = 42;
    const string content_1 = "cat in the city"s;
    const vector<int> ratings_1 = { 4, 4, 4 };

    const int doc_id_2 = 43;
    const string content_2 = "cat in the town"s;
    const vector<int> ratings_2 = { 6, 2, -2 };

    const int doc_id_3 = 44;
    const string content_3 = "cat in the village"s;
    const vector<int> ratings_3 = { 0, 0, 0 };

    const int doc_id_4 = 45;
    const string content_4 = "cat in the space"s;
    const vector<int> ratings_4 = { -2, -2, -2 };

    server.AddDocument(doc_id_1, content_1, DocumentStatus::ACTUAL, ratings_1);
    server.AddDocument(doc_id_2, content_2, DocumentStatus::ACTUAL, ratings_2);
    server.AddDocument(doc_id_3, content_3, DocumentStatus::ACTUAL, ratings_3);
    server.AddDocument(doc_id_4, content_4, DocumentStatus::ACTUAL, ratings_4);

    const auto found_docs = server.FindTopDocuments("cat and dog");
    ASSERT_EQUAL(found_docs[0].rating, 4);
    ASSERT_EQUAL(found_docs[1].rating, 2);
    ASSERT_EQUAL(found_docs[2].rating, 0);
    ASSERT_EQUAL(found_docs[3].rating, -2);
}

//Фильтрация результатов поиска с использованием предиката, задаваемого пользователем.
void TestPredicate() {
    SearchServer server;

    const int doc_id_1 = 42;
    const string content_1 = "cat in the city"s;
    const vector<int> ratings_1 = { 4, 4, 4 };

    const int doc_id_2 = 43;
    const string content_2 = "cat in the town"s;
    const vector<int> ratings_2 = { 5, 5, 5 };

    const int doc_id_3 = 44;
    const string content_3 = "cat in the village"s;
    const vector<int> ratings_3 = { 0, 0, 0 };

    const int doc_id_4 = 45;
    const string content_4 = "cat in the space"s;
    const vector<int> ratings_4 = { -2, -2, -2 };

    server.AddDocument(doc_id_1, content_1, DocumentStatus::ACTUAL, ratings_1);
    server.AddDocument(doc_id_2, content_2, DocumentStatus::ACTUAL, ratings_2);
    server.AddDocument(doc_id_3, content_3, DocumentStatus::ACTUAL, ratings_3);
    server.AddDocument(doc_id_4, content_4, DocumentStatus::ACTUAL, ratings_4);

    const auto found_docs = server.FindTopDocuments("cat"s,
        [](int document_id, DocumentStatus status, int rating)
        {return rating == 5; });
    ASSERT_EQUAL(found_docs.size(), 1u);
}

//Поиск документов, имеющих заданный статус.
void TestStatus() {
    SearchServer server;

    const int doc_id_1 = 42;
    const string content_1 = "cat in the city"s;
    const vector<int> ratings_1 = { 1, 2, 3 };

    const int doc_id_2 = 43;
    const string content_2 = "cat in the town"s;
    const vector<int> ratings_2 = { 1, 2, 3 };

    const int doc_id_3 = 44;
    const string content_3 = "cat in the village"s;
    const vector<int> ratings_3 = { 1, 2, 3 };

    const int doc_id_4 = 45;
    const string content_4 = "cat in the space"s;
    const vector<int> ratings_4 = { 1, 2, 3 };

    const int doc_id_5 = 46;
    const string content_5 = "cat at home"s;
    const vector<int> ratings_5 = { 1, 2, 3 };

    const int doc_id_6 = 47;
    const string content_6 = "you are cat"s;
    const vector<int> ratings_6 = { 1, 2, 3 };

    server.AddDocument(doc_id_1, content_1, DocumentStatus::ACTUAL, ratings_1);
    server.AddDocument(doc_id_2, content_2, DocumentStatus::BANNED, ratings_2);
    server.AddDocument(doc_id_3, content_3, DocumentStatus::BANNED, ratings_3);
    server.AddDocument(doc_id_4, content_4, DocumentStatus::REMOVED, ratings_4);
    server.AddDocument(doc_id_5, content_5, DocumentStatus::REMOVED, ratings_5);
    server.AddDocument(doc_id_6, content_6, DocumentStatus::REMOVED, ratings_6);

    {
        const auto found_docs = server.FindTopDocuments("cat", DocumentStatus::ACTUAL);
        ASSERT_EQUAL(found_docs.size(), 1u);
    }

    {
        const auto found_docs = server.FindTopDocuments("cat", DocumentStatus::BANNED);
        ASSERT_EQUAL(found_docs.size(), 2u);
    }

    {
        const auto found_docs = server.FindTopDocuments("cat", DocumentStatus::IRRELEVANT);
        ASSERT_EQUAL_HINT(found_docs.size(), 0u, "I'm not here at all"s);
    }

    {
        const auto found_docs = server.FindTopDocuments("cat", DocumentStatus::REMOVED);
        ASSERT_EQUAL(found_docs.size(), 3u);
    }
}

//Корректное вычисление релевантности найденных документов.
void TestCalculateRelevance() {
    SearchServer server;

    const int doc_id_1 = 42;
    const string content_1 = "cat in the city"s;
    const vector<int> ratings_1 = { 1, 2, 3 };

    const int doc_id_2 = 43;
    const string content_2 = "cat in the town"s;
    const vector<int> ratings_2 = { 1, 2, 3 };

    const int doc_id_3 = 44;
    const string content_3 = "big cat in the town"s;
    const vector<int> ratings_3 = { 1, 2, 3 };

    server.SetStopWords("in the"s);
    server.AddDocument(doc_id_1, content_1, DocumentStatus::ACTUAL, ratings_1);
    server.AddDocument(doc_id_2, content_2, DocumentStatus::ACTUAL, ratings_2);
    server.AddDocument(doc_id_3, content_3, DocumentStatus::ACTUAL, ratings_3);

    const auto found_docs = server.FindTopDocuments("big cat town"s);
    ASSERT(abs(found_docs[0].relevance - 0.501359) < EPSILON);
    ASSERT(abs(found_docs[1].relevance - 0.202733) < EPSILON);
    ASSERT(abs(found_docs[2].relevance - 0.000000) < EPSILON);
}

// Запуск тестов
void TestSearchServer() {
    RUN_TEST(TestExcludeStopWordsFromAddedDocumentContent);
    RUN_TEST(TestExcludeMinusWords);
    RUN_TEST(TestAddAndFind);
    RUN_TEST(TestMatchDocuments);
    RUN_TEST(TestRelevanceSort);
    RUN_TEST(TestRating);
    RUN_TEST(TestPredicate);
    RUN_TEST(TestStatus);
    RUN_TEST(TestCalculateRelevance);
}

// --------- Окончание модульных тестов поисковой системы -----------

int main() {
    TestSearchServer();
    // Если вы видите эту строку, значит все тесты прошли успешно
    cout << "Search server testing finished"s << endl;
}
*/

void PrintDocument(const Document& document) {
    cout << "{ "s
        << "document_id = "s << document.id << ", "s
        << "relevance = "s << document.relevance << ", "s
        << "rating = "s << document.rating << " }"s << endl;
}

int main() {

    try {
        //SearchServer search_server("и в на"s);
        SearchServer search_server("и в н\x18а"s);

    }
    catch (const invalid_argument& i_a) {
        cout << "Error: "s << i_a.what() << endl;
    }

    try {
        SearchServer search_server("и в на"s);
        search_server.AddDocument(1, "пушистый кот пушистый хвост"s, DocumentStatus::ACTUAL, { 7, 2, 7 });
        search_server.AddDocument(1, "пушистый пёс и модный ошейник"s, DocumentStatus::ACTUAL, { 1, 2 });

    }
    catch (const invalid_argument& i_a) {
        cout << "Error: "s << i_a.what() << endl;
        cout << "Документ не был добавлен, так как его id совпадает с уже имеющимся"s << endl;
    }

    try {
        SearchServer search_server("и в на"s);
        search_server.AddDocument(-1, "пушистый пёс и модный ошейник"s, DocumentStatus::ACTUAL, { 1, 2 });

    }
    catch (const invalid_argument& i_a) {
        cout << "Error: "s << i_a.what() << endl;
        cout << "Документ не был добавлен, так как его id отрицательный"s << endl;
    }

    try {
        SearchServer search_server("и в на"s);
        search_server.AddDocument(3, "большой пёс скво\x12рец"s, DocumentStatus::ACTUAL, { 1, 3, 2 });

    }
    catch (const invalid_argument& i_a) {
        cout << "Error: "s << i_a.what() << endl;
        cout << "Документ не был добавлен, так как содержит спецсимволы"s << endl;
    }

    try {
        SearchServer search_server("и в на"s);
        const auto documents = search_server.FindTopDocuments("--пушистый"s);
        for (const Document& document : documents) {
            PrintDocument(document);
        }

    }
    catch (const invalid_argument& i_a) {
        cout << "Error: "s << i_a.what() << endl;
        cout << "Ошибка в поисковом запросе"s << endl;
    }
    return 0;
}
