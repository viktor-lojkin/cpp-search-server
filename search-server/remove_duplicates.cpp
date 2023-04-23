#include "search_server.h"


using namespace std::string_literals;


void RemoveDuplicates(SearchServer& search_server) {
    //��� id �������
    std::vector<int> remove_ids;
    //������ ���������� ������ ����
    std::set<std::set<std::string>> uniques;

    //�������� �� ���� ���� ����������
    for (const int id : search_server) {
        
        //������ ���������� ����� ����, ������� tf
        std::set<std::string> unique;
         /*
        �� �����, ��� ��������� ��� std::transform � ������ range based for?
        
        std::transform(search_server.GetWordFrequencies(id).begin(), search_server.GetWordFrequencies(id).end(),
                       unique.begin(), [](const auto& [word, tf]){ return word; });
        
        ????
        */

        for (const auto& [word, tf] : search_server.GetWordFrequencies(id)) {
            unique.insert(word);
        }

        //���� ����� ����� ���� ��� ����, ������, ���-�� �� ����� �����
        if (uniques.count(unique)) {
            remove_ids.push_back(id);
        //������ ������ ���� ��� ���, ������, �� ������������� ��������
        } else {
            uniques.insert(unique);
        }
    }

    for (const int& remove_id : remove_ids) {
        std::cout << "Found duplicate document id "s << remove_id << std::endl;
        search_server.RemoveDocument(remove_id);
    }

}