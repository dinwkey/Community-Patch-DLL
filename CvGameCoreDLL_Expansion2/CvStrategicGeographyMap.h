/*	-------------------------------------------------------------------------------------------------------
	Sid Meier's Civilization V — Vox Populi
	Strategic Geography Map: persistent terrain-aware layer for AI defense allocation.
	See docs/military-ai/STRATEGIC_GEOGRAPHY_MAP_PLAN.md for design rationale.
	------------------------------------------------------------------------------------------------------- */
#pragma once

#ifndef CIV5_STRATEGIC_GEOGRAPHY_MAP_H
#define CIV5_STRATEGIC_GEOGRAPHY_MAP_H

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//  Defensive Layer Classification
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

enum eStrategicLayer
{
	STRATEGIC_LAYER_UNKNOWN,
	STRATEGIC_LAYER_FRONT_LINE,   // Within 4 tiles of hostile/neutral border
	STRATEGIC_LAYER_SECOND_LINE,  // 5-8 tiles from hostile border
	STRATEGIC_LAYER_REAR_AREA,    // 9-12 tiles from hostile border
	STRATEGIC_LAYER_CORE,         // >12 tiles from hostile border or deep interior
};

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//  Per-enemy approach data (Phase 5)
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

struct EnemyApproach
{
	EnemyApproach()
		: eEnemy(NO_PLAYER)
		, iDistanceFromEnemy(99)
		, iApproachDifficulty(0)
		, eLikelyApproachDirection(NO_DIRECTION)
	{
	}

	PlayerTypes eEnemy;
	int iDistanceFromEnemy;           // plotDistance from nearest enemy city to our city
	int iApproachDifficulty;          // 0-100: terrain difficulty for enemy to reach us (higher = harder)
	DirectionTypes eLikelyApproachDirection; // Direction FROM which the enemy approaches
};

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//  Per-city strategic analysis (Phase 1-5)
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

struct StrategicCityAnalysis
{
	StrategicCityAnalysis()
		: iCityID(-1)
		, eLayer(STRATEGIC_LAYER_UNKNOWN)
		, iDefensiveDepth(99)
		, bIsCapital(false)
		, bIsFrontLine(false)
		, bIsSecondLine(false)
		, bIsCore(false)
		, iChokePointCount(0)
		, iTerrainDefenseScore(0)
		, bIsSalient(false)
		, bIsDefensibleSalient(false)
		, bEnemyHasIndirectFire(false)
		, iHostileTilesRing3(0)
		, iFriendlyTilesRing3(0)
		, iAdjacentDefensiveTerrain(0)
		, bIsChokepointCity(false)
		, iApproachCorridors(6)
		, iNarrowCorridors(0)
		, bIsFloodgate(false)
		, iDependentCityCount(0)
		, iRoadPriority(0)
		, bNeedsStrategicRoad(false)
	{
	}

	// Phase 1 fields
	int iCityID;
	eStrategicLayer eLayer;
	int iDefensiveDepth;       // Min distance to nearest hostile/neutral border tile
	bool bIsCapital;
	bool bIsFrontLine;
	bool bIsSecondLine;
	bool bIsCore;
	int iChokePointCount;      // Number of adjacent IsChokePoint() tiles
	int iTerrainDefenseScore;  // Aggregate terrain defense bonus of surrounding tiles

	// Phase 2 fields — Salient Detection
	bool bIsSalient;           // City protrudes into hostile territory (hostile/friendly ratio > 2.0)
	bool bIsDefensibleSalient; // Salient but surrounded by forest/jungle — hedgehog viable pre-Indirect Fire
	bool bEnemyHasIndirectFire;// True if any enemy at war has RangeAttackIgnoreLOS units (degrades defensible salient)
	int iHostileTilesRing3;    // Count of hostile-owned tiles within RING3
	int iFriendlyTilesRing3;   // Count of friendly-owned tiles within RING3
	int iAdjacentDefensiveTerrain; // Count of adjacent (RING1) tiles with forest/jungle/hills+forest

	// Phase 3 fields — Chokepoint City Detection
	bool bIsChokepointCity;    // City controls a terrain chokepoint (few open approach corridors)
	int iApproachCorridors;    // Number of wide-open approach directions (0-6; fewer = more choked)
	int iNarrowCorridors;      // Number of directions with corridor width == 1 (partial choke)

	// Phase 4 fields — Floodgate/Dependency Detection
	bool bIsFloodgate;         // Losing this city would expose 2+ other cities to hostile territory
	int iDependentCityCount;   // Number of cities that depend on this city for protection
	std::vector<int> vDependentCities; // City IDs of cities that would become exposed if this city fell

	// Phase 5 fields — Approach Corridor Analysis + Road Priority
	std::vector<EnemyApproach> vEnemyApproaches; // Per-enemy approach data (wartime only)
	int iRoadPriority;         // Derived priority for strategic road building (higher = build first)
	bool bNeedsStrategicRoad;  // True if city lacks road/rail connection to capital

	// Returns a priority modifier that can be added to threat criteria or zone values.
	// Higher = more strategically important to defend.
	int GetDefensePriorityModifier() const;
};

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//  CLASS: CvStrategicGeographyMap
//!  \brief Persistent strategic layer for AI defense allocation.
//!  Owned by CvMilitaryAI, updated infrequently (every N turns or on city gain/loss).
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

class CvStrategicGeographyMap
{
public:
	CvStrategicGeographyMap();

	void Init(PlayerTypes ePlayer);
	void Update();             // Full recalculation
	bool NeedsUpdate() const;  // Check if it's time to recompute

	// Queries consumed by other systems
	const StrategicCityAnalysis* GetCityAnalysis(int iCityID) const;
	eStrategicLayer GetCityLayer(int iCityID) const;
	int GetDefensiveDepth(int iCityID) const;
	int GetDefensePriorityModifier(int iCityID) const;
	int GetChokePointCount(int iCityID) const;

	// Phase 2: Salient queries
	bool IsCitySalient(int iCityID) const;
	bool IsDefensibleSalient(int iCityID) const;
	bool IsExpendableSalient(int iCityID) const; // Salient AND not defensible AND not capital

	// Phase 3: Chokepoint queries
	bool IsCityChokepoint(int iCityID) const;
	int GetApproachCorridors(int iCityID) const;

	// Phase 4: Floodgate queries
	bool IsCityFloodgate(int iCityID) const;
	int GetDependentCityCount(int iCityID) const;

	// Phase 5: Approach & road queries
	int GetRoadPriority(int iCityID) const;
	bool CityNeedsStrategicRoad(int iCityID) const;
	const std::vector<EnemyApproach>* GetEnemyApproaches(int iCityID) const;
	DirectionTypes GetPrimaryThreatDirection(int iCityID) const;

	// Bulk queries
	bool HasAnyCityData() const { return !m_cityAnalysis.empty(); }
	int GetLastUpdateTurn() const { return m_iLastFullUpdate; }

private:
	PlayerTypes m_ePlayer;
	int m_iLastFullUpdate;
	std::map<int, StrategicCityAnalysis> m_cityAnalysis;

	// Internal computation
	void ClassifyAllCities();
	void DetectSalients();          // Phase 2: salient + defensible salient detection
	void DetectChokepointCities();  // Phase 3: approach corridor analysis
	void BuildDependencyGraph();    // Phase 4: floodgate/dependency detection
	void AnalyzeApproachCorridors();// Phase 5: per-enemy approach analysis
	void DeriveRoadPriorities();    // Phase 5: road priority derivation
	int ComputeApproachDifficulty(CvCity* pOurCity, CvCity* pEnemyCity) const;
	int ScanCorridorWidth(CvPlot* pStart, DirectionTypes eDirection) const;
	int ComputeMinBorderDistance(CvCity* pCity) const;
	int CountAdjacentChokepoints(CvCity* pCity) const;
	int ComputeTerrainDefenseScore(CvCity* pCity) const;
	int CountHostileTilesInRing3(CvCity* pCity) const;
	int CountFriendlyTilesInRing3(CvCity* pCity) const;
	int CountAdjacentDefensiveTerrain(CvCity* pCity) const;
	bool DoesAnyEnemyHaveIndirectFire() const;
};

#endif // CIV5_STRATEGIC_GEOGRAPHY_MAP_H
