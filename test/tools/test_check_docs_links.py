"""check_docs.py: the link and anchor walk over the root pages.

SPDX-License-Identifier: MIT
"""

import pytest

import check_docs as cd


@pytest.fixture
def tree(tmp_path, monkeypatch):
    """A temporary tree with one wiki page and the three link-only pages."""
    docs = tmp_path / "docs"
    docs.mkdir()
    (docs / "Page.md").write_text("# Page\n\n## A heading\n",
                                  encoding="utf-8")
    monkeypatch.setattr(cd, "REPO", tmp_path)
    monkeypatch.setattr(cd, "DOCS", docs)
    monkeypatch.setattr(cd, "IMG", docs / "img")
    monkeypatch.setattr(cd, "HARDWARE", tmp_path / "hardware")
    monkeypatch.setattr(cd, "ROOT_PAGES", [])
    monkeypatch.setattr(cd, "LINK_ONLY_PAGES",
                        [tmp_path / name for name in
                         ("CONTRIBUTING.md", "SECURITY.md", "CHANGELOG.md")])
    monkeypatch.setattr(cd, "help_images", set)
    return tmp_path


def walk(tree):
    """The problems of the link walk and the anchor walk over @p tree."""
    problems = []
    cd.check_links(problems)
    cd.check_anchors(problems)
    return problems


@pytest.mark.parametrize("name", ["CONTRIBUTING.md", "SECURITY.md",
                                  "CHANGELOG.md"])
def test_a_dead_link_in_a_root_page_fails(tree, name):
    """A link to a file that is gone fails in each of the three pages."""
    (tree / name).write_text("[gone](docs/Gone.md)\n", encoding="utf-8")
    assert walk(tree) == [f"{name}: link to docs/Gone.md goes nowhere"]


@pytest.mark.parametrize("name", ["CONTRIBUTING.md", "SECURITY.md",
                                  "CHANGELOG.md"])
def test_a_dead_anchor_in_a_root_page_fails(tree, name):
    """A link to a heading that is gone fails in each of the three pages."""
    (tree / name).write_text("[x](docs/Page.md#no-heading)\n",
                             encoding="utf-8")
    assert walk(tree) == [f"{name}: #no-heading points into Page.md, "
                          "which has no such heading"]


def test_links_that_open_pass(tree):
    """A page, a heading of it and a heading of the page itself pass."""
    (tree / "CHANGELOG.md").write_text(
        "# Changelog\n\n## Unreleased\n\n[a](docs/Page.md) "
        "[b](docs/Page.md#a-heading) [c](#unreleased) "
        "[d](https://example.org/x#y)\n", encoding="utf-8")
    assert walk(tree) == []


def test_a_root_page_that_is_missing_is_skipped(tree):
    """A tree without the three pages is walked without them."""
    assert walk(tree) == []
    assert cd.walked_pages() == cd.linked_pages()


def test_the_claim_checks_do_not_read_the_link_only_pages(tree):
    """The changelog is walked for links, not held to the tree's claims."""
    (tree / "CHANGELOG.md").write_text("# Changelog\n", encoding="utf-8")
    assert tree / "CHANGELOG.md" in cd.walked_pages()
    assert tree / "CHANGELOG.md" not in cd.linked_pages()


def test_the_tree_walks_the_three_root_pages():
    """The repository's walk holds CONTRIBUTING, SECURITY and CHANGELOG."""
    names = {p.name for p in cd.walked_pages()}
    assert {"CONTRIBUTING.md", "SECURITY.md", "CHANGELOG.md"} <= names
    assert walk(None) == []
