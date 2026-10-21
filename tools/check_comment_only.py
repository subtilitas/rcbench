#!/usr/bin/env python3
"""Prove that a change between two commits touches comments only.

A documentation sweep rewrites comments and docstrings across many files.
This tool checks that nothing else changed: for every C or C header file
that differs between BASE and HEAD it strips the comments with the
compiler's own lexer (`gcc -fpreprocessed -dD -E -P`, no macro expansion,
no include) and compares the token streams with all whitespace collapsed.
For every Python file it compares the abstract syntax trees with the
docstrings removed.

    tools/check_comment_only.py BASE [HEAD]       HEAD defaults to the
                                                  working tree
    tools/check_comment_only.py BASE [HEAD] --path DIR ...
                                                  only these paths

It fails on

- a C/H file whose code tokens differ;
- a Python file whose syntax tree, docstrings aside, differs;
- a file that BASE or HEAD does not have (a sweep adds and removes none);
- a changed file of any other kind.

A changed line number moves `__LINE__`, so a byte compare of the compiled
objects can differ where `assert` or a logging macro expands it; this check
is the one that holds for every file.
"""

import argparse
import ast
import subprocess
import sys

C_SUFFIXES = (".c", ".h")
PY_SUFFIXES = (".py",)


def git(*args: str) -> str:
    """Run git and return its standard output.

    Args:
        *args: The git arguments.

    Returns:
        The output as text.

    Raises:
        subprocess.CalledProcessError: git exits non-zero.
    """
    return subprocess.run(("git",) + args, check=True, capture_output=True,
                          text=True).stdout


def read(ref: str | None, path: str) -> str | None:
    """Read a file at a commit, or from the working tree.

    Args:
        ref: The commit, or None for the working tree.
        path: The path from the repository root.

    Returns:
        The file's text, or None when the file does not exist there.
    """
    if ref is None:
        try:
            with open(path, encoding="utf-8") as f:
                return f.read()
        except FileNotFoundError:
            return None
    try:
        return git("show", f"{ref}:{path}")
    except subprocess.CalledProcessError:
        return None


def c_tokens(text: str) -> str:
    """Strip the comments from C source and collapse its whitespace.

    Args:
        text: The C or header source.

    Returns:
        The code with comments removed and every whitespace run as one
        space.

    Raises:
        RuntimeError: gcc cannot lex the source.
    """
    run = subprocess.run(
        ["gcc", "-fpreprocessed", "-dD", "-E", "-P", "-x", "c", "-"],
        input=text, capture_output=True, text=True)
    if run.returncode != 0:
        raise RuntimeError(run.stderr.strip())
    return " ".join(run.stdout.split())


def strip_docstrings(tree: ast.AST) -> ast.AST:
    """Remove the docstring of the module and of every function and class.

    Args:
        tree: A parsed module; changed in place.

    Returns:
        The same tree.
    """
    kinds = (ast.Module, ast.FunctionDef, ast.AsyncFunctionDef, ast.ClassDef)
    for node in ast.walk(tree):
        if not isinstance(node, kinds) or not node.body:
            continue
        first = node.body[0]
        if (isinstance(first, ast.Expr)
                and isinstance(first.value, ast.Constant)
                and isinstance(first.value.value, str)):
            node.body = node.body[1:] or [ast.Pass()]
    return tree


def py_tree(text: str) -> str:
    """Dump a Python module's syntax tree without docstrings.

    Args:
        text: The Python source.

    Returns:
        The tree as text, without line or column attributes.
    """
    tree = strip_docstrings(ast.parse(text))
    return ast.dump(tree, include_attributes=False)


def main() -> int:
    """Compare every changed file and print each failure.

    Returns:
        0 when every change is to comments only, 1 otherwise.
    """
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("base")
    parser.add_argument("head", nargs="?")
    parser.add_argument("--path", dest="paths", action="append",
                        default=[], metavar="PATH",
                        help="limit the comparison to this path; repeatable")
    args = parser.parse_args()

    diff = ["diff", "--name-only", args.base]
    if args.head:
        diff.append(args.head)
    if args.paths:
        diff += ["--"] + args.paths
    changed = [p for p in git(*diff).splitlines() if p]

    failures = []
    for path in changed:
        old, new = read(args.base, path), read(args.head, path)
        if old is None or new is None:
            failures.append(f"{path}: added or removed")
        elif path.endswith(C_SUFFIXES):
            try:
                if c_tokens(old) != c_tokens(new):
                    failures.append(f"{path}: code changed")
            except RuntimeError as err:
                failures.append(f"{path}: gcc: {err}")
        elif path.endswith(PY_SUFFIXES):
            try:
                if py_tree(old) != py_tree(new):
                    failures.append(f"{path}: code changed")
            except SyntaxError as err:
                failures.append(f"{path}: {err}")
        else:
            failures.append(f"{path}: not a C, header or Python file")

    for line in failures:
        print(line)
    print(f"check_comment_only: {len(changed)} file(s) changed, "
          f"{len(failures)} failure(s)")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
