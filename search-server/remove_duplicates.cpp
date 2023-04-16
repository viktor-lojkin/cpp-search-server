#include "search_server.h"


using namespace std::string_literals;


void RemoveDuplicates(SearchServer& search_server) {
    //Эти id удаляем
    std::vector<int> remove_ids;
    //Храним уникальные наборы слов с привязкой к id
    std::map<std::set<std::string>, int> uniques;

    //Проходим по всей базе документов
    for (const int id : search_server) {
        
        //Создаём уникальный набор слов, отсекая tf
        std::set<std::string> unique;
        for (const auto& [word, tf] : search_server.GetWordFrequencies(id)) {
            unique.insert(word);
        }

        //Если такой набор слов уже есть, значит, что-то из этого дубль
        if (uniques.count(unique)) {
            //Проверяем id: дублем считаем документ с большим id
            if (uniques.at(unique) < id) {
                remove_ids.push_back(id);
            } else {
                remove_ids.push_back(uniques.at(unique));
                //Обновляем id на меньший
                uniques.at(unique) = id;
            }
        //Такого набора слов ещё нет, значит, он действительно уникален
        } else {
            uniques.insert({ unique, id });
        }
    }

    for (const int& remove_id : remove_ids) {
        std::cout << "Found duplicate document id "s << remove_id << std::endl;
        search_server.RemoveDocument(remove_id);
    }

}