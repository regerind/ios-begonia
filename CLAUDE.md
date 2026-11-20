# Instructions for agents working on this repo

iOS 16 on the Redmi Note 8 Pro (begonia, MT6785). The owner keeps the project's notebook in `docs/regerind-docs/` (Turkish).

## Log every build, then the bugs
1. **Before or as a build starts** (any tag: x, y, p...), add its row to `docs/regerind-docs/BUILD-LOG.md`: tag, time, what
   changed. When the session's log is read (or the user reports), fill in the result: what the log showed and what the
   user said, with numbers where there are some.
2. **Then update the bugs:**
   - A new or still-present problem goes to `docs/regerind-docs/ACIK-HATALAR.md`: symptom, evidence, suspicion, status.
   - A problem that is now gone moves to `docs/regerind-docs/COZULEN-HATALAR.md`, with the build, how it was fixed, how it was
     verified (user / log / both) and what doubt remains. A "fix" may only hide the symptom, so say so when that is
     possible.
   - If a fixed bug comes back, move it back to the open list and note why the old fix was not enough.
3. Big topics (root cause and fix) go in `docs/regerind-docs/NASIL-COZDUK.md`; the work queue is `docs/regerind-docs/YAPILACAKLAR.md`.
4. Write these files in Turkish, as the owner does.

## Safety rules (never relax without the owner's explicit OK)
- Never write the IMEI partitions (nvram, nvdata, protect1/2, persist, proinfo, ...): the phone had an IMEI repair.
  Flashing writes only `boot` and `misc`.
- No voltage, clock or PMIC rail changes without the owner's OK; each approval covers only what was asked.
- Test sessions last 60 s (`WATCH=60`). Do not hide the wallpaper, lower the resolution or turn on Reduce
  Motion/Transparency to win speed.
- A test image runs iOS once; a rerun needs a new build tag.
