// mini_compiler.cpp - Arithmetic, Variables, Assignments to LLVM IR

#include "llvm/ADT/STLExtras.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include <cctype>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <iostream>

using namespace llvm;

//===----------------------------------------------------------------------===//
// Lexer
//===----------------------------------------------------------------------===//

enum Token {
  tok_eof = -1,
  tok_def = -2,
  tok_extern = -3,
  tok_identifier = -4,
  tok_number = -5
};

static std::string IdentifierStr;
static double NumVal;

int gettok() {
  static int LastChar = ' ';

  while (isspace(LastChar))
    LastChar = getchar();

  if (isalpha(LastChar)) {
    IdentifierStr = LastChar;
    while (isalnum((LastChar = getchar())))
      IdentifierStr += LastChar;

    if (IdentifierStr == "def") return tok_def;
    if (IdentifierStr == "extern") return tok_extern;
    return tok_identifier;
  }

  if (isdigit(LastChar) || LastChar == '.') {
    std::string NumStr;
    do {
      NumStr += LastChar;
      LastChar = getchar();
    } while (isdigit(LastChar) || LastChar == '.');

    NumVal = strtod(NumStr.c_str(), nullptr);
    return tok_number;
  }

  if (LastChar == '#') {
    do LastChar = getchar();
    while (LastChar != EOF && LastChar != '\n' && LastChar != '\r');

    if (LastChar != EOF)
      return gettok();
  }

  if (LastChar == EOF)
    return tok_eof;

  int ThisChar = LastChar;
  LastChar = getchar();
  return ThisChar;
}

//===----------------------------------------------------------------------===//
// Abstract Syntax Tree (AST)
//===----------------------------------------------------------------------===//

namespace {

class ExprAST {
public:
  virtual ~ExprAST() = default;
  virtual Value *codegen() = 0;
};

class NumberExprAST : public ExprAST {
  double Val;

public:
  NumberExprAST(double Val) : Val(Val) {}
  Value *codegen() override;
};

class VariableExprAST : public ExprAST {
  std::string Name;

public:
  VariableExprAST(const std::string &Name) : Name(Name) {}
  Value *codegen() override;
};

class AssignExprAST : public ExprAST {
  std::string VarName;
  std::unique_ptr<ExprAST> Expr;

public:
  AssignExprAST(const std::string &VarName, std::unique_ptr<ExprAST> Expr)
      : VarName(VarName), Expr(std::move(Expr)) {}
  Value *codegen() override;
};

class BinaryExprAST : public ExprAST {
  char Op;
  std::unique_ptr<ExprAST> LHS, RHS;

public:
  BinaryExprAST(char Op, std::unique_ptr<ExprAST> LHS, std::unique_ptr<ExprAST> RHS)
      : Op(Op), LHS(std::move(LHS)), RHS(std::move(RHS)) {}
  Value *codegen() override;
};

} // end anonymous namespace

//===----------------------------------------------------------------------===//
// LLVM Code Generation
//===----------------------------------------------------------------------===//

static LLVMContext TheContext;
static IRBuilder<> Builder(TheContext);
static std::unique_ptr<Module> TheModule;
static std::map<std::string, Value *> NamedValues;

Value *NumberExprAST::codegen() {
  return ConstantFP::get(TheContext, APFloat(Val));
}

Value *VariableExprAST::codegen() {
  Value *V = NamedValues[Name];
  if (!V) {
    std::cerr << "Unknown variable name: " << Name << "\n";
    return nullptr;
  }
  return V;
}

Value *AssignExprAST::codegen() {
  Value *Val = Expr->codegen();
  if (!Val) return nullptr;
  NamedValues[VarName] = Val;
  return Val;
}

Value *BinaryExprAST::codegen() {
  Value *L = LHS->codegen();
  Value *R = RHS->codegen();
  if (!L || !R)
    return nullptr;

  switch (Op) {
  case '+':
    return Builder.CreateFAdd(L, R, "addtmp");
  case '-':
    return Builder.CreateFSub(L, R, "subtmp");
  case '*':
    return Builder.CreateFMul(L, R, "multmp");
  case '/':
    return Builder.CreateFDiv(L, R, "divtmp");
  default:
    return nullptr;
  }
}

//===----------------------------------------------------------------------===//
// Parser
//===----------------------------------------------------------------------===//

static int CurTok;
static int getNextToken() { return CurTok = gettok(); }

std::unique_ptr<ExprAST> ParsePrimary();
std::unique_ptr<ExprAST> ParseExpression();

std::unique_ptr<ExprAST> ParseNumberExpr() {
  auto Result = std::make_unique<NumberExprAST>(NumVal);
  getNextToken();
  return std::move(Result);
}

std::unique_ptr<ExprAST> ParseIdentifierExpr() {
  std::string IdName = IdentifierStr;
  getNextToken();

  if (CurTok == '=') {
    getNextToken();
    auto Expr = ParseExpression();
    if (!Expr) return nullptr;
    return std::make_unique<AssignExprAST>(IdName, std::move(Expr));
  }

  return std::make_unique<VariableExprAST>(IdName);
}

int GetTokPrecedence() {
  if (!isascii(CurTok)) return -1;

  switch (CurTok) {
    case '+': case '-': return 20;
    case '*': case '/': return 40;
    default: return -1;
  }
}

std::unique_ptr<ExprAST> ParseBinOpRHS(int ExprPrec, std::unique_ptr<ExprAST> LHS) {
  while (true) {
    int TokPrec = GetTokPrecedence();

    if (TokPrec < ExprPrec)
      return LHS;

    int BinOp = CurTok;
    getNextToken();

    auto RHS = ParsePrimary();
    if (!RHS) return nullptr;

    int NextPrec = GetTokPrecedence();
    if (TokPrec < NextPrec) {
      RHS = ParseBinOpRHS(TokPrec + 1, std::move(RHS));
      if (!RHS) return nullptr;
    }

    LHS = std::make_unique<BinaryExprAST>(BinOp, std::move(LHS), std::move(RHS));
  }
}

std::unique_ptr<ExprAST> ParsePrimary() {
  switch (CurTok) {
    case tok_identifier: return ParseIdentifierExpr();
    case tok_number: return ParseNumberExpr();
    default: return nullptr;
  }
}

std::unique_ptr<ExprAST> ParseExpression() {
  auto LHS = ParsePrimary();
  if (!LHS) return nullptr;
  return ParseBinOpRHS(0, std::move(LHS));
}

//===----------------------------------------------------------------------===//
// Main Driver
//===----------------------------------------------------------------------===//

int main() {
  TheModule = std::make_unique<Module>("my mini compiler", TheContext);

  std::cout << "Enter an expression (e.g., x = 5, x + 2, 3 * 4 + 2):\n> ";
  getNextToken();

  auto AST = ParseExpression();
  if (AST) {
    Value *IR = AST->codegen();
    if (IR) IR->print(errs());
    std::cout << "\n";
  } else {
    std::cerr << "Error parsing input.\n";
  }

  return 0;
}
