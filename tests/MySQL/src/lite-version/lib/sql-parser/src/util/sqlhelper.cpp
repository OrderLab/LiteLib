
#include "sqlhelper.h"
#include <iostream>
#include <map>
#include <sstream>
#include <string>

namespace hsql {

void printOperatorExpression(std::ostream& os, Expr* expr, uintmax_t num_indent);
void printAlias(std::ostream& os, Alias* alias, uintmax_t num_indent);

std::ostream& operator<<(std::ostream& os, const OperatorType& op);
std::ostream& operator<<(std::ostream& os, const DatetimeField& datetime);
std::ostream& operator<<(std::ostream& os, const FrameBound& frame_bound);

std::string indent(uintmax_t num_indent) { return std::string(num_indent, '\t'); }
void inprint(std::ostream& os, int64_t val, uintmax_t num_indent) { os << indent(num_indent).c_str() << val << "  " << std::endl; }
void inprint(std::ostream& os, double val, uintmax_t num_indent) { os << indent(num_indent).c_str() << val << std::endl; }
void inprint(std::ostream& os, const char* val, uintmax_t num_indent) { os << indent(num_indent).c_str() << val << std::endl; }
void inprint(std::ostream& os, const char* val, const char* val2, uintmax_t num_indent) {
  os << indent(num_indent).c_str() << val << "->" << val2 << std::endl;
}
void inprintC(std::ostream& os, char val, uintmax_t num_indent) { os << indent(num_indent).c_str() << val << std::endl; }
void inprint(std::ostream& os, const OperatorType& op, uintmax_t num_indent) { os << indent(num_indent) << op << std::endl; }
void inprint(std::ostream& os, const ColumnType& colType, uintmax_t num_indent) {
  os << indent(num_indent) << colType << std::endl;
}
void inprint(std::ostream& os, const DatetimeField& colType, uintmax_t num_indent) {
  os << indent(num_indent) << colType << std::endl;
}

void printTableRefInfo(std::ostream& os, TableRef* table, uintmax_t num_indent) {
  switch (table->type) {
    case kTableName:
      inprint(os, table->name, num_indent);
      if (table->schema) {
        inprint(os, "Schema", num_indent + 1);
        inprint(os, table->schema, num_indent + 2);
      }
      break;
    case kTableSelect:
      printSelectStatementInfo(os, table->select, num_indent);
      break;
    case kTableJoin:
      inprint(os, "Join Table", num_indent);
      inprint(os, "Left", num_indent + 1);
      printTableRefInfo(os, table->join->left, num_indent + 2);
      inprint(os, "Right", num_indent + 1);
      printTableRefInfo(os, table->join->right, num_indent + 2);
      inprint(os, "Join Condition", num_indent + 1);
      printExpression(os, table->join->condition, num_indent + 2);
      break;
    case kTableCrossProduct:
      for (TableRef* tbl : *table->list) printTableRefInfo(os, tbl, num_indent);
      break;
  }

  if (table->alias) {
    printAlias(os, table->alias, num_indent);
  }
}

void printAlias(std::ostream& os, Alias* alias, uintmax_t num_indent) {
  inprint(os, "Alias", num_indent + 1);
  inprint(os, alias->name, num_indent + 2);

  if (alias->columns) {
    for (char* column : *(alias->columns)) {
      inprint(os, column, num_indent + 3);
    }
  }
}

void printOperatorExpression(std::ostream& os, Expr* expr, uintmax_t num_indent) {
  if (expr == nullptr) {
    inprint(os, "null", num_indent);
    return;
  }

  inprint(os, expr->opType, num_indent);

  printExpression(os, expr->expr, num_indent + 1);
  if (expr->expr2) {
    printExpression(os, expr->expr2, num_indent + 1);
  } else if (expr->exprList) {
    for (Expr* e : *expr->exprList) printExpression(os, e, num_indent + 1);
  }
}

void printExpression(std::ostream& os, Expr* expr, uintmax_t num_indent) {
  if (!expr) return;
  switch (expr->type) {
    case kExprStar:
      inprint(os, "*", num_indent);
      break;
    case kExprColumnRef:
      inprint(os, expr->name, num_indent);
      if (expr->table) {
        inprint(os, "Table:", num_indent + 1);
        inprint(os, expr->table, num_indent + 2);
      }
      break;
    // case kExprTableColumnRef: inprint(os, expr->table, expr->name, num_indent); break;
    case kExprLiteralFloat:
      inprint(os, expr->fval, num_indent);
      break;
    case kExprLiteralInt:
      inprint(os, expr->ival, num_indent);
      break;
    case kExprLiteralString:
      inprint(os, expr->name, num_indent);
      break;
    case kExprLiteralDate:
      inprint(os, expr->name, num_indent);
      break;
    case kExprLiteralNull:
      inprint(os, "NULL", num_indent);
      break;
    case kExprLiteralInterval:
      inprint(os, "INTERVAL", num_indent);
      inprint(os, expr->ival, num_indent + 1);
      inprint(os, expr->datetimeField, num_indent + 1);
      break;
    case kExprFunctionRef:
      inprint(os, expr->name, num_indent);
      for (Expr* e : *expr->exprList) {
        printExpression(os, e, num_indent + 1);
      }

      if (expr->windowDescription) {
        printWindowDescription(os, expr->windowDescription, num_indent + 1);
      }
      break;
    case kExprExtract:
      inprint(os, "EXTRACT", num_indent);
      inprint(os, expr->datetimeField, num_indent + 1);
      printExpression(os, expr->expr, num_indent + 1);
      break;
    case kExprCast:
      inprint(os, "CAST", num_indent);
      inprint(os, expr->columnType, num_indent + 1);
      printExpression(os, expr->expr, num_indent + 1);
      break;
    case kExprOperator:
      printOperatorExpression(os, expr, num_indent);
      break;
    case kExprSelect:
      printSelectStatementInfo(os, expr->select, num_indent);
      break;
    case kExprParameter:
      inprint(os, expr->ival, num_indent);
      break;
    case kExprArray:
      for (Expr* e : *expr->exprList) {
        printExpression(os, e, num_indent + 1);
      }
      break;
    case kExprArrayIndex:
      printExpression(os, expr->expr, num_indent + 1);
      inprint(os, expr->ival, num_indent);
      break;
    default:
      std::cerr << "Unrecognized expression type " << expr->type << std::endl;
      return;
  }
  if (expr->alias) {
    inprint(os, "Alias", num_indent + 1);
    inprint(os, expr->alias, num_indent + 2);
  }
}

void printOrderBy(std::ostream& os, const std::vector<OrderDescription*>* expr, uintmax_t num_indent) {
  if (!expr) return;
  for (const auto& order_description : *expr) {
    printExpression(os, order_description->expr, num_indent);
    if (order_description->type == kOrderAsc) {
      inprint(os, "ascending", num_indent);
    } else {
      inprint(os, "descending", num_indent);
    }
  }
}

void printWindowDescription(std::ostream& os, WindowDescription* window_description, uintmax_t num_indent) {
  inprint(os, "OVER", num_indent);
  if (window_description->partitionList) {
    inprint(os, "PARTITION BY", num_indent + 1);
    for (const auto e : *window_description->partitionList) {
      printExpression(os, e, num_indent + 2);
    }
  }

  if (window_description->orderList) {
    inprint(os, "ORDER BY", num_indent + 1);
    printOrderBy(os, window_description->orderList, num_indent + 2);
  }

  std::stringstream stream;
  switch (window_description->frameDescription->type) {
    case kRows:
      stream << "ROWS";
      break;
    case kRange:
      stream << "RANGE";
      break;
    case kGroups:
      stream << "GROUPS";
      break;
  }
  stream << " BETWEEN " << *window_description->frameDescription->start << " AND "
         << *window_description->frameDescription->end;
  inprint(os, stream.str().c_str(), num_indent + 1);
}

void printSelectStatementInfo(std::ostream& os, const SelectStatement* stmt, uintmax_t num_indent) {
  inprint(os, "SelectStatement", num_indent);
  inprint(os, "Fields:", num_indent + 1);
  for (Expr* expr : *stmt->selectList) printExpression(os, expr, num_indent + 2);

  if (stmt->fromTable) {
    inprint(os, "Sources:", num_indent + 1);
    printTableRefInfo(os, stmt->fromTable, num_indent + 2);
  }

  if (stmt->whereClause) {
    inprint(os, "Search Conditions:", num_indent + 1);
    printExpression(os, stmt->whereClause, num_indent + 2);
  }

  if (stmt->groupBy) {
    inprint(os, "GroupBy:", num_indent + 1);
    for (Expr* expr : *stmt->groupBy->columns) printExpression(os, expr, num_indent + 2);
    if (stmt->groupBy->having) {
      inprint(os, "Having:", num_indent + 1);
      printExpression(os, stmt->groupBy->having, num_indent + 2);
    }
  }
  if (stmt->lockings) {
    inprint(os, "Lock Info:", num_indent + 1);
    for (LockingClause* lockingClause : *stmt->lockings) {
      inprint(os, "Type", num_indent + 2);
      if (lockingClause->rowLockMode == RowLockMode::ForUpdate) {
        inprint(os, "FOR UPDATE", num_indent + 3);
      } else if (lockingClause->rowLockMode == RowLockMode::ForNoKeyUpdate) {
        inprint(os, "FOR NO KEY UPDATE", num_indent + 3);
      } else if (lockingClause->rowLockMode == RowLockMode::ForShare) {
        inprint(os, "FOR SHARE", num_indent + 3);
      } else if (lockingClause->rowLockMode == RowLockMode::ForKeyShare) {
        inprint(os, "FOR KEY SHARE", num_indent + 3);
      }
      if (lockingClause->tables) {
        inprint(os, "Target tables:", num_indent + 2);
        for (char* dtable : *lockingClause->tables) {
          inprint(os, dtable, num_indent + 3);
        }
      }
      if (lockingClause->rowLockWaitPolicy != RowLockWaitPolicy::None) {
        inprint(os, "Waiting policy: ", num_indent + 2);
        if (lockingClause->rowLockWaitPolicy == RowLockWaitPolicy::NoWait)
          inprint(os, "NOWAIT", num_indent + 3);
        else
          inprint(os, "SKIP LOCKED", num_indent + 3);
      }
    }
  }

  if (stmt->setOperations) {
    for (SetOperation* setOperation : *stmt->setOperations) {
      switch (setOperation->setType) {
        case SetType::kSetIntersect:
          inprint(os, "Intersect:", num_indent + 1);
          break;
        case SetType::kSetUnion:
          inprint(os, "Union:", num_indent + 1);
          break;
        case SetType::kSetExcept:
          inprint(os, "Except:", num_indent + 1);
          break;
      }

      printSelectStatementInfo(os, setOperation->nestedSelectStatement, num_indent + 2);

      if (setOperation->resultOrder) {
        inprint(os, "SetResultOrderBy:", num_indent + 1);
        printOrderBy(os, setOperation->resultOrder, num_indent + 2);
      }

      if (setOperation->resultLimit) {
        if (setOperation->resultLimit->limit) {
          inprint(os, "SetResultLimit:", num_indent + 1);
          printExpression(os, setOperation->resultLimit->limit, num_indent + 2);
        }

        if (setOperation->resultLimit->offset) {
          inprint(os, "SetResultOffset:", num_indent + 1);
          printExpression(os, setOperation->resultLimit->offset, num_indent + 2);
        }
      }
    }
  }

  if (stmt->order) {
    inprint(os, "OrderBy:", num_indent + 1);
    printOrderBy(os, stmt->order, num_indent + 2);
  }

  if (stmt->limit && stmt->limit->limit) {
    inprint(os, "Limit:", num_indent + 1);
    printExpression(os, stmt->limit->limit, num_indent + 2);
  }

  if (stmt->limit && stmt->limit->offset) {
    inprint(os, "Offset:", num_indent + 1);
    printExpression(os, stmt->limit->offset, num_indent + 2);
  }
}

void printImportStatementInfo(std::ostream& os, const ImportStatement* stmt, uintmax_t num_indent) {
  inprint(os, "ImportStatement", num_indent);
  inprint(os, stmt->filePath, num_indent + 1);
  switch (stmt->type) {
    case ImportType::kImportCSV:
      inprint(os, "CSV", num_indent + 1);
      break;
    case ImportType::kImportTbl:
      inprint(os, "TBL", num_indent + 1);
      break;
    case ImportType::kImportBinary:
      inprint(os, "BINARY", num_indent + 1);
      break;
    case ImportType::kImportAuto:
      inprint(os, "AUTO", num_indent + 1);
      break;
  }
  inprint(os, stmt->tableName, num_indent + 1);
  if (stmt->whereClause) {
    inprint(os, "WHERE:", num_indent + 1);
    printExpression(os, stmt->whereClause, num_indent + 2);
  }
}

void printExportStatementInfo(std::ostream& os, const ExportStatement* stmt, uintmax_t num_indent) {
  inprint(os, "ExportStatement", num_indent);
  inprint(os, stmt->filePath, num_indent + 1);
  switch (stmt->type) {
    case ImportType::kImportCSV:
      inprint(os, "CSV", num_indent + 1);
      break;
    case ImportType::kImportTbl:
      inprint(os, "TBL", num_indent + 1);
      break;
    case ImportType::kImportBinary:
      inprint(os, "BINARY", num_indent + 1);
      break;
    case ImportType::kImportAuto:
      inprint(os, "AUTO", num_indent + 1);
      break;
  }

  if (stmt->tableName) {
    inprint(os, stmt->tableName, num_indent + 1);
  } else {
    printSelectStatementInfo(os, stmt->select, num_indent + 1);
  }
}

void printCreateStatementInfo(std::ostream& os, const CreateStatement* stmt, uintmax_t num_indent) {
  inprint(os, "CreateStatement", num_indent);
  inprint(os, stmt->tableName, num_indent + 1);
  if (stmt->filePath) inprint(os, stmt->filePath, num_indent + 1);
}

void printInsertStatementInfo(std::ostream& os, const InsertStatement* stmt, uintmax_t num_indent) {
  inprint(os, "InsertStatement", num_indent);
  inprint(os, stmt->tableName, num_indent + 1);
  if (stmt->columns) {
    inprint(os, "Columns", num_indent + 1);
    for (char* col_name : *stmt->columns) {
      inprint(os, col_name, num_indent + 2);
    }
  }
  switch (stmt->type) {
    case kInsertValues:
      inprint(os, "Values", num_indent + 1);
      for (Expr* expr : *stmt->values) {
        printExpression(os, expr, num_indent + 2);
      }
      break;
    case kInsertSelect:
      printSelectStatementInfo(os, stmt->select, num_indent + 1);
      break;
  }
}

void printTransactionStatementInfo(std::ostream& os, const TransactionStatement* stmt, uintmax_t num_indent) {
  inprint(os, "TransactionStatement", num_indent);
  switch (stmt->command) {
    case kBeginTransaction:
      inprint(os, "BEGIN", num_indent + 1);
      break;
    case kCommitTransaction:
      inprint(os, "COMMIT", num_indent + 1);
      break;
    case kRollbackTransaction:
      inprint(os, "ROLLBACK", num_indent + 1);
      break;
  }
}

void printStatementInfo(std::ostream& os, const SQLStatement* stmt) {
  switch (stmt->type()) {
    case kStmtSelect:
      printSelectStatementInfo(os, (const SelectStatement*)stmt, 0);
      break;
    case kStmtInsert:
      printInsertStatementInfo(os, (const InsertStatement*)stmt, 0);
      break;
    case kStmtCreate:
      printCreateStatementInfo(os, (const CreateStatement*)stmt, 0);
      break;
    case kStmtImport:
      printImportStatementInfo(os, (const ImportStatement*)stmt, 0);
      break;
    case kStmtExport:
      printExportStatementInfo(os, (const ExportStatement*)stmt, 0);
      break;
    case kStmtTransaction:
      printTransactionStatementInfo(os, (const TransactionStatement*)stmt, 0);
      break;
    default:
      break;
  }
}

std::ostream& operator<<(std::ostream& os, const OperatorType& op) {
  static const std::map<const OperatorType, const std::string> operatorToToken = {
      {kOpNone, "None"},     {kOpBetween, "BETWEEN"},
      {kOpCase, "CASE"},     {kOpCaseListElement, "CASE LIST ELEMENT"},
      {kOpPlus, "+"},        {kOpMinus, "-"},
      {kOpAsterisk, "*"},    {kOpSlash, "/"},
      {kOpPercentage, "%"},  {kOpCaret, "^"},
      {kOpEquals, "="},      {kOpNotEquals, "!="},
      {kOpLess, "<"},        {kOpLessEq, "<="},
      {kOpGreater, ">"},     {kOpGreaterEq, ">="},
      {kOpLike, "LIKE"},     {kOpNotLike, "NOT LIKE"},
      {kOpILike, "ILIKE"},   {kOpAnd, "AND"},
      {kOpOr, "OR"},         {kOpIn, "IN"},
      {kOpConcat, "CONCAT"}, {kOpNot, "NOT"},
      {kOpUnaryMinus, "-"},  {kOpIsNull, "IS NULL"},
      {kOpExists, "EXISTS"}};

  const auto found = operatorToToken.find(op);
  if (found == operatorToToken.cend()) {
    return os << static_cast<uint64_t>(op);
  } else {
    return os << (*found).second;
  }
}

std::ostream& operator<<(std::ostream& os, const DatetimeField& datetime) {
  static const std::map<const DatetimeField, const std::string> operatorToToken = {
      {kDatetimeNone, "None"}, {kDatetimeSecond, "SECOND"}, {kDatetimeMinute, "MINUTE"}, {kDatetimeHour, "HOUR"},
      {kDatetimeDay, "DAY"},   {kDatetimeMonth, "MONTH"},   {kDatetimeYear, "YEAR"}};

  const auto found = operatorToToken.find(datetime);
  if (found == operatorToToken.cend()) {
    return os << static_cast<uint64_t>(datetime);
  } else {
    return os << (*found).second;
  }
}

std::ostream& operator<<(std::ostream& os, const FrameBound& frame_bound) {
  if (frame_bound.type == kCurrentRow) {
    os << "CURRENT ROW";
    return os;
  }

  if (frame_bound.unbounded) {
    os << "UNBOUNDED";
  } else {
    os << frame_bound.offset;
  }

  os << " ";

  if (frame_bound.type == kPreceding) {
    os << "PRECEDING";
  } else {
    os << "FOLLOWING";
  }

  return os;
}

}  // namespace hsql
