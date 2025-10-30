# UPDATE功能实现总结

## 概述
本次实现为miniob数据库内核系统添加了完整的UPDATE功能，支持单字段更新，并支持WHERE条件过滤。数据更新时会同步考虑索引的维护。

## 实现的功能
- UPDATE语句解析（已在yacc_sql.y中定义）
- 支持单字段更新
- 支持WHERE条件过滤
- 支持事务处理（MVCC）
- 自动维护索引

## SQL语法
```sql
UPDATE table_name SET field_name = value WHERE conditions;
```

示例：
```sql
UPDATE students SET age = 20 WHERE id = 1;
UPDATE employees SET salary = 5000 WHERE department = 'IT';
```

## 实现架构

### 1. SQL解析层 (Parser)
- **文件**: `src/observer/sql/parser/yacc_sql.y`
- **说明**: UPDATE语法规则已定义，解析生成`UpdateSqlNode`

### 2. 语句层 (Statement)
- **文件**: 
  - `src/observer/sql/stmt/update_stmt.h`
  - `src/observer/sql/stmt/update_stmt.cpp`
  - `src/observer/sql/stmt/stmt.cpp`
- **实现内容**:
  - `UpdateStmt::create()`: 解析UpdateSqlNode，验证表名、字段名、类型匹配
  - 创建FilterStmt处理WHERE条件
  - 在`Stmt::create_stmt()`中添加SCF_UPDATE case

### 3. 逻辑算子层 (Logical Operator)
- **文件**:
  - `src/observer/sql/operator/update_logical_operator.h`
  - `src/observer/sql/operator/update_logical_operator.cpp`
  - `src/observer/sql/operator/logical_operator.h`
- **实现内容**:
  - 创建`UpdateLogicalOperator`类
  - 在`LogicalOperatorType`枚举中添加UPDATE
  - 在`OpType`枚举中添加LOGICALUPDATE

### 4. 物理算子层 (Physical Operator)
- **文件**:
  - `src/observer/sql/operator/update_physical_operator.h`
  - `src/observer/sql/operator/update_physical_operator.cpp`
  - `src/observer/sql/operator/physical_operator.h`
- **实现内容**:
  - 创建`UpdatePhysicalOperator`类
  - 在`PhysicalOperatorType`枚举中添加UPDATE
  - 实现`open()`, `next()`, `close()`方法
  - 执行流程：
    1. 从子算子获取需要更新的记录
    2. 对每条记录，创建新记录并修改指定字段
    3. 调用事务的update_record方法

### 5. 计划生成器 (Plan Generator)
- **文件**:
  - `src/observer/sql/optimizer/logical_plan_generator.h/cpp`
  - `src/observer/sql/optimizer/physical_plan_generator.h/cpp`
- **实现内容**:
  - `LogicalPlanGenerator::create_plan(UpdateStmt*)`: 创建UPDATE逻辑计划
    - 创建TableGetLogicalOperator获取数据
    - 创建PredicateLogicalOperator过滤数据
    - 创建UpdateLogicalOperator执行更新
  - `PhysicalPlanGenerator::create_plan(UpdateLogicalOperator&)`: 创建UPDATE物理计划

### 6. 存储引擎层 (Storage Engine)
- **文件**:
  - `src/observer/storage/table/heap_table_engine.h/cpp`
- **实现内容**:
  - `HeapTableEngine::update_record_with_trx()`:
    1. 删除旧索引项
    2. 更新记录数据
    3. 插入新索引项
    4. 失败时回滚

### 7. 事务层 (Transaction)
- **文件**:
  - `src/observer/storage/trx/mvcc_trx.h/cpp`
  - `src/observer/storage/trx/trx.h`
- **实现内容**:
  - `MvccTrx::update_record()`:
    1. 检查记录可见性（MVCC）
    2. 调用表的update_record_with_trx
    3. 记录日志（delete + insert）
    4. 添加到操作列表

## 关键设计决策

### 1. 单字段更新
当前实现仅支持单字段更新，简化了实现复杂度。未来可扩展为多字段更新。

### 2. 索引维护
更新操作通过"删除旧索引 + 插入新索引"的方式维护索引一致性。

### 3. MVCC事务支持
- UPDATE不改变记录的可见性（不修改begin_xid/end_xid）
- 日志记录采用delete+insert的组合方式
- 支持事务回滚和恢复

### 4. 错误处理
- 验证表存在性
- 验证字段存在性
- 验证字段类型匹配
- 索引更新失败时自动回滚

## 测试用例

### 基本更新
```sql
CREATE TABLE test (id INT, name TEXT, age INT);
INSERT INTO test VALUES (1, 'Alice', 20);
INSERT INTO test VALUES (2, 'Bob', 25);

UPDATE test SET age = 21 WHERE id = 1;
SELECT * FROM test WHERE id = 1;  -- 应该看到age=21

UPDATE test SET name = 'Charlie' WHERE age = 25;
SELECT * FROM test WHERE id = 2;  -- 应该看到name='Charlie'
```

### 批量更新
```sql
UPDATE test SET age = 30 WHERE age > 20;
SELECT * FROM test;  -- 所有age>20的记录都应该变成30
```

### 无WHERE条件（更新所有记录）
```sql
UPDATE test SET age = 100;
SELECT * FROM test;  -- 所有记录的age都应该是100
```

## 文件清单

### 新增文件
1. `src/observer/sql/operator/update_logical_operator.h`
2. `src/observer/sql/operator/update_logical_operator.cpp`
3. `src/observer/sql/operator/update_physical_operator.h`
4. `src/observer/sql/operator/update_physical_operator.cpp`

### 修改文件
1. `src/observer/sql/stmt/update_stmt.h` - 重新实现
2. `src/observer/sql/stmt/update_stmt.cpp` - 重新实现
3. `src/observer/sql/stmt/stmt.cpp` - 添加UPDATE case
4. `src/observer/sql/operator/logical_operator.h` - 添加UPDATE枚举
5. `src/observer/sql/operator/physical_operator.h` - 添加UPDATE枚举
6. `src/observer/sql/optimizer/logical_plan_generator.h` - 添加方法声明
7. `src/observer/sql/optimizer/logical_plan_generator.cpp` - 实现UPDATE计划生成
8. `src/observer/sql/optimizer/physical_plan_generator.h` - 添加方法声明
9. `src/observer/sql/optimizer/physical_plan_generator.cpp` - 实现UPDATE物理计划
10. `src/observer/storage/table/heap_table_engine.h` - 实现update_record_with_trx
11. `src/observer/storage/table/heap_table_engine.cpp` - 实现update_record_with_trx
12. `src/observer/storage/trx/mvcc_trx.h` - 实现update_record
13. `src/observer/storage/trx/mvcc_trx.cpp` - 实现update_record

## 编译和测试

```bash
cd /home/resta/miniob
bash build.sh
cd build
./bin/observer -f ../etc/observer.ini
```

在observer命令行中测试：
```sql
create table t(id int, name char, age int);
insert into t values(1, 'a', 10);
insert into t values(2, 'b', 20);
select * from t;
update t set age = 15 where id = 1;
select * from t where id = 1;
```

## 未来改进方向

1. **多字段更新支持**: 支持一次更新多个字段
2. **子查询支持**: 支持从子查询结果更新
3. **JOIN更新**: 支持基于JOIN的更新
4. **性能优化**: 批量更新优化，减少索引操作次数
5. **更完善的日志**: 添加专门的UPDATE日志类型而不是delete+insert组合
6. **表达式支持**: 支持SET field = field + 1这样的表达式

## 注意事项

1. 更新操作会同时更新所有相关索引
2. 在MVCC模式下，更新操作不会改变记录的事务可见性字段
3. 如果更新失败，会自动回滚索引更改
4. 当前实现仅支持单字段更新
5. 字段类型必须严格匹配，不支持自动类型转换（除非在UpdateStmt中已处理）

