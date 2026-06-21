# SimpleDB Engine

SimpleDB Engine is a small educational database engine written in C++17. It provides a SQL-like REPL, binary row persistence, in-memory table/index state, and B+ tree indexes for integer columns.

## Requirements

- CMake 3.16 or newer
- A C++17 compiler
  - Linux: GCC or Clang
  - Windows: MSVC Build Tools/Visual Studio, MinGW, or Clang

## Build With CMake

Linux/macOS:

```bash
cmake -S . -B build
cmake --build build
```

Windows PowerShell with the default Visual Studio generator:

```powershell
cmake -S . -B build
cmake --build build --config Release
```

Windows PowerShell with MinGW/Ninja:

```powershell
cmake -S . -B build -G Ninja
cmake --build build
```

CMake writes executables to `build/bin` on all platforms.

Run the REPL on Linux/macOS:

```bash
./build/bin/simpledb
```

Run the REPL on Windows:

```powershell
.\build\bin\simpledb.exe
```

Run regression tests on Linux/macOS:

```bash
ctest --test-dir build --output-on-failure
```

Run regression tests on Windows with a multi-config generator:

```powershell
ctest --test-dir build -C Release --output-on-failure
```

## Direct Compiler Fallback

If CMake is unavailable, the executable can be compiled directly on Linux:

```bash
mkdir -p build/bin
g++ -std=c++17 -Wall -Wextra -pedantic -Iinclude \
    src/main.cpp src/sql.cpp src/database.cpp src/bplustree.cpp \
    -o build/bin/simpledb
```

Or with MSVC from a Developer PowerShell:

```powershell
New-Item -ItemType Directory -Force build\bin
cl /std:c++17 /EHsc /Iinclude src\main.cpp src\sql.cpp src\database.cpp src\bplustree.cpp /Fe:build\bin\simpledb.exe
```

## Example Session

```sql
CREATE TABLE users (id INT UNIQUE, age INT, name TEXT NOT NULL);
INSERT INTO users VALUES (1, 30, 'Ada');
INSERT INTO users VALUES (2, 40, 'Linus');

CREATE INDEX users_age_idx ON users (age);
SELECT id, name FROM users WHERE age >= 30 ORDER BY id ASC;

DESCRIBE users;
ALTER TABLE users ADD COLUMN score INT;

CREATE TABLE posts (user_id INT, title TEXT);
INSERT INTO posts VALUES (1, 'Intro');
CREATE INDEX posts_user_idx ON posts (user_id);
SELECT users.name, posts.title
FROM users INNER JOIN posts ON users.id = posts.user_id;

DROP INDEX users_age_idx;
DROP TABLE posts;
```

Exit the REPL with:

```sql
EXIT
```

## Supported SQL

- `CREATE TABLE table_name (column TYPE [NOT NULL] [UNIQUE], ...)`
- `ALTER TABLE table_name ADD COLUMN column TYPE [NOT NULL] [UNIQUE]`
- `DROP TABLE table_name`
- `DESCRIBE table_name` or `DESC table_name`
- `INSERT INTO table_name [(columns...)] VALUES (...)`
- `SELECT`, `COUNT(*)`, `WHERE`, `AND`, `OR`, `NOT`, `ORDER BY`, `LIMIT`
- `INNER JOIN`
- `UPDATE ... SET ... [WHERE ...]`
- `DELETE FROM ... [WHERE ...]`
- `CREATE INDEX index_name ON table_name (int_column)`
- `DROP INDEX index_name`

## Storage

SimpleDB stores metadata in `data/catalog.txt` and table rows in binary `data/<table>.rows` files. Full catalog/table rewrites use safer temp-file replacement, and startup can recover from a partial trailing row left by an interrupted append.

Indexes are stored as catalog metadata and rebuilt in memory from table rows on startup.

## Current Limitations

- Supported types are `INT` and `TEXT`.
- B+ tree indexes support `INT` columns only.
- There are no transactions, locks, or multi-user concurrency controls.
- Query planning is still simple; the engine uses indexes for eligible single-table filters and right-side indexed joins.
- `NOT NULL` is limited by the lack of a `NULL` value type; it mainly rejects omitted values in column-list inserts.
- Persistence is more resilient than direct truncating writes, but it is not a full write-ahead log or transactional storage engine.
