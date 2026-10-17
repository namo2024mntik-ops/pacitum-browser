# Ad blocker (FilterEngine)

Replaces the old exact-host list + substring patterns.

## What changed in behavior
| Before | Now |
|---|---|
| Exact hostname match only (`ads.doubleclick.net` was **not** blocked) | `||domain^` rules match the domain **and all subdomains** |
| Substring patterns on the whole URL: `/ads/`, `/ad/`, `/banner`, `/tracking`, `/pixel`, `sponsored`, `advertisement` blocked normal pages (parcel tracking, `banner.png`, articles with "sponsored" in the URL) | Only specific third-party rules (e.g. `/pagead/`, `/adserver/`) |
| Vendor sites were blocked when you *visited* them (taboola.com, hotjar.com, ...) | Rules use `$third-party`; main-frame navigations are never blocked |
| `facebook.com/tr` was in the host list and never matched | `||facebook.com/tr^$third-party` |
| No way to add lists | Drop EasyList-style `.txt` files into the filters folder |

## Using your own lists
Put `*.txt` files (EasyList, EasyPrivacy, uBlock "filters" etc.) into:

    %LOCALAPPDATA%\Pacitum\Pacitum Browser\filters\

They are loaded on a background thread at startup (60k rules parse in ~0.1 s) and added to the built-in rules.
Restart the browser after changing files. Your own exceptions go in any file: `@@||example.com^`.

## Supported syntax
`||host^`, `|start`, `end|`, `*`, `^`, `@@` exceptions, options `third-party` / `~third-party`,
`domain=a.com|~b.com`, and resource types `script image stylesheet xmlhttprequest subdocument media font ping object other websocket` (with `~` to exclude).

Not supported (rule is skipped, never half-applied): cosmetic/element-hiding rules (`##`), regex rules,
`redirect`, `csp`, `removeparam`, `badfilter`, `document`, `popup`, `$all`.

## Tests
    cmake -S tests -B build-tests -G Ninja && cmake --build build-tests && ctest --test-dir build-tests --output-on-failure
or with the main project: `cmake -B build -DPACITUM_BUILD_TESTS=ON ...`
