# UI smoke test

Drives the showcase site in headless Chrome over the DevTools protocol: loads the site, clicks every step,
graph node, candidate, evidence chip, replay selector, slider, filter and view, presses keys, plays the
walkthrough, and fails loudly if any JavaScript error is thrown.

    make serve &              # http://localhost:8000
    cd tests/ui && npm i ws   # test-only dependency, not used by the product
    node ui_smoke.js          # prints one line per check and "errors": [] when clean

Requires Google Chrome at the macOS default path (edit `C` in the script for other platforms).
