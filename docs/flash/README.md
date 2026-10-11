# Browser flasher (GitHub Pages)

`index.html` is the page served at `https://figment-sieve-industries.github.io/esp32-4848s040-lab/`. It is not served from this folder directly: `.github/workflows/release.yml` copies it, together with the merged firmware images and the ESP Web Tools manifests it builds, into the Pages artifact. The page refers to `manifest-<project>.json` and `firmware/<project>.bin` by relative path.

To preview the layout locally, build the images (see the workflow), put the manifests next to `index.html` and the `.bin` files in `firmware/`, and serve the folder over `http://localhost` (Web Serial needs HTTPS or localhost).

One-time repository setup and the reasoning: ADR-36 in `docs/guide/04-patterns-and-decisions.md`.
