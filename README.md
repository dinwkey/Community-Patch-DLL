# Community-Patch-DLL

This is the repository for the Civ V SDK + Vox Populi Mod. 

## What is Vox Populi

Started in 2014, Vox Populi (formerly known as the "Community Balance Patch/Overhaul") is a collaborative effort to improve Civilization V's AI and gameplay. It consists of a collection of mods (see below) that are designed to work together seamlessly.

* The Community Patch (CP) is the base mod
	* Contains the gamecore DLL, which is based on C++ code linked against the official Civ V SDK
    * Contains bugfixes (also for multiplayer), performance improvements and many AI enhancements, but minimal gameplay changes
    * Can be used standalone and is the basis for many other mods
* Vox Populi
	* Expands and changes the core mechanics of the game, offering an entirely new Civilization V experience that feels and plays like an evolution of the series
	* Includes City-State Diplomacy by Gazebo, Civ 4 Diplomacy Features by Putmalk and More Luxuries by Barathor
* EUI (optional)
	* Enhanced User Interface

## Where can I learn more

Check out the [forum](https://forums.civfanatics.com/forums/community-patch-project.497/). 

## How can I play this

* You need the latest version of Civilization V (1.0.3.279) with all expansions and DLC.
* [This thread](https://forums.civfanatics.com/threads/community-patch-how-to-install.528034/) on CivFanatics contains a link to the latest release, along with installation instructions.
* You may also download the automatic installer for your desired version from the [Releases page](https://github.com/LoneGazebo/Community-Patch-DLL/releases).

---

## Fork Enhancements (feature/copilot branch)

This fork contains **178 commits** on top of upstream/master with extensive AI improvements, bug fixes, and performance optimizations. Below is a summary of the major changes:

### Tactical AI Improvements

**Combined Arms Coordination:**
- Air + ground coordination for softening targets before ground assault
- Naval + land amphibious coordination for beach landings
- Ranged-before-melee coordination when attacking cities
- Artillery/battleship indirect fire spotter coordination
- Coastal bombardment coordination for naval ranged units
- SEAD (Suppression of Enemy Air Defenses) - ground/naval forces prioritize enemy AA before air strikes

**Unit-Specific Tactics:**
- Recon units (scouts, explorers) now participate in tactical combat with specialized scoring
- Cavalry and mounted units: terrain awareness, flanking bonus awareness, hit-and-run tactics
- Fast ranged units (skirmishers): improved positioning and target prioritization
- Slow infantry: tactical improvements accounting for movement limitations
- Paratroopers: smarter deployment and usage
- Units with withdrawal promotions avoid being used as blockers/screens
- Helicopter gunships: tank-hunting priority, distinct tactical handling
- Tanks/modern armor: distinct tactics vs cavalry, city attack modifiers

**Terrain & Promotion Awareness:**
- Terrain promotion awareness (Woodsman, etc.) in tactical decisions
- Counter-enemy terrain bonus awareness (vs Iroquois forest bonus, etc.)
- Shoshone combat bonus (encampment) awareness
- Mountain-capable unit positioning (Inca, Recon, Helicopter)
- Great General aura awareness and coordination

**Survivability:**
- Wounded units seek medic support
- Fix suicidal embarkation retreat - water treated as last resort
- Improved ranged unit positioning to avoid suicidal repositions
- Submarine tactical survival improvements

### Naval AI Improvements

**Fleet Coordination:**
- Naval fleet concentration bonuses for focus fire
- Destroyer sub-hunting coordination for ASW warfare
- Comprehensive submarine first-strike coordination
- Naval ranged-melee coordination for combined arms naval warfare
- Counter-blockade prefers melee naval units (can capture)

**Carrier Operations:**
- Improved carrier positioning based on actual loaded air unit range
- Carrier coordination with air operations

**City Defense:**
- Proactive naval defense buildup for coastal cities
- Coastal city naval patrol positioning
- Island city detection for naval-only siege bonus
- Naval melee capture bonus for coastal cities
- Garrison handles coastal/naval threats
- City targeting prioritizes embarked enemy units

### Missile AI (New System)

- Missiles coordinate with ground/naval attacks
- Missiles prioritize enemy AA units to clear path for bombers
- Emergency missile production overrides when needed
- Missile vs air unit slot competition awareness
- Cost-benefit analysis for one-time use missiles
- Missiles prefer dedicated platforms (subs, cruisers) over carriers
- Improved missile rebasing and carrier loading strategy

### City Attack AI

- Dynamic siege threshold based on city strength and defender count
- Coordinated blockade targeting for coastal cities
- Better melee capture timing (wait for city HP to be low enough)
- Siege unit positioning bonuses
- Defensive unit clearing priority before assault
- Smart garrison sortie timing and unit selection

### Diplomacy AI

- Hysteresis added to opinion thresholds to prevent approach oscillation
- Don't send aggressive military warnings when unable to defend
- Smarter opportunistic strikes with provocation avoidance
- Fix AI diplomacy showing tooltip text instead of denounce dialogue
- Morocco UA: AI economic decision system for trade plundering with diplomatic checks
- Stop Plundering diplomacy option for Morocco UA
- Open borders deal equalization improvements
- Pending deals no longer block simultaneous turns

### Trade & Economy

- AI trade route selection aligned with grand strategy
- Deal renewal refactoring and cache optimization
- Trade reroute indexing fixes

### Performance Optimizations

- Plot cache optimization for hot AI pathfinding
- Cache GET_PLAYER lookups in hot AI loops
- Cache enemy attackers in `CheckForEnemiesNearArmy`
- Avoid full danger plot recalculation in attacker checks
- `GetZoneByCityNoRefresh` to avoid redundant tactical map refreshes
- Optimize `verifyUnitValidPlot` to iterate player units only
- Optimize deal evaluation order in trade offers
- A* pathfinding stacking check caching
- Tenet caching system for policy AI
- Religion system optimizations
- Tech system overflow capping and version tracking
- Culture performance optimizations

### Bug Fixes (upstream issues)

- Fix stale swappable great work indices after ownership change (city capture, espionage theft)
- Fix division by zero in `GetDirectiveWeight` when `iBuildTime` overflows
- Fix defensive pact war risk strength calculation
- Fix NULL CvString init in unit naming
- Fix growth/citizen updates, minor-civ pool, and startup slots
- Prevent zero or negative plot purchase costs
- Clamp forced default specialists on overpopulation
- Initialize `numResourceLocal` in TopPanel tooltip
- Fix AI diplomacy showing tooltip text instead of denounce dialogue

### Builder AI

- Strategic railroad value improvements with location-based weighting
- Builder task prioritization improvements
- Core improvements to ROI calculations

### UI Improvements

- TopPanel: show event-derived resources in resource tooltip
- ImprovedTopPanel: parity with TopPanel for event resources
- Lua API: Expose `GetNumResourceFromEvents` to scripts

### Code Quality

- Enable `-Wunused-variable` and `-Wunused-parameter` in clang builds
- Remove unused const variables and deprecated Lua bindings
- Destructor cleanup (1D array cleanup in belief/trait destructors)
- Remove unused culture theming cache
- NO_MINORCIV safety checks throughout codebase

---

## Development and debugging

See `DEVELOPMENT.md` file for more information.

Additional documentation for AI systems and architecture can be found in the `docs/` folder.