# Teacher pipeline (offline, conda env eidolon).
#
# Converts raw experience dumps from eidolon-sim --dump-experiences into a teacher-baked
# policy "wisdom prior" (.eprp) that the C++ runtime loads via --policy-prior. Online
# learning always continues on top of the prior, so the teacher only shapes the initial
# bias. The teacher itself never runs in the C++ runtime and never affects determinism:
# once the .eprp artifact is frozen, no live calls are needed to replay a run bit-for-bit.
