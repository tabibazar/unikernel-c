#!/bin/bash
# OEIS refuses anonymous results past the first 100, so a single broad sweep is
# impossible. Instead: many narrow topic queries, each comfortably under the
# cap, union'd afterwards. The topics are the families the research brief named
# and never reached.
UA='Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0 Safari/537.36'
mkdir -p topic
TOPICS=(
 "Latin+square" "quasigroup" "Steiner" "combinatorial+design" "magic+square"
 "magic+cube" "Sidon" "difference+set" "polycube" "polyomino" "lattice+animal"
 "tiling" "packing" "self-avoiding" "Costas" "Golomb" "queens" "tournament"
 "necklace" "matroid" "poset" "semigroup" "triangulation" "polytope"
 "hypergraph" "perfect+matching" "Hamiltonian" "dissection" "labeled+graph"
 "regular+graph" "Ramsey" "covering+code" "permutation+avoiding" "knot"
)
for t in "${TOPICS[@]}"; do
  for s in 0 10 20 30 40 50 60 70 80 90; do
    f="topic/${t//[^A-Za-z0-9]/_}_$s.json"
    [ -s "$f" ] && continue
    curl -s --max-time 40 -A "$UA" \
      "https://oeis.org/search?q=keyword:hard+keyword:more+keyword:nonn+$t&fmt=json&start=$s" -o "$f"
    head -c 1 "$f" | grep -q '\[' || { rm -f "$f"; break; }   # past the end of this topic
    sleep 1.1
  done
done
echo "topic files: $(ls topic/*.json 2>/dev/null | wc -l)"
