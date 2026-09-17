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

## Publish with GitHub Pages

1. In the repository's **Settings > Pages**, select **GitHub Actions** as the
   publishing source.
2. Push the documentation configuration to `main`. The **Documentation** workflow
   builds and checks the site, then deploys it. Later documentation changes on
   `main` publish automatically. Pull requests run the build without deployment.
3. Open the URL shown by the workflow's **github-pages** environment. The default
   address for this repository is `https://nnamllit1.github.io/hammer-addons/`.

To republish without editing a page, open **Actions > Documentation > Run workflow**
and select `main`. Forks should set the repository Actions variable
`DOCS_SITE_URL` to their own complete Pages URL, including its trailing slash.
For local fork builds, change `site_url` in `mkdocs.yml` or set the same environment
variable.

See [GitHub's custom workflow guide](https://docs.github.com/en/pages/getting-started-with-github-pages/using-custom-workflows-with-github-pages).

## Connect a custom domain

For a subdomain such as `docs.example.com`:

1. Verify domain ownership in GitHub's account Pages settings, then set
   **Settings > Pages > Custom domain** in the repository to `docs.example.com`.
2. At the DNS provider, create a **CNAME** record named `docs` pointing to
   **`nnamllit1.github.io`**. Use the account hostname, without `https://` or the
   repository path. For a fork, use its owner's Pages hostname.
3. In **Settings > Secrets and variables > Actions > Variables**, set
   `DOCS_SITE_URL` to `https://docs.example.com/` and rerun the Documentation
   workflow. This updates canonical URLs and the sitemap.
4. Once GitHub's DNS check and certificate provisioning finish, enable
   **Enforce HTTPS** in Pages settings.

The Actions deployment uses the custom domain in GitHub's settings; it does not
require a checked-in `CNAME` file. For an apex domain such as `example.com`, use
the DNS records in [GitHub's domain guide](https://docs.github.com/en/pages/configuring-a-custom-domain-for-your-github-pages-site/managing-a-custom-domain-for-your-github-pages-site).

To preview a different base URL locally in PowerShell:

```powershell
$env:DOCS_SITE_URL = 'https://docs.example.com/'
.venv-docs\Scripts\python.exe -m mkdocs build --strict
```

## Other hosting providers

Build with the intended `DOCS_SITE_URL` and upload the contents of `site/` to
any static web host. No Python server or database is needed in production.
Preserve the directory structure and configure the host to serve `index.html`
inside each directory.
