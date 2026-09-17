# Documentation website

The website uses [MkDocs](https://www.mkdocs.org/) with
[Material for MkDocs](https://squidfunk.github.io/mkdocs-material/).
Pages live in `docs/`; `mkdocs.yml` defines navigation and appearance.
The same Markdown files remain readable on GitHub and in source packages.

## Preview locally

Python 3.11+ is required. From the repository root on Windows:

```powershell
python -m venv .venv-docs
.venv-docs\Scripts\python.exe -m pip install -r requirements-docs.txt
.venv-docs\Scripts\python.exe -m mkdocs serve
```

Open **http://127.0.0.1:8000/**. Saved documentation changes refresh the preview.
Press **Ctrl+C** to stop. Subsequent previews only need the last command.
On macOS/Linux, use `.venv-docs/bin/python` instead.

To build static files and check navigation, page links, and anchors:

```powershell
.venv-docs\Scripts\python.exe -m mkdocs build --strict
```

Output goes into `site/`, which is ignored by Git. This check does not verify
external websites. Documentation builds do not require CS2, Visual Studio, or Qt.

## Edit pages

Add new pages to the `nav` section of `mkdocs.yml`. Link to other guides using
their Markdown filenames, such as `[Reloading](HOT_RELOAD.md)`; MkDocs converts
these into website URLs. Link to source files outside `docs/` using their full
GitHub URLs so links work on both GitHub and the published site.

Write for users, add-on authors, and contributors. Describe supported behavior,
examples, and limitations. Release notes describe the corresponding release;
the rest of the website tracks `main`.

## Automated checks

The Documentation workflow checks pull requests with a strict MkDocs build.
Documentation changes merged into main are published automatically.
