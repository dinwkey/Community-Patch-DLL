# Unit Movement & Pathfinding Documentation Index

Complete reference for the Civilization V unit movement and pathfinding system in Community Patch DLL.

---

## 📚 Documentation Files

### [UNIT_MOVEMENT_PATHFINDING.md](UNIT_MOVEMENT_PATHFINDING.md) — Complete System Overview
**Length:** ~600 lines | **Scope:** Entire system

The authoritative reference covering:
- **Movement Points System** — cost calculations, variants (standard/selective ZOC/no ZOC)
- **Zones of Control (ZOC)** — enemy unit blocking, selective exemptions, pathfinder integration
- **Embarkation & Disembarkation** — three-tier cost hierarchy (full/cheap/free), city bonuses, deep water support
- **A* Pathfinder** — algorithm overview, path types (20+), cost functions, caching, performance optimizations
- **Movement Flags** — 20+ flag definitions and usage patterns
- **Known Issues & Improvements** — high/medium/low priority list with recommendations
- **Testing & Validation** — test checklist and performance baselines
- **References** — code file pointers and related functions

**Use this document when:**
- Learning the system from scratch
- Understanding overall architecture
- Finding code references
- Reviewing system-wide improvements

---

### [ISSUES_AND_FIXES.md](ISSUES_AND_FIXES.md) — Issue Tracking & Solutions
**Length:** ~400 lines | **Scope:** Known issues (8 tracked)

Detailed issue registry with current status and solutions:

| Issue | Status | Priority | Topic |
|-------|--------|----------|-------|
| UMP-001 | ✅ Done | HIGH | Sane Unit Movement Cost (additive vs multiplicative) |
| UMP-002 | ✅ Done | HIGH | Selective ZOC for group movement |
| UMP-003 | ✅ Done | HIGH | Embark/disembark cost hierarchy |
| UMP-004 | ⚠️ Partial | MEDIUM | Pathfinder recursion guard |
| UMP-005 | 📋 Open | MEDIUM | Heuristic tightness optimization |
| UMP-006 | ⚠️ Partial | MEDIUM | Approximate destination edge cases |
| UMP-007 | ✅ Done | LOW | Precise movement UI display |
| UMP-008 | ✅ Done | MEDIUM | Deep water embarkation support |

Each issue includes:
- Problem description
- Current solution/status
- Code changes (with file locations)
- Validation test cases
- Remaining tasks

**Use this document when:**
- Investigating a specific issue
- Understanding what's been fixed
- Finding test cases for features
- Planning improvements

---

### [CODE_INTEGRATION_GUIDE.md](CODE_INTEGRATION_GUIDE.md) — Practical Examples & Patterns
**Length:** ~500 lines | **Scope:** Code recipes and integration

14 complete, runnable code examples covering:

1. **Basic Pathfinding** — simple path, with flags, siege unit
2. **Movement Costs** — single-step, ZOC check, embarkation
3. **Movement Flags** — custom flag sets, selective ZOC, escort example
4. **Advanced Techniques** — reachable plots, path verification
5. **Performance** — batch pathfinding, path caching
6. **Debugging** — logging decisions, performance profiling
7. **Common Pitfalls** — INT_MAX handling, configuration, path validation
8. **Migration Guide** — vanilla → VP/Community Patch patterns

**Use this document when:**
- Implementing new movement-related features
- Debugging movement problems
- Performance tuning
- Migrating code from vanilla

---

## 🔍 Quick Reference by Topic

### Movement Costs
- **System Overview:** [UNIT_MOVEMENT_PATHFINDING.md § 1](UNIT_MOVEMENT_PATHFINDING.md#1-movement-points-system)
- **Cost Functions:** [UNIT_MOVEMENT_PATHFINDING.md § 1.2](UNIT_MOVEMENT_PATHFINDING.md#getcostsformove)
- **Code Examples:** [CODE_INTEGRATION_GUIDE.md § 2](CODE_INTEGRATION_GUIDE.md#2-movement-cost-calculations)
- **Issues:** [ISSUES_AND_FIXES.md § UMP-001, UMP-005](ISSUES_AND_FIXES.md)

### Embarkation / Naval Movement
- **System Overview:** [UNIT_MOVEMENT_PATHFINDING.md § 3](UNIT_MOVEMENT_PATHFINDING.md#3-embarkation--disembarkation)
- **Cost Hierarchy:** [UNIT_MOVEMENT_PATHFINDING.md § 3.2](UNIT_MOVEMENT_PATHFINDING.md#embarkdisembark-costs)
- **Deep Water:** [UNIT_MOVEMENT_PATHFINDING.md § 3.3](UNIT_MOVEMENT_PATHFINDING.md#deep-water-embarkation-mod_promotions_deep_water_embarkation)
- **Code Examples:** [CODE_INTEGRATION_GUIDE.md § 2.5](CODE_INTEGRATION_GUIDE.md#example-6-check-embarkation-cost)
- **Issues:** [ISSUES_AND_FIXES.md § UMP-003, UMP-008](ISSUES_AND_FIXES.md)

### Zones of Control (ZOC)
- **System Overview:** [UNIT_MOVEMENT_PATHFINDING.md § 2](UNIT_MOVEMENT_PATHFINDING.md#2-zones-of-control-zoc)
- **Selective ZOC:** [UNIT_MOVEMENT_PATHFINDING.md § 2.3](UNIT_MOVEMENT_PATHFINDING.md#pathfinder-integration)
- **Code Examples:** [CODE_INTEGRATION_GUIDE.md § 2.6, 3.8](CODE_INTEGRATION_GUIDE.md)
- **Issues:** [ISSUES_AND_FIXES.md § UMP-002](ISSUES_AND_FIXES.md)

### A* Pathfinder
- **System Overview:** [UNIT_MOVEMENT_PATHFINDING.md § 4](UNIT_MOVEMENT_PATHFINDING.md#4-a-pathfinder)
- **Path Types:** [UNIT_MOVEMENT_PATHFINDING.md § 4.3](UNIT_MOVEMENT_PATHFINDING.md#path-types-enum-pathtype)
- **Unit Movement Path:** [UNIT_MOVEMENT_PATHFINDING.md § 4.4](UNIT_MOVEMENT_PATHFINDING.md#unit-movement-path-pt_unit_movement)
- **Cost Functions:** [UNIT_MOVEMENT_PATHFINDING.md § 4.4](UNIT_MOVEMENT_PATHFINDING.md#cost-function-pathcostparent-node-data-finder)
- **Code Examples:** [CODE_INTEGRATION_GUIDE.md § 1, 4](CODE_INTEGRATION_GUIDE.md)
- **Issues:** [ISSUES_AND_FIXES.md § UMP-004, UMP-005, UMP-006](ISSUES_AND_FIXES.md)

### Movement Flags
- **Flag Reference:** [UNIT_MOVEMENT_PATHFINDING.md § 5.2](UNIT_MOVEMENT_PATHFINDING.md#complete-flag-reference)
- **Usage Patterns:** [UNIT_MOVEMENT_PATHFINDING.md § 5.3](UNIT_MOVEMENT_PATHFINDING.md#flag-usage-patterns)
- **Code Examples:** [CODE_INTEGRATION_GUIDE.md § 3](CODE_INTEGRATION_GUIDE.md#3-working-with-movement-flags)

### Performance & Optimization
- **Caching & Optimizations:** [UNIT_MOVEMENT_PATHFINDING.md § 4.6](UNIT_MOVEMENT_PATHFINDING.md#performance-optimizations)
- **Known Issues:** [UNIT_MOVEMENT_PATHFINDING.md § 6](UNIT_MOVEMENT_PATHFINDING.md#6-known-issues--improvement-opportunities)
- **Code Examples:** [CODE_INTEGRATION_GUIDE.md § 5](CODE_INTEGRATION_GUIDE.md#5-performance-patterns)
- **Issues:** [ISSUES_AND_FIXES.md § UMP-004, UMP-005, UMP-006, UMP-007](ISSUES_AND_FIXES.md)

### Testing & Validation
- **Test Checklist:** [UNIT_MOVEMENT_PATHFINDING.md § 7.1](UNIT_MOVEMENT_PATHFINDING.md#test-checklist)
- **Test Cases:** [ISSUES_AND_FIXES.md throughout](ISSUES_AND_FIXES.md)
- **Code Examples:** [CODE_INTEGRATION_GUIDE.md § 6](CODE_INTEGRATION_GUIDE.md#6-debugging--logging)

---

## 📋 File Structure

```
docs/
├── unit-movement/
│   ├── README.md (this file)
│   ├── UNIT_MOVEMENT_PATHFINDING.md       (Main reference)
│   ├── ISSUES_AND_FIXES.md                (Issue tracking)
│   └── CODE_INTEGRATION_GUIDE.md          (Code examples)
├── military-ai/
│   └── MILITARY_AI_FIXES.md               (Related: threat assessment)
└── naval-air/
    └── NAVAL_AIR_MECHANICS_FIXES.md       (Related: embarkation fixes)
```

---

## 🎯 Learning Path

### For New Developers
1. Start: [UNIT_MOVEMENT_PATHFINDING.md § 1](UNIT_MOVEMENT_PATHFINDING.md#1-movement-points-system) (Movement Points overview)
2. Then: [CODE_INTEGRATION_GUIDE.md § 1](CODE_INTEGRATION_GUIDE.md#1-basic-pathfinding-setup) (Simple pathfinding example)
3. Deep dive: [UNIT_MOVEMENT_PATHFINDING.md § 4](UNIT_MOVEMENT_PATHFINDING.md#4-a-pathfinder) (Pathfinder architecture)

### For Debugging Issues
1. Check: [ISSUES_AND_FIXES.md](ISSUES_AND_FIXES.md) (Issue registry)
2. Review: Related code examples in [CODE_INTEGRATION_GUIDE.md § 6](CODE_INTEGRATION_GUIDE.md#6-debugging--logging)
3. Cross-reference: File locations in [UNIT_MOVEMENT_PATHFINDING.md § 8](UNIT_MOVEMENT_PATHFINDING.md#8-references--related-code)

### For Performance Optimization
1. Check: [UNIT_MOVEMENT_PATHFINDING.md § 4.6](UNIT_MOVEMENT_PATHFINDING.md#performance-optimizations) (Current optimizations)
2. Review: [ISSUES_AND_FIXES.md § UMP-004, UMP-005](ISSUES_AND_FIXES.md) (Known optimizations)
3. Implement: Patterns in [CODE_INTEGRATION_GUIDE.md § 5](CODE_INTEGRATION_GUIDE.md#5-performance-patterns)

### For New Features
1. Plan: [UNIT_MOVEMENT_PATHFINDING.md § 5.4](UNIT_MOVEMENT_PATHFINDING.md#custom-mods-mod_-conditionals) (Mod flags)
2. Integrate: [CODE_INTEGRATION_GUIDE.md § 7](CODE_INTEGRATION_GUIDE.md#7-migration-guide-from-vanilla-to-vpcommunity-patch) (Migration guide)
3. Test: Examples in [ISSUES_AND_FIXES.md](ISSUES_AND_FIXES.md) (Test patterns)

---

## 🔑 Key Concepts Glossary

| Term | Definition | See |
|------|-----------|-----|
| **Movement Points (MP)** | Units of 1/60 tile; `MOVE_DENOMINATOR` = 60 | [§ 1](UNIT_MOVEMENT_PATHFINDING.md#1-movement-points-system) |
| **Zone of Control (ZOC)** | Area around enemy unit that slows or blocks movement | [§ 2](UNIT_MOVEMENT_PATHFINDING.md#2-zones-of-control-zoc) |
| **Embarkation** | Land unit transitions to water (may end turn) | [§ 3](UNIT_MOVEMENT_PATHFINDING.md#3-embarkation--disembarkation) |
| **A* Pathfinder** | Graph search algorithm finding optimal path | [§ 4](UNIT_MOVEMENT_PATHFINDING.md#4-a-pathfinder) |
| **Path Types** | Different cost functions for different pathfinding scenarios | [§ 4.3](UNIT_MOVEMENT_PATHFINDING.md#path-types-enum-pathtype) |
| **Movement Flags** | Bitmask controlling pathfinding behavior | [§ 5](UNIT_MOVEMENT_PATHFINDING.md#5-movement-flags) |
| **Node Cache** | Pre-computed data per plot (terrain, enemies, etc.) | [§ 4.6](UNIT_MOVEMENT_PATHFINDING.md#node-caching) |
| **Heuristic** | Estimate of remaining cost to destination | [§ 4.5](UNIT_MOVEMENT_PATHFINDING.md#heuristic-function-pathheuristicicurrentx-icurrenty-inextx-inexty-idestx-idesty) |

---

## 🔗 Related Documentation

### Military & Combat
- [Military AI Fixes](../military-ai/MILITARY_AI_FIXES.md) — Movement considered in threat assessment
- [Naval & Air Mechanics](../naval-air/NAVAL_AIR_MECHANICS_FIXES.md) — Embarkation bug fixes

### Game Mechanics
- [Game Mechanics Reference](../reference/game-mechanics.md) — Broader system overview

### Code References
- **Core Files:**
  - [CvUnit.h](../../CvGameCoreDLL_Expansion2/CvUnit.h) — Movement flags, unit methods
  - [CvUnitMovement.h/cpp](../../CvGameCoreDLL_Expansion2/CvUnitMovement.h) — Cost calculations
  - [CvAStar.h/cpp](../../CvGameCoreDLL_Expansion2/CvAStar.h) — Pathfinder
  - [CvAStarNode.h](../../CvGameCoreDLL_Expansion2/CvAStarNode.h) — Path node structure

---

## 📊 Documentation Statistics

| Document | Lines | Sections | Examples | Issues | Status |
|----------|-------|----------|----------|--------|--------|
| UNIT_MOVEMENT_PATHFINDING.md | ~600 | 9 | 0 | 10 | ✅ Complete |
| ISSUES_AND_FIXES.md | ~400 | 8 issues | 14 test cases | 8 tracked | ✅ Complete |
| CODE_INTEGRATION_GUIDE.md | ~500 | 8 | 14 examples | 3 pitfalls | ✅ Complete |
| **Total** | ~1500 | 25+ | 14+ | 21 | ✅ Complete |

---

## 🚀 Getting Started

### For Code Review
1. Read [UNIT_MOVEMENT_PATHFINDING.md](UNIT_MOVEMENT_PATHFINDING.md) for system overview
2. Check [ISSUES_AND_FIXES.md](ISSUES_AND_FIXES.md) for known issues
3. Use [CODE_INTEGRATION_GUIDE.md](CODE_INTEGRATION_GUIDE.md) for examples

### For Implementation
1. Find relevant code examples in [CODE_INTEGRATION_GUIDE.md](CODE_INTEGRATION_GUIDE.md)
2. Refer to [UNIT_MOVEMENT_PATHFINDING.md § 8](UNIT_MOVEMENT_PATHFINDING.md#8-references--related-code) for file locations
3. Check [ISSUES_AND_FIXES.md](ISSUES_AND_FIXES.md) for known issues/workarounds

### For Testing
1. Use test checklist in [UNIT_MOVEMENT_PATHFINDING.md § 7](UNIT_MOVEMENT_PATHFINDING.md#7-testing--validation)
2. Review test cases in [ISSUES_AND_FIXES.md](ISSUES_AND_FIXES.md)
3. Use debug examples in [CODE_INTEGRATION_GUIDE.md § 6](CODE_INTEGRATION_GUIDE.md#6-debugging--logging)

---

## 📝 Document Maintenance

| Document | Version | Updated | Maintainer |
|----------|---------|---------|------------|
| UNIT_MOVEMENT_PATHFINDING.md | 1.0 | Jan 2025 | CP-DLL Team |
| ISSUES_AND_FIXES.md | 1.0 | Jan 2025 | CP-DLL Team |
| CODE_INTEGRATION_GUIDE.md | 1.0 | Jan 2025 | CP-DLL Team |

### How to Update
1. Edit the relevant `.md` file in `docs/unit-movement/`
2. Update version number at document bottom
3. Update this index if structure changes
4. Commit with message: `docs: update unit movement docs (section updated)`

---

## ❓ FAQ

**Q: Where do I start if I'm new to the movement system?**
A: Read the [Learning Path](#🎯-learning-path) section above, starting with "For New Developers".

**Q: How do I find the code for a specific movement feature?**
A: Use the [Quick Reference by Topic](#🔍-quick-reference-by-topic) section, which links to both documentation and code files.

**Q: What are the most important things to understand?**
A: (1) Movement points are in 1/60 scale, (2) A* pathfinder with ZOC, (3) Embarkation cost hierarchy, (4) Movement flags control pathfinding.

**Q: How do I debug slow pathfinding?**
A: See [CODE_INTEGRATION_GUIDE.md § 6](CODE_INTEGRATION_GUIDE.md#6-debugging--logging) for profiling and logging examples.

**Q: What's been changed recently?**
A: Check [ISSUES_AND_FIXES.md § Validation Summary](ISSUES_AND_FIXES.md#validation-summary) for status of recent features.

---

## 📞 Support

For questions or corrections:
1. Check this documentation thoroughly (use Ctrl+F to search)
2. Review related code in referenced file locations
3. Check [ISSUES_AND_FIXES.md](ISSUES_AND_FIXES.md) for known issues
4. Ask in Community Patch DLL development channels

---

**Documentation Index Version:** 1.0  
**Last Updated:** January 2025  
**Scope:** Community Patch DLL Unit Movement & Pathfinding System
