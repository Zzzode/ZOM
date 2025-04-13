# Contributing to Zompiler

We love your input! We want to make contributing to Zompiler as easy and transparent as possible, whether it's:

- Reporting a bug
- Discussing the current state of the code
- Submitting a fix
- Proposing new features
- Becoming a maintainer

## Development Process

We use GitHub to host code, to track issues and feature requests, as well as accept pull requests.

1. Fork the repo and create your branch from `main`.
2. If you've added code that should be tested, add tests.
3. If you've changed APIs, update the documentation.
4. Ensure the test suite passes.
5. Make sure your code lints.
6. Issue that pull request!

## Coding Standards

To ensure consistency throughout the source code, please follow these rules:

1. All features or bug fixes **must be tested** by one or more specs (unit tests).
2. All public API methods **must be documented** with Doc comments.
3. Follow our [ClangFormat configuration](.clang-format).
4. Maintain consistent code formatting:
   - Use 2 spaces for indentation
   - End files with a newline
   - Remove trailing whitespace
   - Keep lines under 100 characters

## Commit Message Guidelines

We follow the [Conventional Commits](https://www.conventionalcommits.org/) specification. This leads to more readable messages that are easy to follow when looking through the project history.

### Commit Message Format

Each commit message consists of a **header**, a **body** and a **footer**. The header has a special format that includes a **type**, a **scope** and a **subject**:

```
<type>(<scope>): <subject>
<BLANK LINE>
<body>
<BLANK LINE>
<footer>
```

Example:

```
feat(parser): add ability to parse arrays

This change introduces array parsing capability to the compiler.
The parser now supports both single and multi-dimensional arrays.

Closes #123
```

### Type

Must be one of the following:

- **feat**: A new feature
- **fix**: A bug fix
- **docs**: Documentation only changes
- **style**: Changes that do not affect the meaning of the code
- **refactor**: A code change that neither fixes a bug nor adds a feature
- **perf**: A code change that improves performance
- **test**: Adding missing tests or correcting existing tests
- **chore**: Changes to the build process or auxiliary tools

## Pull Request Process

1. Update the README.md with details of changes to the interface, if applicable.
2. Update the version numbers in any examples files and the README.md to the new version.
3. Include relevant tests for any new functionality.
4. Ensure your PR description clearly describes the problem and solution.
5. The PR will be merged once you have the sign-off of at least one other developer.

## Any Contributions You Make Will Be Under the Project License

In short, when you submit code changes, your submissions are understood to be under the same [license](LICENSE) that covers the project. Feel free to contact the maintainers if that's a concern.

## Report Bugs Using GitHub's Issue Tracker

We use GitHub issues to track public bugs. Report a bug by [opening a new issue](../../issues/new).

## Write Bug Reports with Detail, Background, and Sample Code

**Great Bug Reports** tend to have:

- A quick summary and/or background
- Steps to reproduce
  - Be specific!
  - Give sample code if you can.
- What you expected would happen
- What actually happens
- Notes (possibly including why you think this might be happening, or stuff you tried that didn't work)

## Code Review Process

The core team looks at Pull Requests on a regular basis following this process:

1. Check if the PR description is adequate
2. Check if all CI checks are passing
3. Review the code for:
   - Functionality
   - Complexity
   - Code style
   - Documentation
4. Request changes if necessary
5. Approve and merge when satisfied

## License

By contributing, you agree that your contributions will be licensed under its project License.
