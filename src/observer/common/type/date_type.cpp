/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#include "common/lang/comparator.h"
#include "common/lang/sstream.h"
#include "common/log/log.h"
#include "common/type/date_type.h"
#include "common/value.h"
#include "storage/common/column.h"

int DateType::compare(const Value &left, const Value &right) const
{
  ASSERT(left.attr_type() == AttrType::DATES, "left type is not date");
  ASSERT(right.attr_type() == AttrType::DATES, "right type is not date");
  return common::compare_int((void *)&left.value_.int_value_, (void *)&right.value_.int_value_);
}

int DateType::compare(const Column &left, const Column &right, int left_idx, int right_idx) const
{
  ASSERT(left.attr_type() == AttrType::DATES, "left type is not date");
  ASSERT(right.attr_type() == AttrType::DATES, "right type is not date");
  return common::compare_int((void *)&((int*)left.data())[left_idx],
      (void *)&((int*)right.data())[right_idx]);
}

RC DateType::cast_to(const Value &val, AttrType type, Value &result) const
{
  switch (type) {
  case AttrType::CHARS: {
    string date_str;
    RC rc = date_to_string(val.value_.int_value_, date_str);
    if (rc != RC::SUCCESS) {
      return rc;
    }
    result.set_string(date_str.c_str());
    return RC::SUCCESS;
  }
  default:
    LOG_WARN("unsupported cast from date to type %d", type);
    return RC::SCHEMA_FIELD_TYPE_MISMATCH;
  }
}

bool DateType::is_valid_date(int year, int month, int day)
{
  // 基本范围检查
  if (year < 1000 || year > 9999) {
    return false;
  }
  if (month < 1 || month > 12) {
    return false;
  }
  if (day < 1 || day > 31) {
    return false;
  }

  // 每月天数
  int days_in_month[] = {0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

  // 闰年判断
  bool is_leap_year = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
  if (is_leap_year) {
    days_in_month[2] = 29;
  }

  return day <= days_in_month[month];
}

RC DateType::string_to_date(const string &date_str, int &date_int)
{
<<<<<<< HEAD
  // 支持格式: "YYYY-MM-DD" 或 "YYYY-M-D" 等灵活格式
  if (date_str.length() < 8 || date_str.length() > 10) {
    LOG_WARN("invalid date format: %s, expected YYYY-MM-DD or YYYY-M-D", date_str.c_str());
    return RC::SCHEMA_FIELD_TYPE_MISMATCH;
  }

  // 查找第一个和第二个 '-'
  size_t first_dash = date_str.find('-');
  if (first_dash == string::npos || first_dash != 4) {
    LOG_WARN("invalid date format: %s, year must be 4 digits", date_str.c_str());
    return RC::SCHEMA_FIELD_TYPE_MISMATCH;
  }

  size_t second_dash = date_str.find('-', first_dash + 1);
  if (second_dash == string::npos) {
=======
  // 期望格式: "YYYY-MM-DD"
  if (date_str.length() != 10) {
    LOG_WARN("invalid date format: %s, expected YYYY-MM-DD", date_str.c_str());
    return RC::SCHEMA_FIELD_TYPE_MISMATCH;
  }

  if (date_str[4] != '-' || date_str[7] != '-') {
>>>>>>> 5e5ce29959f5f579fe5a09d30ba9bc8536cd774d
    LOG_WARN("invalid date format: %s, expected YYYY-MM-DD", date_str.c_str());
    return RC::SCHEMA_FIELD_TYPE_MISMATCH;
  }

  int year, month, day;
  try {
<<<<<<< HEAD
    year  = std::stoi(date_str.substr(0, first_dash));
    month = std::stoi(date_str.substr(first_dash + 1, second_dash - first_dash - 1));
    day   = std::stoi(date_str.substr(second_dash + 1));
=======
    year  = std::stoi(date_str.substr(0, 4));
    month = std::stoi(date_str.substr(5, 2));
    day   = std::stoi(date_str.substr(8, 2));
>>>>>>> 5e5ce29959f5f579fe5a09d30ba9bc8536cd774d
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

RC DateType::date_to_string(int date_int, string &date_str)
{
  int year  = date_int / 10000;
  int month = (date_int % 10000) / 100;
  int day   = date_int % 100;

  if (!is_valid_date(year, month, day)) {
    LOG_WARN("invalid date int: %d", date_int);
    return RC::SCHEMA_FIELD_TYPE_MISMATCH;
  }

  char buffer[16];  // "YYYY-MM-DD\0" with extra space to avoid warnings
  snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", year, month, day);
  date_str = buffer;
  return RC::SUCCESS;
}

RC DateType::set_value_from_str(Value &val, const string &data) const
{
  int date_int;
  RC rc = string_to_date(data, date_int);
  if (rc != RC::SUCCESS) {
    return rc;
  }
  val.set_int(date_int);
  return RC::SUCCESS;
}

RC DateType::to_string(const Value &val, string &result) const
{
  return date_to_string(val.value_.int_value_, result);
}

