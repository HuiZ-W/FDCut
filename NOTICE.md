# Third-party notices and licensing

This repository is a source snapshot of a research implementation. No blanket
open-source license is granted for the project-owned research code by this
release. A public repository is not itself a grant of such a license.

The HPF 3.23 pseudoflow solver is provided by Bala Chandran and Dorit S.
Hochbaum, University of California, Berkeley. Its upstream license permits
educational, research and not-for-profit use under its stated conditions;
commercial licensing must be arranged with the licensor. The full notice is
preserved in `src/hpf_memory.c` and in the upstream source. It must not be
removed or replaced by a project license.

- Upstream: https://riot.ieor.berkeley.edu/Applications/Pseudoflow/maxflow.html
- Source archive: https://riot.ieor.berkeley.edu/Applications/Pseudoflow/pseudo-max-3.23_2.tar
- Archive SHA-256: `722bf7d8f8ad7dedb776171e71742e8f5643d1f8cb266b900e2147ff2b390903`

The upstream source is fetched during configuration, not vendored into this
repository. Its license also applies to applicable compiled artifacts. The
adapter includes the solver routines unchanged and supplies in-memory I/O.

The comparison constructors implement the approaches of Jeong, Tian and
Bareinboim (2022), and Wienöbst, van der Zander and Liśkiewicz (2024).
Their use here is credited; these minimum-cost comparison wrappers should not
be confused with authorship of the underlying published algorithms.
