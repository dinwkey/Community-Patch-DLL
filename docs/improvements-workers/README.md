# Improvements & Workers: Documentation Index

## Overview

This directory contains comprehensive documentation, issue tracking, and implementation guidance for the **Improvements & Workers** system in Community Patch and Vox Populi for Civilization V.

**System Scope:**
- Tile improvements (farms, mines, plantations, roads, etc.)
- Worker/builder units and their production decisions
- Build actions and task prioritization
- Build time calculations and tech modifiers
- Builder AI logic and scoring

---

## Documents

### 📋 [IMPROVEMENTS_WORKERS_REVIEW.md](./IMPROVEMENTS_WORKERS_REVIEW.md)

**Main system review document** — comprehensive overview of all systems, code architecture, and known limitations.

**Contents:**
1. **Architecture & Core Systems**
   - Builder AI (CvBuilderTaskingAI)
   - Economic AI (worker production)
   - City strategy triggers
   - Build time calculation
   - Improvement scoring
   - Worker pathfinding

2. **Database Configuration**
   - Build time tuning
   - Tech modifiers
   - Civilization-specific modifiers

3. **Known Issues & Limitations**
   - 5 open TODOs
   - Route planning heuristics
   - Yield calculations
   - Adjacency interactions

4. **Builder AI Decision Flow**
   - Turn-by-turn execution
   - Scoring priority
   - UI integration

5. **Recommendations**
   - Tier 1: Critical clarity
   - Tier 2: Medium priority
   - Tier 3: Nice-to-have

**Audience:** Developers, reviewers, system architects

---

### 🔴 [ISSUES_AND_FIXES.md](./ISSUES_AND_FIXES.md)

**Issue tracking and proposed fixes** — detailed breakdown of known problems with severity, root cause, and solutions.

**Contents:**
1. **Issue Matrix** — Quick reference table of all issues
2. **7 Known Issues**
   - Issue 1: Net gold not factored in scoring (HIGH)
   - Issue 2: Tech distance heuristic (MEDIUM)
   - Issue 3: Adjacency/feature interactions (MEDIUM)
   - Issue 4: Route planning undocumented (MEDIUM)
   - Issue 5: City strategy thresholds unclear (LOW)
   - Issue 6: Pathfinding underestimate (LOW)
   - Issue 7: Worker repair exploit (ADDRESSED)
3. **Priority Matrix** — Implementation priority vs. effort
4. **Action Plan** — Phased approach (Phase 1, 2, 3)

**Audience:** Developers planning implementation, reviewers prioritizing fixes

---

### 🛠️ [IMPLEMENTATION_GUIDE.md](./IMPLEMENTATION_GUIDE.md)

**Step-by-step implementation instructions** — detailed walkthroughs for fixing each issue.

**Contents:**
1. **Net Gold Scoring Fix** (Priority 1, 3-4 hours)
   - Locate scoring function
   - Add maintenance deduction
   - Handle edge cases
   - Unit tests
   - Compilation

2. **Route Planning Documentation** (Priority 2, 2-3 hours)
   - Add GameDefines constants
   - Document decision tree
   - Locate and annotate code
   - Add comments
   - Testing

3. **Tech Distance Heuristic Fix** (Priority 3, 4-6 hours)
   - Understand current implementation
   - Add EconomicAI method
   - Refactor GetRouteBuildTime()
   - Add helper functions
   - Testing

4. **City Strategy Documentation** (Priority 4, 1-2 hours)
   - Add strategy comments
   - Document state machine
   - Extract constants
   - Update code
   - Testing

5. **Validation Checklist** — Test items for each fix
6. **Compilation & Testing** — Build instructions and game testing procedure

**Audience:** Developers implementing fixes, code reviewers

---

## Quick Navigation

### By Role

**System Architect/Reviewer:**
1. Start with [IMPROVEMENTS_WORKERS_REVIEW.md](./IMPROVEMENTS_WORKERS_REVIEW.md) Section 1-3
2. Check [ISSUES_AND_FIXES.md](./ISSUES_AND_FIXES.md) for priority matrix
3. Plan implementation phases

**Developer (Implementing Fixes):**
1. Review [ISSUES_AND_FIXES.md](./ISSUES_AND_FIXES.md) section 1-2 to understand the issue
2. Follow [IMPLEMENTATION_GUIDE.md](./IMPLEMENTATION_GUIDE.md) step-by-step
3. Use validation checklist before committing

**Code Reviewer:**
1. Check [IMPROVEMENTS_WORKERS_REVIEW.md](./IMPROVEMENTS_WORKERS_REVIEW.md) Section 1 for system understanding
2. Use [ISSUES_AND_FIXES.md](./ISSUES_AND_FIXES.md) for expected behavior
3. Verify against [IMPLEMENTATION_GUIDE.md](./IMPLEMENTATION_GUIDE.md) checklist

**Modder/Balance Designer:**
1. Read [IMPROVEMENTS_WORKERS_REVIEW.md](./IMPROVEMENTS_WORKERS_REVIEW.md) Section 2 (Database Configuration)
2. Check database files for build times, tech modifiers
3. Use [ISSUES_AND_FIXES.md](./ISSUES_AND_FIXES.md) Section 4 for known route planning behavior

---

### By Topic

**Builder AI:**
- [IMPROVEMENTS_WORKERS_REVIEW.md](./IMPROVEMENTS_WORKERS_REVIEW.md#11-builder-ai-cvbuildertaskingai)
- [ISSUES_AND_FIXES.md](./ISSUES_AND_FIXES.md) Issues 1, 2, 3, 4

**Worker Production:**
- [IMPROVEMENTS_WORKERS_REVIEW.md](./IMPROVEMENTS_WORKERS_REVIEW.md#12-economic-ai-worker-production)
- [ISSUES_AND_FIXES.md](./ISSUES_AND_FIXES.md) Issue 5

**Build Times:**
- [IMPROVEMENTS_WORKERS_REVIEW.md](./IMPROVEMENTS_WORKERS_REVIEW.md#14-build-time-calculation)
- [IMPROVEMENTS_WORKERS_REVIEW.md](./IMPROVEMENTS_WORKERS_REVIEW.md#21-build-time-tuning)
- [IMPLEMENTATION_GUIDE.md](./IMPLEMENTATION_GUIDE.md#implementation-tech-distance-heuristic-fix)

**Database Configuration:**
- [IMPROVEMENTS_WORKERS_REVIEW.md](./IMPROVEMENTS_WORKERS_REVIEW.md#2-database-configuration)

**Pathfinding:**
- [IMPROVEMENTS_WORKERS_REVIEW.md](./IMPROVEMENTS_WORKERS_REVIEW.md#16-pathfinding-for-workers)
- [ISSUES_AND_FIXES.md](./ISSUES_AND_FIXES.md) Issue 6

---

## Key Concepts

### **BuilderDirective Enum**

Represents a single build task assigned to a worker:
- `BUILD_IMPROVEMENT_ON_RESOURCE` — unlock a resource
- `BUILD_IMPROVEMENT` — basic tile improvement
- `BUILD_ROUTE` — road/railroad
- `REPAIR_IMPROVEMENT` — fix pillaged improvement
- `REMOVE_FEATURE` — chop forest/jungle
- `KEEP_IMPROVEMENT` — planning marker

### **City Strategy States**

Worker production is gated by three states:
- **NEED** (workers < cities × 0.67) — Force production
- **WANT** (0.67 ≤ workers < cities × 1.50) — Prefer production
- **ENOUGH** (workers ≥ cities × 1.50) — Block production

### **Route Planning Categories**

Routes are prioritized:
1. **Main** — Capital to all cities (priority: 100)
2. **Shortcut** — Nearby city pairs (priority: 50)
3. **Strategic** — To key resources (priority: 75)
4. **Trade** — High-gold routes (priority: varies)

### **Improvement Scoring Factors**

Scoring considers:
- Yield improvement (food, production, gold, etc.)
- Strategic value (resource unlock, military benefit)
- Build time penalty (faster ↑, slower ↓)
- City specialization (science cities value science tiles more)
- Worker efficiency (prioritize accessible plots)

---

## File Cross-Reference

### **C++ Core**

| File | Lines | Purpose |
|------|-------|---------|
| CvBuilderTaskingAI.cpp | 1-100 | Init, main API |
| | 800-1100 | Route planning (UpdateRoutePlots) |
| | 1277-1300 | Build time calculation (GetTurnsToBuild) |
| | 3900-4300 | Improvement scoring (ScorePlotBuild) |
| CvCityStrategyAI.cpp | 2358-2510 | Strategy triggers (NEED/WANT/ENOUGH) |
| CvEconomicAI.cpp | 1200-3050 | Worker production decisions |
| CvUnitProductionAI.cpp | 1258-1277 | Production gating |
| CvDefines.h | 8900+ | GameDefines constants |

### **Database (SQL)**

| File | Table | Purpose |
|------|-------|---------|
| Improvements/CoreBuildSweeps.sql | Builds | Build times (CP) |
| Improvements/BuildSweeps.sql | Builds, Build_TechTimeChanges | Build times, tech modifiers (VP) |
| Civilizations/*.sql | Build_TechTimeChanges | Civ-specific modifiers |

### **Lua/UI**

| File | Function | Purpose |
|------|----------|---------|
| UI_bc1/UnitPanel/UnitPanel.lua | getUnitBuildProgressData() | Build progress display |
| UI_bc1/Improvements/YieldIconManager.lua | BuildAnchorYields() | Improvement yields |

---

## Implementation Status

| Issue | Status | Priority | Effort | Notes |
|-------|--------|----------|--------|-------|
| Net gold scoring | OPEN | 1 | 3-4h | Critical; affects late-game treasury |
| Route planning docs | OPEN | 2 | 2-3h | Needed for clarity & modding |
| Tech distance heuristic | OPEN | 3 | 4-6h | Medium impact; improves accuracy |
| City strategy docs | OPEN | 4 | 1-2h | Low impact; improves clarity |
| Adjacency interactions | OPEN | 5 | 6-8h | Low impact; complex to implement |
| Pathfinding optimize | DOCUMENTED | 6 | MEDIUM | Already identified; low priority |
| Worker repair exploit | ADDRESSED | N/A | DONE | Already fixed in CustomMods |

---

## Related Documentation

- [City Management Review](../city-management-review.md) — Production queuing, citizen automation
- [Pathfinding Documentation](../unit-movement/UNIT_MOVEMENT_PATHFINDING.md) — Worker movement
- [Resources Review](../resources/RESOURCES_REVIEW.md) — Resource improvements integration
- [Economy Reviews](../economy/) — Food production, trade, maintenance
- [Build Instructions](../../DEVELOPMENT.md) — How to compile and test

---

## How to Contribute

### **Report a New Issue**

1. File a detailed description in [ISSUES_AND_FIXES.md](./ISSUES_AND_FIXES.md)
2. Include: Severity, File location, Line number, Reproduction steps
3. Propose a fix (if possible)

### **Implement a Fix**

1. Choose an issue from [ISSUES_AND_FIXES.md](./ISSUES_AND_FIXES.md) Priority Matrix
2. Follow steps in [IMPLEMENTATION_GUIDE.md](./IMPLEMENTATION_GUIDE.md)
3. Use validation checklist before committing
4. Document your changes

### **Improve Documentation**

1. Review [IMPROVEMENTS_WORKERS_REVIEW.md](./IMPROVEMENTS_WORKERS_REVIEW.md) for gaps
2. Add comments to code
3. Extract constants to database where appropriate
4. Update this README

---

## Glossary

- **Builder** — Worker unit that performs tile improvements
- **Directive** — A specific build task assigned to a builder
- **Build Action** — The type of work (farm, mine, road, etc.)
- **Build Time** — Turns required to complete a build action
- **Improvement** — Result of a build action (farm, mine, road)
- **Route** — Path of roads/railroads connecting cities
- **Work Rate** — How many build progress points a builder gains per turn
- **Economic AI** — System that makes high-level player decisions
- **City Strategy** — State that determines city production priorities

---

## Document Version History

| Date | Author | Changes |
|------|--------|---------|
| 2026-01-11 | [Initial Review] | Created all documents |

---

## Questions?

- **System architecture:** See [IMPROVEMENTS_WORKERS_REVIEW.md](./IMPROVEMENTS_WORKERS_REVIEW.md) Section 1
- **How to fix an issue:** See [IMPLEMENTATION_GUIDE.md](./IMPLEMENTATION_GUIDE.md)
- **Current known issues:** See [ISSUES_AND_FIXES.md](./ISSUES_AND_FIXES.md)
- **Game mechanics details:** See [IMPROVEMENTS_WORKERS_REVIEW.md](./IMPROVEMENTS_WORKERS_REVIEW.md) Section 3-4

