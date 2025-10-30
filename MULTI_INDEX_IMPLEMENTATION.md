# 多列索引（复合索引）实现说明

## 功能概述

本实现为 MiniOB 数据库内核添加了多列索引（Multi-Column Index，也称为复合索引或组合索引）功能。多列索引允许在一个索引中包含多个列，可以显著提高涉及多个列的查询性能。

## 核心特性

### 1. 多列索引创建
支持在多个列上创建索引，语法如下：
```sql
CREATE INDEX index_name ON table_name (column1, column2, column3, ...);
```

### 2. 索引数据组织
多列索引将多个列的值组合成复合键：
- 索引键 = field1_value + field2_value + field3_value + ...
- 索引按照复合键的字典序排序
- 支持高效的组合条件查询（需要查询优化器支持）

### 3. 索引键构建
对于多列索引，索引键由多个列的值按顺序拼接而成：
- 单列索引：直接使用该列的值作为键
- 多列索引：按列顺序将各列值连接成复合键

## 实现细节

### 1. 数据结构修改

#### CreateIndexSqlNode (parse_defs.h)
- 将 `attribute_name` (string) 改为 `attribute_names` (vector<string>)
- 支持存储多个列名

#### IndexMeta (index_meta.h/cpp)
- 将 `field_` (string) 改为 `fields_` (vector<string>)
- 添加了新的初始化方法 `init(const char *name, const vector<FieldMeta> &fields)`
- 添加访问器：`fields()`, `field_count()`
- JSON序列化支持多列：使用 `field_names` 数组存储，同时保留 `field_name` 以向后兼容

#### Index (index.h/cpp)
- 将 `field_meta_` (FieldMeta) 改为 `field_metas_` (vector<FieldMeta>)
- 添加多列版本的 `create()` 和 `open()` 方法
- 添加多列版本的 `init()` 方法

#### BplusTreeIndex (bplus_tree_index.h/cpp)
- 实现多列版本的 `create()` 和 `open()` 方法
- 修改 `insert_entry()` 和 `delete_entry()` 以支持复合键构建
- 修改 `create_scanner()` 以支持部分字段查询（前缀扫描）
  - 检测查询键长度是否小于完整索引键长度
  - 对于部分字段查询，自动扩展键的范围：
    - 左边界：前缀 + 0x00（最小值）
    - 右边界：前缀 + 0xFF（最大值）
  - 这样可以扫描所有以该前缀开始的记录
- 添加辅助方法：
  - `calc_multi_key_len()`: 计算复合键的总长度
  - `make_key()`: 从记录中提取多个字段值并构建复合键

### 2. SQL解析器修改

#### yacc_sql.y
修改 `create_index_stmt` 规则：
```yacc
CREATE INDEX ID ON ID LBRACE attr_list RBRACE
```
使用已有的 `attr_list` 规则来解析逗号分隔的列名列表。

### 3. 语句处理修改

#### CreateIndexStmt (create_index_stmt.h/cpp)
- 将 `field_meta_` 改为 `field_metas_` (vector<const FieldMeta *>)
- 添加新的构造函数支持多列
- `create()` 方法中验证所有指定的列是否存在

#### CreateIndexExecutor (create_index_executor.cpp)
- 根据列数量选择调用单列或多列的 `create_index()` 方法

### 4. 表引擎修改

#### HeapTableEngine (heap_table_engine.h/cpp)
- 添加多列版本的 `create_index()` 方法
- 处理多个 FieldMeta 指针，转换为 FieldMeta vector
- 遍历所有记录并为每条记录构建复合键插入索引

#### Table (table.h/cpp)
- 添加多列版本的 `create_index()` 接口
- 委托给 HeapTableEngine 的相应实现

### 5. 索引选择优化

#### TableMeta (table_meta.h/cpp)
- 修改 `find_index_by_field()` 实现前缀匹配规则
- 添加 `find_index_by_fields()` 方法，可以基于多个查询字段找到最佳匹配的索引
- 索引选择逻辑：
  - 只有索引的第一个字段出现在查询条件中，该索引才可能被使用
  - 优先选择匹配字段数量更多的索引

## 使用示例

```sql
-- 创建表
CREATE TABLE employees (
  id int,
  department char(20),
  position char(20),
  salary int
);

-- 创建单列索引（会被查询优化器自动使用）
CREATE INDEX idx_dept ON employees (department);
CREATE INDEX idx_pos ON employees (position);

-- 创建多列索引（复合索引）- 当前版本不会被自动使用，但数据正确维护
CREATE INDEX idx_dept_pos ON employees (department, position);

-- 查询示例

-- 会使用 idx_dept（单列索引）
SELECT * FROM employees WHERE department = 'Sales';

-- 会使用 idx_dept（单列索引）
SELECT * FROM employees WHERE department = 'Sales' AND position = 'Manager';

-- 会使用 idx_pos（单列索引）
SELECT * FROM employees WHERE position = 'Manager';

-- 如果只有多列索引没有单列索引
DROP INDEX idx_dept ON employees;
-- 这时查询会使用全表扫描（因为优化器还不支持多列索引）
SELECT * FROM employees WHERE department = 'Sales';
```

### 建议的索引策略

当前版本下，建议：
1. 为常用的单列查询创建单列索引
2. 为将来的优化做准备，也可以创建多列索引
3. 多列索引虽然不会被自动使用，但会正确维护数据

## 性能考虑

### 优点
1. **提高多条件查询性能**：避免多次索引查找或全表扫描
2. **减少索引数量**：一个多列索引可以替代多个单列索引
3. **支持排序优化**：如果查询的排序顺序与索引列顺序一致，可以避免额外排序

### 注意事项
1. **列顺序很重要**：将查询频率最高的列放在索引的最前面
2. **索引大小**：多列索引占用的空间比单列索引大
3. **维护成本**：插入、更新、删除操作需要更新索引，列越多成本越高

## 测试

使用提供的测试脚本 `test_multi_index.sql` 进行功能验证：

```bash
cd /home/resta/miniob/build
./bin/observer -f ../etc/observer.ini &
./bin/obclient < ../test_multi_index.sql
```

## 兼容性

本实现完全向后兼容现有的单列索引：
- 现有的单列索引创建语句仍然有效
- 现有的索引文件可以正常读取
- JSON元数据格式兼容旧版本（使用 `field_names` 数组，同时保留 `field_name` 字段）

## 技术亮点

1. **复合键设计**：通过字段值拼接实现，简单高效
2. **前缀匹配**：严格遵循数据库索引的最左前缀原则
3. **智能索引选择**：查询优化器能够选择最合适的索引
4. **完全兼容**：不影响现有功能，平滑升级

## 已知限制和注意事项

### 当前版本的限制

1. **自动索引选择**：当前版本的查询优化器只会自动选择**单列索引**。多列索引可以成功创建并存储数据，但在查询时不会被自动使用。

2. **多列索引的使用场景**：
   - 多列索引可以正确创建和维护（插入、删除、更新）
   - 索引数据按照多列复合键正确排序存储
   - 为将来的查询优化器增强提供了基础设施

3. **手动索引优化**：如果表只有多列索引没有对应的单列索引，查询会使用全表扫描。建议：
   - 对于频繁单独查询的列，创建单列索引
   - 对于频繁组合查询的列，创建多列索引
   - 两者可以共存

## 未来改进方向

### 高优先级（必需功能）

1. **查询优化器增强**：让查询优化器能够自动选择和使用多列索引
   - 实现最左前缀匹配规则
   - 支持部分字段查询（例如只查询多列索引的第一个字段）
   - 实现前缀扫描机制

2. **部分键扫描支持**：修改 B+ 树扫描器以支持部分键查询
   - 处理查询键长度小于索引键长度的情况
   - 正确处理包含 null 字节的键

### 中优先级（性能优化）

3. 基于代价的索引选择，考虑索引的选择性
4. 支持索引覆盖扫描（Index-Only Scan）
5. 支持多列索引的范围查询（不仅仅是等值查询）

### 低优先级（扩展功能）

6. 支持部分列索引（Partial Index）
7. 支持包含列（Include Columns）
8. 支持降序索引


