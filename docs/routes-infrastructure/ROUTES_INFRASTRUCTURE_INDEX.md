# Routes & Infrastructure Review — Documentation Index

**Review Date:** January 11, 2026  
**Scope:** Roads, railroads, city connections, trade routes, maintenance, route-based yields, unit movement on infrastructure

---

## 📚 Documentation Files Created

### 1. **ROUTES_INFRASTRUCTURE_SUMMARY.md** ← START HERE
Quick overview of all 6 issues, implementation roadmap, and priority tiers.
- 2 critical issues (must fix)
- 4 high-value improvements (should fix)
- Risk/effort matrix for planning
- **Read this first for executive summary**

### 2. **ROUTES_INFRASTRUCTURE_REVIEW.md** — DETAILED ANALYSIS
Complete technical review with code patterns, root causes, testing procedures, and design discussion.
- Issue 1: Trade route creation war validation gap
- Issue 2: Negative trade route slot counts
- Issue 3: Route maintenance not included in builder scoring
- Issue 4: Tech-based movement improvements underdocumented
- Issue 5: Route planning strategy undocumented
- Issue 6: Unit-type movement scaling not optimized
- Secondary findings on maintenance cost balance

### 3. **ROUTES_INFRASTRUCTURE_IMPLEMENTATION.md** — DEVELOPER GUIDE
Step-by-step implementation checklist with code patterns, search strings, and testing procedures.
- Exact file/function locations
- Code examples for each fix
- Detailed testing procedures
- Risk assessment per issue
- Week-by-week implementation plan

---

## 🎯 Quick Navigation

### For Project Managers / Decision Makers
→ Read **ROUTES_INFRASTRUCTURE_SUMMARY.md**
- 5-minute read
- Priority matrix
- Implementation roadmap

### For Developers (Implementation Planning)
→ Read **ROUTES_INFRASTRUCTURE_IMPLEMENTATION.md**
- Step-by-step checklist
- Code file locations
- Testing procedures
- Time estimates

### For Architects / Code Reviewers
→ Read **ROUTES_INFRASTRUCTURE_REVIEW.md**
- Design analysis
- Root cause analysis
- Cross-system impacts
- Design recommendations

---

## 🔴 Critical Issues Summary

### Issue 1: War Zone Trade Routes
- **File:** CvTradeClasses.cpp
- **Problem:** Routes created without re-checking war status
- **Fix:** Add `IsValidTradeRoutePath()` call in `CreateTradeRoute()`
- **Effort:** 1-2 hours
- **Risk:** LOW

### Issue 2: Negative Route Slots
- **File:** CvTradeClasses.cpp
- **Problem:** Negative modifiers create invalid route counts
- **Fix:** Clamp `GetNumTradeRoutesPossible()` to `max(0, result)`
- **Effort:** <1 hour
- **Risk:** VERY LOW

---

## 🟡 High-Value Improvements Summary

### Issue 3: Builder Ignores Route Maintenance
- **File:** CvBuilderTaskingAI.cpp
- **Problem:** Routes with high maintenance costs scored as profitable
- **Fix:** Subtract maintenance from gold yield in `ScorePlotBuild()`
- **Effort:** 2-3 hours
- **Risk:** MEDIUM (balance testing needed)

### Issue 4: Tech Progression Underdocumented
- **File:** RouteChanges.sql, CustomMods.h
- **Problem:** Route speed improvements hard-coded, no explanation
- **Fix:** Add GameDefines constants and documentation comments
- **Effort:** 2-3 hours
- **Risk:** VERY LOW (documentation only)

### Issue 5: Route Planning Strategy Unclear
- **File:** CvBuilderTaskingAI.cpp, CustomMods.h
- **Problem:** Builder priorities for main/shortcut/strategic routes not documented
- **Fix:** Add GameDefines, code comments, design document
- **Effort:** 3-4 hours
- **Risk:** LOW

### Issue 6: Unit Movement Not Domain-Specific
- **File:** CvAStar.cpp, RouteChanges.sql
- **Problem:** All units pay same cost on routes; no historical/gameplay scaling
- **Fix:** Add `Route_DomainMovementModifiers` table for unit-type scaling
- **Effort:** 4-6 hours
- **Risk:** MEDIUM (pathfinding impact)

---

## 📋 Related Existing Reviews

These reviews cover adjacent systems and provide context:

1. **docs/economy/TRADE_ECONOMY_REVIEW.md**
   - Trade route mechanics and validation
   - Covers Issues 1-2 from economic perspective

2. **docs/improvements-workers/IMPROVEMENTS_WORKERS_REVIEW.md**
   - Builder AI and route planning details
   - Covers Issues 3-5 in depth

3. **docs/unit-movement/IMPLEMENTATION_SUMMARY.md**
   - Pathfinding improvements
   - Relevant to Issue 6 (unit movement on routes)

---

## 🗺️ Code File Map

### CvGameCoreDLL_Expansion2/ (C++ Game Core)

| File | Related Issues | Purpose |
|------|----------------|---------|
| CvTradeClasses.cpp/.h | 1, 2 | Trade route creation and validation |
| CvBuilderTaskingAI.cpp/.h | 3, 5 | Builder route planning and scoring |
| CvCityConnections.cpp/.h | 5, 6 | City connection pathfinding |
| CvAStar.cpp/.h | 6 | Pathfinding core algorithm |
| CvInfos.h | 4 | CvRouteInfo class definition |
| CustomMods.h | 4, 5, 6 | GameDefines constants (to add) |

### (2) Vox Populi/ (Database Changes)

| File | Related Issues | Purpose |
|------|----------------|---------|
| RouteChanges.sql | 3, 4, 6 | Route maintenance, movement, tech modifiers |
| RouteTextChanges.sql | 4 | Route localization text |

### Lua/UI Files

| File | Related Issues | Purpose |
|------|----------------|---------|
| TradeRouteHelpers.lua | 1, 2 | Trade route UI display helpers |
| TradeRouteOverview.lua | 2 | Trade route management UI |

---

## ✅ Implementation Checklist

### Phase 1: Critical Fixes (Week 1)
- [ ] **Issue 1:** War validation in `CreateTradeRoute()`
  - [ ] Find function location
  - [ ] Add `IsValidTradeRoutePath()` call
  - [ ] Build and test
  - [ ] Create war scenario test
  - [ ] Commit with message: `fix: validate trade route path before creation`

- [ ] **Issue 2:** Clamp negative route counts
  - [ ] Find `GetNumTradeRoutesPossible()` function
  - [ ] Add clamp: `max(0, iNumRoutes)`
  - [ ] Build and test
  - [ ] Create negative modifier test
  - [ ] Commit with message: `fix: clamp trade route slots to non-negative`

**Effort:** 4-6 hours | **Risk:** Low | **Impact:** Very High

### Phase 2: High-Value Improvements (Week 2-3)
- [ ] **Issue 3:** Maintenance in builder scoring
  - [ ] Find `ScorePlotBuild()` function
  - [ ] Subtract maintenance from gold yield
  - [ ] Build and test
  - [ ] Verify end-game treasury stability
  - [ ] Balance check if needed
  - [ ] Commit with message: `fix: account for route maintenance in builder scoring`

- [ ] **Issue 4:** Tech progression documentation
  - [ ] Add GameDefines constants to CustomMods.h
  - [ ] Add comments to RouteChanges.sql
  - [ ] Create `docs/infrastructure/ROUTE_PROGRESSION_DESIGN.md`
  - [ ] Commit with message: `docs: document route tech progression design`

- [ ] **Issue 5:** Route planning documentation
  - [ ] Add GameDefines constants to CustomMods.h
  - [ ] Add code comments to CvBuilderTaskingAI.cpp
  - [ ] Create `docs/infrastructure/ROUTE_PLANNING_STRATEGY.md`
  - [ ] Test on multiple map types
  - [ ] Commit with message: `docs: document route planning priorities`

**Effort:** 8-10 hours | **Risk:** Low-Medium | **Impact:** High

### Phase 3: Enhancement (Week 4+)
- [ ] **Issue 6:** Unit-type movement modifiers
  - [ ] Create `Route_DomainMovementModifiers` table
  - [ ] Modify CvAStar.cpp to query table
  - [ ] Add test data (DOMAIN_LAND, DOMAIN_SEA)
  - [ ] Build and test pathfinding
  - [ ] Profile performance impact
  - [ ] Commit with message: `feat: add domain-specific movement modifiers for routes`

**Effort:** 6-8 hours | **Risk:** Medium | **Impact:** Medium

---

## 📊 Priority Matrix

```
         Low Effort | High Effort
         
High    Issue 2    | Issue 6
Impact  Issue 4    | Issue 3
        Issue 1    | Issue 5
        
Low     (none)     | (none)
Impact  
```

**Recommended Order:** 1 → 2 → 3 → 4 → 5 → 6

---

## 🔗 Cross-System Dependencies

```
Trade Routes (Issues 1-2)
    ↓
Builder AI (Issues 3, 5)
    ↓
City Connections (Issues 5-6)
    ↓
Pathfinding (Issues 6)
    ↓
Route Properties (Issues 3-4, 6)
```

**Implementation Note:** Issues 3-4 can be done in parallel; Issue 6 depends on Issues 4-5 being understood.

---

## 🧪 Testing Strategy

### Level 1: Unit Tests (Per Issue)
- Isolated function testing
- Mock data and dependencies
- Examples provided in IMPLEMENTATION.md

### Level 2: Integration Tests
- Multiple systems together
- Existing modded gameplay
- Community Patch + Vox Populi compatibility

### Level 3: End-to-End Tests
- Full game scenarios
- Multiple difficulty levels
- Multiplayer compatibility

### Level 4: Performance Tests
- Pathfinding speed (Issue 6)
- Builder AI turn times (Issue 3)
- No regression vs. baseline

---

## 📞 Questions & Answers

**Q: Which issues must be fixed before release?**
A: Issues 1-2 (critical). Without these, multiplayer and surprise war mechanics break.

**Q: Can Issues 4-5 wait for a later release?**
A: Yes, they're documentation/improvement focused. But fixing them improves maintainability.

**Q: Is Issue 6 worth the effort?**
A: It's a nice-to-have. Medium priority if you want better domain-specific gameplay.

**Q: What if we only fix Issues 1-2?**
A: The mod is safe to release, but leaves gameplay issues (3, 5) and maintainability (4-5) unaddressed.

**Q: Can modders fix these themselves?**
A: Issues 4-5 yes (SQL/comments). Issues 1-3, 6 require C++ recompilation.

---

## 📖 How to Use This Documentation

1. **First Time Here?** Start with ROUTES_INFRASTRUCTURE_SUMMARY.md
2. **Planning Implementation?** Use ROUTES_INFRASTRUCTURE_IMPLEMENTATION.md
3. **Deep Dive?** Read ROUTES_INFRASTRUCTURE_REVIEW.md
4. **Need Context?** Check related docs (trade, builders, pathfinding)

---

## 📝 Document Metadata

| Document | Purpose | Audience | Read Time |
|----------|---------|----------|-----------|
| SUMMARY | Overview & priorities | Managers, Leads | 10 min |
| REVIEW | Detailed analysis | Architects, Reviewers | 30 min |
| IMPLEMENTATION | Developer guide | Developers | 20 min |

---

**Repository:** Community-Patch-DLL  
**Review Date:** January 11, 2026  
**Status:** Ready for Developer Review  
**Next Step:** Prioritize Phase 1 fixes (Issues 1-2)

