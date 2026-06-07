# Unique Ability (UA) Documentation

This folder contains detailed reviews and implementation guides for Civilization V unique abilities, with a focus on balance analysis and bug fixes.

## Structure

```
docs/ua/
├── Moroccan/
│   ├── MOROCCO_UA_INDEX.md                    (Start here - navigation guide)
│   ├── MOROCCO_UA_SUMMARY.md                  (Executive summary)
│   ├── MOROCCO_UA_REVIEW.md                   (Technical deep-dive)
│   ├── MOROCCO_UA_VISUAL_GUIDE.md             (Flowcharts & diagrams)
│   ├── MOROCCO_UA_IMPLEMENTATION.md           (Step-by-step guide)
│   ├── MOROCCO_UA_CODE_SNIPPETS.md            (Ready-to-use code)
│   ├── MOROCCO_UA_STATUS.txt                  (Status overview)
│   └── MOROCCO_UA_COMPLETION_SUMMARY.txt      (Completion report)
└── README.md                                  (This file)
```

## Current Reviews

### Moroccan UA - Trade Plundering Issue
**Status:** ✅ Complete & Ready for Implementation  
**Location:** `Moroccan/`  
**Issue:** Morocco's `CanPlunderWithoutWar` trait allows plundering allied/vassal trade routes  
**Solution:** Add diplomatic status checks  
**Effort:** 2-4 hours  
**Risk:** LOW  

**Quick Start:** Read `Moroccan/MOROCCO_UA_INDEX.md`

## Adding New UA Documentation

When documenting a new UA issue:

1. Create a new folder: `docs/ua/[CivilizationName]/`
2. Create the following documents:
   - `[CIV]_UA_INDEX.md` - Navigation guide
   - `[CIV]_UA_SUMMARY.md` - Executive summary
   - `[CIV]_UA_REVIEW.md` - Technical analysis
   - `[CIV]_UA_VISUAL_GUIDE.md` - Diagrams
   - `[CIV]_UA_IMPLEMENTATION.md` - Step-by-step
   - `[CIV]_UA_CODE_SNIPPETS.md` - Code ready to copy
   - `[CIV]_UA_STATUS.txt` - Status overview

3. Update this README with the new civilization entry

## Documentation Standards

All UA documentation should include:
- ✅ Clear problem statement with examples
- ✅ Root cause analysis with code locations
- ✅ Proposed solution(s) with pros/cons
- ✅ Complete implementation guide
- ✅ Copy-paste ready code snippets
- ✅ Comprehensive testing procedures
- ✅ Risk assessment
- ✅ Effort estimate

---

For questions about Moroccan UA, see: `Moroccan/MOROCCO_UA_INDEX.md`
