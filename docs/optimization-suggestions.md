# Optimization Suggestions (8 items)

*Last reviewed: 2025-01-24*

1) **Cache expensive `CvPlayer`/`CvCity` lookups in hot loops**  
   - Status: ✅ Completed (cached GET_PLAYER refs in CvMilitaryAI, CvDiplomacyAI, CvDealAI, CvHomelandAI, CvAIOperation)  
   - Still makes sense: Yes  
   - Gain: Med  
   - Pros: straightforward, local change, low risk  
   - Cons: cache invalidation if data mutates mid-loop; easy to introduce stale reads  
   - Notes: keep caches scoped to the loop/function; avoid caching across captures/deletions

2) **Cache `CvGameDatabase` string lookups (`CvDatabaseUtility`)**  
   - Status: Largely done (most hot-path calls already use `static` caching)  
   - Still makes sense: Limited — remaining uncached calls are mostly in one-time init/CacheResults (load-time)  
   - Gain: Low (diminishing returns)  
   - Pros: reduces repeated hash/string work; stable data  
   - Cons: memory growth if key space is large; need lifecycle management  
   - Notes: codebase already follows `static Type eX = GC.getInfoTypeForString(...)` pattern in most AI code; no major wins left here

3) **Precompute plot filters (terrain/feature masks)**  
   - Status: ✅ Completed (PlotCacheFlags in CvPlot — 14 terrain/feature bits, commit e306737d1)  
   - Gain: Med–High (AI/pathing heavy)  
   - Pros: faster AI scans, reusable per map  
   - Cons: must rebuild on map changes/modifiers; added storage

4) **Replace repeated all-cities/all-units scans with cached lists**  
   - Status: Not started  
   - Still makes sense: Yes (high ROI, higher complexity)  
   - Gain: High in turn-heavy AI  
   - Pros: large speedups in aggregate  
   - Cons: higher complexity; must update on add/remove/ownership changes  
   - Notes: best done after profiling confirms repeated full scans; start with a narrowly-scoped cache  
   - Risk: Can weaken AI if cached lists go stale (miss new/dead units, captured cities)

5) **Throttle AI evaluations not needed per turn**  
   - Status: Not started  
   - Still makes sense: Maybe (highest risk of behavior changes)  
   - Gain: High (AI CPU)  
   - Pros: big turn-time improvement  
   - Cons: behavior changes; risk of AI "lag" or balance shifts  
   - Notes: prefer event-driven invalidation or "every N turns" only for non-critical evaluations  
   - Risk: Most likely to weaken AI — slower reaction to threats/opportunities

6) **Guard expensive logging/`GC.getDefineINT` behind caches**  
   - Status: Not started  
   - Still makes sense: Yes (low risk, usually low priority)  
   - Gain: Low–Med  
   - Pros: safe and easy  
   - Cons: marginal benefit; must ensure cache reload if settings change

7) **Avoid re-sorting vectors every call**  
   - Status: ⏭️ Skip — already addressed by codebase architecture  
   - Still makes sense: No — codebase already does this correctly  
   - Gain: None (no redundant sorts found)  
   - Pros: N/A  
   - Cons: N/A  
   - Notes: Vectors (e.g. `m_CurrentMoveUnits`, `allDirectives`) are rebuilt fresh each phase, sorted once, then consumed. `std::nth_element` already used where only top-K matters (CvCityCitizens). No "dirty flag" needed because vectors aren't persisted across turns.  
   - Risk: None — sorting order doesn't change which options exist, only iteration order

8) **Use `reserve()` on vectors that grow every turn**  
   - Status: Not started  
   - Still makes sense: Yes (opportunistic)  
   - Gain: Low  
   - Pros: trivial, safe  
   - Cons: tiny benefit; may over-allocate memory

## Recommended Implementation Order (Updated)

**Safe, no AI behavior risk:**
1. Item 6 (guard/caches for logging & defines) — easy wins
2. Item 8 (`reserve()` in hot vectors) — trivial, safe

**Medium risk (correctness-dependent):**
3. Item 4 (cached all-units/all-cities lists) — only after profiling

**Higher risk (can weaken AI if done wrong):**
4. Item 5 (throttling) — only with event-driven invalidation

**Already done / low value remaining:**
- Item 1 (cache expensive lookups) — ✅ completed
- Item 2 (DB string lookups) — already largely cached with `static`
- Item 3 (plot filters) — ✅ completed
- Item 7 (avoid repeated sorts) — ⏭️ skip, already addressed by architecture
