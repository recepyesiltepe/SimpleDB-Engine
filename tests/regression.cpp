#include "database.h"
#include "sql.h"
#include "types.h"

#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

struct QueryResult {
    std::string message;
    std::optional<simpledb::SelectResult> rows;
};

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

fs::path testRoot() {
    fs::path root = fs::temp_directory_path() / "simpledb_regression_tests";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);
    return root;
}

simpledb::Database makeDatabase(const fs::path& path) {
    simpledb::Database db(path);
    std::string error;
    expect(db.initialize(error), "database initialize failed: " + error);
    return db;
}

QueryResult executeSql(simpledb::Database& db, const std::string& sql) {
    simpledb::Statement statement;
    std::string parseError;
    expect(simpledb::tryParseStatement(sql, statement, parseError), "parse failed for `" + sql + "`: " + parseError);

    QueryResult result;
    expect(db.execute(statement, result.message, result.rows), "execute failed for `" + sql + "`: " + result.message);
    return result;
}

std::string executeSqlExpectError(simpledb::Database& db, const std::string& sql) {
    simpledb::Statement statement;
    std::string parseError;
    expect(simpledb::tryParseStatement(sql, statement, parseError), "parse failed for `" + sql + "`: " + parseError);

    std::string message;
    std::optional<simpledb::SelectResult> rows;
    expect(!db.execute(statement, message, rows), "expected execution failure for `" + sql + "`");
    return message;
}

void expectValue(const simpledb::Value& value, int64_t expected) {
    expect(std::holds_alternative<int64_t>(value), "expected INT value");
    expect(std::get<int64_t>(value) == expected, "unexpected INT value");
}

void expectValue(const simpledb::Value& value, const std::string& expected) {
    expect(std::holds_alternative<std::string>(value), "expected TEXT value");
    expect(std::get<std::string>(value) == expected, "unexpected TEXT value");
}

void testConstraintsAlterAndSelect(const fs::path& root) {
    fs::path dir = root / "constraints";
    simpledb::Database db = makeDatabase(dir);

    executeSql(db, "CREATE TABLE users (id INT UNIQUE, name TEXT NOT NULL);");
    executeSql(db, "INSERT INTO users VALUES (1, 'Ada');");
    executeSql(db, "INSERT INTO users VALUES (2, 'Linus');");
    expect(executeSqlExpectError(db, "INSERT INTO users VALUES (1, 'Grace');") == "UNIQUE constraint failed: id",
           "duplicate unique insert should fail");
    expect(executeSqlExpectError(db, "INSERT INTO users (id) VALUES (3);") == "NOT NULL constraint failed: name",
           "omitted NOT NULL insert should fail");

    executeSql(db, "ALTER TABLE users ADD COLUMN score INT;");
    QueryResult selected = executeSql(db, "SELECT id, name, score FROM users ORDER BY id ASC;");
    expect(selected.rows.has_value(), "expected SELECT result");
    expect(selected.rows->rows.size() == 2, "expected two users");
    expectValue(selected.rows->rows[0][0], static_cast<int64_t>(1));
    expectValue(selected.rows->rows[0][1], "Ada");
    expectValue(selected.rows->rows[0][2], static_cast<int64_t>(0));

    QueryResult described = executeSql(db, "DESCRIBE users;");
    expect(described.rows.has_value(), "expected DESCRIBE result");
    expect(described.rows->columnNames.size() == 5, "DESCRIBE should include constraint columns");
    expect(described.rows->rows.size() == 3, "expected three described columns");
}

void testDropIndexAndScanFallback(const fs::path& root) {
    fs::path dir = root / "drop_index";
    simpledb::Database db = makeDatabase(dir);

    executeSql(db, "CREATE TABLE users (id INT, age INT, name TEXT);");
    executeSql(db, "INSERT INTO users VALUES (1, 30, 'Ada');");
    executeSql(db, "INSERT INTO users VALUES (2, 40, 'Linus');");
    executeSql(db, "CREATE INDEX users_age_idx ON users (age);");

    QueryResult before = executeSql(db, "DESCRIBE users;");
    expect(before.rows.has_value(), "expected DESCRIBE before DROP INDEX");
    expectValue(before.rows->rows[1][4], "users_age_idx");

    executeSql(db, "DROP INDEX users_age_idx;");
    QueryResult after = executeSql(db, "DESCRIBE users;");
    expect(after.rows.has_value(), "expected DESCRIBE after DROP INDEX");
    expectValue(after.rows->rows[1][4], "");
    expect(executeSqlExpectError(db, "DROP INDEX users_age_idx;") == "Unknown index: users_age_idx",
           "dropping missing index should fail");

    QueryResult selected = executeSql(db, "SELECT name FROM users WHERE age = 40;");
    expect(selected.rows.has_value(), "expected scan fallback SELECT result");
    expect(selected.rows->rows.size() == 1, "expected one fallback match");
    expectValue(selected.rows->rows[0][0], "Linus");

    simpledb::Database reopened = makeDatabase(dir);
    QueryResult persisted = executeSql(reopened, "DESCRIBE users;");
    expect(persisted.rows.has_value(), "expected persisted DESCRIBE result");
    expectValue(persisted.rows->rows[1][4], "");
}

void testIndexedJoin(const fs::path& root) {
    fs::path dir = root / "join";
    simpledb::Database db = makeDatabase(dir);

    executeSql(db, "CREATE TABLE left_items (id INT, name TEXT);");
    executeSql(db, "CREATE TABLE right_items (owner_id INT, tag TEXT);");
    executeSql(db, "INSERT INTO left_items VALUES (1, 'A');");
    executeSql(db, "INSERT INTO left_items VALUES (2, 'B');");
    executeSql(db, "INSERT INTO right_items VALUES (1, 'x');");
    executeSql(db, "INSERT INTO right_items VALUES (1, 'y');");
    executeSql(db, "INSERT INTO right_items VALUES (2, 'z');");
    executeSql(db, "CREATE INDEX right_owner_idx ON right_items (owner_id);");

    QueryResult joined = executeSql(
        db,
        "SELECT left_items.id, right_items.tag FROM left_items INNER JOIN right_items ON left_items.id = right_items.owner_id ORDER BY left_items.id ASC;");
    expect(joined.rows.has_value(), "expected join result");
    expect(joined.rows->rows.size() == 3, "expected three joined rows");
    expectValue(joined.rows->rows[0][0], static_cast<int64_t>(1));
    expectValue(joined.rows->rows[0][1], "x");
}

void testPartialTrailingRowRecovery(const fs::path& root) {
    fs::path dir = root / "partial_row";
    {
        simpledb::Database db = makeDatabase(dir);
        executeSql(db, "CREATE TABLE items (id INT);");
        executeSql(db, "INSERT INTO items VALUES (7);");
    }

    fs::path rowFile = dir / "items.rows";
    {
        std::ofstream out(rowFile, std::ios::binary | std::ios::app);
        int32_t partial = 123;
        out.write(reinterpret_cast<const char*>(&partial), sizeof(partial));
    }

    simpledb::Database recovered = makeDatabase(dir);
    QueryResult selected = executeSql(recovered, "SELECT COUNT(*) FROM items;");
    expect(selected.rows.has_value(), "expected count after recovery");
    expectValue(selected.rows->rows[0][0], static_cast<int64_t>(1));
    expect(fs::file_size(rowFile) == sizeof(int64_t), "partial trailing row should be truncated");
}

void runTest(const std::string& name, void (*test)(const fs::path&), const fs::path& root) {
    test(root);
    std::cout << "[PASS] " << name << "\n";
}

}  // namespace

int main() {
    try {
        fs::path root = testRoot();
        runTest("constraints alter select", testConstraintsAlterAndSelect, root);
        runTest("drop index scan fallback", testDropIndexAndScanFallback, root);
        runTest("indexed join", testIndexedJoin, root);
        runTest("partial trailing row recovery", testPartialTrailingRowRecovery, root);
        std::error_code ec;
        fs::remove_all(root, ec);
    } catch (const std::exception& ex) {
        std::cerr << "[FAIL] " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
