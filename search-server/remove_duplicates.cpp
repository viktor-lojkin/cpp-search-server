#include "search_server.h"


using namespace std::string_literals;


void RemoveDuplicates(SearchServer& search_server) {
    //Эти id удаляем
    std::vector<int> remove_ids;
    //Храним уникальные наборы слов
    std::set<std::set<std::string>> uniques;

    //Проходим по всей базе документов
    for (const int id : search_server) {
        
        //Создаём уникальный набор слов, отсекая tf
        std::set<std::string> unique;
         /*
        Не понял, как применить тут std::transform — вместо range based for?
        
        std::transform(search_server.GetWordFrequencies(id).begin(), search_server.GetWordFrequencies(id).end(),
                       unique.begin(), [](const auto& [word, tf]){ return word; });
        
        ????
        */

        for (const auto& [word, tf] : search_server.GetWordFrequencies(id)) {
            unique.insert(word);
        }

        //Если такой набор слов уже есть, значит, что-то из этого дубль
        if (uniques.count(unique)) {
            remove_ids.push_back(id);
        //Такого набора слов ещё нет, значит, он действительно уникален
        } else {
            uniques.insert(unique);
        }
    }

    for (const int& remove_id : remove_ids) {
        std::cout << "Found duplicate document id "s << remove_id << std::endl;
        search_server.RemoveDocument(remove_id);
    }

}