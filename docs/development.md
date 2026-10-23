# Development

## Layout

| Folder | What is in it |
|---|---|
| `firmware/` | the ESP-IDF project: [firmware/README.md](../firmware/README.md) |
| `plugins/` | the plugin SDK, the build and preview scripts, the standard plugins and examples: [plugins/README.md](../plugins/README.md) |
| `web/` | the web interface and a mock of the device's API: [web/README.md](../web/README.md) |
| `site/` | the root page of `cgdn1.swithy.com`: [site/README.md](../site/README.md) |
| `tools/` | `publish_fw.mjs` and `publish_web.mjs`, which sign and publish releases |
| `docs/` | these guides, [hardware.md](hardware.md), release notes, images |

## Formatting and lint

One root `npm install` sets up Prettier, which covers `web` and the root config files. Python, C and
shell go through `uvx`, so [uv](https://docs.astral.sh/uv/) has to be installed; ruff,
clang-format, shfmt and shellcheck are pinned in `package.json`.

```bash
npm run format          # rewrite JS, Python, C and shell
npm run format:check    # what CI runs
npm run lint            # eslint over web
npm run lint:py         # ruff check
npm run lint:sh         # shellcheck
```

Settings live in `.editorconfig` (shfmt reads it too), `.prettierrc`, `pyproject.toml` and
`.clang-format`. Markdown and the vendored u8g2 are left alone. A program longer than a line goes in
its own file next to the script that runs it, never in a heredoc, where none of these checks see it.
