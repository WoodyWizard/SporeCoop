"""Limits that follow the observer's frame rate (plan 0.7.15 D1, BUG-1114; docs/06-testing.md#testing-lab-budget).

A measure drawn on the observer's screen (how far a copy is off the host's path) grows with its frame time: a busy
lab (BUG-0663, BUG-1098) slows the games and a fixed limit set at 60 fps then fails a correct fix. Such a limit is
per_frame / fps (the measure's growth with the frame time, from the recorded PASS runs) kept between `base` (the
limit at full speed, unchanged) and `cap` (below every recorded FAIL of the twin with the fix off, so the twin still
fails at any fps). Each use names its PASS and FAIL ranges next to its numbers.
"""


def scaled(fps, base, per_frame, cap):
    """the limit for an observer at `fps`: per_frame / fps within [base, cap]; base when the fps is unknown"""
    if not fps or fps <= 0:
        return base
    return min(cap, max(base, per_frame / fps))
