#include <execution>

#include "process_queries.h"

std::vector<std::vector<Document>> ProcessQueries(
	const SearchServer& search_server, const std::vector<std::string>& queries) {

	std::vector<std::vector<Document>> processed_queries(queries.size());

	std::transform(
		std::execution::par,
		queries.begin(), queries.end(), // входные данные
		processed_queries.begin(), // складываем результаты
		[&search_server](std::string query) {
			return search_server.FindTopDocuments(query); // через лямбду применяем метод сёрч_сервера
		}
	);

	return processed_queries;
}

std::vector<Document> ProcessQueriesJoined(
	const SearchServer& search_server, const std::vector<std::string>& queries) {

	std::vector<Document> processed_joined_queries;

	for (std::vector<Document>& docs : ProcessQueries(search_server, queries)) {
		std::transform(
			docs.begin(), docs.end(),
			back_inserter(processed_joined_queries),
			[](Document& doc) {
				return doc;
			}
		);
	}

	return processed_joined_queries;
}