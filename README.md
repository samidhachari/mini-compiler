# Mini LLVM Compiler (C++)

This project is a minimal compiler front-end that parses arithmetic expressions and variable assignments and generates LLVM Intermediate Representation (IR) code.

## Features
- Parses arithmetic expressions: `+`, `-`, `*`, `/`
- Supports variable assignments: `x = 5`, `y = x + 2`
- Handles operator precedence correctly
- Generates LLVM IR using LLVM's C++ API

## Usage
```sh
clang++ -g mini_compiler.cpp `llvm-config --cxxflags --ldflags --system-libs --libs core` -o mini_compiler
./mini_compiler
