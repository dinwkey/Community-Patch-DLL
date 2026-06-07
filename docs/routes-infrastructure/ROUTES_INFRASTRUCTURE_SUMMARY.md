# Routes & Infrastructure: Quick Summary

## 🔴 Critical Issues Found: 2

### 1. Trade Route Creation Can Move Through War Zones (Multiplayer)
- **File:** `CvTradeClasses.cpp` — `CreateTradeRoute()`
- **Problem:** Routes created without re-checking war status; primarily affects multiplayer
- **Single-Player:** Mitigated by turn structure; AI checks war status before its turn
- **Multiplayer:** Risk of brief window where new routes bypass war validation (network timing)
- **Fix:** Add `IsValidTradeRoutePath()` call inside `CreateTradeRoute()` for robustness
- **Effort:** 1-2 hours
- **Status:** ⏳ OPEN

### 2. Negative Trade Route Slot Counts Possible
- **File:** `CvTradeClasses.cpp` — `GetNumTradeRoutesPossible()`
- **Problem:** Modifiers ≤ −100% create negative route counts, breaking UI and logic
- **Risk:** Player sees "−2/4 routes available", AI makes wrong decisions on route creation
- **Fix:** Clamp result to `max(0, iNumRoutes)` after applying modifier
- **Effort:** <1 hour
- **Status:** ⏳ OPEN

---

## 🟡 High-Value Improvements: 4

### 3. Route Maintenance Not Included in Builder Scoring
- **File:** `CvBuilderTaskingAI.cpp` — `ScorePlotBuild()`
- **Problem:** Builders score routes on gross gold without considering maintenance, movement speed bonuses, or strategic value
- **Issue:** Economic routes may be unprofitable; military routes (railroads) deprioritized despite strategic value
- **Fix:** Subtract maintenance, add movement bonuses for fast routes, check treasury state, weight by war situation
- **Effort:** 4-5 hours (includes economic + military testing)
- **Status:** ⏳ OPEN
- **Note:** More complex than initially assessed—requires balance between economic and military priorities

### 4. Tech-Based Route Movement Improvements Underdocumented
- **File:** `RouteChanges.sql` (lines with tech modifiers)
- **Problem:** Route speed progression has no explanation; modders cannot understand or adjust
- **Impact:** Modding community cannot create alternative route systems
- **Fix:** Add code comments and GameDefines constants explaining design intent
- **Effort:** 2-3 hours (documentation only)
- **Status:** ⏳ OPEN

### 5. Route Planning Strategy Undocumented
- **File:** `CvBuilderTaskingAI.cpp` (lines ~200-350)
- **Problem:** Builders prioritize routes using unclear heuristics (main vs. shortcut vs. strategic)
- **Impact:** Difficult to debug AI route choices; modders cannot customize
- **Fix:** Add decision tree comments, extract constants to GameDefines
- **Effort:** 3-4 hours (documentation + refactoring)
- **Status:** ⏳ OPEN

### 6. Movement Cost Scaling Not Optimized for Unit Types
- **File:** `CvAStar.cpp`, `CvCityConnections.cpp`
- **Problem:** All units pay same route cost; historical units (merchants, caravans) should vary
- **Impact:** Routes may not scale well for specific unit types; AI pathfinding complexity
- **Fix:** Add `Route_DomainMovementModifiers` table to scale movement by domain
- **Effort:** 4-6 hours (code + database + testing)
- **Status:** ⏳ OPEN (Lower priority)

---

## 📋 Implementation Roadmap

### Phase 1: Critical Fixes (Week 1)
- [ ] Fix trade route war validation
- [ ] Clamp negative trade route counts
- [ ] Unit tests for both fixes
- **Effort:** 4-6 hours | **Risk:** Low | **Impact:** Very High

### Phase 2: High-Impact Improvements (Week 2-4)
- [ ] Fix route maintenance scoring (REVISED: more complex than initially assessed)
- [ ] Document tech progression with GameDefines
- [ ] Document route planning strategy
- **Effort:** 12-16 hours (increased from 8-10) | **Risk:** Medium | **Impact:** High

### Phase 3: Enhancement (Week 4+)
- [ ] Add unit-type movement modifiers
- [ ] Review route maintenance cost balance
- **Effort:** 6-8 hours | **Risk:** Medium | **Impact:** Medium

---

## 📁 Related Documentation

- [Full Routes & Infrastructure Review](./ROUTES_INFRASTRUCTURE_REVIEW.md) — Detailed analysis, code examples, test cases
- [Trade & Economy Review](./economy/TRADE_ECONOMY_REVIEW.md) — Trade route mechanics and validation
- [Improvements & Workers Review](./improvements-workers/IMPROVEMENTS_WORKERS_REVIEW.md) — Builder AI and route planning details

---

## ✅ Key Files to Review/Modify

1. **CvGameCoreDLL_Expansion2/CvTradeClasses.cpp**
   - `CreateTradeRoute()` — Add war validation
   - `GetNumTradeRoutesPossible()` — Add clamp

2. **CvGameCoreDLL_Expansion2/CvBuilderTaskingAI.cpp**
   - `ScorePlotBuild()` — Subtract maintenance from gold yield
   - `UpdateRoutePlots()` — Add strategy documentation

3. **(2) Vox Populi/Database Changes/WorldMap/Improvements/RouteChanges.sql**
   - Add comments explaining tech progression design
   - Consider adding GameDefines for movement values

4. **CvGameCoreDLL_Expansion2/CustomMods.h**
   - Add route-related constants (movement, maintenance, priorities)
   - Add documentation comments for route system

---

**Review Date:** January 11, 2026  
**Reviewer:** Code Analysis Agent  
**Status:** Ready for developer review and implementation planning

