# LIKE 功能实现总结

## 概述
已成功为 MiniOB 数据库内核系统实现 LIKE 操作符功能，用于在 WHERE 子句中进行字符串模式匹配。

## 实现内容

### 1. 词法和语法分析
- **文件**: `src/observer/sql/parser/lex_sql.l`
  - 添加了 LIKE 关键字的词法规则

- **文件**: `src/observer/sql/parser/yacc_sql.y`
  - 在 token 定义中添加了 LIKE
  - 在 comp_op 语法规则中添加了 LIKE 操作符支持

### 2. 数据结构定义
- **文件**: `src/observer/sql/parser/parse_defs.h`
  - 在 `CompOp` 枚举中添加了 `LIKE_OP` 操作符

### 3. 核心匹配逻辑
- **文件**: `src/observer/sql/expr/expression.cpp`
  - 实现了 `like_match()` 函数，支持以下通配符：
    - `%`: 匹配零个或多个任意字符（单引号 ' 除外）
    - `_`: 匹配一个任意字符（单引号 ' 除外）
  
  - 在 `ComparisonExpr::compare_value()` 方法中添加了 LIKE 操作符的处理逻辑
    - 验证操作数类型（必须都是 CHAR 类型）
    - 调用模式匹配函数进行匹配

## 功能特性

### 支持的模式
1. **前缀匹配**: `'app%'` - 匹配以 "app" 开头的字符串
2. **后缀匹配**: `'%an'` - 匹配以 "an" 结尾的字符串
3. **包含匹配**: `'%an%'` - 匹配包含 "an" 的字符串
4. **位置匹配**: `'b___'` - 匹配 "b" 开头后面跟3个字符的字符串
5. **复合模式**: `'_a%'` - 匹配第二个字符是 'a' 的字符串
6. **精确匹配**: `'cat'` - 等同于 = 操作符
7. **全匹配**: `'%'` - 匹配所有记录
8. **否定匹配**: `NOT LIKE` - 匹配所有不符合 LIKE 模式的记录

### 测试用例
```sql
CREATE TABLE test_like (id INT, name CHAR(50));
INSERT INTO test_like VALUES (1, 'apple');
INSERT INTO test_like VALUES (2, 'application');
INSERT INTO test_like VALUES (3, 'banana');
INSERT INTO test_like VALUES (4, 'band');
INSERT INTO test_like VALUES (5, 'cat');

-- 前缀匹配：匹配 'apple' 和 'application'
SELECT * FROM test_like WHERE name LIKE 'app%';

-- 包含匹配：匹配 'banana' 和 'band'
SELECT * FROM test_like WHERE name LIKE '%an%';

-- 固定长度匹配：匹配 'band'
SELECT * FROM test_like WHERE name LIKE 'b___';

-- 位置匹配：匹配 'cat', 'cat', 'band', 'banana'
SELECT * FROM test_like WHERE name LIKE '_a%';

-- 精确匹配：仅匹配 'cat'
SELECT * FROM test_like WHERE name LIKE 'cat';

-- 否定匹配：匹配所有不包含字母 'a' 的字符串
SELECT * FROM test_like WHERE name NOT LIKE '%a%';
```

## 技术细节

### 匹配算法
- 采用递归算法实现模式匹配
- 对 `