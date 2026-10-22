# Pacitum extensions

Pacitum 1.8.5 can load small extensions: user scripts and user styles that run on the sites you choose.

Load one with **Extensions (puzzle button) → Load extension folder…** and pick a folder containing a `manifest.json`.

```json
{
  "name": "YouTube Focus",
  "version": "1.0",
  "description": "Hides recommendations, Shorts and comments on YouTube.",
  "matches": ["*://www.youtube.com/*"],
  "exclude_matches": [],
  "css": ["focus.css"],
  "js": ["focus.js"],
  "run_at": "document_idle"
}
```

- `matches` / `exclude_matches`: Chrome match patterns (`*://*.example.com/*`, `<all_urls>`).
- `run_at`: `document_start`, `document_end` or `document_idle` (default).
- A Chrome extension whose only feature is `content_scripts` also loads (same keys in each entry).
- Scripts run in an isolated world. There are no `chrome.*` APIs, background pages or popups.

The installed copy lives in your profile's `extensions` folder; turn it on or off from the Extensions menu.
