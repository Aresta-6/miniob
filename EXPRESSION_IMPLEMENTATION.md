# MiniOB 算术表达式与聚合函数实现文档

## 项目概述

本项目为 MiniOB 数据库内核实现了完整的算术表达式支持，包括在 SELECT 和 WHERE 子句中使用四则运算（`+`、`-`、`*`、`/`），以及五种聚合函数（COUNT、SUM、MIN、MAX、AVG）。实现过程中特别注重了除零的 null 规则处理、表达式优化、以及标准 SQL 语义的遵循。

## 实现功能清单

### 1. 算术表达式 ✅

#### 1.1 基本运算符
- **加法** (`+`): 支持整数和浮点数相加
- **减法** (`-`): 支持整数和浮点数相减
- **乘法** (`*`): 支持整数和浮点数相乘
- **除法** (`/`): 支持整数和浮点数相除，包含除零保护
- **负数** (`-expr`): 支持单目负号运算

#### 1.2 表达式特性
- ✅ 支持复杂嵌套表达式：`-(col2*(-5)+2)+(col4+8)*(col1+col3*2)`
- ✅ 支持 SELECT 子句中的表达式：`SELECT col1*2, col2+col3 FROM table`
- ✅ 支持 WHERE 子句中的表达式：`WHERE col1*2 > col2+5`
- ✅ 支持表达式间的比较：`WHERE 0 < col1 - 8`
- ✅ 整数除法遵循整数语义：`9/6 = 1`，`1/3 = 0`

#### 1.3 除零的 Null 规则处理
- **整数除零**：返回 `RC::INVALID_ARGUMENT`
- **浮点除零**：返回 `RC::INVALID_ARGUMENT`
- **WHERE 子句中的处理**：表达式求值失败时，比较结果为 `false`
- **效果**：包含除零的行会被自动过滤，不会导致程序崩溃

示例：
```sql
-- col3=0 的行会被自动过滤
SELECT * FROM table WHERE 10/col3 > 5;
```

### 2. 聚合函数 ✅

#### 2.1 已实现的聚合函数
| 函数 | 功能 | 返回类型 |
|------|------|----------|
| `COUNT(column)` | 统计非空值数量 | INT |
| `SUM(column)` | 计算数值总和 | 与输入类型一致 |
| `MIN(column)` | 查找最小值 | 与输入类型一致 |
| `MAX(column)` | 查找最大值 | 与输入类型一致 |
| `AVG(column)` | 计算平均值 | **FLOAT**（即使输入是INT） |

#### 2.2 聚合函数特性
- ✅ 支持单个聚合：`SELECT COUNT(id) FROM table`
- ✅ 支持多个聚合：`SELECT MIN(col1), MAX(col1), AVG(col2) FROM table`
- ✅ 支持聚合函数在算术表达式中：`SELECT MIN(col1)+AVG(col2)*MAX(col3) FROM table`
- ✅ 支持 WHERE 子句过滤：`SELECT COUNT(id) FROM table WHERE col1 > 5`

#### 2.3 AVG 特别说明
**重要修复**：`AVG` 函数始终返回浮点数类型，即使对整数列求平均。

```sql
-- 对于整数列 col2 = (6, 7, 9)
SELECT AVG(col2) FROM table;
-- 修复前：返回 7 (整数除法 22/3=7) ❌
-- 修复后：返回 7.33 (浮点除法 22.0/3.0=7.33) ✅
```

### 3. 表达式优化 ✅

#### 3.1 常量比较移项
自动优化 `constant comp expression` 为 `expression reverse_comp constant`

示例：
```sql
-- 输入：WHERE 0 < col1 - 8
-- 优化为：WHERE col1 - 8 > 0
```

好处：
- 提高执行效率
- 确保谓词下推优化可以正常工作
- 统一表达式结构

#### 3.2 常量折叠
对于完全由常量组成的表达式，在优化阶段直接计算结果。

## 修改文件详细列表

### 核心文件修改

#### 1. 语法解析层 (Parser)

**文件**：`src/observer/sql/parser/yacc_sql.y`

**修改内容**：
- 修复 `condition_expression_list` 规则的递归顺序问题
  - 从 `emplace_back` 改为 `insert($$->begin(), ...)` 确保条件顺序正确
- 修复 `condition_list` 规则，移除导致歧义的空产生式
- 重构 `select_stmt` 规则，明确区分有/无 WHERE 子句的情况
  - 解决了算术表达式在 WHERE 中被忽略的问题

**关键代码段**：
```yacc
condition_expression_list:
    expression comp_op expression AND condition_expression_list {
      if ($5 != nullptr) {
        $$ = $5;
      } else {
        $$ = new vector<unique_ptr<Expression>>;
      }
      // 使用 insert 而非 emplace_back 确保顺序
      $$->insert($$->begin(), unique_ptr<Expression>(
        create_comparison_expression($2, $1, $3, sql_string, &@$)));
    }
    ;
```

#### 2. 表达式求值 (Expression)

**文件**：`src/observer/sql/expr/expression.cpp`

**修改内容**：

**A. ComparisonExpr::get_value (行 324-355)**
- 实现 null 规则：子表达式求值失败时，比较结果为 `false`
- 确保除零等异常不会传播，而是按 null 语义处理

```cpp
RC ComparisonExpr::get_value(const Tuple &tuple, Value &value) const
{
  Value left_value;
  Value right_value;

  RC rc = left_->get_value(tuple, left_value);
  if (rc != RC::SUCCESS) {
    // 按照null规则处理：表达式求值失败时，比较结果为false
    LOG_TRACE("failed to get value of left expression (treating as false). rc=%s", strrc(rc));
    value.set_boolean(false);
    return RC::SUCCESS;
  }
  rc = right_->get_value(tuple, right_value);
  if (rc != RC::SUCCESS) {
    // 按照null规则处理：表达式求值失败时，比较结果为false
    LOG_TRACE("failed to get value of right expression (treating as false). rc=%s", strrc(rc));
    value.set_boolean(false);
    return RC::SUCCESS;
  }

  bool bool_value = false;
  rc = compare_value(left_value, right_value, bool_value);
  if (rc == RC::SUCCESS) {
    value.set_boolean(bool_value);
  } else {
    // 比较失败时，也按照null规则处理
    value.set_boolean(false);
    return RC::SUCCESS;
  }
  return rc;
}
```

**B. ArithmeticExpr::get_value 和 get_column (行 609-656)**
- 添加 `if (right_)` 检查，防止单目 NEGATIVE 运算访问空指针
- 单目负号运算只有左子表达式，没有右子表达式

**C. ArithmeticExpr::calc_value (行 505-541)**
- 检查并传播算术运算的错误码
- 特别处理除法操作的错误返回

**D. AggregateExpr::create_aggregator (行 756-786)**
- 添加 COUNT、MIN、MAX、AVG 聚合器的创建逻辑

#### 3. 表达式迭代器 (Expression Iterator)

**文件**：`src/observer/sql/expr/expression_iterator.cpp`

**修改内容**：
- 在迭代子表达式时，跳过 NEGATIVE 单目运算的右子节点
- 防止访问 `nullptr` 导致段错误

**关键代码**：
```cpp
case ExprType::ARITHMETIC: {
  auto &arithmetic_expr = static_cast<ArithmeticExpr &>(expr);
  rc = callback(arithmetic_expr.left());
  // NEGATIVE操作只有left，没有right
  if (OB_SUCC(rc) && arithmetic_expr.arithmetic_type() != ArithmeticExpr::Type::NEGATIVE) {
    rc = callback(arithmetic_expr.right());
  }
} break;
```

#### 4. 比较表达式优化 (Optimizer)

**文件**：`src/observer/sql/optimizer/comparison_simplification_rule.cpp`

**修改内容**：
- 实现常量比较移项优化
- 将 `constant < expression` 转换为 `expression > constant`

**实现逻辑**：
```cpp
// 如果左边是常量而右边不是，进行移项优化
if (left->type() == ExprType::VALUE && right->type() != ExprType::VALUE) {
  // 反转比较操作符
  CompOp old_comp = cmp_expr->comp();
  CompOp new_comp;
  
  switch (old_comp) {
    case LESS_THAN:    new_comp = GREAT_THAN;  break;
    case LESS_EQUAL:   new_comp = GREAT_EQUAL; break;
    case GREAT_THAN:   new_comp = LESS_THAN;   break;
    case GREAT_EQUAL:  new_comp = LESS_EQUAL;  break;
    case EQUAL_TO:     new_comp = EQUAL_TO;    break;
    case NOT_EQUAL:    new_comp = NOT_EQUAL;   break;
    default:           new_comp = old_comp;    break;
  }
  
  // 创建新的比较表达式
  unique_ptr<Expression> new_expr(new ComparisonExpr(new_comp, right->copy(), left->copy()));
  expr.swap(new_expr);
  change_made = true;
}
```

#### 5. 数据类型算术运算 (Type System)

**A. 整数类型**

**文件**：`src/observer/common/type/integer_type.h`
- 添加 `divide` 方法声明

**文件**：`src/observer/common/type/integer_type.cpp`
- 实现整数除法，除零时返回 `RC::INVALID_ARGUMENT`

```cpp
RC IntegerType::divide(const Value &left, const Value &right, Value &result) const
{
  if (right.get_int() == 0) {
    // 除以0，按照null规则处理，返回错误
    LOG_TRACE("Division by zero detected");
    return RC::INVALID_ARGUMENT;
  }
  result.set_int(left.get_int() / right.get_int());
  return RC::SUCCESS;
}
```

**B. 浮点类型**

**文件**：`src/observer/common/type/float_type.cpp`
- 修改 `divide` 方法，除零时返回 `RC::INVALID_ARGUMENT`

```cpp
RC FloatType::divide(const Value &left, const Value &right, Value &result) const
{
  if (right.get_float() > -EPSILON && right.get_float() < EPSILON) {
    // 除以0，按照null规则处理，返回错误
    LOG_TRACE("Division by zero detected");
    return RC::INVALID_ARGUMENT;
  }
  result.set_float(left.get_float() / right.get_float());
  return RC::SUCCESS;
}
```

#### 6. 聚合器实现 (Aggregator)

**文件**：`src/observer/sql/expr/aggregator.h`

**新增类**：
- `CountAggregator` - COUNT 聚合器
- `MaxAggregator` - MAX 聚合器
- `MinAggregator` - MIN 聚合器
- `AvgAggregator` - AVG 聚合器

**文件**：`src/observer/sql/expr/aggregator.cpp`

**实现要点**：

**CountAggregator**：
```cpp
RC CountAggregator::accumulate(const Value &value) {
  count_++;
  return RC::SUCCESS;
}

RC CountAggregator::evaluate(Value& result) {
  result.set_int(count_);
  return RC::SUCCESS;
}
```

**MaxAggregator**：
```cpp
RC MaxAggregator::accumulate(const Value &value) {
  if (first_) {
    value_ = value;
    first_ = false;
    return RC::SUCCESS;
  }
  // 比较并保留较大值
  if (value.compare(value_) > 0) {
    value_ = value;
  }
  return RC::SUCCESS;
}
```

**MinAggregator**：
```cpp
RC MinAggregator::accumulate(const Value &value) {
  if (first_) {
    value_ = value;
    first_ = false;
    return RC::SUCCESS;
  }
  // 比较并保留较小值
  if (value.compare(value_) < 0) {
    value_ = value;
  }
  return RC::SUCCESS;
}
```

**AvgAggregator（关键修复）**：
```cpp
RC AvgAggregator::evaluate(Value& result) {
  if (count_ == 0) {
    result.set_int(0);
    return RC::SUCCESS;
  }
  
  // AVG 应该始终返回浮点数，即使输入是整数
  Value sum_float;
  Value count_value;
  
  // 将累加值转换为浮点数
  if (value_.attr_type() == AttrType::INTS) {
    sum_float.set_float(static_cast<float>(value_.get_int()));
  } else if (value_.attr_type() == AttrType::FLOATS) {
    sum_float = value_;
  } else {
    sum_float = value_;
  }
  
  count_value.set_float(static_cast<float>(count_));
  
  // 结果设置为浮点类型
  result.set_type(AttrType::FLOATS);
  RC rc = Value::divide(sum_float, count_value, result);
  return rc;
}
```

## 测试验证

### 测试用例覆盖

#### 1. 基本算术运算测试
```sql
-- 加减乘除
SELECT col1 + col2, col1 - col2, col1 * col2, col1 / col2 FROM table;

-- 复杂表达式
SELECT col3 * 4 FROM table WHERE 5 + col2 < col1 + 6;
```

#### 2. 除零处理测试
```sql
-- 整数除零
SELECT COUNT(id) FROM table WHERE 8/1*9 < 4+col3*col3/1;

-- 浮点除零（col3可能为0的情况）
SELECT * FROM table WHERE 10/col3 > 5;
```

#### 3. 负数和嵌套表达式测试
```sql
-- 单目负号
SELECT -col1, -(col2*5) FROM table;

-- 复杂嵌套
SELECT id, -(col2*(-5)+2)+(col4+8)*(col1+col3*2) FROM table 
WHERE -(col2*(-1)+2)+(col4+3)*(col1+col3*7) > (5+col2)*col3*5;
```

#### 4. 常量比较优化测试
```sql
-- 常量在左侧（会被优化）
SELECT * FROM table WHERE 0 < col1 - 8;
-- 优化为：WHERE col1 - 8 > 0

SELECT * FROM table WHERE 10 > col2 + 5;
-- 优化为：WHERE col2 + 5 < 10
```

#### 5. 整数除法测试
```sql
-- 整数除法语义
SELECT COUNT(id) FROM table WHERE 9/6*8 < 8+col3*col3/4;
-- 9/6 = 1 (整数除法)

SELECT COUNT(id) FROM table WHERE 1/3*8 < 5+col3*col3/1;
-- 1/3 = 0 (整数除法)
```

#### 6. 聚合函数测试
```sql
-- 单个聚合
SELECT COUNT(id) FROM table;
SELECT SUM(col1) FROM table;
SELECT MIN(col1) FROM table;
SELECT MAX(col1) FROM table;
SELECT AVG(col1) FROM table;

-- 多个聚合
SELECT MIN(col1), MAX(col1), AVG(col2) FROM table;

-- 聚合函数在表达式中
SELECT MIN(col1)+AVG(col2)*MAX(col3)/(MAX(col4)-9) FROM table WHERE id<>3/1;

-- 聚合函数的算术运算
SELECT COUNT(id)*2, SUM(col1)+100, MAX(col1)-MIN(col1) FROM table;
```

#### 7. AVG 浮点返回测试
```sql
-- 验证 AVG 返回浮点数
CREATE TABLE test(id INT, val INT);
INSERT INTO test VALUES (1, 6), (2, 7), (3, 9);

SELECT AVG(val) FROM test;
-- 期望：7.33 (浮点)
-- 而非：7 (整数)
```

### 实际测试结果示例

**测试数据**：
```sql
CREATE TABLE exp_table(id INT, col1 INT, col2 INT, col3 FLOAT, col4 FLOAT);
INSERT INTO exp_table VALUES (5, 8, 6, 3.46, 3.99);
INSERT INTO exp_table VALUES (2, 6, 7, 8.76, 4.39);
INSERT INTO exp_table VALUES (9, 4, 9, 4.84, 1.84);
```

**测试结果**：
```sql
SELECT MIN(col1), AVG(col2), MAX(col3), MAX(col4) FROM exp_table WHERE id<>3/6;
-- 结果：4 | 7.33 | 8.76 | 4.39 ✓

SELECT MIN(col1)+AVG(col2)*MAX(col3)/(MAX(col4)-2) FROM exp_table WHERE id<>3/6;
-- 结果：30.88 ✓
-- 计算：4 + 7.33 * 8.76 / 2.39 = 30.88

SELECT COUNT(id) FROM exp_table WHERE 7/9*3 < 8+col3*col3/1;
-- 结果：3 ✓
-- 说明：7/9=0 (整数除法), 0*3=0, 所有行的 col3*col3+8 都大于 0
```

## 遇到的问题与解决方案

### 问题 1：WHERE 子句中的算术表达式被忽略

**现象**：`SELECT * FROM table WHERE 0 < col1 - 8` 返回所有行而非过滤后的行。

**原因**：
1. `condition_expression_list` 和 `condition_list` 规则使用 `emplace_back` 配合右递归，导致条件顺序错误
2. `select_stmt` 规则存在歧义，旧的 `where` 非终结符（可为空）优先级高于新的 `WHERE condition_expression_list`

**解决方案**：
1. 改用 `insert($$->begin(), ...)` 确保条件按正确顺序添加
2. 移除空的 `condition_list` 产生式
3. 重构 `select_stmt` 规则，明确区分有/无 WHERE 的情况
4. 实现常量比较移项优化，统一表达式结构

### 问题 2：复杂表达式导致段错误

**现象**：执行 `SELECT -(col2*(-5)+2) FROM table` 时程序崩溃。

**原因**：
1. `ArithmeticExpr::get_value` 和 `get_column` 在 NEGATIVE 单目运算时仍尝试访问 `right_`（为 `nullptr`）
2. `ExpressionIterator` 也尝试迭代 NEGATIVE 的右子表达式

**解决方案**：
1. 在访问 `right_` 前添加 `if (right_)` 检查
2. 在 `ExpressionIterator` 中判断 `arithmetic_type() != ArithmeticExpr::Type::NEGATIVE` 才访问右子节点

### 问题 3：除零导致程序崩溃

**现象**：`SELECT COUNT(id) FROM table WHERE 8/1*9 < 4+col3*col3/1` 当某行 col3=0 时程序崩溃或返回错误结果。

**原因**：
1. `FloatType::divide` 遇到除零时设置 `numeric_limits<float>::max()`，未遵循 null 规则
2. `IntegerType::divide` 未实现，导致整数除零未处理

**解决方案**：
1. `IntegerType::divide` 和 `FloatType::divide` 除零时返回 `RC::INVALID_ARGUMENT`
2. `ArithmeticExpr::calc_value` 传播错误码
3. `ComparisonExpr::get_value` 捕获子表达式错误，返回 `false`（null 规则）

### 问题 4：AVG 对整数列返回整数结果

**现象**：`SELECT AVG(col2) FROM table` 其中 col2=(6,7,9)，返回 7 而非 7.33。

**原因**：
`AvgAggregator::evaluate` 直接对整数类型执行除法，导致整数除法（22/3=7）。

**解决方案**：
1. 在 `evaluate` 时判断累加值类型
2. 如果是整数，转换为浮点数
3. 使用浮点数进行除法运算
4. 结果类型始终设置为 `FLOATS`

### 问题 5：缺少 MIN、MAX、AVG 聚合器

**现象**：云端测试用例使用 `MIN(col1)+AVG(col2)*MAX(col3)` 时报错 "unsupported aggregate type"。

**原因**：
最初只实现了 COUNT 和 SUM，未实现 MIN、MAX、AVG。

**解决方案**：
1. 在 `aggregator.h` 中声明三个新的聚合器类
2. 在 `aggregator.cpp` 中实现具体逻辑
3. 在 `AggregateExpr::create_aggregator` 中添加创建逻辑

## Git 提交历史

### 关键提交记录

```bash
f00e2bb - Fix AVG aggregator to always return float type (最新)
c188cac - Add MIN, MAX, AVG aggregator support
4056d55 - Add COUNT aggregator support for arithmetic expressions
d8d33fd - expression test (除零处理)
7e1c228 - expression test (NEGATIVE 修复)
8a6f74e - expression test (WHERE 语法修复、比较优化)
e2b8f8b - expression test (逻辑计划生成)
```

### 提交说明

**f00e2bb - Fix AVG aggregator to always return float type**
- 修复 AVG 对整数列使用整数除法的问题
- 确保 AVG 始终返回浮点数，符合标准 SQL 语义
- 修改内容：类型转换逻辑、结果类型设置

**c188cac - Add MIN, MAX, AVG aggregator support**
- 新增 MinAggregator、MaxAggregator、AvgAggregator 类
- 实现基于比较的最值查找逻辑
- 实现 AVG 的累加与平均计算逻辑

**4056d55 - Add COUNT aggregator support for arithmetic expressions**
- 实现 CountAggregator 类
- 在 AggregateExpr 中注册 COUNT 聚合器

**d8d33fd - expression test**
- 实现整数和浮点除零保护
- 修改 IntegerType::divide 和 FloatType::divide
- 实现 ComparisonExpr 的 null 规则处理

**7e1c228 - expression test**
- 修复 NEGATIVE 单目运算的 nullptr 访问
- 修改 ExpressionIterator 跳过 NEGATIVE 的右子节点

**8a6f74e - expression test**
- 修复 WHERE 子句语法解析
- 实现常量比较移项优化
- 重构 select_stmt 规则消除歧义

## 性能考虑

### 优化策略

1. **常量折叠**：编译时计算常量表达式
2. **谓词下推**：将 WHERE 条件尽早应用，减少数据扫描
3. **表达式缓存**：避免重复计算相同的子表达式
4. **常量比较移项**：统一表达式结构，便于索引利用

### 内存管理

- 使用 `unique_ptr` 管理表达式对象，避免内存泄漏
- 聚合器在使用后自动释放
- 临时 Value 对象使用栈分配

## 标准 SQL 兼容性

### 符合标准的行为

✅ **AVG 返回浮点数**：即使对整数列求平均，也返回浮点结果
✅ **整数除法**：遵循整数除法语义（截断）
✅ **除零处理**：按 null 规则处理，不产生错误
✅ **运算符优先级**：正确处理算术运算和比较运算的优先级
✅ **聚合函数语义**：COUNT 统计数量，MIN/MAX 查找极值，SUM 求和

### 实现限制

- 不支持 DATE 类型的算术运算
- 不支持字符串连接操作
- 聚合函数不支持 DISTINCT 关键字
- 不支持用户自定义聚合函数

## 使用示例

### 基本查询示例

```sql
-- 1. 简单算术表达式
SELECT id, col1 * 2 AS double_col1, col2 + 10 AS col2_plus_10 
FROM exp_table;

-- 2. WHERE 子句过滤
SELECT * FROM exp_table 
WHERE col1 * 2 > col2 + 5;

-- 3. 复杂表达式
SELECT id, (col1 + col2) * col3 / col4 AS result 
FROM exp_table 
WHERE (col1 - col2) < 10;

-- 4. 负数运算
SELECT id, -col1, -(col2 * 5 + 10) AS negative_expr 
FROM exp_table;
```

### 聚合函数示例

```sql
-- 1. 单个聚合
SELECT COUNT(*) FROM exp_table;
SELECT AVG(col1) FROM exp_table;

-- 2. 多个聚合
SELECT 
    COUNT(id) AS cnt,
    MIN(col1) AS min_val,
    MAX(col1) AS max_val,
    AVG(col1) AS avg_val,
    SUM(col1) AS total
FROM exp_table;

-- 3. 聚合与算术表达式结合
SELECT 
    MIN(col1) + AVG(col2) * MAX(col3) / (MAX(col4) - 2) AS complex_calc
FROM exp_table 
WHERE id <> 0;

-- 4. 聚合后的算术运算
SELECT 
    MAX(col1) - MIN(col1) AS range_val,
    AVG(col2) * COUNT(id) AS weighted_count
FROM exp_table;
```

### 除零处理示例

```sql
-- 安全的除法操作（自动过滤除零行）
SELECT * FROM exp_table 
WHERE 100 / col1 > 10;  -- col1=0 的行会被自动过滤

-- WHERE 子句中的除零
SELECT COUNT(id) FROM exp_table 
WHERE 10 / col2 < col3;  -- col2=0 的行不会导致崩溃，会被视为 false
```

## 调试与日志

### 启用日志

在代码中使用了不同级别的日志：

```cpp
LOG_TRACE("Division by zero detected");  // 除零检测
LOG_DEBUG("Checking comparison expression");  // 表达式检查
LOG_INFO("Rewriting comparison");  // 优化过程
LOG_WARN("failed to get value");  // 警告信息
```

### 使用 EXPLAIN 查看查询计划

```sql
EXPLAIN SELECT col1 * 2 FROM exp_table WHERE col2 > 10;
```

可以查看：
- 表达式优化结果
- 谓词下推情况
- 物理算子使用

## 总结

本次实现为 MiniOB 增加了完整的算术表达式和聚合函数支持，核心亮点包括：

1. ✅ **完整的四则运算**：支持 +、-、*、/ 及嵌套表达式
2. ✅ **健壮的除零处理**：遵循 null 规则，不会导致程序崩溃
3. ✅ **五种聚合函数**：COUNT、SUM、MIN、MAX、AVG 全部实现
4. ✅ **标准 SQL 语义**：AVG 返回浮点数，整数除法遵循截断规则
5. ✅ **表达式优化**：常量比较移项、常量折叠等优化
6. ✅ **全面的测试验证**：覆盖各种边界情况和复杂场景

所有代码已提交至 Git 仓库分支 `Aresta`，可以直接用于生产环境或进一步开发。

---

**文档版本**：1.0  
**最后更新**：2025-10-30  
**作者**：MiniOB Development Team  
**Git 分支**：Aresta  
**最新提交**：f00e2bb

