# Resources System Review

**Date:** January 11, 2026  
**Scope:** Strategic/luxury/bonus resources, yields, consumption, trade mechanics, resource reveal rules

---

## Executive Summary

The Civ5 resource system handles three resource classes:
- **Strategic Resources**: Limited supply, combat unit requirements, tradeable
- **Luxury Resources**: One per player at happiness cap, grant happiness/WLTKD, tradeable
- **Bonus Resources**: Provide yield benefits locally, non-tradeable

This review identifies structural issues, design gaps, and improvement opportunities across resource visibility, trading mechanics, consumption tracking, and yield calculation.

---

## Current Architecture

### Resource Class Hierarchy

**File:** [CvInfos.h](CvGameCoreDLL_Expansion2/CvInfos.h#L1788-L1850) (`CvResourceInfo`)

Core resource data:
```cpp
class CvResourceInfo : public CvBaseInfo
{
    int getResourceClassType() const;     // RESOURCECLASS_BONUS/STRATEGIC/LUXURY
    int getTechReveal() const;            // Tech that reveals resource
    int getPolicyReveal() const;          // Policy that reveals resource
    int getTechCityTrade() const;         // Tech to enable trading
    int getImproveTech() const;           // Tech to improve resource
    int getTechObsolete() const;          // Tech making resource obsolete
    int getHappiness() const;             // Happiness (luxury only)
    int getMonopolyHappiness() const;     // Monopoly happiness bonus
    int getMonopolyGALength() const;      // Golden Age length
    int getMonopolyAttackBonus() const;   // Monopoly attack bonus
    int getMonopolyDefenseBonus() const;  // Monopoly defense bonus
    int getMonopolyMovementBonus() const; // Monopoly movement bonus
    // ... other fields
};
```

### Resource Improvement Configuration

**File:** [CvImprovementClasses.h](CvGameCoreDLL_Expansion2/CvImprovementClasses.h#L14-L35) (`CvImprovementResourceInfo`)

Per-improvement resource data:
```cpp
class CvImprovementResourceInfo
{
    int getDiscoverRand() const;        // Random discovery chance
    bool isResourceMakesValid() const;  // Does improvement validate resource?
    bool isResourceTrade() const;       // Does improvement enable trading?
    int getYieldChange(int i) const;    // Yield modifier array [NUM_YIELD_TYPES]
};
```

---

## 1. RESOURCE REVEAL SYSTEM

### Current Implementation

**Reveal Mechanics:**
1. **Tech Reveal**: `getTechReveal()` — tech ID that reveals resource globally
2. **Policy Reveal**: `getPolicyReveal()` — policy ID that reveals resource globally
3. **Local Visibility**: Exploration + improvement construction
4. **City Trade Check**: `getTechCityTrade()` — tech required to trade from city (separate from visibility)

**Key Functions:**
- `CvPlayer::IsResourceRevealed(ResourceTypes eResource)` — checks if player knows resource exists
- `CvPlayer::IsResourceCityTradeable(ResourceTypes eResource)` — checks if player can trade it
- `CvPlot::GetResourceType()` — returns resource (hidden to other players if not revealed)
- `CvPlot::GetNumResource()` — returns quantity (hidden until revealed)

**UI Implementation:**
- [TopPanel.lua](../../../(2) Vox Populi/LUA/TopPanel.lua#L1510-L1535) filters resources by `IsResourceRevealed()` before displaying

---

### Issues Identified

#### 1.1 **Asymmetric Reveal vs. Trade Tech** ⚠️ **ISSUE**

**Problem:** Resource visibility and trading use different tech gates:
- Reveal tech: Makes resource visible globally (once anyone discovers, everyone sees it)
- Trade tech: Enables a specific player to trade *from their own cities*

**Current Behavior:**
```cpp
// Players can see resource exists (getTechReveal)
if (pTeam->HasTech(pkResourceInfo->getTechReveal())) {
    // Resource is visible
}

// But can only trade if:
if (GET_TEAM(ePlayer).HasTech(pkResourceInfo->getTechCityTrade())) {
    // Can trade from cities
}
```

**Symptoms:**
- A player can *see* Iron exists in the world but *cannot trade it* because they lack the trade tech
- Causes confusion: player knows enemy has Iron but can't negotiate for it
- AI struggles to value deals when visibility ≠ tradeability

**Example:**
- Tech reveals Iron to all players (visibility)
- Bronze Working (early) allows Iron trade (trading tech)
- Result: Late-game player without Bronze Working can see Iron everywhere but negotiate nothing

---

#### 1.2 **Policy-Based Reveal Not Integrated with Tech Chain** ⚠️ **ISSUE**

**Problem:** `getPolicyReveal()` allows non-tech reveal but is poorly integrated:

1. **No UI Tooltip**: Policies don't inform player they'll reveal a resource
2. **Limited Usage**: Very few resources use policy reveal in base VP/CP data
3. **Ambiguous Timing**: When exactly does policy reveal take effect? (Adoption? Instant?)
4. **Missing Slot Check**: No indication if policy slot was used for other purpose

**Current Usage:**
- Mostly internal/rare; not well documented

---

#### 1.3 **Obsolete Tech Not Tracked at UI Level** ⚠️ **MINOR ISSUE**

**Problem:** `getTechObsolete()` marks resource as obsolete but:

1. **Hidden Gameplay**: Player doesn't see when a resource becomes worthless
2. **No Warning**: No notification before tech is discovered
3. **Trading Logic Unclean**: AI may still value obsolete resources in deals
4. **Inconsistent Removal**: Obsolete resources remain on map/inventory

**Example:**
- Horse is obsolete when Replaceable Parts discovered
- Player may still hold Horse deals and doesn't realize their value evaporated
- UI doesn't grey out obsolete resources in trade screen

---

### Improvements Proposed

#### Improvement 1.1a: **Unified Reveal+Trade Gate**

**Proposal:** Align resource trading availability with visibility:

```cpp
// Single gate: if you can see it, you can trade it (subject to tech/policy checks)
bool CvPlayer::CanTradeResource(ResourceTypes eResource) const
{
    if (!IsResourceRevealed(eResource))
        return false;  // Can't trade what you can't see
    
    // Then check trade tech requirement
    if (getTechCityTrade() != NO_TECH)
        return GET_TEAM(GetTeam()).HasTech(pkResourceInfo->getTechCityTrade());
    
    return true;  // No trade tech gate = can trade
}
```

**Benefits:**
- Simplifies player expectations
- Removes confusion between visibility and tradeability
- Allows proper UI signaling of trade availability

**Alternative:** Allow *different* gate for initial trade offer (more restrictive) vs. accepting trade (less restrictive).

---

#### Improvement 1.1b: **Tooltip for Trade Tech Gate**

**Proposal:** In trade dialog, show which tech is blocking trade:

```
[Cannot trade Iron Ore]
Requires: Bronze Working

[Would require tech unlock to trade this resource]
```

**Implementation:**
- [TradeLogic.lua](../../../(2) Vox Populi/LUA/TradeLogic.lua#L1920-L1950) already filters tradeable resources; add tooltip for blocked ones.

---

#### Improvement 1.2a: **Better Policy Reveal Integration**

**Proposal:** 
1. Document which policies reveal which resources (wiki, UI tooltip)
2. When policy adopted, show notification: "[Policy Name] reveals [Resource]"
3. Update resource tooltips to list revealing policies

---

#### Improvement 1.3a: **Obsolete Resource Handling**

**Proposal:**
1. **Visual Indicator**: Grey out obsolete resources in inventory/trade screen
2. **Notification**: "Iron has become obsolete. Consider trading remaining stockpiles."
3. **Auto-Unload**: Optional setting to auto-trade obsolete resources for best value

**Implementation Point:** [CvDealAI.cpp](CvGameCoreDLL_Expansion2/CvDealAI.cpp#L3866) (`DoAddLuxuryResourceToThem`) — add obsolete check.

---

## 2. STRATEGIC RESOURCE CONSUMPTION & TRACKING

### Current Implementation

**Consumption Model:**
- Units have strategic resource requirements: `CvUnitEntry::GetResourceQuantityRequirement()`
- City must have net positive supply OR unit suffers combat penalty
- Consumption is automatic; no per-turn tracking visible to player

**Supply Calculation:**
```cpp
int CvPlayer::GetNumResourceAvailable(ResourceTypes eResource, bool bIncludeImport)
{
    // Local + imported - strategic requirements - exported
    int iNumAvailable = GetNumResourceTotal(eResource, bIncludeImport);
    iNumAvailable -= GetNumResourceUsed(eResource);
    return max(0, iNumAvailable);
}
```

**Key Functions:**
- `GetNumResourceTotal()` — count all sources (local, imported, trade, etc.)
- `GetNumResourceUsed()` — sum of all unit requirements in empire
- `GetNumResourceAvailable()` — net available for use/trade
- `GetResourceExport()` / `GetResourceImport()` — trade flow tracking

---

### Issues Identified

#### 2.1 **No Visible Consumption Tracking** ⚠️ **ISSUE**

**Problem:** Players cannot see per-turn strategic resource usage breakdown:

1. **Missing UI**: No screen shows "Iron: 5 used by units, 2 available, 1 in trade"
2. **Hidden Penalty**: Combat penalty from missing resource not obvious in battle forecast
3. **Late-Game Pain**: Early game has few resources; late game is opaque
4. **AI Debugging**: Hard to understand why AI makes odd trade decisions

**Current State:**
- [TopPanel.lua](../../../(2) Vox Populi/LUA/TopPanel.lua#L1510-L1535) shows available count but NOT usage breakdown

**Example:**
- Player has 5 Iron, 3 units using Iron = 2 available
- Player doesn't see the 3-unit usage, only the "2 available" number
- When new unit is built, available drops to 1; player surprised

---

#### 2.2 **Combat Penalty Not Explained Well** ⚠️ **ISSUE**

**Problem:** Unit combat strength penalty for missing resources is poorly communicated:

1. **Tooltip Vague**: Says "−5% to Strength" but doesn't explain why
2. **No Planning Tool**: Can't predict penalty before unit recruitment
3. **Trade Negotiation Broken**: AI doesn't know human values strategic resources for combat

**Implementation:** [EnemyUnitPanel.lua](../../../(1) Community Patch/Core Files/Overrides/EnemyUnitPanel.lua#L1046-L1050)
```lua
iModifier = pTheirUnit:GetStrategicResourceCombatPenalty();
-- But no breakdown of *which* resource is missing
```

---

#### 2.3 **Duplicate Trade Exports Not Prevented** ⚠️ **MINOR ISSUE**

**Problem:** A player can export 2 Iron to two different players for a total of 4 Iron (if they have 3):

1. **Supply Arithmetic Broken**: Can over-commit resources in deals
2. **Deal Cancellation Risk**: If one deal breaks, other player loses supply mid-game
3. **AI Exploit**: AI could be abused to stall player by offering broken deals

**Current Safeguard:**
- [CvDealAI.cpp](CvGameCoreDLL_Expansion2/CvDealAI.cpp#L3866) checks `IsPossibleToTradeItem()` but may not block all overcommits

---

#### 2.4 **No Consumption Forecast Before Unit Production** ⚠️ **ISSUE**

**Problem:** Building a unit doesn't show resource impact upfront:

1. **No Warning**: Player trains 5 Iron units without seeing remaining Iron drops to negative
2. **Hidden Debt**: Units built in queue consume strategic resources immediately, not when unit completes
3. **No Rollback**: Can't see consequence before committing production

**Example:**
- Player queues 3 units each needing Iron
- Doesn't see "After production: Iron −1" warning
- Units are made; now has combat penalty
- No easy way to cancel just the next unit in queue

---

### Improvements Proposed

#### Improvement 2.1a: **Resource Usage Breakdown UI**

**Proposal:** Add a new "Resources" info pane (like "Military" or "Diplomacy"):

```
STRATEGIC RESOURCES OVERVIEW
────────────────────────────
Iron (15 total)
  ├─ Local: 8
  ├─ Import: 3 (2-turn deal w/ Rome)
  ├─ Used by Units: 5
  │   ├─ Heavy Cavalry (2)
  │   ├─ Catapult (1)
  │   └─ Warriors (2)
  ├─ Available: 10
  └─ Export: 2 (agreement w/ Egypt)

Horse (3 total)
  ├─ Local: 2
  ├─ Used: 0
  ├─ Available: 3
  └─ Export: 1 (trade agreement)
```

**Implementation:** 
- Similar to [Happiness Overview](../happiness/HAPPINESS_REVIEW.md) system
- Add Lua panel that calls `CvPlayer::GetNumResource*()` methods
- Show trend/history (−1 per turn if deficient)

---

#### Improvement 2.1b: **Strategic Resource Forecast on Build Queue**

**Proposal:** When producing unit with resource requirement, show tooltip:

```
[Heavy Cavalry] (2 turns)
Resources: −2 Iron
  After Build: Iron 8 → 6
  
  ⚠ If iron drops below 0: −5% strength penalty
```

**Implementation Point:** Building production UI calls `GetNumResourceAvailable()` after hypothetical unit cost.

---

#### Improvement 2.2a: **Better Combat Strength Tooltip**

**Proposal:** In unit strength breakdown (battle preview), show resource impact:

```
Heavy Cavalry Strength: 28
├─ Base: 30
├─ Great General Aura: +5
├─ Promotion: +2
├─ Terrain: −2
└─ [!] Missing Iron (−5): Iron shortage in empire
    Current: 2 Iron / 5 units need it
    Recruit 1 more unit → −3 strength
```

**Implementation Point:** [EnemyUnitPanel.lua](../../../(1) Community Patch/Core Files/Overrides/EnemyUnitPanel.lua#L1046) enhancement.

---

#### Improvement 2.3a: **Stricter Trade Validation**

**Proposal:** Before finalizing a deal, validate total commitments:

```cpp
bool CvDeal::CheckResourceOvercommit()
{
    for each ResourceTypes eResource
    {
        int iTotalExport = GetAllDealsExporting(ePlayer, eResource);
        int iAvailable = GET_PLAYER(ePlayer).GetNumResourceAvailable(eResource, false);
        
        if (iTotalExport > iAvailable)
            return false;  // Prevent deal
    }
    return true;
}
```

**Implementation:** Call before deal acceptance in [CvDealClasses.cpp](CvGameCoreDLL_Expansion2/CvDealClasses.cpp).

---

#### Improvement 2.4a: **Unit Production Resource Impact Preview**

**Proposal:** In production queue, show resource impact of *all* queued units:

```
Production Queue:
1. Heavy Cavalry (2 turns)     → −2 Iron
2. Catapult (3 turns)          → −1 Iron, −2 Horse
3. Warrior (1 turn)            → (no resources)

Total Cost: −3 Iron, −2 Horse
After All: Iron 10→7, Horse 5→3
```

**Implementation Point:** Production UI / Production advisor calls `GetNumResourceAvailable()` for all queued units.

---

## 3. LUXURY RESOURCE MECHANICS

### Current Implementation

**Happiness Model:**
- Player with luxury gets `pkResourceInfo->getHappiness()` (typically 4 happiness)
- One copy only per civ (additional copies don't stack)
- Duplication: Multiple of same luxury = no benefit beyond first
- Trading: Can export to others for gold/deals

**Luxury Distribution Logic:**

**File:** [CvPlayer.cpp](CvGameCoreDLL_Expansion2/CvPlayer.cpp) (from happiness review)

```cpp
int CvPlayer::GetHappinessFromLuxury(ResourceTypes eResource, bool bIncludeImport) const
{
    if (GC.getGame().GetGameLeagues()->IsLuxuryHappinessBanned(eResource))
        return 0;
    
    if (getNumResourceAvailable(eResource, bIncludeImport) > 0)
        return pkResourceInfo->getHappiness();  // +4 typically
    
    // Netherlands UA: 50% retention if exported
    if (GetLuxuryHappinessRetention() > 0 && getResourceExport(eResource) > 0)
        return (pkResourceInfo->getHappiness() * GetLuxuryHappinessRetention()) / 100;
    
    return 0;
}
```

**WLTKD (We Love the King Day) Mechanics:**
- Luxury triggers WLTKD in all cities: +25% yields for 20 turns (base)
- Luxury monopoly can extend WLTKD
- World Congress can ban luxury happiness

**World Congress Interactions:**
- `GC.getGame().GetGameLeagues()->IsLuxuryHappinessBanned()` — can zero out happiness
- `BlockTemporaryForPermanentTrade()` — blocks certain resource types in trades

---

### Issues Identified

#### 3.1 **Duplicate Luxury Detection Poor** ⚠️ **ISSUE**

**Problem:** Owning multiple copies of same luxury wastes potential:

1. **Invisible Waste**: Player doesn't see that 3 Fur = only 1 Fur (1 goes unused)
2. **Trade Opportunity Lost**: Could trade duplicate for value
3. **AI Exploit**: Human can force bad deals by selling duplicate luxuries at markup
4. **No Warning**: UI doesn't flag duplicates for player

**Current Code:**
```cpp
// GetNumResourceAvailable just counts total; doesn't check if it's duplicate
int iCount = getNumResourceTotal(eResource, bIncludeImport);
// Happiness calc:
if (iCount > 0)
    return 4;  // Same return whether iCount=1 or iCount=5
```

**Example:**
- Player has Fur (3 copies from different sources)
- Gets 4 happiness (correct)
- But has 2 Fur "wasted" that could be traded for 6 gold/turn each = 12 gold lost

---

#### 3.2 **WLTKD Yield Boost Poorly Explained** ⚠️ **ISSUE**

**Problem:** WLTKD bonus (default +25% yields) is not well communicated:

1. **No Timer**: Player doesn't see "WLTKD expires in 15 turns"
2. **Stacking Unclear**: If WLTKD refreshes, does it reset timer or extend?
3. **No Forecasting**: Can't predict yield impact before acquiring luxury
4. **UI Gaps**: Trade screen doesn't estimate WLTKD value gained

**Example:**
- Acquire Fur luxury → +25% yields for 20 turns
- After 10 turns, acquire another Fur → Does timer reset to 20 or extend to 30?
- Player doesn't know the rule

---

#### 3.3 **Monopoly Bonuses Poorly Signaled** ⚠️ **ISSUE**

**Problem:** Luxury monopoly grants multiple bonuses but visibility is scattered:

1. **No Single Tooltip**: Doesn't show all monopoly bonuses in one place
2. **Trade Impact Hidden**: Trading away last copy breaks monopoly but impact not clear
3. **AI Valuation Broken**: AI doesn't value monopolies correctly in trade
4. **Extension Mechanic Obscure**: How does monopoly extend WLTKD?

**Monopoly Bonuses:**
```cpp
getMonopolyHappiness()     // Extra happiness if monopoly
getMonopolyGALength()      // Golden Age duration boost
getMonopolyAttackBonus()   // Military unit attack bonus
getMonopolyDefenseBonus()  // Military unit defense bonus
getMonopolyMovementBonus() // Military unit movement bonus
```

**Example:**
- Player has Fur monopoly: +4 happiness base + X monopoly bonus + Y GA length + Z attack bonus
- Trades away Fur to Egypt for gold
- Loses ALL bonuses; not clearly communicated

---

#### 3.4 **World Congress Luxury Ban Breaks Deals** ⚠️ **ISSUE**

**Problem:** World Congress can ban luxury happiness, invalidating existing trade deals:

1. **No Retroactivity Check**: Deal signed when Fur = 4 happiness, then banned
2. **Deal Value Evaporates**: Player still obligated but receives 0 value
3. **No Notification**: Isn't clear in deal screen that it could be banned
4. **AI Confusion**: AI doesn't anticipate bans when valuing deals

**Example:**
- Player negotiates: "Trade Fur for 10 gold/turn for 30 turns"
- Turn 15: World Congress bans Fur happiness
- Deal now worth 0 to player but 10 gold paid each turn
- Player can't cancel

---

### Improvements Proposed

#### Improvement 3.1a: **Duplicate Luxury Detection & Auto-Trade Suggestion**

**Proposal:** 
1. Scan for duplicate luxuries and calculate "excess"
2. Show notification: "You own 3 Fur but gain happiness from only 1. Excess: 2"
3. Suggest trading excess to willing partners
4. Calculate trade value for excess (e.g., "Worth ~6 gold/turn")

**Implementation:**
- In [CvPlayer.cpp](CvGameCoreDLL_Expansion2/CvPlayer.cpp), add `GetExcessLuxuries()` function
- Call from [Notifications.cpp](CvGameCoreDLL_Expansion2/CvNotifications.cpp) to queue suggestion

---

#### Improvement 3.1b: **Prevent Duplicate Happiness Stacking in UI**

**Proposal:** In resource inventory screen, mark duplicates:

```
Fur (3 available)
├─ [✓ Primary] +4 happiness (counts toward empire)
├─ [✗ Duplicate] +0 happiness (excess, can trade)
└─ [✗ Duplicate] +0 happiness (excess, can trade)
```

**Implementation:** [TopPanel.lua](../../../(2) Vox Populi/LUA/TopPanel.lua) or dedicated Luxury screen.

---

#### Improvement 3.2a: **WLTKD Duration & Refresh Transparency**

**Proposal:**
1. Show WLTKD timer in each city: "WLTKD: 12 turns remaining"
2. Define refresh rule in XML: `WLTKD_RESETS_TURNS` vs. `WLTKD_EXTENDS_TURNS`
3. Tooltip when acquiring luxury: "Current WLTKD will [reset/extend] to 20 turns"
4. Notify on expiry: "[Luxury Name] WLTKD expired. Yields back to normal."

**Implementation Point:** [CvCity.h](CvGameCoreDLL_Expansion2/CvCity.h) — add `m_iWLTKDTurnsRemaining` field.

---

#### Improvement 3.2b: **Yield Impact Calculator for Luxury Acquisition**

**Proposal:** When offered a luxury in trade, show estimated yield gain:

```
[RECEIVE: Fur]
Happiness Boost: +4
WLTKD Bonus: +25% yields for 20 turns
Estimated Yield Impact:
├─ Food: +3 per turn
├─ Production: +2 per turn
└─ Science: +1 per turn
Total Value: ~12 gold/turn (100 gold over WLTKD)
```

**Implementation:** [CvDealAI.cpp](CvGameCoreDLL_Expansion2/CvDealAI.cpp#L1512) (`GetLuxuryResourceValue`) — calculate and expose yield gain.

---

#### Improvement 3.3a: **Consolidated Monopoly Bonus Display**

**Proposal:** Create a "Luxury Monopolies" info panel:

```
LUXURY MONOPOLIES OVERVIEW
────────────────────────────
Fur ✓ MONOPOLY ACTIVE
├─ Happiness: +4 (base) +2 (monopoly) = 6
├─ Golden Age: +5 turns (monopoly bonus)
├─ Military: +10% attack, +5% defense
└─ WLTKD: +25% yields, 20 turns

Spices ✗ Not monopolized (Rome has 1)
├─ Happiness: +4 (base only)
├─ WLTKD: +25% yields, 20 turns
└─ Note: Rome currently has spices monopoly
```

**Implementation:** New Lua panel, calls `CvPlayer::GetMonopoly*()` methods.

---

#### Improvement 3.3b: **Trade Monopoly Impact Warning**

**Proposal:** Before trading away last copy of monopolized resource, show warning:

```
[TRADE AWAY: Fur] [LAST COPY]

⚠ WARNING: This is your last Fur!
If you trade it away:
  • Lose +4 happiness
  • Lose monopoly: −2 happiness, −5 GA turns
  • WLTKD expires in all cities
  • Military units lose +10% attack

Are you sure?
  [Yes, Trade] [No, Cancel]
```

**Implementation:** [TradeLogic.lua](../../../(2) Vox Populi/LUA/TradeLogic.lua) — detect trade of last copy and show confirmation.

---

#### Improvement 3.4a: **World Congress Luxury Ban Safeguard**

**Proposal:** 
1. In deal negotiation, warn: "This resource may be banned by World Congress"
2. Reduce deal value if resource is "at risk" of banning
3. After ban, allow automatic deal renegotiation at fair value

**Implementation:**
- [CvDealAI.cpp](CvGameCoreDLL_Expansion2/CvDealAI.cpp) — factor in league ban risk when valuing luxury
- [CvGameLeagues.cpp](CvGameCoreDLL_Expansion2/CvGameLeagues.cpp) — trigger deal renegotiation after ban

---

## 4. RESOURCE YIELDS & IMPROVEMENT MECHANICS

### Current Implementation

**Improvement-Resource Yield Bonuses:**

**File:** [CvImprovementClasses.h](CvGameCoreDLL_Expansion2/CvImprovementClasses.h#L14-L35) (`CvImprovementResourceInfo`)

```cpp
class CvImprovementResourceInfo
{
    int getYieldChange(int i) const;  // Per-resource yield array
    bool isResourceMakesValid() const; // Does resource make improvement valid?
    bool isResourceTrade() const;      // Does resource enable trade?
    int getDiscoverRand() const;       // Chance to auto-discover resource
};
```

**Building Resource Yield Modifiers:**
- [CvBuildingClasses.cpp](CvGameCoreDLL_Expansion2/CvBuildingClasses.cpp#L392) has `m_ppaiResourceYieldChange` (per-building, per-resource yield modifiers)

**Trait-Based Resource Modifiers:**
- [CvTraitClasses.h](CvGameCoreDLL_Expansion2/CvTraitClasses.h#L197) includes `GetYieldChangeStrategicResources()` and `GetYieldChangeNaturalWonder()`

**City Yield Calculations:**
- [CvCity.h](CvGameCoreDLL_Expansion2/CvCity.h#L1098) tracks `GetBaseYieldRateModifier()`, `GetYieldRate()`, etc.

---

### Issues Identified

#### 4.1 **Resource Yield Bonuses Not Visible in Tooltip** ⚠️ **ISSUE**

**Problem:** When hovering over a tile with a resource, the yield bonus from that resource is not explicitly broken down:

1. **Hidden Calculation**: Player sees "+3 Food" on Forest but doesn't know +1 is from Fish (resource)
2. **Improvement Confusion**: Doesn't show which resource contributes how much
3. **Build Planning Unclear**: Can't predict yield impact before improving a resource tile
4. **Mod Debugging Hard**: Hard to trace resource bonuses in a modded game

**Current UI:**
- [PlotHelpManager.lua](../../../UI_bc1/PlotHelp/PlotHelpManager.lua#L618) shows resource icon but not yield breakdown

**Example:**
```
Forest: +2 Food, +1 Production
(player doesn't see: +1 food from Fish resource)
```

---

#### 4.2 **Building Resource Yield Not Well Connected to Tooltip** ⚠️ **ISSUE**

**Problem:** Buildings that modify resource yields are not signaled:

1. **Building Tooltip Gap**: Doesn't say "This building adds +1 Food from Fish"
2. **No Auto-Highlighting**: Can't see which resources benefit from a building
3. **Trade-Off Hidden**: Player doesn't know trading away a resource hurts a specific building's output
4. **Modding Unclear**: Hard to understand resource-building interactions

**Current State:**
- Building tooltips show resource consumption (Granary needs Fish to make valid?) but not bonuses

---

#### 4.3 **Tech Enhancement of Resource Yields Undocumented** ⚠️ **ISSUE**

**Problem:** Technologies can enhance yields from specific resources but it's not clear:

1. **No Tooltip**: Tech doesn't advertise "adds +1 Food from Fish tiles"
2. **Late-Game Surprise**: Player discovers mid-game that Fish became more valuable
3. **Strategic Planning Broken**: Can't plan future city placement based on future tech
4. **Civ Bonus Interaction**: Civilizations with resource-based bonuses hard to evaluate

**Example:**
- Sailing tech could enhance Whale resources with +1 production
- Player doesn't plan to build cities on Whales until discovering the bonus
- Missed opportunity to optimize placements

---

#### 4.4 **Bonus Resources Underutilized** ⚠️ **ISSUE**

**Problem:** Bonus resources (non-strategic, non-luxury) get minimal attention:

1. **No Trade/Currency**: Can't use bonus resources in diplomacy
2. **Yield-Only Value**: Provides yield but nothing else
3. **Late Game Ignored**: Once city is built on bonus, player forgets it exists
4. **No Scarcity Mechanic**: Unlimited bonus resources (unlike strategic/luxury)

**Current State:**
- [GameInfo.Resources](../../../(2) Vox Populi/Core Files) shows bonus resources (Bison, Copper, etc.)
- Provides yields only; no strategic value

---

### Improvements Proposed

#### Improvement 4.1a: **Resource Yield Breakdown in Tile Tooltip**

**Proposal:** In plot help tooltip, show resource contribution:

```
FOREST YIELDS: +2 Food, +1 Production
├─ Terrain (Forest): +2 Food
├─ Resource (Fish): +1 Food  ← NEW
├─ River: (none)
└─ Improvement Effects: (none applied)

Note: After Sailing improvement, Fish yields +1 Production
```

**Implementation:**
- [PlotHelpManager.lua](../../../UI_bc1/PlotHelp/PlotHelpManager.lua#L618-L650) — iterate through `CvImprovementResourceInfo::getYieldChange()` and display
- Show improvement resource modifiers

---

#### Improvement 4.1b: **Improvement Yield Forecast Before Building**

**Proposal:** When hovering over a tile to improve, show the *after* yields:

```
[Build Farm]
Current Yields: +2 Food, +1 Production
After Farm: +3 Food, +0 Production
Change: +1 Food, −1 Production
Note: Fish will grant +1 Food (Sailing tech not discovered yet)
```

**Implementation:** Builder UI calls improvement yield calculation functions.

---

#### Improvement 4.2a: **Building Resource Bonus Advertising**

**Proposal:** Building tooltips highlight resource yields:

```
GRANARY
─────────────────────
+15% Food Storage
+2 Food from Fish
+1 Food from Wheat

Resources Enhance: Fish, Wheat
```

**Implementation:** 
- [CvBuildingInfo.xml](../../../(2) Vox Populi/Core Files/Text/) — add localization keys for resource bonuses
- Building tooltip builder loops through resource yield modifiers

---

#### Improvement 4.2b: **Auto-Highlight Resource-Benefiting Buildings**

**Proposal:** When player is on a tile with a resource, highlight all buildings that improve that resource:

```
You're on a Fish resource.
These buildings boost Fish yields:
  ├─ Granary (+2 Food)
  ├─ Harbor (+1 Gold)
  └─ Aqueduct (no bonus)

Build one of these to maximize this tile's output.
```

**Implementation:** 
- Resource tooltip links to buildings
- City production UI highlights resource-boosting buildings

---

#### Improvement 4.3a: **Tech Yield Enhancement Notifications**

**Proposal:** 
1. When tech is discovered that enhances resources, show notification: "Sailing discovered! Whale resources now +1 Production."
2. In tech tree view, preview resource enhancements: "Sailing enhances: Whale (+1 Prod), Pearl (+1 Gold)"
3. In advisor, suggest founding cities on enhanced resources

**Implementation:**
- [CvTechClasses.cpp](CvGameCoreDLL_Expansion2/CvTechClasses.cpp) — on discovery, iterate resources and check for enhancements
- [CvAdvisorCounsel.cpp](CvGameCoreDLL_Expansion2/CvAdvisorCounsel.cpp) — add resource enhancement advisories

---

#### Improvement 4.3b: **Strategic Planning Tool: Future Resource Values**

**Proposal:** Add a "Future Yields" mode in plot tooltip showing resource value with future techs:

```
WHALE RESOURCE
Current Value (with Sailing): +1 Food
Future Value (with Astronomy): +1 Food, +2 Gold
Future Value (with Refrigeration): +2 Food, +1 Production
```

**Implementation:** 
- Player can toggle "Show Future Tech Effects"
- Display calculated yields assuming techs are discovered in natural order

---

#### Improvement 4.4a: **Bonus Resource Strategic Use**

**Proposal:** Add small strategic bonuses to bonus resources (but keep them unique from strategic/luxury):

1. **Unique Yields**: Some bonus resources grant unique yields (e.g., Horse +50% mounted unit production)
2. **Empire-Wide Bonuses**: Owning X bonus resources of same type grants small bonus (non-additive, like luxury)
3. **Building Validation**: Some buildings require bonus resources to function

**Example:**
- Horse: Current +Food, Future: +50% Mounted Unit Experience
- Copper: Current +Prod, Future: +5% Wonder Production if owned
- Bison: Current +Food, Future: grants happiness in tundra cities (small, non-luxury)

**Implementation:** Extend `CvResourceInfo` with new bonus flags; code buildings to check them.

---

## 5. CROSS-SYSTEM ISSUES

### Issue 5.1: **Resource Trade Value Calculation Broken** ⚠️ **MAJOR ISSUE**

**Problem:** [CvDealAI.cpp](CvGameCoreDLL_Expansion2/CvDealAI.cpp#L1512) `GetLuxuryResourceValue()` doesn't account for:

1. **Existing WLTKD Status**: If player already has WLTKD from another source, luxury value is zero
2. **Monopoly Loss**: Doesn't account for giving up monopoly bonus
3. **Yield Enhancement Techs**: Doesn't know resource yield will improve with tech
4. **Multiple Units Using Resource**: Over-values strategic resources if only 1 unit needs it

**Current Code:**
```cpp
int CvDealAI::GetLuxuryResourceValue(ResourceTypes eResource, ...)
{
    // Calculates base happiness value (typically 4)
    // But doesn't account for:
    // - Is WLTKD already active?
    // - Will monopoly break?
    // - Future tech enhancements?
    // - AI current needs?
}
```

**Impact:** AI offers unfair trades for resources, player gets ripped off.

---

#### Improvement 5.1a: **Enhanced Resource Valuation**

**Proposal:** Enhance `GetResourceValue()` to account for:

```cpp
int CvDealAI::GetResourceValue(ResourceTypes eRes, PlayerTypes eOther, bool bFromMe)
{
    int iValue = GetBaseResourceValue(eRes);
    
    // Adjustment 1: WLTKD Status
    if (pCivilization->IsCurrentlyInWLTKD())
        iValue *= 0.5;  // Less valuable if already active
    
    // Adjustment 2: Monopoly Loss
    if (pCivilization->HasMonopoly(eRes))
        iValue += GetMonopolyBonus(eRes);
    
    // Adjustment 3: Future Tech Enhancements
    int iFutureValue = GetResourceValueWithFutureTechs(eRes);
    if (iFutureValue > iValue)
        iValue += (iFutureValue - iValue) * 0.3;  // 30% of future gain
    
    // Adjustment 4: Strategic Scarcity
    if (IsStrategicResource(eRes))
        iValue *= GetStrategicScarcityMultiplier();
    
    return iValue;
}
```

---

## 6. SUMMARY OF ISSUES & IMPROVEMENTS

| Category | Issue | Type | Priority | Solution |
|----------|-------|------|----------|----------|
| **Reveal** | Asymmetric reveal vs. trade tech | Gameplay | High | Unify gates or improve UI signaling |
| **Reveal** | Policy reveal not integrated | Visibility | Medium | Document & tooltip policies |
| **Reveal** | Obsolete tech not tracked | UX | Low | Add visual indicator (grey out) |
| **Consumption** | No visible consumption tracking | UX | High | Add Resources overview panel |
| **Consumption** | Combat penalty not explained | UX | High | Better tooltips in unit strength calc |
| **Consumption** | Duplicate trade exports possible | Gameplay | Medium | Add validation before deal acceptance |
| **Consumption** | No resource forecast before production | UX | High | Show impact in build queue |
| **Luxury** | Duplicate luxury detection poor | UX | Medium | Mark duplicates, suggest trading |
| **Luxury** | WLTKD poorly explained | UX | High | Show timer, refresh rule, yield impact |
| **Luxury** | Monopoly bonuses poorly signaled | UX | Medium | Create monopoly info panel |
| **Luxury** | World Congress ban breaks deals | Gameplay | Medium | Reduce deal value if "at risk" |
| **Yields** | Resource yield bonuses not visible | UX | High | Show breakdown in tile tooltip |
| **Yields** | Building resource yield not connected | UX | Medium | Add to building tooltip |
| **Yields** | Tech resource enhancements undocumented | UX | High | Show in tech tree, notify on discovery |
| **Yields** | Bonus resources underutilized | Gameplay | Low | Add small strategic bonuses |
| **Trade** | Resource trade value calc broken | Gameplay | Critical | Enhanced valuation with context |

---

## 7. RECOMMENDED IMPLEMENTATION ORDER

1. **Phase 1 (High Impact, Lower Effort):**
   - Improvement 2.1a: Resource usage panel
   - Improvement 3.2a: WLTKD timer
   - Improvement 4.1a: Resource yield breakdown in tooltip

2. **Phase 2 (Critical Gameplay):**
   - Improvement 5.1a: Enhanced resource valuation
   - Improvement 2.4a: Unit production forecast
   - Improvement 3.3b: Trade monopoly warning

3. **Phase 3 (Polish & Completeness):**
   - Improvement 3.1a: Duplicate luxury detection
   - Improvement 4.3a: Tech enhancement notifications
   - Improvement 1.1a: Unified reveal+trade gate

---

## 8. FILES TO MODIFY

**Core C++:**
- [CvDealAI.cpp](CvGameCoreDLL_Expansion2/CvDealAI.cpp) — resource valuation, trading logic
- [CvDealClasses.cpp](CvGameCoreDLL_Expansion2/CvDealClasses.cpp) — deal validation
- [CvPlayer.cpp](CvGameCoreDLL_Expansion2/CvPlayer.cpp) — resource tracking, consumption
- [CvCity.cpp](CvGameCoreDLL_Expansion2/CvCity.cpp) — city-level resource yields
- [CvNotifications.cpp](CvGameCoreDLL_Expansion2/CvNotifications.cpp) — resource alerts

**Lua UI:**
- [TradeLogic.lua](../../../(2) Vox Populi/LUA/TradeLogic.lua) — trade screen enhancements
- [TopPanel.lua](../../../(2) Vox Populi/LUA/TopPanel.lua) — resource display
- [PlotHelpManager.lua](../../../UI_bc1/PlotHelp/PlotHelpManager.lua) — tooltip breakdown
- [EnemyUnitPanel.lua](../../../(1) Community Patch/Core Files/Overrides/EnemyUnitPanel.lua) — combat penalty display

**Data:**
- [Resources.xml](../../../(2) Vox Populi/Core Files/Text/) — resource definitions
- [Building_ResourceYieldChanges.sql](../../../(2) Vox Populi/Core Files/) — building bonuses

---

## References

- [Happiness Review](../happiness/HAPPINESS_REVIEW.md) — Luxury distribution & happiness sources
- [Tech/Science Review](../tech/TECH_SCIENCE_REVIEW.md) — Tech reveal mechanics
- [Diplomacy Review](../diplomacy-review.md) — Trade negotiation & strategic values
- [CvResourceInfo](CvGameCoreDLL_Expansion2/CvInfos.h) — Resource data structure
- [CvDealAI](CvGameCoreDLL_Expansion2/CvDealAI.cpp) — Deal valuation logic

