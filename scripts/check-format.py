# Copyright 2023 The pRimTS Authors. All rights reserved.

import os
import subprocess
import sys
import argparse

# ANSI escape sequences for colored output
RED = "\033[91m"
GREEN = "\033[92m"
YELLOW = "\033[93m"
RESET = "\033[0m"

formatted_files = []  # Global list to track formatted files


def check_and_format(file_path, auto_format=False):
    """Check if the given file is formatted properly using clang-format and optionally format it."""
    try:
        # Run clang-format on the file.
        formatted = subprocess.run(
            ["clang-format", file_path], capture_output=True, text=True
        )

        # Read the contents of the file.
        with open(file_path, "r") as file:
            content = file.read()

        same = formatted.stdout == content
        if same:
            return True
        else:
            if auto_format:
                # Write the formatted content back to the file
                with open(file_path, "w") as file:
                    file.write(formatted.stdout)
                print(f"{YELLOW}Automatically formatted {file_path}{RESET}")
                formatted_files.append(
                    file_path
                )  # Add the file to the list of formatted files
                return True
            else:
                return False

    except Exception as e:
        print(f"{RED}Error checking/formatting file {file_path}: {e}{RESET}")
        return False


def get_changed_files():
    """Get a list of changed files in the last commit and uncommitted tracked files."""
    print(f"Retrieving list of changed files since last commit...")

    # Check the number of commits in the repository.
    commit_count_result = subprocess.run(
        ["git", "rev-list", "--count", "HEAD"], capture_output=True, text=True
    )
    if (
        commit_count_result.returncode == 0
        and int(commit_count_result.stdout.strip()) > 1
    ):
        # There is more than one commit, so we can use HEAD~1
        result = subprocess.run(
            ["git", "diff", "--name-only", "--diff-filter=d", "HEAD~1", "HEAD"],
            capture_output=True,
            text=True,
        )
    else:
        # This is the first commit, use git show
        result = subprocess.run(["git", "fetch"], capture_output=True, text=True)
        result = subprocess.run(
            ["git", "diff", "--name-only", "--diff-filter=d", "origin/dev", "--", "."],
            capture_output=True,
            text=True,
        )

    if result.returncode != 0:
        print(
            f"{RED}Failed to get changed files from git. Error message: {result.stderr}{RESET}"
        )
        sys.exit(result.returncode)

    # Get uncommitted but tracked files
    uncommitted_result = subprocess.run(
        ["git", "diff", "--name-only", "--diff-filter=d"],
        capture_output=True,
        text=True,
    )
    if uncommitted_result.returncode != 0:
        print(
            f"{RED}Failed to get uncommitted files from git. Error message: {uncommitted_result.stderr}{RESET}"
        )
        sys.exit(uncommitted_result.returncode)

    # Get staged but not committed files
    staged_result = subprocess.run(
        ["git", "diff", "--cached", "--name-only"], capture_output=True, text=True
    )
    if staged_result.returncode != 0:
        print(
            f"{RED}Failed to get staged files from git. Error message: {staged_result.stderr}{RESET}"
        )
        sys.exit(staged_result.returncode)

    # Combine the results
    changed_files = (
        result.stdout.split()
        + uncommitted_result.stdout.split()
        + staged_result.stdout.split()
    )

    # Filter the list for allowed extensions.
    allowed_extensions = {".c", ".cpp", ".cc", ".h", ".hpp"}
    # TODO: remove skip runtime judge after runtime format is determined
    filtered_files = [
        f
        for f in changed_files
        if os.path.splitext(f)[1] in allowed_extensions
        and (
            not f.startswith("third_party/")
            and not f.startswith("src/rtsvm/")
            and not f.startswith("src/rts/")
            and not f.startswith("test/rtsvm/")
            or f.startswith("src/rtsvm/vm/heap/")
        )
    ]

    if filtered_files:
        print(f"Files changed (relevant to format check):")
        for file_path in filtered_files:
            print(f"{GREEN} - {file_path}{RESET}")
    else:
        print(f"{GREEN}No files changed that require format checking.{RESET}")

    return filtered_files


def main():
    parser = argparse.ArgumentParser(
        description="Check and optionally auto-format files."
    )
    parser.add_argument(
        "--auto-format",
        action="store_true",
        help="Automatically format unformatted files",
    )
    args = parser.parse_args()

    # Check if clang-format is available
    clang_format_available = (
        subprocess.run(
            ["which", "clang-format"], capture_output=True, text=True
        ).returncode
        == 0
    )
    if not clang_format_available:
        print(
            f"{RED}clang-format is not available. Please install it or check your PATH.{RESET}"
        )
        sys.exit(1)

    # Check if we are in a git repository
    git_repo_check = (
        subprocess.run(
            ["git", "rev-parse", "--is-inside-work-tree"],
            capture_output=True,
            text=True,
        ).returncode
        == 0
    )
    if not git_repo_check:
        print(f"{RED}Current directory is not a git repository.{RESET}")
        sys.exit(1)

    # Get the list of changed files that need to be checked.
    changed_files = get_changed_files()

    # Check the format of each changed file and optionally format it.
    unformatted_files = [
        f for f in changed_files if not check_and_format(f, args.auto_format)
    ]

    # Check if there are any unformatted files.
    if unformatted_files:
        num_unformatted = len(unformatted_files)
        print(
            f"{RED}Found {num_unformatted} file(s) that are not formatted correctly:{RESET}"
        )
        for i, file_path in enumerate(unformatted_files, start=1):
            print(f"{RED}{i}. {file_path}{RESET}")
        sys.exit(1)

    print(f"{GREEN}All changed files are formatted correctly.{RESET}")
    sys.exit(0)


if __name__ == "__main__":
    main()
