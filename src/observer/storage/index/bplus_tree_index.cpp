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
// Created by wangyunlai.wyl on 2021/5/19.
//

#include "storage/index/bplus_tree_index.h"
#include "common/log/log.h"
#include "storage/table/table.h"
#include "storage/db/db.h"

BplusTreeIndex::~BplusTreeIndex() noexcept { close(); }

RC BplusTreeIndex::create(Table *table, const char *file_name, const IndexMeta &index_meta, const FieldMeta &field_meta)
{
  if (inited_) {
    LOG_WARN("Failed to create index due to the index has been created before. file_name:%s, index:%s, field:%s",
        file_name, index_meta.name(), index_meta.field());
    return RC::RECORD_OPENNED;
  }

  Index::init(index_meta, field_meta);

  BufferPoolManager &bpm = table->db()->buffer_pool_manager();
  RC rc = index_handler_.create(table->db()->log_handler(), bpm, file_name, field_meta.type(), field_meta.len());
  if (RC::SUCCESS != rc) {
    LOG_WARN("Failed to create index_handler, file_name:%s, index:%s, field:%s, rc:%s",
        file_name, index_meta.name(), index_meta.field(), strrc(rc));
    return rc;
  }

  inited_ = true;
  table_  = table;
  LOG_INFO("Successfully create index, file_name:%s, index:%s, field:%s",
    file_name, index_meta.name(), index_meta.field());
  return RC::SUCCESS;
}

RC BplusTreeIndex::open(Table *table, const char *file_name, const IndexMeta &index_meta, const FieldMeta &field_meta)
{
  if (inited_) {
    LOG_WARN("Failed to open index due to the index has been initedd before. file_name:%s, index:%s, field:%s",
        file_name, index_meta.name(), index_meta.field());
    return RC::RECORD_OPENNED;
  }

  Index::init(index_meta, field_meta);

  BufferPoolManager &bpm = table->db()->buffer_pool_manager();
  RC rc = index_handler_.open(table->db()->log_handler(), bpm, file_name);
  if (RC::SUCCESS != rc) {
    LOG_WARN("Failed to open index_handler, file_name:%s, index:%s, field:%s, rc:%s",
        file_name, index_meta.name(), index_meta.field(), strrc(rc));
    return rc;
  }

  inited_ = true;
  table_  = table;
  LOG_INFO("Successfully open index, file_name:%s, index:%s, field:%s",
    file_name, index_meta.name(), index_meta.field());
  return RC::SUCCESS;
}

RC BplusTreeIndex::create(Table *table, const char *file_name, const IndexMeta &index_meta, const vector<FieldMeta> &field_metas)
{
  if (inited_) {
    LOG_WARN("Failed to create index due to the index has been created before. file_name:%s, index:%s",
        file_name, index_meta.name());
    return RC::RECORD_OPENNED;
  }

  if (field_metas.empty()) {
    LOG_ERROR("Failed to create index, field_metas is empty.");
    return RC::INVALID_ARGUMENT;
  }

  Index::init(index_meta, field_metas);

  // 对于多列索引，需要计算总的键长度
  int total_key_len = calc_multi_key_len();

  BufferPoolManager &bpm = table->db()->buffer_pool_manager();
  // 使用CHARS类型和总长度来创建索引
  RC rc = index_handler_.create(table->db()->log_handler(), bpm, file_name, AttrType::CHARS, total_key_len);
  if (RC::SUCCESS != rc) {
    LOG_WARN("Failed to create index_handler, file_name:%s, index:%s, rc:%s",
        file_name, index_meta.name(), strrc(rc));
    return rc;
  }

  inited_ = true;
  table_  = table;
  LOG_INFO("Successfully create multi-column index, file_name:%s, index:%s, fields:%d",
    file_name, index_meta.name(), (int)field_metas.size());
  return RC::SUCCESS;
}

RC BplusTreeIndex::open(Table *table, const char *file_name, const IndexMeta &index_meta, const vector<FieldMeta> &field_metas)
{
  if (inited_) {
    LOG_WARN("Failed to open index due to the index has been opened before. file_name:%s, index:%s",
        file_name, index_meta.name());
    return RC::RECORD_OPENNED;
  }

  if (field_metas.empty()) {
    LOG_ERROR("Failed to open index, field_metas is empty.");
    return RC::INVALID_ARGUMENT;
  }

  Index::init(index_meta, field_metas);

  BufferPoolManager &bpm = table->db()->buffer_pool_manager();
  RC rc = index_handler_.open(table->db()->log_handler(), bpm, file_name);
  if (RC::SUCCESS != rc) {
    LOG_WARN("Failed to open index_handler, file_name:%s, index:%s, rc:%s",
        file_name, index_meta.name(), strrc(rc));
    return rc;
  }

  inited_ = true;
  table_  = table;
  LOG_INFO("Successfully open multi-column index, file_name:%s, index:%s, fields:%d",
    file_name, index_meta.name(), (int)field_metas.size());
  return RC::SUCCESS;
}

RC BplusTreeIndex::close()
{
  if (inited_) {
    LOG_INFO("Begin to close index, index:%s, field:%s", index_meta_.name(), index_meta_.field());
    index_handler_.close();
    inited_ = false;
  }
  LOG_INFO("Successfully close index.");
  return RC::SUCCESS;
}

RC BplusTreeIndex::insert_entry(const char *record, const RID *rid)
{
  if (field_metas_.size() == 1) {
    // 单列索引，使用原有逻辑
    return index_handler_.insert_entry(record + field_metas_[0].offset(), rid);
  } else {
    // 多列索引，构建复合键
    char key_buf[1024];  // 临时缓冲区，足够大以容纳复合键
    int key_len = 0;
    RC rc = make_key(record, key_buf, key_len);
    if (rc != RC::SUCCESS) {
      return rc;
    }
    return index_handler_.insert_entry(key_buf, rid);
  }
}

RC BplusTreeIndex::delete_entry(const char *record, const RID *rid)
{
  if (field_metas_.size() == 1) {
    // 单列索引，使用原有逻辑
    return index_handler_.delete_entry(record + field_metas_[0].offset(), rid);
  } else {
    // 多列索引，构建复合键
    char key_buf[1024];  // 临时缓冲区
    int key_len = 0;
    RC rc = make_key(record, key_buf, key_len);
    if (rc != RC::SUCCESS) {
      return rc;
    }
    return index_handler_.delete_entry(key_buf, rid);
  }
}

IndexScanner *BplusTreeIndex::create_scanner(
    const char *left_key, int left_len, bool left_inclusive, const char *right_key, int right_len, bool right_inclusive)
{
  BplusTreeIndexScanner *index_scanner = new BplusTreeIndexScanner(index_handler_);
  RC rc = index_scanner->open(left_key, left_len, left_inclusive, right_key, right_len, right_inclusive);
  if (rc != RC::SUCCESS) {
    LOG_WARN("failed to open index scanner. rc=%d:%s", rc, strrc(rc));
    delete index_scanner;
    return nullptr;
  }
  return index_scanner;
}

RC BplusTreeIndex::sync() { return index_handler_.sync(); }

////////////////////////////////////////////////////////////////////////////////
BplusTreeIndexScanner::BplusTreeIndexScanner(BplusTreeHandler &tree_handler) : tree_scanner_(tree_handler) {}

BplusTreeIndexScanner::~BplusTreeIndexScanner() noexcept { tree_scanner_.close(); }

RC BplusTreeIndexScanner::open(
    const char *left_key, int left_len, bool left_inclusive, const char *right_key, int right_len, bool right_inclusive)
{
  return tree_scanner_.open(left_key, left_len, left_inclusive, right_key, right_len, right_inclusive);
}

RC BplusTreeIndexScanner::next_entry(RID *rid) { return tree_scanner_.next_entry(*rid); }

RC BplusTreeIndexScanner::destroy()
{
  delete this;
  return RC::SUCCESS;
}

////////////////////////////////////////////////////////////////////////////////
// BplusTreeIndex 辅助方法实现

int BplusTreeIndex::calc_multi_key_len() const
{
  int total_len = 0;
  for (const FieldMeta &field_meta : field_metas_) {
    total_len += field_meta.len();
  }
  return total_len;
}

RC BplusTreeIndex::make_key(const char *record, char *key_buf, int &key_len)
{
  key_len = 0;
  
  // 按顺序将每个字段的值拷贝到key_buf中，形成复合键
  for (const FieldMeta &field_meta : field_metas_) {
    const char *field_data = record + field_meta.offset();
    int field_len = field_meta.len();
    
    memcpy(key_buf + key_len, field_data, field_len);
    key_len += field_len;
  }
  
  return RC::SUCCESS;
}
