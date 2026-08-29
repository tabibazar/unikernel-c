# Unikernel and library-OS survey

**Surveyed 28 August 2026.** Twenty projects, ten fields each, produced by an autonomous
research agent — one stateless run per project, each field traced to a page the agent
actually fetched. Cited URLs are verified in code against what a run retrieved, so a
citation here was opened, not guessed.

Empty cells mean *the agent could not establish this from a source it read*. They are not
an assertion that the answer does not exist — and they are deliberately not filled in from
a model's background knowledge, which would turn 'unverified' into 'plausible-looking'.

## Summary

| | Projects |
|---|---|
| Active (committed to 2024 or later) | 8 — MirageOS, Unikraft, Nanos, HermitOS, Solo5, Gramine, ToroKernel, BareMetal |
| Gone quiet | 9 — LING (2015), Mini-OS (2016), ClickOS (2017), HaLVM (2018), UniK (2019), Rumprun (2020), OSv (2022*), IncludeOS (2023*), Unikernel Linux (2023) |
| Undetermined | 3 — Clive, Drawbridge, runtime.js |
| TLS support established | 3 — Nanos, OSv, Rumprun |

\* OSv and IncludeOS both published *relative* dates ('approximately two weeks ago',
'roughly 3 months ago as of August 2023') with no way to resolve them against a known
reference point. Their years come from the most recent dated release instead, and both
should be treated as uncertain rather than dormant.

### Corrections from a second pass (29 Aug 2026)

A focused follow-up run improved two cells and produced one outright error, both worth recording:

- **Clive is no longer unknown.** MIT licence, repository `github.com/fjballest/clive`, archived
  2 July 2022, no tagged releases. It should be counted among the dead, not the undetermined.
- **HermitOS does NOT use BearSSL.** A run reported that, citing `cheriot.org` — a real page it
  genuinely fetched, about the unrelated CHERIoT platform, which does use BearSSL. The citation
  check passed because the URL was real; the inference was false. HermitOS's TLS story remains
  unestablished.

That second item is the more useful of the two. Verifying that a cited page exists does not verify
that it is about the thing you asked, and a cheap domain-vs-subject comparison would have caught
it. The same sweep flags two more claims as weakly sourced: Gramine's TLS answer cites PyPI, and
Unikernel Linux's cites a general unikernel portal rather than the project.

Two further caveats worth stating plainly:

- **BareMetal's TLS cell is empty, and we know it does TLS** — we ran mbedTLS, lwIP and
  curl inside a 16 MiB instance and posted to a live API. No page the agent read stated it,
  so the table records what was sourced, not what we happen to know.
- **Only 3 of 20 could be shown to support TLS.** For the rest this is unstated rather than
  absent; it is the single field most likely to be wrong by omission.

## The table

| Project | Language | Licence | Latest release | Last commit | Status | Hypervisors | TLS | libc |
|---|---|---|---|---|---|---|---|---|
| baremetal | x86-64 Assembly | MIT | 2026.01 (January 2026) | 2026-06 | active | bare metal, virtualized environments | unknown | custom |
| clickos | C/C++ | MIT-like with a clause requiri | v0.2 (June 17, 2014) | October 16, 2017 | dormant | Xen | — | — |
| clive | — | — | — | — | — | — | — | — |
| drawbridge | C/C++ | — | — | — | archived | custom "picoprocess" isolation container | — | — |
| gramine | C | — | v1.9 on approximately June 18, | around June 20, 2024 | active | Intel SGX | — | — |
| halvm | Haskell | BSD 3-Clause "New" or "Revised | — | December 6, 2018 | dormant | Xen | — | — |
| hermitos | Rust | Apache License, Version 2.0 an | — | August 24, 2026 | — | KVM | — | — |
| includeos | C++ | Apache License 2.0 | v0.15.0 Cunning Conan, May 9,  | roughly 3 months ago (as of Au | active | x86, ukvm (via Solo5) | — | libc++ and libc++abi (ve |
| ling | Erlang, C | custom permissive license simi | v0.3.2r (February 4, 2015) | October 12, 2015 | dormant | Xen | — | — |
| mini-os | C | BSD-2-Clause, with some compon | xen-RELEASE-4.7.0, tagged on J | September 6, 2016 | dormant | Xen Project Hypervisor | — | minimal libc support |
| mirageos | OCaml | — | v4.11.2 on July 28, 2026 | July 2026 | active | Xen, KVM, FreeBSD's BHyve, OpenBSD's VMM | — | — |
| nanos | C | Apache-2.0 | 0.1.55 (2024) | 2024-08 | active | QEMU/KVM, Xen, Firecracker | yes, via klib | custom |
| osv | C++, Rust | 3-clause BSD license | v0.57.0, dated December 14, 20 | approximately two weeks ago | active | QEMU/KVM, Firecracker, Xen, VMWare, Virt | available | its own custom libc impl |
| rumprun | C | 2-clause BSD | — | May 11, 2020 | dormant | KVM, Xen, bare metal | OpenSSL | NetBSD libc |
| runtimejs | JavaScript | unknown | unknown | unknown | dormant | unknown | unknown | unknown |
| solo5 | C | ISC License | v0.13.0 (Aug 28, 2026) | August 2026 | active | QEMU/KVM, sandboxed process (via spt) | — | — |
| torokernel | Pascal | GNU General Public License v3. | — | February 2024 | active | — | — | — |
| unik | Go | Apache 2.0 License | 0.0, dated May 23, 2016 | July 17, 2019 | dormant | Virtualbox, vSphere, Firecracker, ARM pr | — | — |
| unikernel-linux | C | LGPL-2.1 | none | August 2023 | active | QEMU/KVM, bare metal | — | glibc |
| unikraft | C | BSD-3 | v0.21.0 (Ijiraq) from 2026 | August 2026 | active | QEMU/KVM, Xen, Linux userspace | — | nolibc |

## Sources

**baremetal** — https://github.com/ReturnInfinity/BareMetal, https://github.com/ReturnInfinity/BareMetal/blob/main/LICENSE, https://github.com/ReturnInfinity/BareMetal/tree/main/src

**clickos** — https://github.com/sysml/clickos, https://github.com/sysml/clickos/blob/master/COPYING, https://github.com/sysml/clickos/tags

**clive** — no verified sources retained

**drawbridge** — https://www.microsoft.com/en-us/research/project/drawbridge/, https://github.com/thinkcz/DrawBridge

**gramine** — https://github.com/gramineproject/gramine, https://gramineproject.io/, https://github.com/gramineproject/gramine/tags

**halvm** — https://github.com/GaloisInc/HaLVM, https://github.com/GaloisInc/HaLVM/blob/master/LICENSE

**hermitos** — https://github.com/hermit-os/hermit-rs, https://github.com/hermit-os/hermit-rs/commits/main/, http://unikernel.org/projects/

**includeos** — https://github.com/includeos/includeos, https://github.com/includeos/IncludeOS/blob/main/LICENSE, https://github.com/includeos/IncludeOS/releases

**ling** — https://github.com/cloudozer/ling, https://github.com/cloudozer/ling/blob/master/LICENSE, https://github.com/cloudozer/ling/tags

**mini-os** — https://github.com/xen-project/mini-os, https://github.com/xen-project/mini-os/blob/master/COPYING, https://github.com/xen-project/mini-os/blob/master/README

**mirageos** — https://github.com/mirage/mirage, https://github.com/mirage/mirage/blob/main/CHANGES.md, https://github.com/mirage/mirage/blob/main/README.md

**nanos** — https://github.com/nanovms/nanos, https://github.com/nanovms/nanos/blob/master/LICENSE, https://github.com/nanovms/nanos/commits/master/

**osv** — https://github.com/cloudius-systems/osv, https://github.com/cloudius-systems/osv/tags, https://github.com/cloudius-systems/osv/blob/master/LICENSE

**rumprun** — https://github.com/rumpkernel/rumprun, https://github.com/rumpkernel/rumprun/blob/master/LICENSE, https://github.com/rumpkernel/rumprun/blob/master/README.md

**runtimejs** — https://github.com/runtimejs/runtime

**solo5** — https://github.com/Solo5/solo5, https://github.com/Solo5/solo5/blob/main/LICENSE, https://github.com/Solo5/solo5/tags

**torokernel** — https://github.com/torokernel/torokernel, https://github.com/torokernel/torokernel/blob/master/COPYING

**unik** — https://github.com/solo-io/unik, https://github.com/solo-io/unik/wiki/UniK:-Build-and-Run-Unikernels-with-Ease, https://github.com/solo-io/unik/tags

**unikernel-linux** — https://github.com/unikernelLinux/ukl, https://github.com/unikernelLinux/ukl/blob/main/COPYING.LIB, https://github.com/unikernelLinux/ukl/blob/main/README.md

**unikraft** — https://github.com/unikraft/unikraft, https://github.com/unikraft/unikraft/blob/staging/COPYING.md, https://github.com/unikraft/unikraft/blob/staging/README.md

## Method, including what went wrong

`research/research.c` was run once per project. It is stateless: one question in, one
answer out, nothing carried between runs — which is what makes twenty parallel
investigations safe, since none can contaminate another.

Half the first pass failed, and both causes were ours rather than the projects':

1. **Self-inflicted rate limiting.** Three agents searching concurrently tripped the
   search API, and the tool reported one error to the model as though the fact did not
   exist. Fetches now retry with backoff on 429 and 5xx.
2. **Too large a question.** Asking for ten fields in one run exhausted the step budget
   before the agent would commit to an answer. Split into two smaller questions, a project
   that had failed twice answered in four steps.

A third fix was needed mid-run: `submit_answer` took an array of source URLs, and the
model malformed that call often enough to lose whole investigations. Changing the
parameter to a single space-separated string — while still accepting an array — removed
the failure. The same lesson as elsewhere in this repository: when a model keeps getting
a call wrong, look at what it is being asked for.

Re-running is one command; anything here will be stale eventually.
