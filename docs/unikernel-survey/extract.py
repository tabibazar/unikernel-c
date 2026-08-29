#!/usr/bin/env python3
"""Turn each research answer into structured fields.

The research agent answered in prose rather than the field format it was asked
for. Re-running twenty investigations to fix the formatting would waste the
research; extracting from what it already found does not.

This pass is deliberately narrow: it reads only the text the agent produced and
pulls out what is stated there. Anything the summary does not say becomes null.
It is not allowed to contribute knowledge of its own — that would quietly
convert "the agent could not establish this" into "a model thinks it is
probably X", which is exactly the failure this whole project keeps warning about.
"""

import json
import os
import pathlib
import re
import subprocess
import sys

KEY = pathlib.Path.home().joinpath(".gemini_key").read_text().strip()
URL = "https://generativelanguage.googleapis.com/v1beta/openai/chat/completions"

FIELDS = ["language", "license", "repo", "latest_release", "last_commit",
          "hypervisors", "network_stack", "tls", "libc", "status"]

PROMPT = """Below is a research summary about one unikernel or library-OS project.

Extract these fields as JSON: {fields}

Rules:
- Use ONLY what the summary states. If it does not say, use null.
- Do not add anything you happen to know about the project.
- "status" should be active, dormant, or archived only if the summary supports it.
- last_commit and latest_release: keep whatever date granularity is given.
- Reply with the JSON object alone, no commentary.

SUMMARY:
{body}
"""


def gemini(prompt):
    payload = json.dumps({
        "model": "gemini-2.5-flash",
        "messages": [{"role": "user", "content": prompt}],
    })
    for attempt in range(4):
        p = subprocess.run(
            ["curl", "-sS", "--max-time", "120", "-X", "POST", URL,
             "-H", f"Authorization: Bearer {KEY}",
             "-H", "Content-Type: application/json", "-d", payload],
            capture_output=True, text=True)
        try:
            d = json.loads(p.stdout)
            return d["choices"][0]["message"]["content"]
        except Exception:
            if attempt == 3:
                return ""
    return ""


def main():
    out = {}
    results = sorted(pathlib.Path("/tmp/survey/results").glob("*.txt"))
    for f in results:
        body = f.read_text().strip()
        if not body:
            continue
        name = f.stem
        raw = gemini(PROMPT.format(fields=", ".join(FIELDS), body=body[:6000]))
        m = re.search(r"\{.*\}", raw, re.S)
        rec = {}
        if m:
            try:
                rec = json.loads(m.group(0))
            except json.JSONDecodeError:
                rec = {}
        # sources come from the agent's own run, not from this pass
        srcs = re.findall(r"\[\d+\]\s+(\S+)", body)
        rec["sources"] = srcs[:3]
        out[name] = {k: rec.get(k) for k in FIELDS} | {"sources": rec["sources"]}
        got = sum(1 for k in FIELDS if out[name].get(k))
        print(f"{name:22} {got}/{len(FIELDS)} fields", flush=True)

    pathlib.Path("/tmp/survey/table.json").write_text(json.dumps(out, indent=2))
    print(f"\nwrote {len(out)} records to /tmp/survey/table.json")


if __name__ == "__main__":
    main()
