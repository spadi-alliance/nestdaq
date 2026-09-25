# Contribution Guidelines

[English](CONTRIBUTING.md) | [日本語](CONTRIBUTING.ja.md)

[Top: NestDAQ](README.md) | [Previous: CMake](cmake/README.md)

This document describes recommended and prohibited practices for contributing to NestDAQ.

## Forking workflow

In this document, the **upstream repository** is [github.com/spadi-alliance/nestdaq](https://github.com/spadi-alliance/nestdaq).

- The `main` branch contains the latest released version of NestDAQ and is the normal choice for users and other non-developers.
- The `develop` branch contains the latest development version.
- Before starting development, fork the upstream repository to your own GitHub account.
- Synchronize your fork with the upstream `develop` branch before starting development.
- Make changes in your fork and push commits to your fork.
- Do not create working branches in the upstream repository.
- The upstream `main` and `develop` branches are protected and do not accept direct pushes.
- Open a Pull Request or Draft Pull Request from your fork to the upstream `develop` branch.
- Only authorized maintainers may merge changes into the upstream `main` branch.
- Pull Requests to upstream `main` must come from the upstream `develop` branch; Pull Requests from forks or other branches to upstream `main` are not accepted.
- Use a Draft Pull Request when a change is not ready for final review but would benefit from early feedback.

## Commits and Pull Requests

- Avoid combining several unrelated changes into one large commit.
- Split commits by intent when separate changes can be reviewed independently.
- Keep Pull Requests small enough to review carefully.
- Prefer opening Pull Requests frequently instead of waiting until many unrelated changes have accumulated.

## Formatting

- Apply a formatter before opening a Pull Request or marking a Draft Pull Request as ready for review.
- For C/C++ files, apply `astyle`.
- Format only files touched by your change.
- Do not reformat unrelated files.

## Static analysis

- Run `clang-tidy` before opening a Pull Request.
- Use the repository-local `.clang-tidy` configuration.
- Do not enable extra checks for project code unless the Pull Request changes the clang-tidy policy itself.
- To run `clang-tidy` through CMake, configure with `-DNESTDAQ_ENABLE_CLANG_TIDY=ON`.

## Code style and naming

### C++

- Indent with 4 spaces.

### Naming

- `PascalCase` and `UpperCamelCase` mean the same naming style.
- Class and type names: `PascalCase` / `UpperCamelCase`.
- Namespaces: `snake_case`.
- Functions and member functions: prefer `lowerCamelCase`; `PascalCase` / `UpperCamelCase` is allowed for consistency with existing code.
- Variables: prefer `snake_case`; `lowerCamelCase` is allowed for consistency with existing code.
- `using` alias names are outside the scope of these naming rules.
  They may follow local readability requirements, external library conventions, or common short forms.
- Public `struct` data fields: `snake_case`.
- Private and protected class data members: `fPascalCase`.
- Static data members: start with `fg`, for example `fgPascalCase`.
- Static variables: start with `g`, for example `gPascalCase`.
- Constants: start with `k`, for example `kPascalCase`, or use `SCREAMING_SNAKE_CASE`.
- Macro names: `SCREAMING_SNAKE_CASE`.
- Enum constants: `kPascalCase`, `PascalCase` / `UpperCamelCase`, or `SCREAMING_SNAKE_CASE`.
- Base namespace for NestDAQ code: `nestdaq`.

### File naming

- Preferred file extensions: `.cpp` and `.hpp`.
- Allowed file extensions: `.cxx`, `.h`, `.hh`, and `.hxx`.
