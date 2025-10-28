/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

#pragma once

#include "common/type/data_type.h"

/**
 * @brief 日期类型
 * @ingroup DataType
 * @details 日期类型以整数形式存储，格式为 YYYYMMDD
 * 例如：2024-10-28 存储为 20241028
 */
class DateType : public DataType
{
public:
  DateType() : DataType(AttrType::DATES) {}
  virtual ~DateType() {}

  int compare(const Value &left, const Value &right) const override;
  int compare(const Column &left, const Column &right, int left_idx, int right_idx) const override;

  RC cast_to(const Value &val, AttrType type, Value &result) const override;

  int cast_cost(const AttrType type) override
  {
    if (type == AttrType::DATES) {
      return 0;
    } else if (type == AttrType::CHARS) {
      return 1;
    }
    return INT32_MAX;
  }

  RC set_value_from_str(Value &val, const string &data) const override;

  RC to_string(const Value &val, string &result) const override;

public:
  /**
   * @brief 验证日期是否合法
   * @param year 年份
   * @param month 月份 (1-12)
   * @param day 日期 (1-31)
   * @return true 如果日期合法
   */
  static bool is_valid_date(int year, int month, int day);

  /**
   * @brief 将日期字符串转换为整数 (YYYYMMDD)
   * @param date_str 日期字符串，格式为 "YYYY-MM-DD"
   * @param date_int 输出的日期整数
   * @return RC::SUCCESS 如果转换成功
   */
  static RC string_to_date(const string &date_str, int &date_int);

  /**
   * @brief 将日期整数 (YYYYMMDD) 转换为字符串
   * @param date_int 日期整数
   * @param date_str 输出的日期字符串，格式为 "YYYY-MM-DD"
   * @return RC::SUCCESS 如果转换成功
   */
  static RC date_to_string(int date_int, string &date_str);
};


