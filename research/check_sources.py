#!/usr/bin/env python3
"""Flag answers whose sources are not about the thing that was asked.

The agent already verifies that a cited URL was actually retrieved during the
run, which stops it inventing plausible links. That check passed happily on an
answer claiming HermitOS gets TLS from BearSSL, cited to cheriot.org — a real
page, genuinely fetched, about an entirely different project. The agent had
searched for one thing, landed on another, and merged them.

Verifying that a source exists is not verifying that it is about your subject.
This is the second check: compare the cited domains against the subject, and
flag answers that rest only on unrelated hosts.

It is deliberately crude. It cannot tell whether a claim is true — only whether
the evidence offered even appears to concern the right thing, which is where the
cheapest errors hide.

    ./check_sources.py <dir-of-answers> [subject-from-filename-regex]
"""

import pathlib
import re
import sys

# Hosts that are legitimate evidence for almost any subject, so their presence
# neither confirms nor denies relevance.
GENERIC = {
    "github.com", "gitlab.com", "en.wikipedia.org", "news.ycombinator.com",
    "reddit.com", "www.reddit.com", "stackoverflow.com", "lwn.net",
    "pypi.org", "crates.io", "npmjs.com", "www.npmjs.com", "medium.com",
    "arxiv.org", "linkedin.com", "www.linkedin.com", "youtube.com",
    "www.youtube.com", "twitter.com", "x.com",
}


def tokens(subject):
    """Words from the subject that a related domain might plausibly contain.

    Punctuation is dropped from both sides before comparison, because a subject
    written 'flyio' and a domain written 'fly.io' are the same name. Short
    tokens are kept: 'aws' and 'e2b' are exactly the cases that matter, and
    discarding them produced false mismatches on correctly sourced answers."""
    parts = re.split(r"[-_\s.]+", subject.lower())
    words = {p for p in parts if len(p) >= 3 and p not in
             {"what", "does", "cloud", "price", "with", "from", "the"}}
    words.add(re.sub(r"[^a-z0-9]", "", subject.lower()))   # the whole name, joined
    return {w for w in words if w}


def domains(text):
    out = []
    for m in re.finditer(r"https?://([^/\s\)]+)", text):
        d = m.group(1).lower()
        out.append(d[4:] if d.startswith("www.") else d)
    return out


def main():
    root = pathlib.Path(sys.argv[1])
    strip = sys.argv[2] if len(sys.argv) > 2 else r"-(what|price|tls|stack|use|rel)$"

    flagged = ok = nosrc = 0
    for f in sorted(root.glob("*.txt")):
        subject = re.sub(strip, "", f.stem)
        body = f.read_text(errors="replace")
        # only the citation block: what the answer actually rests on
        cited = body.split("sources:")[-1] if "sources:" in body else ""
        doms = [d for d in domains(cited)]
        if not doms:
            nosrc += 1
            print(f"  NO SOURCES  {f.stem}")
            continue

        toks = tokens(subject)
        specific = [d for d in doms if d not in GENERIC]
        # compare on letters and digits alone: fly.io == flyio, e2b.dev contains e2b
        related = [d for d in specific
                   if any(t in re.sub(r"[^a-z0-9]", "", d) for t in toks)]

        if related:
            ok += 1
        elif not specific:
            print(f"  GENERIC ONLY {f.stem:26} rests only on {', '.join(sorted(set(doms))[:3])}")
            flagged += 1
        else:
            print(f"  MISMATCH     {f.stem:26} subject '{subject}' vs {', '.join(sorted(set(specific))[:3])}")
            flagged += 1

    total = ok + flagged + nosrc
    print(f"\n{ok}/{total} corroborated by a subject-related domain, "
          f"{flagged} flagged, {nosrc} without sources")


if __name__ == "__main__":
    main()
