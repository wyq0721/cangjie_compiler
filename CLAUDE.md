# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is the Cangjie programming language compiler, consisting of a compiler frontend and a modified LLVM backend. Cangjie is a general-purpose language designed for all-scenario application development with concise syntax, multi-paradigm programming, and type safety.

## Build Commands

The build system uses `build.py` (Python script) with CMake underneath.

### Basic Build Workflow

```bash
# Clean workspace
python3 build.py clean

# Build compiler (choose build type: debug, release, or relwithdebinfo)
python3 build.py build -t release

# Install to output/ directory
python3 build.py install

# Set up environment and verify
source ./output/envsetup.sh
cjc -v
```

### Build Options

Key build options for `python3 build.py build`:
- `-t {debug,release,relwithdebinfo}`: Build type (required)
- `-j JOBS`: Parallel jobs (default: CPU count + 2)
- `--no-tests`: Skip building unit tests
- `--build-cjdb`: Build with debugger support (cjdb/lldb)
- `--target {native,windows-x86_64,ohos-aarch64,...}`: Cross-compilation target
- `--product {all,cjc,libs}`: Build specific components
- `--enable-assert`: Enable assertions in release mode
- `--print-cmd`: Print CMake command without executing

### Testing

```bash
# Run all unit tests (after build)
python3 build.py test
```

Unit tests are built by default and located in `build/build/bin/`. Individual tests can be run directly:
```bash
./build/build/bin/LexerTest
./build/build/bin/ParserTest
```

## Compiler Architecture

The compilation pipeline follows this flow:

**Source Code → Lexer → Parser → AST → Semantic Analysis → Macro Expansion → CHIR → CodeGen → LLVM IR → LLVM Backend → Machine Code**

### Frontend Components (src/)

- **Lex/**: Tokenizes Cangjie source code into tokens
- **Parse/**: Builds Abstract Syntax Tree (AST) from tokens according to grammar rules
- **Sema/**: Semantic analysis - type checking, type inference, scope analysis
- **Macro/**: Macro expansion and processing
- **AST/**: AST node definitions and manipulation utilities
- **CHIR/**: Cangjie High-Level Intermediate Representation
  - Converts AST to IR and performs high-level optimizations
  - Contains subdirectories: AST2CHIR, Analysis, Checker, Expression, Serializer, Transformation, Type
  - CHIR is serialized/deserialized using FlatBuffers (schemas in schema/)
- **CodeGen/**: Translates CHIR to LLVM IR (LLVM BitCode)
- **Mangle/**: Symbol name mangling for Cangjie symbols
- **Modules/**: Package management - handles dependencies, namespace isolation, multi-module development
  - Uses FlatBuffers for package serialization (PackageFormat.fbs)
- **Driver/**: Orchestrates the entire compilation process, manages frontend/backend coordination
- **Frontend/**: CompilerInstance and compilation workflow management
- **ConditionalCompilation/**: Conditional compilation based on predefined/custom conditions
- **IncrementalCompilation/**: Incremental compilation using cache files
- **Option/**: Command-line option parsing and management

### Backend (third_party/llvm-project)

Modified LLVM 15.0.4 with Cangjie-specific patches:
- **opt**: LLVM IR optimization passes
- **llc**: LLVM IR to machine code compilation
- **ld**: Linking object files and libraries
- **cjdb**: Debugger (based on lldb)

### Key Files

- `src/main.cpp`: Compiler entry point
- `build.py`: Build script with all build logic
- `schema/*.fbs`: FlatBuffers schemas for serialization (ModuleFormat, PackageFormat, NodeFormat, etc.)
- `third_party/llvmPatch.diff`: LLVM modifications for Cangjie

## Cross-Compilation

The compiler supports cross-compilation for multiple targets:
- Windows x86-64 (from Linux)
- OHOS (OpenHarmony) aarch64/arm/x86-64
- iOS aarch64/simulator
- Android aarch64/x86-64

Use `--target` flag with build command. Note: Building Windows binaries directly on Windows is not currently supported; use Linux cross-compilation.

## Output Structure

After `python3 build.py install`, the `output/` directory contains:
- `bin/cjc`: Main compiler executable
- `bin/cjc-frontend`: Frontend-only executable (symlink)
- `lib/`: Compiler libraries organized by target platform
- `modules/`: Standard library .cjo files by target platform
- `runtime/`: Runtime libraries
- `third_party/`: LLVM toolchain binaries
- `tools/`: Cangjie development tools
- `envsetup.sh`: Environment setup script (source this before using cjc)

## Third-Party Dependencies

- **LLVM**: Backend compiler infrastructure (llvmorg-15.0.4 base)
- **FlatBuffers**: Serialization for .cjo files and macros
- **libboundscheck**: Safe function library (OpenHarmony)
- **mingw-w64**: Windows SDK static libraries (for Windows cross-compilation)
- **googletest**: Unit testing framework

## Development Notes

- The compiler uses C++17 standard
- FlatBuffers schemas in `schema/` define serialization formats for packages, modules, AST nodes, and macros
- Symbol mangling follows Cangjie-specific rules (see src/Mangle/)
- The Driver coordinates between frontend phases and backend invocation
- CompilerInstance manages the entire compilation state and workflow
- Type system is managed centrally by TypeManager (src/Sema/)
- Package/module system uses ImportManager and PackageManager for dependency resolution
