# INNER JOIN 实现文档

## 1. 概述

本文档描述了 MiniOB 数据库系统中 INNER JOIN 功能的完整实现方案。INNER JOIN 是 SQL 中最常用的连接操作，用于根据连接条件从多个表中组合数据。

### 1.1 功能目标

- 支持标准 SQL INNER JOIN 语法
- 支持两表及多表连接
- 支持单个或多个 ON 条件（AND 连接）
- 支持 JOIN 条件与 WHERE 条件的组合使用
- 采用嵌套循环连接（Nested Loop Join）算法实现

### 1.2 语法支持

```sql
-- 基本语法
SELECT ... FROM table1 INNER JOIN table2 ON condition;

-- 多表连接
SELECT ... FROM table1 
INNER JOIN table2 ON condition1 
INNER JOIN table3 ON condition2;

-- 多条件连接
SELECT ... FROM table1 
INNER JOIN table2 ON condition1 AND condition2;

-- 与 WHERE 结合
SELECT ... FROM table1 
INNER JOIN table2 ON join_condition 
WHERE filter_condition;
```

## 2. 系统架构

### 2.1 数据流概览

```
SQL 查询
    ↓
[词法分析] lex_sql.l - 识别 INNER、JOIN 关键字
    ↓
[语法分析] yacc_sql.y - 解析 JOIN 语法结构
    ↓
[语义分析] select_stmt.cpp - 验证表和字段，创建 FilterStmt
    ↓
[逻辑计划] logical_plan_generator.cpp - 生成 JoinLogicalOperator
    ↓
[物理计划] physical_plan_generator.cpp - 生成 NestedLoopJoinPhysicalOperator
    ↓
[执行引擎] nested_loop_join_physical_operator.cpp - 执行连接操作
    ↓
查询结果
```

### 2.2 核心组件

| 组件 | 文件 | 职责 |
|------|------|------|
| 词法分析器 | lex_sql.l | 识别 INNER、JOIN 关键字 |
| 语法分析器 | yacc_sql.y | 解析 JOIN 语法，构建 AST |
| 数据结构 | parse_defs.h | 定义 JoinSqlNode 结构 |
| 语句处理 | select_stmt.cpp/h | 验证和处理 JOIN 信息 |
| 逻辑算子 | join_logical_operator.h | 表示逻辑层的 JOIN 操作 |
| 物理算子 | nested_loop_join_physical_operator.cpp/h | 实现具体的连接算法 |

## 3. 详细设计

### 3.1 词法分析（Lexical Analysis）

**文件**: `src/observer/sql/parser/lex_sql.l`

**修改内容**:
```lex
INNER                                   RETURN_TOKEN(INNER);
JOIN                                    RETURN_TOKEN(JOIN);
```

**说明**: 添加 INNER 和 JOIN 作为保留关键字，使词法分析器能够识别它们。

### 3.2 语法分析（Syntax Analysis）

**文件**: `src/observer/sql/parser/yacc_sql.y`

#### 3.2.1 Token 定义

```yacc
%token INNER JOIN
```

#### 3.2.2 Union 类型扩展

```yacc
%union {
  // ... 其他类型 ...
  JoinSqlNode *                              join_node;
  vector<JoinSqlNode> *                      join_list;
}
```

#### 3.2.3 类型声明

```yacc
%type <join_node>           join_node
%type <join_list>           join_list
%type <condition_list>      on_conditions
```

#### 3.2.4 语法规则

**SELECT 语句扩展**:
```yacc
select_stmt:
    SELECT expression_list FROM relation join_list where group_by
    {
      $$ = new ParsedSqlNode(SCF_SELECT);
      // 第一个表
      if ($4 != nullptr) {
        $$->selection.relations.push_back($4);
      }
      // JOIN 的表
      if ($5 != nullptr) {
        $$->selection.joins.swap(*$5);
        delete $5;
      }
      // ... 处理其他部分 ...
    }
    | SELECT expression_list FROM rel_list where group_by  // 保持兼容性
    ;
```

**JOIN 列表规则**:
```yacc
join_list:
    /* empty */ { $$ = nullptr; }
    | join_node join_list
    {
      if ($2 != nullptr) {
        $$ = $2;
      } else {
        $$ = new vector<JoinSqlNode>();
      }
      $$->insert($$->begin(), *$1);
      delete $1;
    }
    ;
```

**JOIN 节点规则**:
```yacc
join_node:
    INNER JOIN relation ON on_conditions
    {
      $$ = new JoinSqlNode();
      $$->table_name = $3;
      if ($5 != nullptr) {
        $$->conditions.swap(*$5);
        delete $5;
      }
    }
    ;
```

**ON 条件规则**:
```yacc
on_conditions:
    /* empty */ { $$ = nullptr; }
    | condition 
    {
      $$ = new vector<ConditionSqlNode>;
      $$->emplace_back(*$1);
      delete $1;
    }
    | condition AND on_conditions 
    {
      $$ = $3;
      $$->emplace_back(*$1);
      delete $1;
    }
    ;
```

### 3.3 数据结构定义

**文件**: `src/observer/sql/parser/parse_defs.h`

#### 3.3.1 JoinSqlNode 结构

```cpp
/**
 * @brief 描述一个JOIN表信息
 * @ingroup SQLParser
 * @details 包含JOIN的表名和JOIN条件
 */
struct JoinSqlNode
{
  string                   table_name;  ///< JOIN的表名
  vector<ConditionSqlNode> conditions;  ///< JOIN条件，使用AND串联
};
```

#### 3.3.2 SelectSqlNode 扩展

```cpp
struct SelectSqlNode
{
  vector<unique_ptr<Expression>> expressions;  ///< 查询的表达式
  vector<string>                 relations;    ///< 查询的表
  vector<ConditionSqlNode>       conditions;   ///< 查询条件
  vector<unique_ptr<Expression>> group_by;     ///< group by clause
  vector<JoinSqlNode>            joins;        ///< INNER JOIN的表和条件
};
```

**设计要点**:
- `relations` 存储第一个表（FROM 子句中的表）
- `joins` 存储所有 INNER JOIN 的表和对应的 ON 条件
- 每个 `JoinSqlNode` 对应一个 INNER JOIN 子句

### 3.4 语义分析与语句处理

**文件**: `src/observer/sql/stmt/select_stmt.cpp`, `select_stmt.h`

#### 3.4.1 SelectStmt 类扩展

```cpp
class SelectStmt : public Stmt
{
  // ... 其他成员 ...
private:
  vector<FilterStmt *> join_filter_stmts_;  ///< JOIN条件对应的FilterStmt
  
public:
  const vector<FilterStmt *> &join_filter_stmts() const { 
    return join_filter_stmts_; 
  }
};
```

#### 3.4.2 析构函数

```cpp
SelectStmt::~SelectStmt()
{
  if (nullptr != filter_stmt_) {
    delete filter_stmt_;
    filter_stmt_ = nullptr;
  }
  
  // 清理 JOIN 条件的 FilterStmt
  for (FilterStmt *join_filter : join_filter_stmts_) {
    if (join_filter != nullptr) {
      delete join_filter;
    }
  }
  join_filter_stmts_.clear();
}
```

#### 3.4.3 CREATE 方法实现

**处理 JOIN 表**:
```cpp
// 收集 FROM 子句中的表
for (size_t i = 0; i < select_sql.relations.size(); i++) {
  const char *table_name = select_sql.relations[i].c_str();
  Table *table = db->find_table(table_name);
  // 错误检查...
  binder_context.add_table(table);
  tables.push_back(table);
  table_map.insert({table_name, table});
}

// 收集 JOIN 子句中的表
for (size_t i = 0; i < select_sql.joins.size(); i++) {
  const char *table_name = select_sql.joins[i].table_name.c_str();
  Table *table = db->find_table(table_name);
  // 错误检查...
  binder_context.add_table(table);
  tables.push_back(table);
  table_map.insert({table_name, table});
}
```

**创建 JOIN 条件的 FilterStmt**:
```cpp
vector<FilterStmt *> join_filter_stmts;

// 第一个表没有 JOIN 条件
for (size_t i = 0; i < select_sql.relations.size(); i++) {
  join_filter_stmts.push_back(nullptr);
}

// 为每个 JOIN 创建 FilterStmt
for (size_t i = 0; i < select_sql.joins.size(); i++) {
  FilterStmt *join_filter = nullptr;
  rc = FilterStmt::create(db,
      nullptr,  // no default table
      &table_map,
      select_sql.joins[i].conditions.data(),
      static_cast<int>(select_sql.joins[i].conditions.size()),
      join_filter);
  
  if (rc != RC::SUCCESS) {
    // 清理已创建的 FilterStmt...
    return rc;
  }
  join_filter_stmts.push_back(join_filter);
}

select_stmt->join_filter_stmts_.swap(join_filter_stmts);
```

**设计要点**:
- `join_filter_stmts_` 的大小与 `tables_` 相同
- 索引对应：`join_filter_stmts_[i]` 是 `tables_[i]` 的 JOIN 条件
- 第一个表（FROM 子句）的 JOIN 条件为 nullptr

### 3.5 逻辑计划生成

**文件**: `src/observer/sql/optimizer/logical_plan_generator.cpp`

#### 3.5.1 CREATE_PLAN 实现

```cpp
RC LogicalPlanGenerator::create_plan(
    SelectStmt *select_stmt, 
    unique_ptr<LogicalOperator> &logical_operator)
{
  const vector<Table *> &tables = select_stmt->tables();
  const vector<FilterStmt *> &join_filter_stmts = 
      select_stmt->join_filter_stmts();
  
  unique_ptr<LogicalOperator> table_oper(nullptr);
  
  for (size_t i = 0; i < tables.size(); i++) {
    Table *table = tables[i];
    unique_ptr<LogicalOperator> table_get_oper(
        new TableGetLogicalOperator(table, ReadWriteMode::READ_ONLY));
    
    if (table_oper == nullptr) {
      // 第一个表
      table_oper = std::move(table_get_oper);
    } else {
      // 创建 JoinLogicalOperator
      JoinLogicalOperator *join_oper = new JoinLogicalOperator;
      join_oper->add_child(std::move(table_oper));
      join_oper->add_child(std::move(table_get_oper));
      
      // 添加 JOIN 条件
      if (i < join_filter_stmts.size() && 
          join_filter_stmts[i] != nullptr) {
        FilterStmt *join_filter = join_filter_stmts[i];
        const vector<FilterUnit *> &filter_units = 
            join_filter->filter_units();
        
        for (const FilterUnit *filter_unit : filter_units) {
          // 构造左右表达式
          const FilterObj &left = filter_unit->left();
          const FilterObj &right = filter_unit->right();
          
          unique_ptr<Expression> left_expr(
              left.is_attr 
                  ? static_cast<Expression *>(new FieldExpr(left.field))
                  : static_cast<Expression *>(new ValueExpr(left.value)));
          
          unique_ptr<Expression> right_expr(
              right.is_attr 
                  ? static_cast<Expression *>(new FieldExpr(right.field))
                  : static_cast<Expression *>(new ValueExpr(right.value)));
          
          // 创建比较表达式
          ComparisonExpr *cmp_expr = new ComparisonExpr(
              filter_unit->comp(), 
              std::move(left_expr), 
              std::move(right_expr));
          
          join_oper->add_join_predicate(unique_ptr<Expression>(cmp_expr));
        }
      }
      
      table_oper = unique_ptr<LogicalOperator>(join_oper);
    }
  }
  
  // ... 处理 WHERE、GROUP BY、PROJECT 等 ...
}
```

**设计要点**:
- 左深树结构：每次将当前 table_oper 作为左子树
- JOIN 条件存储在 JoinLogicalOperator 的 `join_predicates_` 中
- 支持每个 JOIN 有多个条件（AND 连接）

### 3.6 物理计划生成

**文件**: `src/observer/sql/optimizer/physical_plan_generator.cpp`

```cpp
RC PhysicalPlanGenerator::create_plan(
    JoinLogicalOperator &join_oper, 
    unique_ptr<PhysicalOperator> &oper, 
    Session* session)
{
  RC rc = RC::SUCCESS;
  
  vector<unique_ptr<LogicalOperator>> &child_opers = join_oper.children();
  if (child_opers.size() != 2) {
    return RC::INTERNAL;
  }
  
  NestedLoopJoinPhysicalOperator *nlj_oper = 
      new NestedLoopJoinPhysicalOperator();
  unique_ptr<PhysicalOperator> join_physical_oper(nlj_oper);
  
  // 创建子物理算子
  for (auto &child_oper : child_opers) {
    unique_ptr<PhysicalOperator> child_physical_oper;
    rc = create(*child_oper, child_physical_oper, session);
    if (rc != RC::SUCCESS) {
      return rc;
    }
    join_physical_oper->add_child(std::move(child_physical_oper));
  }
  
  // 设置 JOIN 条件
  vector<unique_ptr<Expression>> &join_predicates = 
      join_oper.get_join_predicates();
  
  if (!join_predicates.empty()) {
    // 将多个条件合并为一个 ConjunctionExpr (AND)
    unique_ptr<ConjunctionExpr> conjunction_expr(
        new ConjunctionExpr(
            ConjunctionExpr::Type::AND, 
            join_predicates));
    nlj_oper->set_predicates(std::move(conjunction_expr));
  }
  
  oper = std::move(join_physical_oper);
  return rc;
}
```

**设计要点**:
- 使用 `ConjunctionExpr` 将多个 JOIN 条件组合为 AND 表达式
- 通过 `set_predicates()` 方法将条件传递给物理算子

### 3.7 物理算子实现

**文件**: `src/observer/sql/operator/nested_loop_join_physical_operator.h`

#### 3.7.1 类定义

```cpp
class NestedLoopJoinPhysicalOperator : public PhysicalOperator
{
public:
  NestedLoopJoinPhysicalOperator();
  virtual ~NestedLoopJoinPhysicalOperator() = default;
  
  PhysicalOperatorType type() const override { 
    return PhysicalOperatorType::NESTED_LOOP_JOIN; 
  }
  
  RC     open(Trx *trx) override;
  RC     next() override;
  RC     close() override;
  Tuple *current_tuple() override;
  
  void set_predicates(unique_ptr<Expression> &&expr);

private:
  RC left_next();
  RC right_next();
  RC evaluate_predicate(bool &result);

private:
  Trx                   *trx_ = nullptr;
  PhysicalOperator      *left_ = nullptr;
  PhysicalOperator      *right_ = nullptr;
  Tuple                 *left_tuple_ = nullptr;
  Tuple                 *right_tuple_ = nullptr;
  JoinedTuple            joined_tuple_;
  bool                   round_done_ = true;
  bool                   right_closed_ = true;
  unique_ptr<Expression> predicate_;  ///< JOIN 条件表达式
};
```

#### 3.7.2 核心方法实现

**OPEN 方法**:
```cpp
RC NestedLoopJoinPhysicalOperator::open(Trx *trx)
{
  if (children_.size() != 2) {
    return RC::INTERNAL;
  }
  
  left_ = children_[0].get();
  right_ = children_[1].get();
  right_closed_ = true;
  round_done_ = true;
  trx_ = trx;
  
  return left_->open(trx);
}
```

**NEXT 方法**:
```cpp
RC NestedLoopJoinPhysicalOperator::next()
{
  RC rc = RC::SUCCESS;
  
  while (RC::SUCCESS == rc) {
    bool left_need_step = (left_tuple_ == nullptr);
    if (round_done_) {
      left_need_step = true;
    }
    
    if (left_need_step) {
      rc = left_next();
      if (rc != RC::SUCCESS) {
        return rc;
      }
    }
    
    rc = right_next();
    if (rc != RC::SUCCESS) {
      if (rc == RC::RECORD_EOF) {
        rc = RC::SUCCESS;
        round_done_ = true;
        continue;
      } else {
        return rc;
      }
    }
    
    // 评估 JOIN 条件
    bool predicate_result = true;
    rc = evaluate_predicate(predicate_result);
    if (rc != RC::SUCCESS) {
      return rc;
    }
    
    if (predicate_result) {
      // 条件满足，返回当前 tuple
      return RC::SUCCESS;
    }
    // 条件不满足，继续下一轮
  }
  
  return rc;
}
```

**EVALUATE_PREDICATE 方法**:
```cpp
RC NestedLoopJoinPhysicalOperator::evaluate_predicate(bool &result)
{
  result = true;
  if (predicate_ == nullptr) {
    return RC::SUCCESS;  // 无条件则返回 true
  }
  
  Value value;
  RC rc = predicate_->get_value(joined_tuple_, value);
  if (rc != RC::SUCCESS) {
    return rc;
  }
  
  result = value.get_boolean();
  return RC::SUCCESS;
}
```

**LEFT_NEXT 和 RIGHT_NEXT 方法**:
```cpp
RC NestedLoopJoinPhysicalOperator::left_next()
{
  RC rc = left_->next();
  if (rc != RC::SUCCESS) {
    return rc;
  }
  
  left_tuple_ = left_->current_tuple();
  joined_tuple_.set_left(left_tuple_);
  return rc;
}

RC NestedLoopJoinPhysicalOperator::right_next()
{
  RC rc = RC::SUCCESS;
  
  if (round_done_) {
    if (!right_closed_) {
      rc = right_->close();
      right_closed_ = true;
      if (rc != RC::SUCCESS) {
        return rc;
      }
    }
    
    rc = right_->open(trx_);
    if (rc != RC::SUCCESS) {
      return rc;
    }
    right_closed_ = false;
    round_done_ = false;
  }
  
  rc = right_->next();
  if (rc != RC::SUCCESS) {
    if (rc == RC::RECORD_EOF) {
      round_done_ = true;
    }
    return rc;
  }
  
  right_tuple_ = right_->current_tuple();
  joined_tuple_.set_right(right_tuple_);
  return rc;
}
```

## 4. 算法详解

### 4.1 嵌套循环连接算法（Nested Loop Join）

```
algorithm NestedLoopJoin(R, S, condition):
    result = []
    for each tuple r in R:
        for each tuple s in S:
            joined = concatenate(r, s)
            if evaluate(condition, joined):
                result.append(joined)
    return result
```

**时间复杂度**: O(|R| × |S|)  
**空间复杂度**: O(1)（不包括结果集）

### 4.2 执行流程

```
1. Open Phase:
   - 打开左子算子（outer table）
   - 初始化状态变量

2. Next Phase (循环):
   a. 如果需要，获取下一个左表元组
   b. 如果新的一轮，重新打开右子算子（inner table）
   c. 获取下一个右表元组
   d. 如果右表结束 (EOF):
      - 标记 round_done = true
      - 返回步骤 a（获取下一个左表元组）
   e. 组合左右元组
   f. 评估 JOIN 条件
   g. 如果条件满足，返回该元组
   h. 否则继续循环

3. Close Phase:
   - 关闭左右子算子
   - 清理资源
```

### 4.3 状态管理

| 状态变量 | 含义 | 初始值 |
|---------|------|--------|
| `left_tuple_` | 当前左表元组 | nullptr |
| `right_tuple_` | 当前右表元组 | nullptr |
| `round_done_` | 当前轮是否结束 | true |
| `right_closed_` | 右算子是否关闭 | true |
| `predicate_` | JOIN 条件表达式 | nullptr |

## 5. 示例分析

### 5.1 简单两表 JOIN

**SQL**:
```sql
SELECT * FROM t1 INNER JOIN t2 ON t1.id = t2.id;
```

**执行计划**:
```
ProjectOperator
  └─ JoinOperator (predicate: t1.id = t2.id)
       ├─ TableGetOperator(t1)
       └─ TableGetOperator(t2)
```

**数据流**:
```
t1: {(1,'a'), (2,'b'), (3,'c')}
t2: {(1,10), (2,20)}

执行过程:
1. left_tuple = (1,'a'), right_tuple = (1,10) → 条件满足 → 输出 (1,'a',1,10)
2. left_tuple = (1,'a'), right_tuple = (2,20) → 条件不满足
3. left_tuple = (2,'b'), right_tuple = (1,10) → 条件不满足
4. left_tuple = (2,'b'), right_tuple = (2,20) → 条件满足 → 输出 (2,'b',2,20)
5. left_tuple = (3,'c'), right_tuple = (1,10) → 条件不满足
6. left_tuple = (3,'c'), right_tuple = (2,20) → 条件不满足

结果: {(1,'a',1,10), (2,'b',2,20)}
```

### 5.2 多表 JOIN

**SQL**:
```sql
SELECT * FROM t1 
INNER JOIN t2 ON t1.id = t2.id 
INNER JOIN t3 ON t1.id = t3.id;
```

**执行计划**:
```
ProjectOperator
  └─ JoinOperator (predicate: t1.id = t3.id)
       ├─ JoinOperator (predicate: t1.id = t2.id)
       │    ├─ TableGetOperator(t1)
       │    └─ TableGetOperator(t2)
       └─ TableGetOperator(t3)
```

**说明**: 采用左深树结构，先执行 t1 ⋈ t2，再将结果与 t3 连接。

### 5.3 多条件 JOIN

**SQL**:
```sql
SELECT * FROM t1 
INNER JOIN t2 ON t1.id = t2.id AND t2.num > 10;
```

**条件表达式**:
```
ConjunctionExpr(AND)
  ├─ ComparisonExpr(EQ, t1.id, t2.id)
  └─ ComparisonExpr(GT, t2.num, 10)
```

## 6. 性能优化考虑

### 6.1 当前实现的局限性

1. **算法单一**: 仅实现嵌套循环连接，性能较低
2. **无索引优化**: 未利用索引加速连接
3. **无统计信息**: 无法基于成本选择最优连接顺序
4. **内存占用**: 右表需要多次扫描

### 6.2 未来优化方向

#### 6.2.1 Hash Join

```cpp
// 伪代码
algorithm HashJoin(R, S, R.key = S.key):
    hash_table = {}
    // Build phase
    for each tuple r in R:
        hash_table[r.key].append(r)
    
    // Probe phase
    result = []
    for each tuple s in S:
        if s.key in hash_table:
            for each r in hash_table[s.key]:
                result.append(concatenate(r, s))
    return result
```

**优势**: O(|R| + |S|) 时间复杂度  
**适用场景**: 等值连接，内存充足

#### 6.2.2 Merge Join

```cpp
// 伪代码
algorithm MergeJoin(sorted_R, sorted_S, R.key = S.key):
    result = []
    i = 0, j = 0
    while i < |R| and j < |S|:
        if R[i].key == S[j].key:
            result.append(concatenate(R[i], S[j]))
            j += 1
        elif R[i].key < S[j].key:
            i += 1
        else:
            j += 1
    return result
```

**优势**: O(|R| + |S|) 时间复杂度（已排序）  
**适用场景**: 数据已排序或有索引

#### 6.2.3 索引嵌套循环连接

```cpp
algorithm IndexNestedLoopJoin(R, S, R.key = S.key):
    result = []
    for each tuple r in R:
        // 使用索引查找匹配的 S 元组
        matching_tuples = S.index_lookup(r.key)
        for each s in matching_tuples:
            result.append(concatenate(r, s))
    return result
```

**优势**: 减少右表扫描次数  
**适用场景**: 右表有索引

## 7. 测试用例

### 7.1 基本功能测试

```sql
-- 1. 两表 JOIN
CREATE TABLE t1(id int, name char);
CREATE TABLE t2(id int, num int);
INSERT INTO t1 VALUES (1, 'a'), (2, 'b'), (3, 'c');
INSERT INTO t2 VALUES (1, 10), (2, 20);

SELECT * FROM t1 INNER JOIN t2 ON t1.id = t2.id;
-- 预期结果: (1,'a',1,10), (2,'b',2,20)

-- 2. 多表 JOIN
CREATE TABLE t3(id int, value int);
INSERT INTO t3 VALUES (1, 100), (2, 200);

SELECT * FROM t1 
INNER JOIN t2 ON t1.id = t2.id 
INNER JOIN t3 ON t1.id = t3.id;
-- 预期结果: (1,'a',1,10,1,100), (2,'b',2,20,2,200)

-- 3. 多条件 JOIN
SELECT * FROM t1 
INNER JOIN t2 ON t1.id = t2.id AND t2.num > 15;
-- 预期结果: (2,'b',2,20)

-- 4. JOIN + WHERE
SELECT * FROM t1 
INNER JOIN t2 ON t1.id = t2.id 
WHERE t1.name = 'a';
-- 预期结果: (1,'a',1,10)
```

### 7.2 边界情况测试

```sql
-- 1. 空表 JOIN
CREATE TABLE empty1(id int);
CREATE TABLE empty2(id int);

SELECT * FROM empty1 INNER JOIN empty2 ON empty1.id = empty2.id;
-- 预期结果: 空集

-- 2. 无匹配记录
INSERT INTO t1 VALUES (4, 'd');

SELECT * FROM t1 INNER JOIN t2 ON t1.id = t2.id;
-- 预期结果: 不包含 (4,'d',...)

-- 3. 笛卡尔积（无条件）
SELECT * FROM 
  (SELECT * FROM t1 WHERE id = 1) AS a 
  INNER JOIN 
  (SELECT * FROM t2) AS b ON 1=1;
-- 预期结果: (1,'a') 与 t2 所有行组合
```

### 7.3 性能测试

```sql
-- 大表 JOIN（100 x 100 = 10000 行）
CREATE TABLE large1(id int, val1 int);
CREATE TABLE large2(id int, val2 int);

-- 插入 100 行到每个表...

SELECT * FROM large1 
INNER JOIN large2 ON large1.id = large2.id;
-- 测试指标: 执行时间、内存使用
```

## 8. 错误处理

### 8.1 编译时错误

| 错误情况 | 检查位置 | 错误信息 |
|---------|---------|---------|
| 表不存在 | select_stmt.cpp | "no such table" |
| 字段不存在 | FilterStmt::create | "unknown field" |
| 字段歧义 | FilterStmt::create | "ambiguous field" |
| 类型不匹配 | expression.cpp | "type mismatch" |

### 8.2 运行时错误

| 错误情况 | 处理方式 |
|---------|---------|
| 内存不足 | 返回 RC::NOMEM |
| I/O 错误 | 返回对应错误码 |
| 事务冲突 | 返回 RC::LOCKED_CONCURRENCY_CONFLICT |

## 9. 兼容性说明

### 9.1 向后兼容

- 保留原有的隐式 JOIN 语法（逗号分隔）
- WHERE 子句仍然支持表间条件
- 两种语法可以混用

```sql
-- 隐式 JOIN（仍然支持）
SELECT * FROM t1, t2 WHERE t1.id = t2.id;

-- 显式 INNER JOIN（新增）
SELECT * FROM t1 INNER JOIN t2 ON t1.id = t2.id;
```

### 9.2 与标准 SQL 的差异

| 特性 | 标准 SQL | MiniOB 实现 | 说明 |
|-----|---------|------------|-----|
| INNER 关键字 | 可选 | 必需 | 必须写 INNER JOIN |
| USING 子句 | 支持 | 不支持 | 仅支持 ON 子句 |
| NATURAL JOIN | 支持 | 不支持 | - |
| CROSS JOIN | 支持 | 不支持 | - |
| LEFT/RIGHT JOIN | 支持 | 不支持 | 仅实现 INNER JOIN |

## 10. 总结

### 10.1 实现成果

✅ 完整的 INNER JOIN 语法解析  
✅ 多表连接支持  
✅ 多条件 JOIN（AND）  
✅ 与 WHERE 子句的正确集成  
✅ 符合 SQL 标准的执行语义  

### 10.2 代码变更统计

| 文件 | 变更类型 | 行数 |
|-----|---------|------|
| lex_sql.l | 修改 | +2 |
| yacc_sql.y | 修改 | +60 |
| parse_defs.h | 新增结构 | +10 |
| select_stmt.h | 扩展类 | +5 |
| select_stmt.cpp | 修改逻辑 | +45 |
| logical_plan_generator.cpp | 修改逻辑 | +30 |
| physical_plan_generator.cpp | 修改逻辑 | +15 |
| nested_loop_join_physical_operator.h | 扩展类 | +10 |
| nested_loop_join_physical_operator.cpp | 实现方法 | +40 |
| **总计** | - | **~220** |

### 10.3 技术要点

1. **左深树结构**: 多表 JOIN 构建为左深树，便于流式处理
2. **条件传递**: JOIN 条件从语法树 → FilterStmt → LogicalOperator → PhysicalOperator
3. **表达式求值**: 使用 ConjunctionExpr 组合多个条件
4. **内存管理**: 正确的 FilterStmt 生命周期管理
5. **算法实现**: 经典的嵌套循环连接算法

### 10.4 学习价值

本实现展示了完整的数据库功能开发流程：

1. **语法设计**: 如何扩展 SQL 语法
2. **编译原理**: 词法、语法、语义分析的实践
3. **查询优化**: 逻辑计划到物理计划的转换
4. **算法实现**: 经典 JOIN 算法的工程实现
5. **系统集成**: 如何在复杂系统中添加新功能

## 11. 参考资料

1. **SQL 标准**: ISO/IEC 9075-2:2016 (SQL/Foundation)
2. **数据库系统概念**: Abraham Silberschatz et al., 第 7 版
3. **PostgreSQL 源码**: src/backend/executor/nodeNestloop.c
4. **MySQL 文档**: JOIN Syntax
5. **CMU 15-445 课程**: Query Execution II

---

**文档版本**: 1.0  
**最后更新**: 2025-10-29  
**作者**: MiniOB Development Team

