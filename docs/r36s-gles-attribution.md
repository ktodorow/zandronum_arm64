# Native GLES source provenance

This manifest records code actually imported or adapted for the native GLES backend. Architectural references are listed separately; a reference does not mean its source was copied.

## Adapted upstream code

| Local code | Adapted upstream code | Changes |
| --- | --- | --- |
| `src/gl/system/gles_compat.cpp`: `Compile` and compile/link diagnostics in `BasicProgram` | [`FShaderProgram::CompileShader`, `Link`, `GetShaderInfoLog`, `GetProgramInfoLog`](https://github.com/ZDoom/gzdoom/blob/c26ce2e6ca2a0c770f140cb25dde0d30073ca8f7/src/common/rendering/gles/gles_shaderprogram.cpp) | Local free functions and error API; logs sized using `GL_INFO_LOG_LENGTH` instead of a fixed 10,000-byte buffer; local basic shader and attribute bindings; shader objects deleted after linking |

The adapted diagnostic code retains Magnus Norddahl's copyright and complete zlib-style notice in `gles_compat.cpp`, with the altered source explicitly identified. The notice is reproduced below for package documentation.

Native context initialization in `src/gl/system/gl_interface.cpp`, scene state, matrix handling and vertex submission are local implementation. Existing file notices remain in force. The basic GLSL source is local code; the adapted block is the compiler/linker failure handling, not GZDoom's shader assets.

Add an entry here, with the original copyright and license notice in the affected source file, whenever another upstream block is adapted. Identify the local function, upstream function, exact source commit and substantive changes.

### Shader compiler handling notice

```text
Copyright (c) 2016-2020 Magnus Norddahl

This software is provided 'as-is', without any express or implied
warranty.  In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.
```

## Pinned architectural references

The research donor is [GZDoom commit `c26ce2e6ca2a0c770f140cb25dde0d30073ca8f7`](https://github.com/ZDoom/gzdoom/tree/c26ce2e6ca2a0c770f140cb25dde0d30073ca8f7), fetched on 2026-10-15.

| Upstream source | Relevant design | File notice |
| --- | --- | --- |
| [`src/common/rendering/gles/gles_buffers.cpp`, lines190–205](https://github.com/ZDoom/gzdoom/blob/c26ce2e6ca2a0c770f140cb25dde0d30073ca8f7/src/common/rendering/gles/gles_buffers.cpp#L190) | `GLBuffer::Unlock` nonmapped fallback replaces full storage through `glBufferData(..., GL_STATIC_DRAW)`; `SetData` selects static or stream usage. Local batch upload independently uses whole-store replacement with `GL_STREAM_DRAW` to avoid measured Mali subupdate stalls. Upstream owns one buffer ID per `GLBuffer`; this path provides no rotating-pool implementation or pool-performance evidence. Upstream partial-update strategy was not retained locally | Copyright 2018–2020 Christoph Oelckers; three-clause BSD notice; design reference only |
| [`src/common/rendering/gles/gles_shaderprogram.cpp`](https://github.com/ZDoom/gzdoom/blob/c26ce2e6ca2a0c770f140cb25dde0d30073ca8f7/src/common/rendering/gles/gles_shaderprogram.cpp) | `FShaderProgram::CompileShader`, `Link`, `GetShaderInfoLog`, `GetProgramInfoLog`: explicit compile/link status and named driver diagnostics | Copyright 2016–2020 Magnus Norddahl; zlib-style notice |
| [`wadsrc/static/shaders_gles/glsl/material_normal.fp`, lines42–108](https://github.com/ZDoom/gzdoom/blob/c26ce2e6ca2a0c770f140cb25dde0d30073ca8f7/wadsrc/static/shaders_gles/glsl/material_normal.fp#L42) | Fixed-bound GLSL ES1.00 light loops stop at actual active-light count. Local `main.fp` independently applies generic early termination to its existing light loop, retaining all light categories and full supported count | Design reference only; no donor shader body imported and no permissive shader license inferred |

Modern GZDoom GLSL assets and renderer files with other license notices are not covered by these two permissive notices. The native backend keeps the existing Zandronum shader source lineage unless a separate, documented import is made. See [upstream reuse research](../research_notes/Direct%20GLES%20for%20R36S/upstream_reuse.md) for the full source and license audit.
