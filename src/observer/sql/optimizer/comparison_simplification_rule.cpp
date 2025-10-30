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
// Created by Wangyunlai on 2022/12/13.
//

#include "sql/optimizer/comparison_simplification_rule.h"
#include "common/log/log.h"
#include "sql/expr/expression.h"

RC ComparisonSimplificationRule::rewrite(unique_ptr<Expression> &expr, bool &change_made)
{
  RC rc = RC::SUCCESS;

  change_made = false;
  if (expr->type() == ExprType::COMPARISON) {
    ComparisonExpr *cmp_expr = static_cast<ComparisonExpr *>(expr.get());

    // 首先尝试常量折叠
    Value value;
    RC sub_rc = cmp_expr->try_get_value(value);
    if (sub_rc == RC::SUCCESS) {
      unique_ptr<Expression> new_expr(new ValueExpr(value));
      expr.swap(new_expr);
      change_made = true;
      LOG_TRACE("comparison expression is simplified to constant");
      return rc;
    }

    // 如果左边是常量而右边不是，进行移项优化
    // 将 "constant comp expression" 转换为 "expression reverse_comp constant"
    unique_ptr<Expression> &left = cmp_expr->left();
    unique_ptr<Expression> &right = cmp_expr->right();
    
    LOG_DEBUG("Checking comparison expression: left type=%d, right type=%d", left->type(), right->type());
    
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
      
      LOG_INFO("Rewriting comparison: constant(%d) %d expr => expr %d constant", 
               left->type(), old_comp, new_comp);
      
      // 创建新的比较表达式：expression reverse_comp constant
      unique_ptr<Expression> new_expr(new ComparisonExpr(new_comp, right->copy(), left->copy()));
      expr.swap(new_expr);
      change_made = true;
      LOG_TRACE("comparison expression is rewritten: constant comp expression => expression reverse_comp constant");
    }
  }
  return rc;
}
