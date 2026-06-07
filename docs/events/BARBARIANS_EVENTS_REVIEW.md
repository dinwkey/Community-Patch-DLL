# Barbarian Spawning & Random Events System Review
## Vox Populi (Community Patch DLL)

**Review Date:** 2024  
**Scope:** Barbarian camp/city spawning, unit generation, random game events, and goody huts  
**Key Files:**
- `CvBarbarians.cpp` / `CvBarbarians.h` - Barbarian spawning and management
- `CvPlayer.cpp` - Player-level and city-level events, goody huts  
- `CvCity.cpp` - City events system
- `CustomMods.h` - Game event hooks

---

## 1. BARBARIAN SPAWNING SYSTEM

### 1.1 Architecture Overview

The barbarian system is completely static, driven by hardcoded turns and probability rolls. Camps spawn deterministically across the map, then periodically spawn units. Two distinct camp types exist:

- **Encampments** (normal barbarian camps): Found across most terrain
- **City Captures** (barbarian cities): Spawn when barbarians capture player cities

All spawning is controlled via global defines in `CvGlobals.cpp`.

### 1.2 Camp Initialization Phase

**When:** Turn 2 (VP) or Turn 0 (CP), determined by `BARBARIAN_INITIAL_SPAWN_TURN` (default: **2**)

**Initial Spawn Percentage:**
```
In VP:
  Initial camps = (max_camps * BARBARIAN_CAMP_FIRST_TURN_PERCENT_OF_TARGET_TO_ADD / 100)
  Initial camps += (current_era * BARBARIAN_CAMP_FIRST_TURN_PERCENT_PER_ERA / 100)
  Defaults: 10% of max camps, +5% per era
  
In CP:
  Initial camps = 33% of max camps
  No era scaling
```

**Maximum Camp Cap:**
```
max_camps = (map_plot_count / fog_of_war_tiles_per_barb_camp)
Default: map_plot_count / 512 ≈ ~78 camps on Standard map (9500 plots)
```

**Camp Placement Rules:**
- Randomly biased towards coastal plots (33% chance to prioritize coast)
- Must be `BARBARIAN_CAMP_MINIMUM_ISLAND_SIZE` (2) minimum tiles from land
- Must be ≥`BARBARIAN_CAMP_MINIMUM_DISTANCE_CAPITAL` (4) tiles from any player capital
- Must be ≥`BARBARIAN_CAMP_MINIMUM_DISTANCE_ANOTHER_CAMP` (4) tiles from other camps
- Prioritizes valid plot selection (non-water, non-mountain, non-ice by default)

### 1.3 Camp Spawning Rates (Post-Turn-2)

**VP Mode - Periodic Spawn Check:**
```cpp
iGameTurn = (current_turn - BARBARIAN_INITIAL_SPAWN_TURN)
iSpawnRate = BARBARIAN_CAMP_SPAWN_RATE (default: 2)

// Game options modify spawn rate
iSpawnRate += CHILL_BARBARIANS ? BARBARIAN_CAMP_SPAWN_RATE_CHILL (1) : 0
iSpawnRate -= RAGING_BARBARIANS ? abs(BARBARIAN_CAMP_SPAWN_RATE_RAGING (-1)) : 0

// Roll probability every (iSpawnRate) turns
if (iGameTurn % iSpawnRate == 0)
  // Attempt to spawn 1 new camp (subject to max camp cap and distance checks)
```

**Spawn Probability:**
```
Every (iSpawnRate) turns:
  iRandom = GC.getGame().randRangeInclusive(1, 100, seed)
  if (iRandom <= BARBARIAN_CAMP_ODDS_OF_NEW_CAMP_SPAWNING (50))
    Attempt to add 1 new camp (or more based on game options)
```

**Chill/Raging Options:**
- `CHILL_BARBARIANS`: Doubles spawn rates (spawn every 3 turns instead of 2)
- `RAGING_BARBARIANS`: Halves spawn rates (spawn every 1 turn); adds 1 extra camp per spawn event

### 1.4 Barbarian Spawner (Camp/City) Unit Emission

**Mechanism:**
Each camp/city has a spawn counter that decrements every turn. When the counter reaches 0, units spawn and counter is reset.

**Counter Initialization (via `ActivateBarbSpawner`):**
```cpp
// Base delay from camp type
if (pPlot->isCity())
  iNumTurnsToSpawn = BARBARIAN_SPAWN_DELAY_FROM_CITY (6)
                   + randRangeInclusive(0, BARBARIAN_SPAWN_DELAY_FROM_CITY_RAND (4))
else
  iNumTurnsToSpawn = BARBARIAN_SPAWN_DELAY_FROM_ENCAMPMENT (12)
                   + randRangeInclusive(0, BARBARIAN_SPAWN_DELAY_FROM_ENCAMPMENT_RAND (9))

// Game difficulty scaling
iNumTurnsToSpawn += handicap.getBarbarianSpawnDelay()

// Game speed scaling (67-400% based on game speed)
iNumTurnsToSpawn *= game_speed_info.getBarbarianPercent() / 100

// Game options scaling
if (CHILL_BARBARIANS)
  iNumTurnsToSpawn *= (CITY ? 150 : 200) / 100  // +50% to +100% delay
if (RAGING_BARBARIANS)
  iNumTurnsToSpawn *= (CITY ? 50 : 50) / 100    // -50% delay

// Ensure minimum delay
iNumTurnsToSpawn = max(iNumTurnsToSpawn, BARBARIAN_MIN_SPAWN_DELAY (2))

// Cities spawn units more often: deduct turns per unit spawned
if (pPlot->isCity())
  iNumTurnsToSpawn += (-1 * min(3, GetBarbSpawnerNumUnitsSpawned) * BARBARIAN_CITY_SPAWN_DELAY_REDUCTION_PER_SPAWN (-1))
else
  iNumTurnsToSpawn += (-1 * min(3, GetBarbSpawnerNumUnitsSpawned) * BARBARIAN_ENCAMPMENT_SPAWN_DELAY_REDUCTION_PER_SPAWN (-1))
```

**Summary of Default Spawn Intervals:**
```
Encampments:  12-21 base turns (+ bonuses/penalties)
Cities:       6-10 base turns (+ bonuses/penalties)
Chill Mode:   +50-100% delay (extends intervals)
Raging Mode:  -50% delay (halves intervals)
Difficulty:   +X turns per difficulty level
```

### 1.5 Unit Spawning at Camps/Cities

**When Units Spawn:**
- Encampment or City's spawn counter reaches 0 (counter set via `ActivateBarbSpawner`)
- Called via `DoCamps()` → `SpawnBarbarianUnits(pPlot, iNumUnits, BARB_SPAWN_FROM_ENCAMPMENT/CITY)`

**Number of Units Spawned:**
```cpp
// Default: 1 unit per spawn
iNumBarbs = BARBARIAN_NUM_UNITS_PER_ENCAMPMENT_SPAWN (1)  // encampments
iNumBarbs = BARBARIAN_NUM_UNITS_PER_CITY_SPAWN (1)         // cities

// Game options modify count
iNumBarbs += CHILL_BARBARIANS ? SPAWN_CHILL (-1) : 0
iNumBarbs += RAGING_BARBARIANS ? SPAWN_RAGING (1) : 0

// Era scaling
iNumBarbs += (current_era * BARBARIAN_NUM_UNITS_PER_SPAWN_PER_ERA / 100)
```

**Spawn Location Rules:**
```
iMaxBarbarians = 2 (encampments) or 4 (cities) within nearby range
iMaxBarbarianRange = 4 tiles

// Priority: spawn on ungarrisoned cities/camps first, then adjacent plots
// Search up to 4 rings from origin
// Skip ocean (unless barbarian can traverse it), mountains, impassable terrain

// Ranged unit restrictions:
// - Encampments: NO ranged units EXCEPT on 1-tile islands
// - Cities: ranged units allowed
```

**Barbarian Unit Types:**
- Selected via `GetRandomBarbarianUnitType()` with logic prioritizing:
  1. Melee units over ranged (except on islands)
  2. Units that can use nearby unowned resources
  3. Naval units (only after turn 30: `BARBARIAN_NAVAL_UNIT_START_TURN_SPAWN`)
  4. Unique units if capturing a city's original owner

### 1.6 Camp Clearing & Respawn

**When Camp is Cleared:**
- Player unit defeats barbarian camp (via `DoBarbCampCleared()`)
- Respawn timer set: `BARBARIAN_CAMP_CLEARED_MIN_TURNS_TO_RESPAWN` (15 turns default)
- If enough time passes and map space available, camp may respawn

**Clearance Penalties/Bonuses:**
- No visible penalty to player for clearing camps
- Camps naturally regenerate after 15+ turns if space available

### 1.7 Barbarian vs. Player Dynamics

**Naval Barbarians:**
- Start spawning turn 30 (`BARBARIAN_NAVAL_UNIT_START_TURN_SPAWN`)
- Only from coastal encampments/cities
- Can create problems for coastal expansion

**Barbarian Combat:**
- No special combat bonuses (flagged as equal strength)
- No morale or experience bonuses
- Can steal from trade routes, take over city-states, and wage war

**Barbarian Visibility:**
- Revealed to players via fog of war if in range
- Barbarian camps always visible to adjacent players (scouted)
- Policy `"Policy with AlwaysSeeBarbCamps"` reveals all camps on screen

---

## 2. RANDOM EVENTS SYSTEM

### 2.1 Event Architecture

Two parallel event systems:
1. **Player-Level Events** (`CvPlayer::DoEvents()`) - triggered each turn
2. **City-Level Events** (`CvCity::DoEvents()`) - triggered for each city each turn

Both use probabilistic triggers with cooldown management and prerequisite checks.

### 2.2 Player-Level Events

**Source:** `CvPlayer.cpp` lines 5551-5650  
**Trigger:** Called during player turn processing (`CvPlayer::doTurn()`)

**Event Selection Flow:**
```
1. Check PLAYER event cooldown (global cooldown on ALL events)
   if (cooldown > 0) return early
2. Gather all valid player events (not on cooldown, meets prerequisites)
3. Probability roll for "ANY event happens this turn"
   iChance = GLOBAL_EVENT_PROBABILITY (default: 100, always happens)
4. For each valid event, roll random chance:
   if (rand(1,100) <= event_chance + event_increment)
     Add to candidate pool
5. Weight-select from candidate pool
6. Trigger chosen event, set global cooldown
7. Increment all non-chosen events' chances by their delta
```

**Probability Mechanics:**
```cpp
// Base chance (out of 100)
int iChance = pkEventInfo->getRandomChance() + GetEventIncrement(eEvent)

// Roll for this event
int iRoll = GC.getGame().randRangeInclusive(1, 100, seed)
if (iRoll <= iChance)
  // Event qualifies

// If not chosen and has delta, increase next turn's chance
if (eEvent != eChosenEvent && delta > 0)
  IncrementEvent(eEvent, delta)
```

**Event Cooldowns:**
```
Global cooldown: GLOBAL_EVENT_MIN_DURATION_BETWEEN (25 turns)
Per-event cooldown: pkEventInfo->getCooldown()
```

### 2.3 City-Level Events

**Source:** `CvCity.cpp` lines 3289-3450  
**Trigger:** Called each turn for each city (`CvCity::DoTurn()`)

**Event Selection Flow:**
```
1. Check city event global cooldown
   if (GetCityEventCooldown() > 0) return early
2. For each valid city event (not on cooldown, meets prerequisites):
   a. Check if event is on individual cooldown
   b. Validate all event requirements (resources, population, etc.)
   c. Roll random chance
3. Weight-select from valid events
4. Start chosen event, set global cooldown
5. Increment non-chosen events
```

**Key Differences from Player Events:**
- Per-city cooldown tracking (not player-wide)
- City-specific prerequisites (population, buildings, terrain features)
- Events can trigger "one-shot" (fire only once per city)
- Espionage setup events have different handling

**Probability:**
```
Per turn: CITY_EVENT_PROBABILITY_EACH_TURN (100 = always check)
// Same delta/increment system as player events
```

### 2.4 Event Prerequisites & Validity

**CvModEventInfo Fields (Player Events):**
- `getPrereqTech()` - Required technology
- `getObsoleteTech()` - Event stops triggering after this tech
- `getMinimumNationalPopulation()` - Empire must have X citizens
- `getMinimumNumberCities()` - Empire must control X cities
- `getRequiredCiv()` / `getRequiredEra()` / `getObsoleteEra()`
- `getYieldMinimum(eYield)` - Must produce X of yield per turn
- `getRequiredPolicy()` / `getRequiredIdeology()`
- `getRequiredImprovement()` / `getRequiredReligion()`
- `getRandomChance()` / `getRandomChanceDelta()`
- `getCooldown()` / `IgnoresGlobalCooldown()`

**CvModCityEventInfo Fields (City Events):**
- Similar structure with city-specific checks
- `getMinimumPopulation()` - City must have X pop
- `getEventClass()` - Event categorization
- `isOneShot()` - Can only trigger once per city
- `GetDescription()` - Logging/debug text

### 2.5 Event Triggering & Consequences

**When Event Fires:**
```
1. Event marked as ACTIVE (SetEventActive)
2. If one-shot, marked as FIRED (SetEventFired)
3. Lua hook called: GAMEEVENT_CityEventActivated
4. Event choice processing (if event has multiple outcomes)
5. Consequences applied (yields, unhappiness, building destruction, etc.)
```

**Event Consequences (City Events):**
- Yield changes (gold, science, culture, faith)
- Unhappiness modifiers
- Improvement/building destruction (with probability bias towards coastal/river/mountain features)
- Great Person generation
- Population changes

**Example:** Disaster Event
- May destroy improvements, buildings
- Rolls probability for each improvement based on feature type
- Coastal disasters prioritize coastal improvements
- Mountain disasters prioritize mountain improvements

### 2.6 Event Game Options

**Controllable via Game Setup:**
- `GAMEOPTION_GOOD_EVENTS` - Only positive events
- `GAMEOPTION_NEUTRAL_EVENTS` - Balanced events
- `GAMEOPTION_BAD_EVENTS` - Only negative events
- `GAMEOPTION_TRADE_EVENTS` - Trade-related events
- `GAMEOPTION_CIV_SPECIFIC_EVENTS` - Civ-unique events

If none selected, no events fire.

---

## 3. GOODY HUTS SYSTEM

### 3.1 Goody Hut Mechanics

**Activation:**
- Player unit enters a goody hut plot (flagged `isGoody()`)
- Hut exists as improvement on map (set during map initialization)

**Reward Types (from `CvGoodyInfo`):**
```
- Gold (lump sum + randomized amount)
- Map fog revelation (% of nearby tiles)
- Experience (unit XP)
- Healing (unit HP restoration)
- Population (city pop increase)
- Production (city production boost)
- Golden Age (turns)
- Free tiles (culture expansion)
- Science (tech boost)
- Culture
- Faith
- Food
- Border growth (permanent culture bonus)
- Barbarian deterrent (reveals nearby barbarian camps)
```

**Reward Selection (via `doGooty()`):**
```cpp
// Player can only receive each goody once per game (tracked in CvGoodyHuts)
vector<GoodyTypes> avValidGoodies;
for (each goody type)
  if (canReceiveGoody(pPlot, eGoody, pUnit))
    avValidGoodies.push_back(eGoody)

// For units with "Goody Hut Picker" promotion:
// Show UI popup to choose reward (if human, AI chooses auto)

// Otherwise, random selection from valid goodies
uint uRand = GC.getGame().urandLimitExclusive(avValidGoodies.size(), seed)
eGoody = avValidGoodies[uRand]
receiveGoody(pPlot, eGoody, pUnit)
```

**Goody Hut Placement (Map Generation):**
```lua
-- From MapGenerator.lua
-- Each goody improvement has TilesPerGoody XML property
-- Randomly distributed across non-water plots
-- One goody per TilesPerGoody tiles
-- Default: TilesPerGoody varies by goody type (e.g., 150-250 tiles per hut)
```

**Goody Removal:**
- Hut removed immediately after unit pops it (`pPlot->removeGoody()`)
- No respawn (goodies are one-time only)

### 3.2 Ancient Ruins (Late-Game Goodies)

**Unique Goody Type (BNW+):**
- More valuable than standard goodies
- Reward: `doInstantYield(INSTANT_YIELD_TYPE_ANCIENT_RUIN)` 
- Creates `ARTIFACT_ANCIENT_RUIN` archaeological record
- Special UI for Ancient Ruin discovery

**Requirements:**
- Unit with "Ancient Ruin Archaeologist" promotion can prioritize ruins
- Only recon units and cultural units can claim (if mod-enabled)
- Yields instant science/culture/faith bonuses

---

## 4. ANALYSIS: ISSUES & OBSERVATIONS

### 4.1 Barbarian Spawning Issues

**Issue #1: Completely Predictable Spawn Timing**
- **Problem:** Camps spawn on fixed turn 2, then every (2-3) turns deterministically
- **Impact:** Players can route armies proactively; camps never pose genuine tactical surprise
- **Root Cause:** `BARBARIAN_INITIAL_SPAWN_TURN` and `BARBARIAN_CAMP_SPAWN_RATE` are hardcoded
- **Severity:** MEDIUM - affects gameplay variety but not balance

**Issue #2: Arbitrary Camp Spawn Rate in VP**
- **Problem:** VP spawns camps every 2 turns (or 1 turn in Raging), meaning 20-30 new camps per 50 turns
- **Impact:** Map becomes saturated with barbarians; impossible to suppress militarily
- **Root Cause:** `BARBARIAN_CAMP_SPAWN_RATE = 2` is very aggressive
- **Severity:** MEDIUM - balance issue at high difficulties

**Issue #3: No Naval Barbarian Threat Until Turn 30**
- **Problem:** Naval barbarians don't spawn until turn 30 (`BARBARIAN_NAVAL_UNIT_START_TURN_SPAWN`)
- **Impact:** Naval powers get 30-turn free reign to expand/explore
- **Root Cause:** Hardcoded turn gate
- **Severity:** LOW - balances coastal vs. land gameplay

**Issue #4: Encampments Spawn NO Ranged Units (Except Islands)**
- **Problem:** Land encampments only spawn melee units
- **Impact:** Encampments are weak; ranged barbarians only spawn from captured cities
- **Root Cause:** `bAllowRanged = !bIsBarbCamp` in `SpawnBarbarianUnits()`
- **Severity:** LOW - may be intentional balance

**Issue #5: Camp Respawn After Clearing (15-Turn Delay)**
- **Problem:** Cleared camps respawn after 15 turns if space available
- **Impact:** Can't permanently kill a region of barbarians
- **Root Cause:** `BARBARIAN_CAMP_CLEARED_MIN_TURNS_TO_RESPAWN = 15`
- **Severity:** LOW - adds challenge to anti-barb campaigns

### 4.2 Random Event Issues

**Issue #1: Global Cooldown Blocks ALL Events**
- **Problem:** After 1 event fires, 25-turn cooldown prevents ANY event (player or city)
- **Impact:** Very long periods without events; feels static
- **Root Cause:** `GLOBAL_EVENT_MIN_DURATION_BETWEEN = 25` applies to all
- **Severity:** MEDIUM - event frequency feels too low

**Issue #2: Event Increment System Unbounded**
- **Problem:** If event doesn't fire, `RandomChanceDelta` is added every turn (infinite growth)
- **Impact:** After 20+ turns, guaranteed event fires (99%+ chance)
- **Root Cause:** No cap on `GetEventIncrement()` accumulation
- **Severity:** MEDIUM - unpredictable event clustering after droughts

**Issue #3: City Events Never Fire If Global Cooldown Active**
- **Problem:** City events check global cooldown and skip if active
- **Impact:** City disasters stack (all fire at once after cooldown expires)
- **Root Cause:** `if (GetCityEventCooldown() > 0) return early` in `CvCity::DoEvents()`
- **Severity:** MEDIUM - cascading events feel unfair

**Issue #4: One-Shot Events Never Reset**
- **Problem:** If `isOneShot()` is true, event never fires again (even if requirements become available)
- **Impact:** Can miss unique events by bad luck
- **Root Cause:** `if (pkEventInfo->isOneShot() && IsEventFired(eEvent)) continue`
- **Severity:** LOW - probably intentional, but limits strategic planning

**Issue #5: No UI Feedback for Disabled Events**
- **Problem:** Players don't know WHY event didn't fire (cooldown? prerequisite?)
- **Impact:** Feels random; no sense of control
- **Root Cause:** No tooltip or notification system for event status
- **Severity:** MEDIUM - UX problem

### 4.3 Goody Hut Issues

**Issue #1: One-Time-Only Goodies Not Renewable**
- **Problem:** Once a goody is claimed, player can never get it again
- **Impact:** Early-game luck heavily influences late-game options
- **Root Cause:** `CvGoodyHuts::IsHasPlayerReceivedGoodyLately()` checks last 3 goodies claimed
- **Severity:** LOW - balances early/late game

**Issue #2: Ancient Ruins Archaeology Abuse**
- **Problem:** Archaeologist units can repeatedly dig same ruins, generating endless Great Works
- **Impact:** Tourism economy becomes broken with infinite archaeology chains
- **Root Cause:** Archaeology system doesn't prevent re-diggi of same ruin
- **Severity:** MEDIUM - exploit in endgame

**Issue #3: Goody Hut Rewards Not Difficulty-Scaled**
- **Problem:** Goody rewards are identical on Prince, Emperor, Deity
- **Impact:** Difficulty scaling incomplete; AI doesn't benefit from goodies
- **Root Cause:** No difficulty multiplier in `receiveGoody()`
- **Severity:** LOW - intended behavior (RNG should balance)

---

## 5. RECOMMENDATIONS

### 5.1 Barbarian Improvements

**High Priority:**
1. **Dynamic Spawn Rate:** Tie `BARBARIAN_CAMP_SPAWN_RATE` to current map saturation %
   - If camps < 20% of cap: spawn rate = 1 (faster)
   - If camps > 80% of cap: spawn rate = 4 (slower)
   - Provides natural equilibrium

2. **Difficulty-Based Spawn Scaling:** 
   - Prince/King: current rate
   - Emperor: +1 camp per spawn event
   - Deity: +2 camps per spawn event

3. **Naval Barbarian Earlier:** Reduce `BARBARIAN_NAVAL_UNIT_START_TURN_SPAWN` from 30 to 15
   - Balances naval/land expansion timelines

**Medium Priority:**
4. **Ranged Barbarians from Encampments:** Remove restriction, allow ranged units
   - Encampments become more threatening
   - Balance: ranged units cost more to spawn (longer delay)

5. **Camp Respawn Cooldown Difficulty Scaling:**
   - Prince: 15 turns (current)
   - Emperor: 20 turns
   - Deity: 25 turns

### 5.2 Event System Improvements

**High Priority:**
1. **Separate Global Cooldowns:** 
   - Player event cooldown: 25 turns
   - City event cooldown: 10 turns (independent)
   - Allows city events even if player event recently fired

2. **Bounded Increment System:**
   - Cap `GetEventIncrement()` at 50 (max 50% additional chance)
   - Prevents guaranteed-fire clusters after long droughts
   - Reset increment to 0 when event fires

3. **Event Frequency Tuning:**
   - `GLOBAL_EVENT_MIN_DURATION_BETWEEN`: 25 → 15 turns
   - `CITY_EVENT_MIN_DURATION_BETWEEN`: 25 → 5 turns
   - More frequent events overall

**Medium Priority:**
4. **Event Status UI:**
   - Tooltip on disabled events: "On cooldown (X turns)" or "Missing: Tech X"
   - Notifications when major event becomes available

5. **Difficulty-Based Event Frequency:**
   - Prince: current (15-turn cooldown)
   - Emperor: 12-turn cooldown, +5% bonus event chance
   - Deity: 10-turn cooldown, +10% bonus event chance

### 5.3 Goody Hut Improvements

**Low Priority (mostly working as intended):**
1. **Difficulty-Based Goody Rewards:**
   - Emperor: +25% gold/science rewards
   - Deity: +50% gold/science rewards

2. **Archaeology Prevention:**
   - Mark excavated ruins as "drained" after archaeology
   - Allow re-diggi after 50+ turns

---

## 6. FORMULAS & CALCULATIONS

### 6.1 Barbarian Camp Spawn Probability

```
Every BARBARIAN_CAMP_SPAWN_RATE turns (VP mode):
  if ((game_turn - initial_turn) % spawn_rate == 0)
    iRandom = rand(1, 100)
    if (iRandom <= BARBARIAN_CAMP_ODDS_OF_NEW_CAMP_SPAWNING (50))
      Attempt to add (1 + raging_bonus) camp
      
Success depends on:
  - Available plot count > 0
  - New camp satisfies distance checks (4 tiles from capital, other camps, etc.)
  - Map spot available (current camps < max_camps)
```

### 6.2 Barbarian Unit Spawn Interval

```
Initial delay = base_delay + rand(0, max_rand)
  City base:        6 + rand(0, 4) = 6-10 turns
  Encampment base:  12 + rand(0, 9) = 12-21 turns

Modified by:
  * Difficulty bonus: +X turns per difficulty level
  * Game speed: *(game_speed_percent / 100)
  * Game options:
      - CHILL: *1.5 to 2.0 (slower)
      - RAGING: *0.5 (faster)
  * Unit spawned count: -1 turn per spawn (accelerates repeat spawns)

Final: max(iNumTurnsToSpawn, BARBARIAN_MIN_SPAWN_DELAY (2))
```

### 6.3 Random Event Probability

```
Turn check:
  if (rand(1, 100) <= GLOBAL_EVENT_PROBABILITY (100))
    Continue to event selection

Per-event roll:
  iChance = base_chance + event_increment (unbounded)
  iRoll = rand(1, 100)
  if (iRoll <= iChance)
    Candidate for selection

Selection:
  Weighted random choice from candidates
  Weight = iChance (higher = more likely)

Consequences:
  - Global cooldown: 25 turns
  - Per-event cooldown: defined in event XML
  - Non-selected events: increment += delta (if delta > 0)
```

---

## 7. DATA REFERENCES

### Key Defines (CvGlobals.cpp)

**Barbarian Camp Spawning:**
```cpp
BARBARIAN_INITIAL_SPAWN_TURN = 2              // VP (0 in CP)
BARBARIAN_CAMP_FIRST_TURN_PERCENT_OF_TARGET_TO_ADD = 10  // VP
BARBARIAN_CAMP_FIRST_TURN_PERCENT_PER_ERA = 5            // VP
BARBARIAN_CAMP_SPAWN_RATE = 2                 // VP
BARBARIAN_CAMP_SPAWN_RATE_CHILL = 1
BARBARIAN_CAMP_SPAWN_RATE_RAGING = -1
BARBARIAN_CAMP_ODDS_OF_NEW_CAMP_SPAWNING = 50 (1/2 chance)
BARBARIAN_CAMP_NUM_AFTER_INITIAL = 1
BARBARIAN_CAMP_MINIMUM_DISTANCE_CAPITAL = 4
BARBARIAN_CAMP_MINIMUM_DISTANCE_ANOTHER_CAMP = 4
BARBARIAN_CAMP_MINIMUM_ISLAND_SIZE = 2
BARBARIAN_CAMP_CLEARED_MIN_TURNS_TO_RESPAWN = 15
BARBARIAN_CAMP_COASTAL_SPAWN_ROLL = 33       // % chance to prioritize coast
```

**Barbarian Unit Spawning:**
```cpp
BARBARIAN_SPAWN_DELAY_FROM_ENCAMPMENT = 12
BARBARIAN_SPAWN_DELAY_FROM_ENCAMPMENT_RAND = 9
BARBARIAN_SPAWN_DELAY_FROM_CITY = 6
BARBARIAN_SPAWN_DELAY_FROM_CITY_RAND = 4
BARBARIAN_SPAWN_DELAY_FROM_ENCAMPMENT_CHILL_MULTIPLIER = 200 (+100%)
BARBARIAN_SPAWN_DELAY_FROM_CITY_CHILL_MULTIPLIER = 150 (+50%)
BARBARIAN_SPAWN_DELAY_FROM_ENCAMPMENT_RAGING_MULTIPLIER = 50 (-50%)
BARBARIAN_SPAWN_DELAY_FROM_CITY_RAGING_MULTIPLIER = 50 (-50%)
BARBARIAN_SPAWN_DELAY_FROM_ENCAMPMENT_REDUCTION_PER_SPAWN = -1
BARBARIAN_SPAWN_DELAY_FROM_CITY_REDUCTION_PER_SPAWN = -1
BARBARIAN_MIN_SPAWN_DELAY = 2
BARBARIAN_NUM_UNITS_ENCAMPMENT_CREATION_SPAWN = 2  // VP (1 in CP)
BARBARIAN_NUM_UNITS_PER_ENCAMPMENT_SPAWN = 1
BARBARIAN_NUM_UNITS_PER_CITY_SPAWN = 1
BARBARIAN_NAVAL_UNIT_START_TURN_SPAWN = 30
MAX_BARBARIANS_FROM_CAMP_NEARBY = 2
MAX_BARBARIANS_FROM_CAMP_NEARBY_RANGE = 4
MAX_BARBARIANS_FROM_CITY_NEARBY = 4
MAX_BARBARIANS_FROM_CITY_NEARBY_RANGE = 4
```

**Events:**
```cpp
GLOBAL_EVENT_PROBABILITY = 100           // Always roll for event
GLOBAL_EVENT_MIN_DURATION_BETWEEN = 25   // Cooldown after event fires
CITY_EVENT_PROBABILITY_EACH_TURN = 100   // Always roll for city event
CITY_EVENT_MIN_DURATION_BETWEEN = 25
```

---

## 8. SUMMARY

**Barbarian System Strengths:**
- Simple, deterministic spawning creates predictable challenge progression
- Camp saturation prevents infinite spawn; creates natural equilibrium
- Difficulty scaling (Chill/Raging) allows player customization
- Naval/Land split balances expansion vectors

**Barbarian System Weaknesses:**
- Spawning entirely predictable (no tactical surprise)
- Spawn rate too aggressive in VP (camps never get killed)
- Encampment ranged restriction makes camps weak
- Naval barbarians delayed until mid-game

**Events System Strengths:**
- Flexible event framework allows mod extensibility
- One-shot events create unique historical moments
- Game options let players control event frequency

**Events System Weaknesses:**
- Global cooldown too long (25 turns = ~4+ real-world minutes)
- Cooldown blocks ALL event types simultaneously
- Increment system unbounded (leads to clustering)
- Event prerequisites not clearly communicated to player

**Goody Huts Strengths:**
- One-time-only design prevents snowballing
- Wide variety of rewards maintains gameplay variety
- Archaeology integration adds lategame feature

**Goody Huts Weaknesses:**
- Archaeology can be exploited (infinite Great Works)
- No difficulty scaling for rewards
- No visual distinction between drained/active ruins

