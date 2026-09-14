# c-cmaes vendor snapshot

Upstream: https://github.com/CMA-ES/c-cmaes

Pin: commit 4450d3deccf2aacb6aa955d8216cfc4461699c60 (upstream master at retrieval, 2026-09-14)

Provenance: the vendored C sources are **comment-stripped from that pin**, not byte-identical to upstream. A lexer-aware mechanical pass removed C block and line comments only: string and character literals, escapes, code, directives, and the newline structure (and therefore line numbering) are preserved, and a second lexer pass verifies no comment tokens remain. No other change was made. The upstream `LICENSE` file is retained byte-identical; it is the project's short dual-license notice rather than the full terms of either license, so the complete canonical Apache 2.0 terms are shipped separately as described below.

All removed copyright, license, and attribution notices are retained verbatim in `UPSTREAM_NOTICES` (plain text): the complete leading notice comment blocks of `src/cmaes.c`, `src/cmaes.h`, and `src/cmaes_interface.h` at the pin, byte for byte, with a byte-exact round-trip verification against freshly fetched pin sources. A scan of the unmodified pin files confirms those three blocks are the only copyright, license, or attribution notices in the vendored sources.

Upstream reference hashes at the pin (unmodified files):

| Upstream file | sha256 (unmodified upstream) |
| --- | --- |
| `src/cmaes.c` | `d5825571e3f65777fd532d43acb2801c06e8e7d1dba341ca79ddd6d0580e3e0c` |
| `src/cmaes.h` | `598ed77a410da273c91da8afc14e0a138a056ce7fe3fe1d73ec486529f2a47d7` |
| `src/cmaes_interface.h` | `9ed046c369e5522e1bc7466d5bbab36e1c1c71d134c7c699045664a2a215127f` |
| `LICENSE` | `2cd9a8656ed11cf71407acde0db6dd44cef4a02ac4003d281c29fc38aecb0dcb` |

Vendored files as shipped:

| File | sha256 (comment-stripped) |
| --- | --- |
| `src/cmaes.c` | `6bcbf428af87446c06ff906a59f16686ee6b5ae810048ba2bb687bde18a2409d` |
| `src/cmaes.h` | `10422cba74a119b8a76cc83e7e36115036f524ed4ce9756a1e4377c0677a5738` |
| `src/cmaes_interface.h` | `6218efd5ab7a2038cb80773af82b776be64942861c7c307af30e8d5f785deca4` |
| `LICENSE` | `2cd9a8656ed11cf71407acde0db6dd44cef4a02ac4003d281c29fc38aecb0dcb` |
| `LICENSE.Apache-2.0` | `cfc7749b96f63bd31c3c42b5c471bf756814053e847c10f3eb003417bc523d30` |
| `UPSTREAM_NOTICES` | `44e390d53a6dab8481e204d90ddbb9f010cbe6d32b179a8b2aec0af5a3cea437` |

Upstream files not vendored: examples, parameter files, plot scripts, docs, build files, and the boundary transformation module, which this project does not use.

## License

Upstream is dual licensed under Apache License 2.0 and LGPL 2.1 or later. This vendoring selects Apache License 2.0, and the licensing files are:

- `LICENSE`: the pinned upstream file, retained byte-identical. It is the project's short dual-license notice (an author and copyright line, the statement that the library may be used under Apache License 2.0 or LGPL 2.1 or later, and pointers to the license texts). It does not contain the complete terms of either license.
- `LICENSE.Apache-2.0`: the complete canonical Apache License 2.0 terms for the selected license, fetched from https://www.apache.org/licenses/LICENSE-2.0.txt and retained byte-identical (nine numbered sections, END OF TERMS AND CONDITIONS, and the application appendix).
- `UPSTREAM_NOTICES`: the verbatim copyright, license, and attribution notice blocks removed from the three stripped source headers.

The exact copyright and attribution statements belong to those files, not to this summary: the upstream notice line reads "Author and copyright: Nikolaus Hansen, 2014", while the per-file notices in `UPSTREAM_NOTICES` carry per-file year ranges (for example 1996, 2003, 2007, 2013 in `cmaes.c`). Refer to `LICENSE` and `UPSTREAM_NOTICES` for the authoritative text. Upstream ships no separate `NOTICE` file.

## Library behavior notes

- Files are written only on error paths (`errcmaes.err` from the internal `ERRORMESSAGE` handler) and by `cmaes_Optimize`, `cmaes_WriteToFile*`, and `cmaes_ReadSignals`. The wrapper in `src/tournament_cmaes.cpp` calls none of these.
- Upstream quirk: `cmaes_init_final` appends `actparcmaes.par` unless `sp.filename` is a none string, and the programmatic init path leaves `sp.filename` NULL even when the caller passes `non`, so the file is written anyway. The wrapper therefore sets `sp.filename` to a heap copy of `non` between `cmaes_init_para` and `cmaes_init_final`, which suppresses the write and is freed by `cmaes_readpara_exit`. An empty working directory test in `src/tournament_cmaes_test.cpp` verifies that no incidental files appear.
- The library applies an initial standard deviation normalization (`sqrt(N / trace)` scaling with `sigma = sqrt(trace/N)`), so the effective initial per coordinate standard deviation equals the configured coordinate scales.
- Seeds below 1 fall back to a clock derived value. The wrapper rejects seeds outside [1, 2147483647].
- The lazy eigendecomposition can skip recomputation based on wall-clock timing (`updateCmode.maxtime`, default 0.20). The wrapper sets `sp.updateCmode.maxtime = 1.0` after initialization so sampling never depends on timing.

## Wrapper contract (src/tournament_cmaes.h)

- `ask()` and `tell(ordinal_fitness)` strictly alternate. `ask()` returns the flattened lambda by dimension population; entry `i * dimension + j` is coordinate `j` of sample `i`. `tell()` consumes one ordinal fitness value per sample, lower is better, matching the library's minimization convention. Values are used only through their ordering.
- `tell()` rejects values that are non-finite or flat in the sense that the best and the median ordinal value are equal, because the library responds to flat fitness by inflating sigma and emitting an error file.
- The `Optimizer` pimpl is exception-safe and tracks two teardown states: after `cmaes_init_para` the state is parameter-initialized and unwinding invokes `cmaes_readpara_exit`, releasing the read-parameter arrays; after `cmaes_init_final` the state is fully initialized and unwinding invokes `cmaes_exit`, which releases everything. A checked `none` filename marker allocation sits between the two states, and its failure throws `std::bad_alloc` with the parameter arrays still released. Tests exercise this through `tournament_cmaes::detail::set_none_marker_allocation_hook`, a test-only seam in the `detail` namespace that replaces the marker allocator; a LeakSanitizer negative control confirms the old single-phase teardown would leak the `new_void` read-parameter arrays on exactly this path.
- `save_state()` and `load_state()` serialize the exact evolution state for continuation: distribution, evolution paths, packed covariance, eigensystem, best-ever tracking, sort index, fitness history, and the complete random generator state. Blobs are valid only at a generation boundary (after `tell`, before the next `ask`). A blob can only be loaded into an optimizer built from a matching configuration (dimension, lambda, seed, and bitwise equal mean and coordinate scales).
- `load_state()` validates counts before allocating: the histogram length must be positive and equal the library-derived expected value before the histogram buffer is reserved, and every vector read is bounded by the remaining blob size, so malformed blobs throw `std::runtime_error` instead of attempting huge allocations.
- The blob requires 8-byte IEC 559 doubles, enforced by static assertions. Signed integer fields are encoded and decoded with `std::bit_cast`, so decoding is portable two's complement rather than relying on out-of-range unsigned-to-signed casts.

## State blob layout (version TCMAES02)

All fields are fixed width and explicitly little endian regardless of host byte order: unsigned 32 and 64 bit integers, signed integers encoded as their two's complement bit pattern through `std::bit_cast`, and IEEE-754 64-bit doubles encoded through their bit pattern. Sequential layout:

1. magic `TCMAES02`, 8 bytes
2. u32 dimension, u32 lambda, u64 seed
3. f64 mean[dimension], f64 coordinate_scales[dimension]
4. f64 sigma, f64 gen, f64 countevals, f64 state
5. f64 xmean[dimension], f64 xold[dimension], f64 ps[dimension], f64 pc[dimension]
6. packed lower triangular covariance C, row i has i+1 f64 values
7. f64 B[dimension][dimension] row major, f64 D[dimension]
8. f64 maxdiagC, f64 mindiagC, f64 maxEW, f64 minEW, f64 genOfEigensysUpdate, f64 dLastMinEWgroesserNull
9. i32 flgEigensysIsUptodate, i32 flgCheckEigen, i32 flgIniphase, i32 flgStop
10. f64 bestever x[dimension], f64 bestever fitness, f64 bestever evaluations
11. f64 last fitness values[lambda], i32 sort index[lambda]
12. i32 histogram length, f64 fitness histogram[histogram length]
13. i64 random startseed, i64 aktseed, i64 aktrand, i64 rgrand[32], i32 flgstored, f64 hold
14. u64 FNV-1a checksum of all preceding bytes
