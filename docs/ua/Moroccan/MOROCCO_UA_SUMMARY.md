# Moroccan UA Trade Plundering - Executive Summary

## Quick Assessment

| Aspect | Status | Severity |
|--------|--------|----------|
| **Plunders without war** | ✅ Working as designed | N/A |
| **Checks diplomacy before plundering** | ❌ NOT IMPLEMENTED | **HIGH** |
| **Respects allies/vassals** | ❌ NO | **HIGH** |
| **Prevents friendly trade route abuse** | ❌ NO | **HIGH** |
| **Victim has response options** | ❌ NO | **MEDIUM** |
| **Victim feedback/notification** | ✅ YES | N/A |
| **Diplomatic penalties applied** | ✅ YES (+3-5 penalty) | N/A |

---

## The Problem

### Current Behavior
Morocco can plunder **ANY** civ's trade route at any time, including:
- ✅ **SHOULD be able to**: Neutral civs, rivals, enemies
- ❌ **SHOULD NOT be able to**: Allies, vassals, civs it fears

### Example Scenario
- Morocco is formally allied with France
- Morocco creates a Berber Cavalry unit
- Moroccan cavalry encounters France's trade caravan to England
- **Current result**: Plunders successfully (wrong!)
- **Expected result**: Cannot plunder (allied status blocks it)

### Diplomatic Impact
- Plundering generates diplomatic penalty (+3 if visible)
- But victim has **NO WAY** to make Morocco stop
- Unlike spies: plundering has no escalation/confrontation mechanic

---

## Root Cause

**File:** `CvUnit.cpp` (lines 9158-9168)

```cpp
if (GET_PLAYER(m_eOwner).GetPlayerTraits()->IsCanPlunderWithoutWar())
{
    PlayerTypes eTradeUnitDest = GC.getGame().GetGameTrade()->GetDestFromID(...);
    if (eTradeUnitDest != m_eOwner)
    {
        return true;  // ❌ ONLY checks: "Is it going to Morocco?"
                      // ❌ Does NOT check: "Am I allied to the owner?"
    }
}
```

**Missing Checks:**
- Alliance status
- Vassal relationships  
- Fear/intimidation level
- Relationship attitude

---

## The Fix

### Recommended Approach: Add Diplomatic Filters

**Implementation:** Add 4 checks to `CvUnit::canPlunderTradeRoute()` and mirror in `CvUnit::plunderTradeRoute()`

```cpp
// Pseudocode
if (IsCanPlunderWithoutWar && destination != self)
{
    owner = GetTradeRouteOwner()
    
    if (IsAlly(owner))
        return false;  // ❌ Can't plunder allies
    
    if (IsVassal(owner) or IsOverlord(owner))
        return false;  // ❌ Can't plunder vassals
    
    if (IsAfraidOf(owner))
        return false;  // ❌ Can't plunder civs we fear
    
    return true;  // ✅ Can plunder rivals/neutrals/enemies
}
```

**Result:**
- Plundering still allowed for rivals/neutrals/enemies (UA preserved)
- Plundering blocked for allies/vassals (respectful, not exploitative)
- Diplomatic penalties still apply to discovered plundering

---

## Impact Assessment

### Gameplay Changes
- **Before fix**: Morocco can plunder anyone anytime
- **After fix**: Morocco can only plunder rivals, neutrals, enemies—like wartime

### Balance
- **Still strong**: UA allows plundering at all (other civs need war)
- **Now fair**: Respects diplomatic relationships
- **Not nerfed**: Can still target most civilizations (only blocks allies)

### Mod Compatibility
- Changes only affect Morocco and other civs with `CanPlunderWithoutWar` trait
- No other mechanics affected
- Safe, isolated fix

---

## Implementation Details

### Files to Modify
1. **CvUnit.cpp** - Add diplomatic checks (2 functions, ~30 lines added total)
2. **CvLuaPlayer.cpp** - Update tooltip helper (~20 lines)
3. **SQL/XML** - Add localization strings (new text keys)

### Build Process
```powershell
# From repo root
.\build_vp_clang.ps1 -Config debug
# Wait ~10-20 minutes for compilation
# Verify: clang-output/Debug/CvGameCore_Expansion2.dll exists
```

### Testing
1. Create Morocco game
2. Test ally/vassal scenarios → Plundering blocked
3. Test rival/neutral scenarios → Plundering works
4. Verify no crashes, tooltips show reasons

---

## Optional Enhancement: Player Response System

**Future enhancement** (not included in main fix):
- When Morocco plunders visible trade route, notify victim with options:
  1. "Accept it" → +3 diplo penalty
  2. "Demand Morocco stop" → Morocco loses relations, victim gets bonus
  3. "Declare war" → Immediate war declaration

**Status**: Good idea but requires more UI/code changes; recommend after main fix

---

## Estimated Effort

| Task | Time | Difficulty |
|------|------|-----------|
| Code changes | 1-2 hours | Easy (straightforward copy-paste) |
| Building/testing | 1-2 hours | Medium (first time might need troubleshooting) |
| Localization | 30 min | Easy (just text strings) |
| **Total** | **3-4 hours** | **Low-Medium** |

---

## Risk Level: LOW ✅

- **Isolated change**: Only affects Morocco's plundering
- **No breaking changes**: Doesn't modify other mechanics
- **Backward compatible**: Doesn't affect existing saves
- **Simple logic**: Easy to understand and debug
- **Well-tested**: Uses existing diplomatic APIs

---

## Decision Matrix

| Decision | Recommendation | Rationale |
|----------|---|---|
| **Fix the issue?** | YES ✅ | Current behavior is clearly unbalanced |
| **Approach** | Add diplomatic checks | Simple, effective, maintains UA spirit |
| **When?** | Next patch | Low-risk, high-value fix |
| **Include response system?** | Later | Nice-to-have, not blocking issue |

---

## Questions to Answer

Before implementing, decide:

1. **Alliance level threshold**: Should formal alliance block plundering, or only defensive pact?
   - Recommendation: **Defensive Pact or higher** (safer)

2. **Fear level**: Block if afraid, or only if terrified?
   - Recommendation: **Afraid or higher** (respect fear mechanic)

3. **Vassal relations**: Block completely or allow if dominant?
   - Recommendation: **Block completely** (respect vassal bond)

4. **Notification changes**: Keep current "discovered" notifications?
   - Recommendation: **YES, keep as-is** (still informative)

---

## Next Steps

1. ✅ **Review code** (DONE - see MOROCCO_UA_REVIEW.md)
2. ✅ **Understand issue** (DONE - this document)
3. → **Plan implementation** (see MOROCCO_UA_IMPLEMENTATION.md)
4. → **Write code changes** (detailed guide provided)
5. → **Test** (test scenarios documented)
6. → **Commit** (merge to main)

---

## References

- **Code locations**: CvUnit.cpp (9103-9238), CvTradeClasses.cpp (4964-5169)
- **Trait definition**: Morocco.sql
- **Diplomatic APIs**: CvDiplomacyAI, CvTeam
- **Related systems**: Alliance strength, vassal mechanics, fear levels

