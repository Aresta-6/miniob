# DROP TABLE 功能实现说明

## 概述
本文档描述了在 miniob 数据库内核系统中实现 DROP TABLE 功能的完整过程。

## 实现的文件和修改

### 1. SQL 语句解析层 (Statement)

#### 新增文件:
- `src/observer/sql/stmt/drop_table_stmt.h` - DROP TABLE 语句类的头文件
- `src/observer/sql/stmt/drop_table_stmt.cpp` - DROP TABLE 语句类的实现

#### 修改文件:
- `src/observer/sql/stmt/stmt.cpp` - 添加了对 DROP TABLE 语句的处理

**实现说明:**
- `DropTableStmt` 类继承自 `Stmt` 基类
- 实现了 `create` 静态方法，用于验证表是否存在并创建语句对象
- 如果表不存在，返回 `RC::SCHEMA_TABLE_NOT_EXIST` 错误码

### 2. 数据库层 (Database)

#### 修改文件:
- `src/observer/storage/db/db.h` - 添加了 `drop_table` 方法声明
- `src/observer/storage/db/db.cpp` - 实现了 `drop_table` 方法

**实现说明:**
`drop_table` 方法执行以下操作：
1. 检查表是否存在（不存在则返回错误）
2. 获取表的元数据信息（用于后续删除索引文件）
3. **在删除表对象前**收集所有索引名称到 vector 中（避免 use-after-free）
4. 从 `opened_tables_` 映射中移除表
5. 删除表对象，释放内存
6. 删除以下文件：
   - 表的元数据文件 (`.table`)
   - 表的数据文件 (`.data`)
   - 所有索引文件 (`.index`)
   - LOB 文件 (`.lob`)，如果存在

### 3. SQL 执行器层 (Executor)

#### 新增文件:
- `src/observer/sql/executor/drop_table_executor.h` - DROP TABLE 执行器头文件
- `src/observer/sql/executor/drop_table_executor.cpp` - DROP TABLE 执行器实现

#### 修改文件:
- `src/observer/sql/executor/command_executor.cpp` - 添加了对 DROP_TABLE 的处理分支

**实现说明:**
- `DropTableExecutor` 负责调用数据库的 `drop_table` 方法
- 继承了统一的执行器接口，保持代码一致性

### 4. SQL 解析器 (Parser)

**已有实现:**
- `src/observer/sql/parser/yacc_sql.y` - 已经包含了 DROP TABLE 的语法解析规则（第279-283行）
- `src/observer/sql/parser/parse_defs.h` - 已经定义了 `DropTableSqlNode` 结构体

## 实现架构

```
SQL 输入: DROP TABLE table_name
    ↓
SQL Parser (yacc_sql.y)
    ↓
ParsedSqlNode (SCF_DROP_TABLE)
    ↓
DropTableStmt::create() - 验证表存在
    ↓
CommandExecutor::execute()
    ↓
DropTableExecutor::execute()
    ↓
Db::drop_table() - 删除表及所有相关文件
    ↓
返回执行结果
```

## 代码特点

1. **完整性**: 删除表的所有相关文件（元数据、数据、索引、LOB）
2. **安全性**: 在删除前检查表是否存在
3. **一致性**: 遵循现有代码的设计模式（参考 CREATE TABLE、DESC TABLE 等）
4. **DDL 事务**: 作为 DDL 操作，在执行完成后会自动同步数据库
5. **内存安全**: 修复了 use-after-free 错误，先收集索引名称再删除表对象

## 关键代码片段

### drop_table_stmt.h
```cpp
class DropTableStmt : public Stmt
{
public:
  DropTableStmt(const string &table_name) : table_name_(table_name) {}
  virtual ~DropTableStmt() = default;

  StmtType type() const override { return StmtType::DROP_TABLE; }
  const string &table_name() const { return table_name_; }

  static RC create(Db *db, const DropTableSqlNode &drop_table, Stmt *&stmt);

private:
  string table_name_;
};
```

### db.cpp - drop_table 方法核心逻辑
```cpp
RC Db::drop_table(const char *table_name)
{
  // 检查表是否存在
  Table *table = find_table(table_name);
  if (table == nullptr) {
    LOG_WARN("Table %s does not exist.", table_name);
    return RC::SCHEMA_TABLE_NOT_EXIST;
  }

  // 先收集所有索引名称，避免在删除 table 对象后访问 table_meta
  const TableMeta &table_meta = table->table_meta();
  vector<string> index_names;
  for (int i = 0; i < table_meta.index_num(); i++) {
    const IndexMeta *index_meta = table_meta.index(i);
    index_names.push_back(index_meta->name());
  }
  
  // 从 opened_tables_ 中移除表
  opened_tables_.erase(table_name);
  
  // 删除表对象
  delete table;
  table = nullptr;

  // 删除表的元数据文件
  string meta_file = table_meta_file(path_.c_str(), table_name);
  if (filesystem::exists(meta_file)) {
    filesystem::remove(meta_file);
  }

  // 删除表的数据文件
  string data_file = table_data_file(path_.c_str(), table_name);
  if (filesystem::exists(data_file)) {
    filesystem::remove(data_file);
  }

  // 删除所有索引文件
  for (const string &index_name : index_names) {
    string index_file = table_index_file(path_.c_str(), table_name, index_name.c_str());
    if (filesystem::exists(index_file)) {
      filesystem::remove(index_file);
    }
  }

  // 删除 LOB 文件（如果存在）
  string lob_file = table_lob_file(path_.c_str(), table_name);
  if (filesystem::exists(lob_file)) {
    filesystem::remove(lob_file);
  }

  LOG_INFO("Drop table success. table name=%s", table_name);
  return RC::SUCCESS;
}
```

## 测试用例

### 测试用例 1: 正常删除表
```sql
CREATE TABLE test_table (id int, name char(20));
INSERT INTO test_table VALUES (1, 'Alice');
DROP TABLE test_table;
SHOW TABLES;
```
**期望结果**: 表被成功删除，SHOW TABLES 不再显示该表

### 测试用例 2: 删除不存在的表
```sql
DROP TABLE non_existent_table;
```
**期望结果**: 返回错误 "Table not exists"

### 测试用例 3: 删除带索引的表
```sql
CREATE TABLE indexed_table (id int, name char(20));
CREATE INDEX idx_id ON indexed_table(id);
DROP TABLE indexed_table;
```
**期望结果**: 表和所有索引文件都被删除

### 测试用例 4: 删除后重建同名表
```sql
CREATE TABLE test_table (id int);
INSERT INTO test_table VALUES (1);
DROP TABLE test_table;
CREATE TABLE test_table (id int, name char(20));
INSERT INTO test_table VALUES (2, 'Bob');
```
**期望结果**: 可以成功删除后重建同名表，新表只包含新插入的数据

## 已知限制

1. **事务支持**: 当前实现不支持在事务中回滚 DROP TABLE 操作（这是 DDL 的通用限制）
2. **并发控制**: 删除表时没有完整的并发控制，需要由上层保证没有其他操作正在使用该表
3. **级联删除**: 如果其他表通过外键引用了被删除的表，当前实现不会检查或处理这种情况

## 与标准 SQL 的差异

- 不支持 `DROP TABLE IF EXISTS` 语法（可以作为未来的改进）
- 不支持一次删除多个表（`DROP TABLE t1, t2, t3`）
- 不支持 CASCADE/RESTRICT 选项

## 后续改进建议

1. 添加 `IF EXISTS` 选项支持
2. 添加更完善的并发控制机制
3. 在日志中记录更详细的删除操作信息
4. 支持外键约束检查
5. 添加更多的错误处理和恢复机制

## Bug 修复记录

### Use-After-Free 错误修复
**问题**: 初始版本在删除 `table` 对象后继续使用 `table_meta` 引用，导致程序崩溃。

**解决方案**: 在删除 `table` 对象之前，先将所有索引名称提取并保存到 `vector<string>` 中。

**修改前**:
```cpp
const TableMeta &table_meta = table->table_meta();
delete table;  // 删除 table 对象
// ... 后续代码仍使用 table_meta 引用（错误！）
for (int i = 0; i < table_meta.index_num(); i++) { ... }
```

**修改后**:
```cpp
const TableMeta &table_meta = table->table_meta();
vector<string> index_names;
for (int i = 0; i < table_meta.index_num(); i++) {
  index_names.push_back(table_meta.index(i)->name());
}
delete table;  // 现在可以安全删除
// 使用保存的 index_names，不再访问已删除的对象
for (const string &index_name : index_names) { ... }
```

## 提交信息

本次实现包含以下变更：
- 新增 2 个 stmt 文件（drop_table_stmt.h/cpp）
- 新增 2 个 executor 文件（drop_table_executor.h/cpp）
- 修改 4 个现有文件（stmt.cpp, db.h, db.cpp, command_executor.cpp）

所有代码都通过了 linter 检查，没有编译警告或错误。

## 编译和使用

### 编译
```bash
cd /home/resta/miniob/build
make -j4
```

### 使用
```bash
# 启动 observer
./bin/observer

# 连接客户端
./bin/obclient

# 执行 DROP TABLE 命令
miniob > DROP TABLE table_name;
```

## 总结

DROP TABLE 功能已完全实现并集成到 miniob 数据库系统中，提供了：
- ✅ 完整的表删除功能
- ✅ 自动清理所有相关文件
- ✅ 错误处理和验证
- ✅ 内存安全（修复 use-after-free）
- ✅ 符合现有代码架构

实现遵循了 miniob 现有的代码架构和设计模式，与其他 DDL 操作（CREATE TABLE, CREATE INDEX 等）保持一致。

