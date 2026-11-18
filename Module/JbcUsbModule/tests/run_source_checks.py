"""Run both historical import-time assertions and plain test_* functions."""
from pathlib import Path
import runpy


def main():
    files = functions = 0
    for path in sorted(Path(__file__).resolve().parent.glob("test_*.py")):
        namespace = runpy.run_path(str(path))
        files += 1
        for name, check in namespace.items():
            if name.startswith("test_") and callable(check):
                check()
                functions += 1
    print(f"PASS: {files} JBC USB source/codec scripts and {functions} test functions")


if __name__ == "__main__":
    main()
