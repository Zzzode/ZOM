import sys
import re
import os


def check_file(filename, prefix):
    errors = []
    with open(filename, 'r') as f:
        for i, line in enumerate(f, 1):
            match = re.search(r'#include\s*"([^"]+\.h)"', line)
            if match and not match.group(1).startswith(prefix):
                included_path = match.group(1)
                if prefix.endswith('/'):
                    suggested_include = f"{prefix}.../{included_path}"
                else:
                    suggested_include = f"{prefix}/.../{included_path}"
                errors.append({
                    'file': filename,
                    'line': i,
                    'content': line.strip(),
                    'suggestion': suggested_include
                })
    return errors


def main():
    args = sys.argv[1:]
    if len(args) % 2 != 0:
        print("Error: Each directory must have a corresponding prefix")
        sys.exit(1)

    all_errors = []
    for i in range(0, len(args), 2):
        directory = args[i]
        prefix = args[i + 1]
        for root, _, files in os.walk(directory):
            for file in files:
                if file.endswith(('.cc', '.h')):
                    all_errors.extend(check_file(os.path.join(root, file), prefix))

    if all_errors:
        print(f"Found {len(all_errors)} invalid include(s):")
        for error in all_errors:
            print(f"{error['file']}:{error['line']}")
            print(f"  {error['content']}")
            print(f"  Suggestion: #include \"{error['suggestion']}\"")
        sys.exit(1)
    # Remove the else clause to avoid printing when no errors are found


if __name__ == "__main__":
    main()
