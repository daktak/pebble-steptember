# Pebble App Template

A GitHub template for building Pebble Time apps and watchfaces. Includes a Docker-based build pipeline and GitHub Actions CI that produces `.pbw` artifacts and GitHub Releases on version tags.

The starter watchface displays a digital clock with time and date. Replace `src/c/main.c` with your own code.

## Using this template

1. Click **Use this template** → **Create a new repository**
2. Clone your new repository
3. Make the [required customizations](#customization) below
4. Push to GitHub — CI builds your `.pbw` automatically

## Customization

### Required

Edit `package.json`:

| Field                | Description                                                      |
| -------------------- | ---------------------------------------------------------------- |
| `pebble.uuid`        | **Must be unique per app.** Run `uuidgen` to generate a new one. |
| `pebble.displayName` | App name shown on the watch and in the Pebble app                |
| `pebble.shortName`   | Short launcher name (≤9 characters)                              |
| `name`               | Package name (lowercase, hyphens ok)                             |

Then replace `src/c/main.c` with your app logic.

### Optional

- `pebble.watchapp.watchface: false` — build a regular app instead of a watchface
- `pebble.targetPlatforms` — add or remove platforms:
  - `aplite` — Pebble / Pebble Steel (B&W, 144×168)
  - `basalt` — Pebble Time / Time Steel (color, 144×168)
  - `chalk` — Pebble Time Round (color, 180×180 round)
  - `diorite` — Pebble 2 SE (B&W, 144×168)
  - `emery` — Pebble Time 2 (color, 200×228)
  - `gabbro` — Pebble 2 Round (color, 260×260 round)
- `version` — must follow `major.minor.0` format (patch must be `0`)
- Add `src/pkjs/index.js` for a phone-side JavaScript component (see [JS notes](#javascript))

## Building

### With Docker (no SDK install required)

```bash
make docker-run
```

The `.pbw` will be in `build/`.

### With pebble-tool installed locally

```bash
# Install (requires Python 3.10–3.13)
uv tool install pebble-tool --python 3.13
pebble sdk install latest

make build
```

### Emulator

```bash
make pt1   # Pebble Time (basalt)
make pt2   # Pebble Time Round (emery)
```

## Releasing

Push a version tag — GitHub Actions will create a release with the `.pbw` attached:

```bash
git tag v1.0.0
git push origin v1.0.0
```

## JavaScript

If your app needs a phone-side JS component (fetching data, configuration page), add `src/pkjs/index.js` and update `wscript` to bundle it:

```python
ctx.pbl_bundle(binaries=binaries,
               js=ctx.path.ant_glob(['src/pkjs/**/*.js']),
               js_entry_file='src/pkjs/index.js')
```

**The Pebble SDK uses an ES5 JavaScript parser.** These features will break the build:

- Trailing commas in objects/arrays/calls
- Arrow functions — use `function() {}`
- Template literals — use `"a" + b`
- `const`/`let` — use `var`
- Spread, destructuring, or any ES6+ syntax

## CI

Every push triggers a build. Artifacts are available under **Actions → your run → Artifacts**. A GitHub Release is created automatically when a `v*` tag is pushed.
