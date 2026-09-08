# ACO guest post for ReturnInfinity

Return Infinity asked to publish the ant colony experiment and needed a copy
their CMS can take. The Claude artifact is a full web page and does not paste.

## Which copy to post

**`aco-cms.html`** — this is the one. It is the "How ant colonies solve hard
problems" explainer, which is the piece Varun referred to and the one the quoted
line comes from ("Consensus has a cost. Our sharing swarm agreed faster and
finished worse.").

The other artifact in that thread is the *findings* page — narrower, aimed at
people already inside the project, and not a guest post. Do not publish that one.

## What changed for the CMS, and why

| | artifact | this file |
|---|---|---|
| `<style>` block | yes, with CSS custom properties | **none** |
| SVG colours | `var(--ink)` etc. | **literal hex** |
| SVG text styling | via CSS classes | **inline `font-family` / `fill`** |
| JavaScript | interactive simulation | **removed** |
| Dark mode | yes | light only |
| Classes | throughout | **none** |

Most CMSs strip `<style>` on paste. Had the variables been left in, every figure
would have rendered with unresolved colours — black text on black, or nothing at
all — and it would have looked fine in preview and broken on publish. That is
the single change that matters here.

There are no classes and no inline `style` attributes on the prose, so the
article inherits ReturnInfinity's own typography rather than fighting it. The
eight SVGs are self-contained and scale.

## What was lost

The interactive simulation — the one where you move a fifth of the cities and
watch the stale trails decay — cannot travel. It is a canvas and a few hundred
lines of JavaScript. The section still stands on its measurements and its static
figure; readers simply cannot poke it.

If ReturnInfinity wants the interactive version too, the honest options are an
`<iframe>` to the hosted artifact, or a short screen recording. An iframe would
work but ties their page to a claude.ai URL, which is probably not what they
want on their own site.

## Checks run

```
CSS variables remaining : 0
<style> blocks          : 0
<script> blocks         : 0
class attributes        : 0
figures                 : 8
unresolved fill/stroke  : none (only fill="none")
```

## One thing to confirm before it goes out

The byline reads "Reza Tabibazar, Avriz Engineering. Implementation and analysis
with Claude (Anthropic)." That matches the artifact. If ReturnInfinity's house
style handles AI attribution differently, that line is theirs to adjust — but it
should not simply be dropped, since it is accurate.
