#!/bin/bash
# web/build_site.sh: the web page in web/_site, ready for GitHub Pages: the pinned core.wasm (its hash checked; nothing is
# compiled, see build.sh) and the page's script bundled (esbuild). The ROMs and Elite are fetched by the page, not part of the site.
set -e
cd "$(dirname "$0")"
{ command -v sha256sum >/dev/null && sha256sum -c core.wasm.sha256; } || shasum -a 256 -c core.wasm.sha256
[ -d node_modules/esbuild ] || npm ci --no-audit --no-fund -s
rm -rf _site && mkdir -p _site
SHA1() { command -v sha1sum >/dev/null && sha1sum || shasum; }
AV=$(cat core.wasm panel.json galaxies.json | SHA1 | cut -c1-10)
npx esbuild src/main.js --bundle --format=esm --minify --outfile=_site/app.js --define:ASSET_V="\"$AV\"" --log-level=warning
npx esbuild src/paper.js --bundle --format=esm --minify --outfile=_site/paper.js --log-level=warning
cp index.html style.css core.wasm panel.json galaxies.json gh.png og.png blog.html paper-cmdr.png paper-presto.jpg paper_senses.json paper_fair.json _site/
mkdir -p _site/chapters && cp chapters/deadly.cmdr _site/chapters/
V=$(cat _site/app.js _site/style.css | SHA1 | cut -c1-10)
perl -pi -e '$_ = "" if /<iframe id="beeb"/; s|href="style.css"|href="style.css?v='$V'"|; s|src="app.js"|src="app.js?v='$V'"|' _site/index.html
printf 'elite-agent web page (a fork, with four games: Solid Elite on the 6502 Second Processor, the Acorn Electron, the BBC Micro\nand the BBC Master). Its source: https://github.com/ebenupton/elite-agent (web/), forked from\nhttps://github.com/8bitkick/elite-agent. core.wasm includes Kieran Connell'"'"'s BBC Master core (https://github.com/kieranhj/esp32-beeb,\nGPL-3.0), also built as a BBC Model B, a Tube ULA after jsbeeb'"'"'s (https://github.com/mattgodbolt/jsbeeb, GPL-3.0) and an Acorn\nElectron after Clock Signal'"'"'s ULA, so this page as a whole is offered under the GPL-3.0-or-later; elite-agent'"'"'s own code is also\nMIT licensed. Elite (c) Ian Bell and David Braben and the machines'"'"' ROMs are not part of this site: the browser fetches them\nfrom public repositories at the commits in pins.json.\n' > _site/LICENSES.txt
cp pins.json UPLOAD.txt _site/
touch _site/.nojekyll
du -sh _site
