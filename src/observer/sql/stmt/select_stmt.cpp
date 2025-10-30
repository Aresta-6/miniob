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
// Created by Wangyunlai on 2022/6/6.
//

#include "sql/stmt/select_stmt.h"
#include "common/lang/string.h"
#include "common/log/log.h"
#include "sql/stmt/filter_stmt.h"
#include "storage/db/db.h"
#include "storage/table/table.h"
#include "sql/parser/expression_binder.h"

using namespace std;
using namespace common;

SelectStmt::~SelectStmt()
{
  if (nullptr != filter_stmt_) {
    delete filter_stmt_;
    filter_stmt_ = nullptr;
  }
  
  for (FilterStmt *join_filter : join_filter_stmts_) {
    if (join_filter != nullptr) {
      delete join_filter;
    }
  }
  join_filter_stmts_.clear();
}

RC SelectStmt::create(Db *db, SelectSqlNode &select_sql, Stmt *&stmt)
{
  if (nullptr == db) {
    LOG_WARN("invalid argument. db is null");
    return RC::INVALID_ARGUMENT;
  }

  BinderContext binder_context;

  // collect tables in `from` statement
  vector<Table *>                tables;
  unordered_map<string, Table *> table_map;
  for (size_t i = 0; i < select_sql.relations.size(); i++) {
    const char *table_name = select_sql.relations[i].c_str();
    if (nullptr == table_name) {
      LOG_WARN("invalid argument. relation name is null. index=%d", i);
      return RC::INVALID_ARGUMENT;
    }

    Table *table = db->find_table(table_name);
    if (nullptr == table) {
      LOG_WARN("no such table. db=%s, table_name=%s", db->name(), table_name);
      return RC::SCHEMA_TABLE_NOT_EXIST;
    }

    binder_context.add_table(table);
    tables.push_back(table);
    table_map.insert({table_name, table});
  }

  // collect tables in JOIN clause
  for (size_t i = 0; i < select_sql.joins.size(); i++) {
    const char *table_name = select_sql.joins[i].table_name.c_str();
    if (nullptr == table_name) {
      LOG_WARN("invalid argument. join relation name is null. index=%d", i);
      return RC::INVALID_ARGUMENT;
    }

    Table *table = db->find_table(table_name);
    if (nullptr == table) {
      LOG_WARN("no such table. db=%s, table_name=%s", db->name(), table_name);
      return RC::SCHEMA_TABLE_NOT_EXIST;
    }

    binder_context.add_table(table);
    tables.push_back(table);
    table_map.insert({table_name, table});
  }

  // collect query fields in `select` statement
  vector<unique_ptr<Expression>> bound_expressions;
  ExpressionBinder expression_binder(binder_context);
  
  for (unique_ptr<Expression> &expression : select_sql.expressions) {
    RC rc = expression_binder.bind_expression(expression, bound_expressions);
    if (OB_FAIL(rc)) {
      LOG_INFO("bind expression failed. rc=%s", strrc(rc));
      return rc;
    }
  }

  vector<unique_ptr<Expression>> group_by_expressions;
  for (unique_ptr<Expression> &expression : select_sql.group_by) {
    RC rc = expression_binder.bind_expression(expression, group_by_expressions);
    if (OB_FAIL(rc)) {
      LOG_INFO("bind expression failed. rc=%s", strrc(rc));
      return rc;
    }
  }

  Table *default_table = nullptr;
  if (tables.size() == 1) {
    default_table = tables[0];
  }

  // handle WHERE conditions: prefer expression conditions over old-style conditions
  FilterStmt *filter_stmt = nullptr;
  vector<unique_ptr<Expression>> bound_condition_expressions;
  
  if (!select_sql.condition_expressions.empty()) {
    // Bind expression conditions
    for (unique_ptr<Expression> &expression : select_sql.condition_expressions) {
      RC rc = expression_binder.bind_expression(expression, bound_condition_expressions);
      if (OB_FAIL(rc)) {
        LOG_INFO("bind condition expression failed. rc=%s", strrc(rc));
        return rc;
      }
    }
  } else if (!select_sql.conditions.empty()) {
    // Use old-style FilterStmt for backward compatibility
    RC rc = FilterStmt::create(db,
        default_table,
        &table_map,
        select_sql.conditions.data(),
        static_cast<int>(select_sql.conditions.size()),
        filter_stmt);
    if (rc != RC::SUCCESS) {
      LOG_WARN("cannot construct filter stmt");
      return rc;
    }
  }

  // create filter statements for JOIN conditions
  vector<FilterStmt *> join_filter_stmts;
  // 第一个表没有 JOIN 条件，设置为 nullptr
  for (size_t i = 0; i < select_sql.relations.size(); i++) {
    join_filter_stmts.push_back(nullptr);
  }
  
  RC rc = RC::SUCCESS;
  for (size_t i = 0; i < select_sql.joins.size(); i++) {
    FilterStmt *join_filter = nullptr;
    rc = FilterStmt::create(db,
        nullptr,  // no default table for JOIN conditions
        &table_map,
        select_sql.joins[i].conditions.data(),
        static_cast<int>(select_sql.joins[i].conditions.size()),
        join_filter);
    if (rc != RC::SUCCESS) {
      LOG_WARN("cannot construct join filter stmt for table %s", select_sql.joins[i].table_name.c_str());
      // 清理已创建的 FilterStmt
      for (FilterStmt *f : join_filter_stmts) {
        if (f != nullptr) delete f;
      }
      if (filter_stmt != nullptr) delete filter_stmt;
      return rc;
    }
    join_filter_stmts.push_back(join_filter);
  }

  // everything alright
  SelectStmt *select_stmt = new SelectStmt();

  select_stmt->tables_.swap(tables);
  select_stmt->query_expressions_.swap(bound_expressions);
  select_stmt->filter_stmt_ = filter_stmt;
  select_stmt->group_by_.swap(group_by_expressions);
  select_stmt->condition_expressions_.swap(bound_condition_expressions);
  select_stmt->join_filter_stmts_.swap(join_filter_stmts);
  stmt                      = select_stmt;
  return RC::SUCCESS;
}
