# The commands. `just --list` shows them.

set shell := ["pwsh", "-NoProfile", "-Command"]

# every check: the Markdown, the editorconfig rules
check: check-md check-ec

# the Markdown linter over the repo
check-md *FILES:
    uvx rumdl check {{FILES}}

# the Markdown reflowed to 100 columns (never by hand)
fmt-md *FILES:
    uvx rumdl fmt {{FILES}}

# every text file against .editorconfig
check-ec:
    uvx --from editorconfig-checker ec

# Emscripten's environment, loaded per build so its Python and Node stay off PATH; on Windows
# only (LOCALAPPDATA set): elsewhere, as on GitHub's runner, `emcc` is on PATH already
emsdk_env := env('LOCALAPPDATA', '') / 'emsdk/emsdk_env.ps1'

# MSVC's environment, loaded per build (cl is not on PATH)
vcvars := 'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'

# a bare module, no Emscripten JavaScript: the worklet instantiates it itself
wasm_flags := '-O2 -std=c99 -Wall -Wextra --no-entry -s STANDALONE_WASM=1'
wasm_exports := '_page_init,_page_reverb,_page_render,_page_dry,_pf_note_on,_pf_note_off,_pf_control,_pf_config,_pf_calibration,_pf_calibrate,_pf_map,_pf_state'

# the page's C: the synth, the reverb, web/page.c
wasm_sources := '(Get-ChildItem src/*.c, web/page.c).FullName'

# the web page's WebAssembly module (emcc does not expand wildcards, so PowerShell does)
build-wasm:
    if ($env:LOCALAPPDATA) { & '{{emsdk_env}}' *> $null }; emcc {{wasm_flags}} -s EXPORTED_FUNCTIONS={{wasm_exports}} {{wasm_sources}} -o web/panflute.wasm

# the native harness, with MSVC (cl expands the wildcard itself)
build-harness:
    New-Item -ItemType Directory -Force build | Out-Null; cmd /c '"{{vcvars}}" >nul 2>&1 && cl /nologo /O2 /W4 /std:c11 /Fe:build/render.exe harness/render.c /Fo:build/'

# the harness played: a phrase (a swell, hops, a bend, a vibrato, ff, the release), the 25 pipes
# in turn at breath 12 (mf), and G4, D5, G6 at pp, mf, ff, to build/phrase.wav, scale.wav,
# levels.wav
render: build-harness
    ./build/render.exe build/phrase.wav; ./build/render.exe build/scale.wav scale 12; ./build/render.exe build/levels.wav levels

# the page served locally (any static server does; Web MIDI needs http://localhost or https)
serve:
    python -m http.server 8000 --directory web

# the page served over https on the local network, for the phone page (the microphone and the
# motion sensors need https): a self-signed certificate in build/https/, the addresses printed
serve-https:
    uv run tools/serve_https.py
