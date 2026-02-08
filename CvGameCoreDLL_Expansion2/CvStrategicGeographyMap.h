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
//  Per-city strategic analysis (Phase 1: layer classification only)
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

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
	{
	}

	int iCityID;
	eStrategicLayer eLayer;
	int iDefensiveDepth;       // Min distance to nearest hostile/neutral border tile
	bool bIsCapital;
	bool bIsFrontLine;
	bool bIsSecondLine;
	bool bIsCore;
	int iChokePointCount;      // Number of adjacent IsChokePoint() tiles
	int iTerrainDefenseScore;  // Aggregate terrain defense bonus of surrounding tiles

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

	// Bulk queries
	bool HasAnyCityData() const { return !m_cityAnalysis.empty(); }
	int GetLastUpdateTurn() const { return m_iLastFullUpdate; }

private:
	PlayerTypes m_ePlayer;
	int m_iLastFullUpdate;
	std::map<int, StrategicCityAnalysis> m_cityAnalysis;

	// Internal computation
	void ClassifyAllCities();
	int ComputeMinBorderDistance(CvCity* pCity) const;
	int CountAdjacentChokepoints(CvCity* pCity) const;
	int ComputeTerrainDefenseScore(CvCity* pCity) const;
};

#endif // CIV5_STRATEGIC_GEOGRAPHY_MAP_H
