#include <cmath>
#include <numeric>
#include <iterator>

#include "search_server.h"


SearchServer::SearchServer(const std::string& stop_words_text)
    : SearchServer(SplitIntoWords(stop_words_text)) { }
SearchServer::SearchServer(std::string_view stop_words_view)
    : SearchServer(SplitIntoWords(stop_words_view)) { }

// Добавляем документ и выбрасываем исключение, если документ невалидный(см.ниже в private),
// его id отрицательный или повторяется в базе
void SearchServer::AddDocument(int id_document, std::string_view document, DocumentStatus status,
    const std::vector<int>& ratings) {
    //Теперь при добавлении документа явно проверяем только коректность его ID
    if (id_document < 0 || documents_.count(id_document) > 0) {
        throw std::invalid_argument("Something wrong with ID!"s);
    }
    //Теперь проверка валидности каждого слова добавлена в SplitIntoWordsNoStop  
    const std::vector<std::string_view> words = SplitIntoWordsNoStop(document);
    //Добавляем документ и вычисляем TF конкретного слова в нём
    const double tf = 1.0 / static_cast<double>(words.size());

    for (std::string_view word : words) {
        //std::string word{word_view.data(), word_view.size()};
        auto insert_word = all_words_.insert(std::string(word));
        word_to_document_freqs_[*insert_word.first][id_document] += tf;
        document_to_word_freqs_[id_document][*insert_word.first] += tf;
    }
    documents_.emplace(id_document, DocumentData{ ComputeAverageRating(ratings), status });
    ids_.insert(id_document);
}

// Выбираем ТОП-документы (по статусу)
std::vector<Document> SearchServer::FindTopDocuments(std::string_view raw_query, DocumentStatus status) const {
    return FindTopDocuments(raw_query, [status](int id_document, DocumentStatus document_status, int rating)
        { return document_status == status; });
}

// Выбираем ТОП-документы (если хочется вызвать с одним аргументом)
std::vector<Document> SearchServer::FindTopDocuments(std::string_view raw_query) const {
    // Тогда будем показывать только АКТУАЛЬНЫЕ
    return FindTopDocuments(raw_query, DocumentStatus::ACTUAL);
}

// Узнаём сколько у нас документов
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

const std::map<std::string_view, double>& SearchServer::GetWordFrequencies(int document_id) const {
    static const std::map<std::string_view, double> empty_map{};
    if (document_to_word_freqs_.empty()) {
        return empty_map;
    }
    else {
        return document_to_word_freqs_.at(document_id);
    }
}

// Проверка - "это стоп-слово?"
bool SearchServer::IsStopWord(std::string_view word) const {
    return stop_words_.count(word) > 0;
}

// Проверка на спец-символы
bool SearchServer::IsValidWord(std::string_view word) {
    // Валидным считаем то слово, которое не содержит спец-символы
    return std::none_of(word.begin(), word.end(),
        [](char c) { return c >= '\0' && c < ' '; });
}

// Выбрасываем исключение, если находим одинокий минус (' - ')
void SearchServer::LonelyMinusTerminator(std::string_view word) const {
    if (word.size() == 1 && word[0] == '-') {
        throw std::invalid_argument("This word contains only \'-\' and nothing else"s);
    }
}

//Формируем вектор из строки с пробелами и вычёркиваем стоп-слова
std::vector<std::string_view> SearchServer::SplitIntoWordsNoStop(std::string_view text) const {
    std::vector<std::string_view> words = SplitIntoWords(text);
    std::vector<std::string_view> words_no_stop;
    words_no_stop.reserve(words.size());

    std::for_each(
        words.begin(), words.end(),
        [this, &words_no_stop](std::string_view word) {
            if (stop_words_.count(word) == 0) {
                !IsValidWord(word) ? throw std::invalid_argument("Invalid word(s) in the adding doccument!"s)
                    : words_no_stop.push_back(word);
            }
        }
    );

    return words_no_stop;
}

// Вычисляем средний рейтинг документа
int SearchServer::ComputeAverageRating(const std::vector<int>& ratings) {
    if (ratings.empty()) {
        return 0;
    }
    return accumulate(ratings.begin(), ratings.end(), 0) /
        static_cast<int>(ratings.size());
}

// Парсинг слова строки запроса
SearchServer::QueryWord SearchServer::ParseQueryWord(std::string_view word) const {
    //Это валидное слово?
    if (!IsValidWord(word)) {
        throw std::invalid_argument("Your word has a special character!"s);
    }
    //Проверка на отсутствие одинокого минуса
    LonelyMinusTerminator(word);

    bool is_minus = false;
    //Это минус-слово?
    if (word[0] == '-') {
        //Это слово с префиксом из двух минусов?
        if (word[1] == '-') {
            //Нам такое не подходит!
            throw std::invalid_argument("Trying to set minus-minus word!"s);
        }
        //Это просто минус-слово! Теперь уберём "-" впереди.
        is_minus = true;
        word = word.substr(1);
    }
    return { is_minus, IsStopWord(word), word };
}

// Парсинг строки запроса
SearchServer::Query SearchServer::ParseQuerySeq(std::string_view text) const {

    std::vector<std::string_view> words = SplitIntoWords(text);
    Query query;

    std::for_each(
        words.begin(), words.end(),
        [this, &query](auto& word) {
            const QueryWord query_word = ParseQueryWord(word);
            if (!query_word.is_stop) {
                query_word.is_minus ? query.minus_words.push_back(query_word.word)
                    : query.plus_words.push_back(query_word.word);
            }
        }
    );

    std::sort(query.minus_words.begin(), query.minus_words.end());
    std::sort(query.plus_words.begin(), query.plus_words.end());
    auto delete_m = std::unique(query.minus_words.begin(), query.minus_words.end());
    auto delete_p = std::unique(query.plus_words.begin(), query.plus_words.end());
    query.minus_words.erase(delete_m, query.minus_words.end());
    query.plus_words.erase(delete_p, query.plus_words.end());
    return query;
}

SearchServer::Query SearchServer::ParseQuery(std::string_view text) const {

    std::vector<std::string_view> words = SplitIntoWords(text);
    Query query;

    std::for_each(
        words.begin(), words.end(),
        [this, &query](auto& word) {
            const QueryWord query_word = ParseQueryWord(word);
            if (!query_word.is_stop) {
                query_word.is_minus ? query.minus_words.push_back(query_word.word)
                    : query.plus_words.push_back(query_word.word);
            }
        }
    );

    return query;
}

// Вычилсить IDF конкретного слова из запроса
double SearchServer::CalculateIDF(std::string_view plus_word) const {
    return std::log(static_cast<double>(GetDocumentCount()) / static_cast<double>(word_to_document_freqs_.find(plus_word)->second.size()));
}

void SearchServer::RemoveDocument(int document_id) {
    RemoveDocument(std::execution::seq, document_id);
}

// Матчинг документов - возвращаем все слова из поискового запроса, присутствующие в документе
std::tuple<std::vector<std::string_view>, DocumentStatus> SearchServer::MatchDocument(std::string_view raw_query,
    int document_id) const {
    // Валидность поискового запроса проверяется внутри ParseQuery (ParseQueryWord)
    const Query query = ParseQuerySeq(raw_query);

    for (std::string_view word : query.minus_words) {
        if (word_to_document_freqs_.count(word) == 0) {
            continue;
        }
        if (word_to_document_freqs_.find(word)->second.count(document_id)) {
            return { std::vector<std::string_view> {}, documents_.at(document_id).document_status };
        }
    }

    std::vector<std::string_view> matched_words;
    //matched_words.reserve(query.plus_words.size());

    for (std::string_view word : query.plus_words) {
        if (word_to_document_freqs_.count(word) == 0) {
            continue;
        }
        if (word_to_document_freqs_.find(word)->second.count(document_id)) {
            matched_words.push_back(word);
        }
    }

    return { matched_words, documents_.at(document_id).document_status };
}

std::tuple<std::vector<std::string_view>, DocumentStatus> SearchServer::MatchDocument(std::execution::sequenced_policy, std::string_view raw_query, int document_id) const {
    return MatchDocument(raw_query, document_id);
}

std::tuple<std::vector<std::string_view>, DocumentStatus> SearchServer::MatchDocument(std::execution::parallel_policy, std::string_view raw_query, int document_id) const {

    if (!ids_.count(document_id)) {
        throw std::out_of_range("There is no document with this id"s);
    }

    Query query = ParseQuery(raw_query);

    if (
        std::any_of(
            query.minus_words.begin(), query.minus_words.end(),
            [&](auto& minus_word) {
                return document_to_word_freqs_.at(document_id).count(minus_word);
            }
        )) {
        return std::tuple<std::vector<std::string_view>, DocumentStatus>{};
    }

    std::vector<std::string_view> matched_words;
    matched_words.reserve(query.plus_words.size());
    auto last = std::copy_if(
        std::execution::par,
        query.plus_words.begin(), query.plus_words.end(),
        matched_words.begin(),
        [&](auto& plus_word) {
            if (word_to_document_freqs_.count(plus_word)) {
                return word_to_document_freqs_.find(plus_word)->second.count(document_id) > 0;
            }
            else {
                return false;
            }
        }
    );

    matched_words.erase(last, matched_words.end());
    std::sort(matched_words.begin(), matched_words.end());
    auto to_delete = std::unique(matched_words.begin(), matched_words.end());
    matched_words.erase(to_delete, matched_words.end());

    return { matched_words, documents_.at(document_id).document_status };
}