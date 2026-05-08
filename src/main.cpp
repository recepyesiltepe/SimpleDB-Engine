#include "database.h"
#include "sql.h"
#include "types.h"

#include <iostream>
#include <optional>
#include <string>

namespace {

void printSelectResult(const simpledb::SelectResult& result) {
    for (std::size_t i = 0; i < result.columnNames.size(); ++i) {
        std::cout << result.columnNames[i];
        if (i + 1 < result.columnNames.size()) {
            std::cout << "\t";
        }
    }
    std::cout << "\n";

    for (const auto& row : result.rows) {
        for (std::size_t i = 0; i < row.size(); ++i) {
            std::cout << simpledb::valueToString(row[i]);
            if (i + 1 < row.size()) {
                std::cout << "\t";
            }
        }
        std::cout << "\n";
    }
}

}  // namespace

int main() {
    simpledb::Database db("data");
    std::string initError;
    if (!db.initialize(initError)) {
        std::cerr << "Initialization error: " << initError << "\n";
        return 1;
    }

    std::cout << "SimpleDB ready. Type SQL or EXIT.\n";
    std::string input;
    while (true) {
        std::cout << "db> ";
        if (!std::getline(std::cin, input)) {
            break;
        }
        if (simpledb::toUpper(input) == "EXIT" || simpledb::toUpper(input) == "QUIT") {
            break;
        }
        if (input.empty()) {
            continue;
        }

        simpledb::Statement statement;
        std::string parseError;
        if (!simpledb::tryParseStatement(input, statement, parseError)) {
            std::cout << "Parse error: " << parseError << "\n";
            continue;
        }

        std::string message;
        std::optional<simpledb::SelectResult> selectResult;
        if (!db.execute(statement, message, selectResult)) {
            std::cout << "Execution error: " << message << "\n";
            continue;
        }

        if (selectResult.has_value()) {
            printSelectResult(selectResult.value());
        }
        std::cout << message << "\n";
    }

    return 0;
}
