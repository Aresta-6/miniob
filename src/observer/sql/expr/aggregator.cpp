/* Copyright (c) 2021 OceanBase and/or its affiliates. All rights reserved.
miniob is licensed under Mulan PSL v2.
You can use this software according to the terms and conditions of the Mulan PSL v2.
You may obtain a copy of Mulan PSL v2 at:
         http://license.coscl.org.cn/MulanPSL2
THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
See the Mulan PSL v2 for more details. */

//
// Created by Wangyunlai on 2024/05/29.
//

#include "sql/expr/aggregator.h"
#include "common/log/log.h"

RC SumAggregator::accumulate(const Value &value)
{
  if (value_.attr_type() == AttrType::UNDEFINED) {
    value_ = value;
    return RC::SUCCESS;
  }
  
  ASSERT(value.attr_type() == value_.attr_type(), "type mismatch. value type: %s, value_.type: %s", 
        attr_type_to_string(value.attr_type()), attr_type_to_string(value_.attr_type()));
  
  Value::add(value, value_, value_);
  return RC::SUCCESS;
}

RC SumAggregator::evaluate(Value& result)
{
  result = value_;
  return RC::SUCCESS;
}

RC CountAggregator::accumulate(const Value &value)
{
  // COUNT统计所有非空值，包括异常值（按null规则处理的情况）
  // 这里简化处理，只要accumulate被调用就计数
  count_++;
  return RC::SUCCESS;
}

RC CountAggregator::evaluate(Value& result)
{
  result.set_int(count_);
  return RC::SUCCESS;
}

RC MaxAggregator::accumulate(const Value &value)
{
  if (first_) {
    value_ = value;
    first_ = false;
    return RC::SUCCESS;
  }
  
  ASSERT(value.attr_type() == value_.attr_type(), "type mismatch. value type: %s, value_.type: %s", 
        attr_type_to_string(value.attr_type()), attr_type_to_string(value_.attr_type()));
  
  // 比较并保留较大值
  if (value.compare(value_) > 0) {
    value_ = value;
  }
  return RC::SUCCESS;
}

RC MaxAggregator::evaluate(Value& result)
{
  result = value_;
  return RC::SUCCESS;
}

RC MinAggregator::accumulate(const Value &value)
{
  if (first_) {
    value_ = value;
    first_ = false;
    return RC::SUCCESS;
  }
  
  ASSERT(value.attr_type() == value_.attr_type(), "type mismatch. value type: %s, value_.type: %s", 
        attr_type_to_string(value.attr_type()), attr_type_to_string(value_.attr_type()));
  
  // 比较并保留较小值
  if (value.compare(value_) < 0) {
    value_ = value;
  }
  return RC::SUCCESS;
}

RC MinAggregator::evaluate(Value& result)
{
  result = value_;
  return RC::SUCCESS;
}

RC AvgAggregator::accumulate(const Value &value)
{
  if (value_.attr_type() == AttrType::UNDEFINED) {
    value_ = value;
    count_ = 1;
    return RC::SUCCESS;
  }
  
  ASSERT(value.attr_type() == value_.attr_type(), "type mismatch. value type: %s, value_.type: %s", 
        attr_type_to_string(value.attr_type()), attr_type_to_string(value_.attr_type()));
  
  Value::add(value, value_, value_);
  count_++;
  return RC::SUCCESS;
}

RC AvgAggregator::evaluate(Value& result)
{
  if (count_ == 0) {
    result.set_int(0);
    return RC::SUCCESS;
  }
  
  // 计算平均值
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
