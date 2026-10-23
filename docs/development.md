# Development

## Layout

| Folder | What is in it |
|---|---|
| `firmware/` | the ESP-IDF project: [firmware/README.md](../firmware/README.md) |
| `plugins/` | the plugin SDK, the build and preview scripts, the standard plugins and examples: [plugins/README.md](../plugins/README.md) |
| `web/` | the web interface and a mock of the device's API: [web/README.md](../web/README.md) |
| `site/` | the root page of `cgdn1.swithy.com`: [site/README.md](../site/README.md) |
| `tools/` | `publish_fw.py` and `publish_web.py`, which sign and publish releases |
| `docs/` | these guides, [hardware.md](hardware.md), release notes, images |

## Formatting

One root `npm install` sets up Prettier, which covers `web` and the root config files. Python and C
go through `uvx`, so [uv](https://docs.astral.sh/uv/) has to be installed; ruff and clang-format
are pinned in `package.json`.

```bash
npm run format          # rewrite JS, Python and C
npm run format:check    # what CI runs
```

Settings live in `.editorconfig`, `.prettierrc`, `pyproject.toml` and `.clang-format`. Markdown and
the vendored u8g2 are left alone.
