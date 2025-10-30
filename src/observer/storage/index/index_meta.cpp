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
// Created by Wangyunlai.wyl on 2021/5/18.
//

#include "storage/index/index_meta.h"
#include "common/lang/string.h"
#include "common/log/log.h"
#include "storage/field/field_meta.h"
#include "storage/table/table_meta.h"
#include "json/json.h"

const static Json::StaticString FIELD_NAME("name");
const static Json::StaticString FIELD_FIELD_NAME("field_name");
const static Json::StaticString FIELD_FIELD_NAMES("field_names");

RC IndexMeta::init(const char *name, const FieldMeta &field)
{
  if (common::is_blank(name)) {
    LOG_ERROR("Failed to init index, name is empty.");
    return RC::INVALID_ARGUMENT;
  }

  name_ = name;
  fields_.clear();
  fields_.push_back(field.name());
  return RC::SUCCESS;
}

RC IndexMeta::init(const char *name, const vector<FieldMeta> &fields)
{
  if (common::is_blank(name)) {
    LOG_ERROR("Failed to init index, name is empty.");
    return RC::INVALID_ARGUMENT;
  }

  if (fields.empty()) {
    LOG_ERROR("Failed to init index, fields is empty.");
    return RC::INVALID_ARGUMENT;
  }

  name_ = name;
  fields_.clear();
  for (const FieldMeta &field : fields) {
    fields_.push_back(field.name());
  }
  return RC::SUCCESS;
}

void IndexMeta::to_json(Json::Value &json_value) const
{
  json_value[FIELD_NAME] = name_;
  
  // 保存多字段信息
  Json::Value fields_value(Json::arrayValue);
  for (const string &field : fields_) {
    fields_value.append(field);
  }
  json_value[FIELD_FIELD_NAMES] = fields_value;
  
  // 为了向后兼容，也保存第一个字段到field_name
  if (!fields_.empty()) {
    json_value[FIELD_FIELD_NAME] = fields_[0];
  }
}

RC IndexMeta::from_json(const TableMeta &table, const Json::Value &json_value, IndexMeta &index)
{
  const Json::Value &name_value = json_value[FIELD_NAME];
  if (!name_value.isString()) {
    LOG_ERROR("Index name is not a string. json value=%s", name_value.toStyledString().c_str());
    return RC::INTERNAL;
  }

  // 优先读取多字段格式
  if (json_value.isMember(FIELD_FIELD_NAMES.c_str())) {
    const Json::Value &fields_value = json_value[FIELD_FIELD_NAMES];
    if (!fields_value.isArray()) {
      LOG_ERROR("Field names of index [%s] is not an array. json value=%s",
          name_value.asCString(), fields_value.toStyledString().c_str());
      return RC::INTERNAL;
    }

    vector<FieldMeta> fields;
    for (uint32_t i = 0; i < fields_value.size(); i++) {
      const Json::Value &field_name_value = fields_value[i];
      if (!field_name_value.isString()) {
        LOG_ERROR("Field name of index [%s] at position %d is not a string.",
            name_value.asCString(), i);
        return RC::INTERNAL;
      }

      const FieldMeta *field = table.field(field_name_value.asCString());
      if (nullptr == field) {
        LOG_ERROR("Deserialize index [%s]: no such field: %s", 
            name_value.asCString(), field_name_value.asCString());
        return RC::SCHEMA_FIELD_MISSING;
      }
      fields.push_back(*field);
    }

    return index.init(name_value.asCString(), fields);
  } else {
    // 向后兼容：读取单字段格式
    const Json::Value &field_value = json_value[FIELD_FIELD_NAME];
    if (!field_value.isString()) {
      LOG_ERROR("Field name of index [%s] is not a string. json value=%s",
          name_value.asCString(), field_value.toStyledString().c_str());
      return RC::INTERNAL;
    }

    const FieldMeta *field = table.field(field_value.asCString());
    if (nullptr == field) {
      LOG_ERROR("Deserialize index [%s]: no such field: %s", 
          name_value.asCString(), field_value.asCString());
      return RC::SCHEMA_FIELD_MISSING;
    }

    return index.init(name_value.asCString(), *field);
  }
}

const char *IndexMeta::name() const { return name_.c_str(); }

const char *IndexMeta::field() const { 
  return fields_.empty() ? "" : fields_[0].c_str(); 
}

const vector<string> &IndexMeta::fields() const { 
  return fields_; 
}

int IndexMeta::field_count() const { 
  return fields_.size(); 
}

void IndexMeta::desc(ostream &os) const { 
  os << "index name=" << name_ << ", fields=(";
  for (size_t i = 0; i < fields_.size(); i++) {
    if (i > 0) os << ", ";
    os << fields_[i];
  }
  os << ")";
}