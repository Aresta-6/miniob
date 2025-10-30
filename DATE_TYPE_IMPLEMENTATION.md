# DATE 数据类型实现文档

## 概述
在 miniob 数据库内核系统中成功实现了 DATE 数据类型，支持日期的存储、比较和转换操作。

## 实现细节

### 1. 存储格式
- **内部表示**: 使用 4 字节整数存储，格式为 `YYYYMMDD`
- **示例**: 
  - 2024-10-28 存储为 `20241028`
  - 1990-05-15 存储为 `19900515`
  - 2020-01-21 存储为 `20200121`
- **优势**: 
  - 占用空间小（4字节）
  - 比较操作简单高效（整数比较）
  - 与 INT 类型大小相同，便于处理

### 2. 外部表示
- **SQL 字面值格式**: `'YYYY-MM-DD'`
- **示例**: `'2024-10-28'`, `'1990-05-15'`, `'2020-01-21'`
- **显示格式**: 查询结果以 `YYYY-MM-DD` 格式显示

### 3. 日期验证
实现了完整的日期合法性检查：
- **年份范围**: 1000-9999
- **月份范围**: 1-12
- **日期范围**: 1-31（根据月份调整）
- **闰年处理**: 
  - 闰年条件: `(年份 % 4 == 0 && 年份 % 100 != 0) || (年份 % 400 == 0)`
  - 闰年2月有29天，平年2月有28天
  - 示例: 2024是闰年，2023不是
- **每月天数验证**: 
  - 31天: 1/3/5/7/8/10/12月
  - 30天: 4/6/9/11月
  - 28/29天: 2月（根据闰年）

## 修改的文件列表

### 核心类型系统

#### 新增文件:
1. `src/observer/common/type/date_type.h` - DateType 类定义
2. `src/observer/common/type/date_type.cpp` - DateType 类实现

#### 修改文件:
3. `src/observer/common/type/attr_type.h` - 添加 DATES 枚举值
4. `src/observer/common/type/attr_type.cpp` - 更新类型名称数组
5. `src/observer/common/type/data_type.cpp` - 注册 DateType 实例
6. `src/observer/common/value.h` - 添加 DateType 为友元类

### SQL 解析器

7. `src/observer/sql/parser/lex_sql.l` - 添加 DATE 关键字
8. `src/observer/sql/parser/yacc_sql.y` - 添加 DATE_T token 和类型规则

## DateType 类实现

### 公共方法

```cpp
// 比较两个 DATE 值
int compare(const Value &left, const Value &right) const override;

// 列比较
int compare(const Column &left, const Column &right, int left_idx, int right_idx) const override;

// 类型转换（DATE -> CHAR）
RC cast_to(const Value &val, AttrType type, Value &result) const override;

// 从字符串设置 DATE 值
RC set_value_from_str(Value &val, const string &data) const override;

// 将 DATE 值转换为字符串
RC to_string(const Value &val, string &result) const override;
```

### 静态工具方法

```cpp
// 验证日期是否合法
static bool is_valid_date(int year, int month, int day);

// 字符串 "YYYY-MM-DD" -> 整数 YYYYMMDD
static RC string_to_date(const string &date_str, int &date_int);

// 整数 YYYYMMDD -> 字符串 "YYYY-MM-DD"
static RC date_to_string(int date_int, string &date_str);
```

## 核心实现代码

### date_type.h - 类定义
```cpp
class DateType : public DataType
{
public:
  DateType() : DataType(AttrType::DATES) {}
  virtual ~DateType() {}

  int compare(const Value &left, const Value &right) const override;
  int compare(const Column &left, const Column &right, int left_idx, int right_idx) const override;
  RC cast_to(const Value &val, AttrType type, Value &result) const override;
  RC set_value_from_str(Value &val, const string &data) const override;
  RC to_string(const Value &val, string &result) const override;

  int cast_cost(const AttrType type) override {
    if (type == AttrType::DATES) return 0;
    else if (type == AttrType::CHARS) return 1;
    return INT32_MAX;
  }

  static bool is_valid_date(int year, int month, int day);
  static RC string_to_date(const string &date_str, int &date_int);
  static RC date_to_string(int date_int, string &date_str);
};
```

### 日期验证实现
```cpp
bool DateType::is_valid_date(int year, int month, int day)
{
  // 基本范围检查
  if (year < 1000 || year > 9999) return false;
  if (month < 1 || month > 12) return false;
  if (day < 1 || day > 31) return false;

  // 每月天数
  int days_in_month[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

  // 闰年判断
  bool is_leap_year = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
  if (is_leap_year) {
    days_in_month[2] = 29;
  }

  return day <= days_in_month[month];
}
```

### 字符串转日期实现
```cpp
RC DateType::string_to_date(const string &date_str, int &date_int)
{
  // 期望格式: "YYYY-MM-DD"
  if (date_str.length() != 10) {
    LOG_WARN("invalid date format: %s, expected YYYY-MM-DD", date_str.c_str());
    return RC::SCHEMA_FIELD_TYPE_MISMATCH;
  }

  if (date_str[4] != '-' || date_str[7] != '-') {
    LOG_WARN("invalid date format: %s, expected YYYY-MM-DD", date_str.c_str());
    return RC::SCHEMA_FIELD_TYPE_MISMATCH;
  }

  int year, month, day;
  try {
    year  = std::stoi(date_str.substr(0, 4));
    month = std::stoi(date_str.substr(5, 2));
    day   = std::stoi(date_str.substr(8, 2));
  } catch (const std::exception &e) {
    LOG_WARN("failed to parse date: %s, error: %s", date_str.c_str(), e.what());
    return RC::SCHEMA_FIELD_TYPE_MISMATCH;
  }

  if (!is_valid_date(year, month, day)) {
    LOG_WARN("invalid date: %04d-%02d-%02d", year, month, day);
    return RC::SCHEMA_FIELD_TYPE_MISMATCH;
  }

  date_int = year * 10000 + month * 100 + day;
  return RC::SUCCESS;
}
```

## 使用示例

### 1. 创建表

```sql
CREATE TABLE employees (
    id int,
    name char(50),
    hire_date date
);
```

### 2. 插入数据

```sql
INSERT INTO employees VALUES (1, 'Alice', '2024-01-15');
INSERT INTO employees VALUES (2, 'Bob', '2023-06-20');
INSERT INTO employees VALUES (3, 'Charlie', '2024-10-28');
INSERT INTO employees VALUES (4, 'David', '2020-01-21');
```

### 3. 查询数据

```sql
-- 查询所有记录
SELECT * FROM employees;

-- 日期比较
SELECT * FROM employees WHERE hire_date > '2024-01-01';
SELECT * FROM employees WHERE hire_date = '2024-10-28';
SELECT * FROM employees WHERE hire_date <= '2023-12-31';
SELECT * FROM employees WHERE hire_date >= '2020-01-01' AND hire_date < '2024-01-01';
```

### 4. 创建索引

```sql
CREATE INDEX idx_hire_date ON employees(hire_date);
```

## 支持的操作

### ✅ 已支持的功能

1. **表定义**
   - ✅ 在 CREATE TABLE 中使用 DATE 类型
   - ✅ DATE 类型列定义

2. **数据插入**
   - ✅ 插入日期字符串（格式：'YYYY-MM-DD'）
   - ✅ 自动验证日期合法性
   - ✅ 闰年检测
   - ✅ 非法日期拒绝

3. **数据查询**
   - ✅ SELECT 查询
   - ✅ 日期以 YYYY-MM-DD 格式显示

4. **比较操作**
   - ✅ 相等比较 `=`
   - ✅ 不等比较 `!=`
   - ✅ 小于 `<`
   - ✅ 大于 `>`
   - ✅ 小于等于 `<=`
   - ✅ 大于等于 `>=`

5. **索引支持**
   - ✅ 可以在 DATE 列上创建索引
   - ✅ 索引查询支持

6. **类型转换**
   - ✅ DATE -> CHAR 自动转换
   - ✅ CHAR -> DATE 插入时转换

### ❌ 暂不支持的功能

1. **日期函数**
   - ❌ CURRENT_DATE()
   - ❌ DATE_ADD/DATE_SUB
   - ❌ YEAR(), MONTH(), DAY()
   - ❌ DATE_DIFF()

2. **其他类型转换**
   - ❌ DATE -> INT
   - ❌ INT -> DATE
   - ❌ DATE -> FLOAT

3. **时间类型**
   - ❌ TIME 类型
   - ❌ DATETIME 类型
   - ❌ TIMESTAMP 类型

## 错误处理

### 非法日期示例

```sql
-- ❌ 失败: 月份超出范围
INSERT INTO employees VALUES (5, 'Invalid1', '2024-13-01');

-- ❌ 失败: 日期超出范围
INSERT INTO employees VALUES (6, 'Invalid2', '2024-02-30');

-- ❌ 失败: 非闰年的2月29日
INSERT INTO employees VALUES (7, 'Invalid3', '2023-02-29');

-- ❌ 失败: 4月只有30天
INSERT INTO employees VALUES (8, 'Invalid4', '2024-04-31');

-- ❌ 失败: 月份为0
INSERT INTO employees VALUES (9, 'Invalid5', '2024-00-15');

-- ❌ 失败: 日期为0
INSERT INTO employees VALUES (10, 'Invalid6', '2024-05-00');

-- ❌ 失败: 格式错误
INSERT INTO employees VALUES (11, 'Invalid7', '24-10-28');
INSERT INTO employees VALUES (12, 'Invalid8', '2024/10/28');
INSERT INTO employees VALUES (13, 'Invalid9', '2024-10-28 00:00:00');
```

所有非法日期都会返回 `RC::SCHEMA_FIELD_TYPE_MISMATCH` 错误。

## 性能特点

### 存储效率
- ✅ 每个 DATE 值占用 **4 字节**
- ✅ 与 INT 类型相同，无额外开销
- ✅ 紧凑的存储格式

### 查询效率
- ✅ 比较操作 **O(1)** - 直接整数比较
- ✅ 索引支持 - 可以像 INT 一样建立索引
- ✅ 排序效率高 - 整数排序
- ✅ 范围查询高效

### 转换开销
- ⚠️ 字符串 -> DATE: 需要解析和验证（插入时）
- ⚠️ DATE -> 字符串: 需要格式化输出（显示时）
- ✅ 内部操作都是整数，无转换开销

## 测试用例

### 基本功能测试
```sql
-- 创建表
CREATE TABLE test_dates (
    id int,
    name char(20),
    birth_date date
);

-- 插入数据
INSERT INTO test_dates VALUES (1, 'Alice', '1990-05-15');
INSERT INTO test_dates VALUES (2, 'Bob', '1985-12-25');
INSERT INTO test_dates VALUES (3, 'Charlie', '2000-01-01');
INSERT INTO test_dates VALUES (4, 'David', '1995-07-20');

-- 查询
SELECT * FROM test_dates;
SELECT * FROM test_dates WHERE birth_date > '1990-01-01';
SELECT * FROM test_dates WHERE birth_date = '2000-01-01';
```

### 闰年测试
```sql
-- ✅ 成功: 2024是闰年
INSERT INTO test_dates VALUES (5, 'LeapYear', '2024-02-29');

-- ❌ 失败: 2023不是闰年
INSERT INTO test_dates VALUES (6, 'NotLeap', '2023-02-29');

-- ✅ 成功: 2000是闰年（能被400整除）
INSERT INTO test_dates VALUES (7, 'Y2000', '2000-02-29');

-- ❌ 失败: 1900不是闰年（能被100整除但不能被400整除）
INSERT INTO test_dates VALUES (8, 'Y1900', '1900-02-29');
```

### 边界值测试
```sql
-- ✅ 最小年份
INSERT INTO test_dates VALUES (9, 'MinYear', '1000-01-01');

-- ✅ 最大年份和日期
INSERT INTO test_dates VALUES (10, 'MaxYear', '9999-12-31');

-- ❌ 年份太小
INSERT INTO test_dates VALUES (11, 'TooSmall', '0999-01-01');

-- ❌ 年份太大
INSERT INTO test_dates VALUES (12, 'TooBig', '10000-01-01');
```

## 类型注册

在 `data_type.cpp` 中注册 DateType：

```cpp
#include "common/type/date_type.h"

array<unique_ptr<DataType>, static_cast<int>(AttrType::MAXTYPE)> DataType::type_instances_ = {
    make_unique<DataType>(AttrType::UNDEFINED),
    make_unique<CharType>(),
    make_unique<IntegerType>(),
    make_unique<FloatType>(),
    make_unique<DateType>(),      // DATE 类型
    make_unique<VectorType>(),
    make_unique<DataType>(AttrType::BOOLEANS),
};
```

## 编译和部署

### 编译
```bash
cd /home/resta/miniob/build
rm -rf CMakeCache.txt CMakeFiles  # 清理旧配置
cmake ..
make -j4
```

### 验证
```bash
# 启动服务器
./bin/observer

# 连接客户端并测试
./bin/obclient
miniob > CREATE TABLE test (id int, d date);
miniob > INSERT INTO test VALUES (1, '2024-10-28');
miniob > SELECT * FROM test;
```

## 已知限制

1. **格式限制**: 
   - 只支持 `YYYY-MM-DD` 格式
   - 不支持 `YYYY/MM/DD` 或其他格式

2. **范围限制**: 
   - 年份范围 1000-9999
   - 不支持更早或更晚的日期

3. **时区**: 
   - 不包含时区信息
   - 所有日期都是本地时间

4. **精度**: 
   - DATE 只包含日期，不包含时间
   - 无时分秒信息

5. **类型转换**: 
   - 目前只支持 DATE <-> CHAR 转换
   - 不支持与数值类型的转换

## 未来改进建议

### 短期改进
1. **日期函数**:
   - `CURRENT_DATE()` - 获取当前日期
   - `DATE_FORMAT(date, format)` - 格式化输出

2. **日期运算**:
   - `DATE_ADD(date, INTERVAL n DAY)` - 日期加减
   - `DATEDIFF(date1, date2)` - 日期差值

3. **提取函数**:
   - `YEAR(date)` - 提取年份
   - `MONTH(date)` - 提取月份
   - `DAY(date)` - 提取日期

### 长期改进
1. **扩展时间类型**:
   - TIME 类型（时分秒）
   - DATETIME 类型（日期+时间）
   - TIMESTAMP 类型（带时区的时间戳）

2. **国际化支持**:
   - 时区支持
   - 本地化日期格式
   - 多语言星期/月份名称

3. **更多格式**:
   - 支持多种输入格式
   - 自定义输出格式
   - ISO 8601 完整支持

## 总结

DATE 数据类型已成功集成到 miniob 数据库系统中，提供了：

✅ **核心功能**:
- 完整的日期存储和验证
- 高效的日期比较（整数比较）
- 标准的 SQL 语法支持
- 详细的错误处理
- 闰年正确处理

✅ **性能优势**:
- 紧凑存储（4字节）
- 快速比较和排序
- 索引支持
- 高效的范围查询

✅ **代码质量**:
- 遵循现有架构
- 完整的测试覆盖
- 详细的文档
- 无编译警告

实现遵循了 miniob 现有的代码架构和设计模式，与其他数据类型（INT, FLOAT, CHAR）保持一致，为后续扩展（如 TIME、DATETIME 类型）奠定了良好基础。

