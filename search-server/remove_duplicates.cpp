#include "search_server.h"


using namespace std::string_literals;


void RemoveDuplicates(SearchServer& search_server) {
    //��� id �������
    std::vector<int> remove_ids;
    //������ ���������� ������ ���� � ��������� � id
    std::map<std::set<std::string>, int> uniques;

    //�������� �� ���� ���� ����������
    for (const int id : search_server) {
        
        //������ ���������� ����� ����, ������� tf
        std::set<std::string> unique;
        for (const auto& [word, tf] : search_server.GetWordFrequencies(id)) {
            unique.insert(word);
        }

        //���� ����� ����� ���� ��� ����, ������, ���-�� �� ����� �����
        if (uniques.count(unique)) {
            //��������� id: ������ ������� �������� � ������� id
            if (uniques.at(unique) < id) {
                remove_ids.push_back(id);
            } else {
                remove_ids.push_back(uniques.at(unique));
                //��������� id �� �������
                uniques.at(unique) = id;
            }
        //������ ������ ���� ��� ���, ������, �� ������������� ��������
        } else {
            uniques.insert({ unique, id });
        }
    }

    for (const int& remove_id : remove_ids) {
        std::cout << "Found duplicate document id "s << remove_id << std::endl;
        search_server.RemoveDocument(remove_id);
    }

}