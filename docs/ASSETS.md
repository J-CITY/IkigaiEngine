# Game assets

`assets/` is a local copy. It is not in Git (`ASSETS_PATH` stays `assets/` in code). After a clone, `python run_vs.py` / `python utils/setup_env.py` call `ensure_assets`: pull only when the local tree is missing or older than [`utils/assets.source.json`](../utils/assets.source.json).

Manual pull (any OS):

```text
python utils/sync_assets.py
python utils/sync_assets.py pull
python utils/sync_assets.py ensure
```

No rclone, no Google account, no tokens. The Drive folder is shared as **Anyone with the link** (view).

## When setup pulls

`run_vs.py` (unless `--skip-setup`) and `setup_env.py` compare a local stamp (`assets/.ikigai-assets-stamp.json`) to `assets.source.json` (`mode`, `version`, `sha256`, Drive ids).

| Local state | Action |
|-------------|--------|
| Missing or empty `assets/` | Pull |
| Stamp older than `assets.source.json` | Pull |
| Stamp matches | Skip |
| Local stamp version is newer (same Drive source) | Skip — do not overwrite unpublished local work |
| Assets exist, no stamp, version/hash empty | Stamp as current, no download |

`--skip-setup` skips this check (offline / already synced).

## Pull (anyone)

Python 3 is enough. The script installs [gdown](https://github.com/wkentaro/gdown) on first run.

| Action | What it does |
|--------|----------------|
| `pull` (default) | Download from Drive and merge into `assets/`. Extra local files stay. |
| `pull-mirror` | Same download, then make `assets/` match the source (deletes extras). |
| `ensure` | Pull only if missing or stale. |

```text
python utils/sync_assets.py pull
python utils/sync_assets.py pull-mirror
python utils/sync_assets.py pull --dry-run
```

`mode` in `assets.source.json`:

- **`folder`** — list the public folder, then download each file with a current browser user-agent and without gdown's cookie jar. Google sometimes still refuses a file after many downloads (`Cannot retrieve the public link` / many accesses). The script waits and retries that file instead of dropping the whole pull.
- **`zip`** — download one public zip by `driveFileId`, check `sha256`, unpack. Prefer this once a packed archive exists.

## Push (publisher only)

Upload needs [rclone](https://rclone.org/) configured once on this machine. The OAuth file stays local (`%APPDATA%\rclone\rclone.conf` on Windows, `~/.config/rclone/rclone.conf` on Linux/macOS) and is not committed.

Default push is **`rclone copy`**: it adds/updates files and does **not** delete leftovers on Drive.

```text
python utils/sync_assets.py push --dry-run
python utils/sync_assets.py push
```

If the rclone remote name is not unique, create `utils/assets.remote.json` (gitignored):

```json
{
  "rcloneRemote": "gdrive",
  "rclonePath": ""
}
```

Empty `rclonePath` uses `--drive-root-folder-id` from `assets.source.json`. You can also set `IKIGAI_RCLONE_REMOTE`.

### Mirror (deletes on the destination)

`rclone sync` removes files on the receiver that are gone from the source. Do not use this by accident.

```text
python utils/sync_assets.py push-mirror --dry-run
python utils/sync_assets.py push-mirror
```

Run `--dry-run` before the first real `push-mirror`.

## Pack (zip + version)

After you change `assets/` locally and want a stable Pull:

```text
python utils/sync_assets.py pack --use-zip-pull
python utils/sync_assets.py push
```

`pack` writes `utils/.pack/IkigaiEngine-assets.zip` and updates `version` + `sha256` in `assets.source.json`. `--use-zip-pull` sets `mode` to `zip`. `push` then uploads that zip and fills `driveFileId` when rclone can see it.

Commit `utils/assets.source.json` after pack/push so other clones pull the new archive.

## Layout

| Path | In Git | Role |
|------|--------|------|
| `utils/sync_assets.py` | yes | Pull / Pack / Push / ensure |
| `utils/assets.source.json` | yes | public folder/file ids, version, hash |
| `utils/assets.remote.json` | no | rclone remote name override |
| `utils/.pack/` | no | local zip from pack |
| `docs/ASSETS.md` | yes | this page |
| `assets/` | no | local tree (`engine/`, `game/`) |

Do not commit rclone tokens or `assets.remote.json`.
