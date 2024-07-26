#ifndef SQLPARSER_SQLHELPER_H
#define SQLPARSER_SQLHELPER_H

#include "../sql/statements.h"

#include <iostream>

namespace hsql {

// Prints a summary of the given SQLStatement.
void printStatementInfo(std::ostream& os, const SQLStatement* stmt);

// Prints a summary of the given SelectStatement with the given indentation.
void printSelectStatementInfo(std::ostream& os, const SelectStatement* stmt, uintmax_t num_indent);

// Prints a summary of the given ImportStatement with the given indentation.
void printImportStatementInfo(std::ostream& os, const ImportStatement* stmt, uintmax_t num_indent);

// Prints a summary of the given CopyStatement with the given indentation.
void printExportStatementInfo(std::ostream& os, const ExportStatement* stmt, uintmax_t num_indent);

// Prints a summary of the given InsertStatement with the given indentation.
void printInsertStatementInfo(std::ostream& os, const InsertStatement* stmt, uintmax_t num_indent);

// Prints a summary of the given CreateStatement with the given indentation.
void printCreateStatementInfo(std::ostream& os, const CreateStatement* stmt, uintmax_t num_indent);

// Prints a summary of the given TransactionStatement with the given indentation.
void printTransactionStatementInfo(std::ostream& os, const TransactionStatement* stmt, uintmax_t num_indent);

// Prints a summary of the given Expression with the given indentation.
void printExpression(std::ostream& os, Expr* expr, uintmax_t num_indent);

// Prints an ORDER BY clause
void printOrderBy(std::ostream& os, const std::vector<OrderDescription*>* expr, uintmax_t num_indent);

// Prints WindowDescription.
void printWindowDescription(std::ostream& os, WindowDescription* window_description, uintmax_t num_indent);

}  // namespace hsql

#endif
