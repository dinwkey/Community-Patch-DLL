/*	-------------------------------------------------------------------------------------------------------
	© 1991-2012 Take-Two Interactive Software and its subsidiaries.  Developed by Firaxis Games.
	Sid Meier's Civilization V, Civ, Civilization, 2K Games, Firaxis Games, Take-Two Interactive Software
	and their respective logos are all trademarks of Take-Two interactive Software, Inc.
	All other marks and trademarks are the property of their respective owners.
	All rights reserved.
	------------------------------------------------------------------------------------------------------- */
#pragma once

#ifndef CV_UNIT_SIGHTING_MANAGER_H
#define CV_UNIT_SIGHTING_MANAGER_H

#include <map>
#include <utility>
#include <vector>

#include "CvEnums.h"

class CvCity;
class CvPlayer;
class CvPlot;
class CvUnit;
class FDataStream;

//=====================================
// Per-Unit Tracking (Extended Memory System)
//=====================================

static const int AI_MAX_UNIT_SIGHTINGS = 300;       // Max individually tracked enemy units
static const int AI_FOG_GHOST_EXPIRY_TURNS = 10;    // Forget fog-of-war units after this many turns
static const int AI_FOG_GHOST_USEFUL_TURNS = 5;     // Direction prediction becomes unreliable after this

// Unit intent prediction (for fog ghosts)
enum UnitPredictedIntent
{
	UNIT_INTENT_UNKNOWN,
	UNIT_INTENT_ATTACK_CITY,
	UNIT_INTENT_ATTACK_UNIT,
	UNIT_INTENT_RETREAT,
	UNIT_INTENT_PATROL,
	UNIT_INTENT_REINFORCE,
	UNIT_INTENT_EXPLORE,

	NUM_UNIT_INTENTS
};

// Sighting flags for unit classification (bitfield)
enum UnitSightingFlags
{
	SIGHTING_FLAG_EMBARKED  = 0x01,
	SIGHTING_FLAG_FORTIFIED = 0x02,
	SIGHTING_FLAG_DAMAGED   = 0x04,  // Below 50% health
	SIGHTING_FLAG_RANGED    = 0x08,
	SIGHTING_FLAG_SIEGE     = 0x10,
	SIGHTING_FLAG_NAVAL     = 0x20,
	SIGHTING_FLAG_AIR       = 0x40,
};

/// Per-unit sighting record with fog-of-war prediction support.
struct UnitSighting
{
	// === Identification ===
	int            unitId;           // Unit's ID (for tracking same unit across turns)
	UnitTypes      unitType;         // UnitTypes enum
	PlayerTypes    owner;            // PlayerTypes - which civ owns it

	// === Position ===
	short          x, y;             // Last confirmed position
	short          lastSeenTurn;     // When we last SAW this unit

	// === State ===
	unsigned char  health;           // Health percentage (0-100)
	unsigned char  flags;            // UnitSightingFlags bitfield

	// === Fog Prediction ===
	char           lastDeltaX;       // Last movement X direction (overall heading this turn)
	char           lastDeltaY;       // Last movement Y direction (overall heading this turn)
	unsigned char  movementPoints;   // Unit's movement capability (tiles/turn)
	unsigned char  predictedIntent;  // UnitPredictedIntent enum

	// === Multi-Turn Direction (EMA smoothed) ===
	short          turnStartX;       // Position at start of current movement turn (-1 = not set)
	short          turnStartY;       // Position at start of current movement turn (-1 = not set)
	char           avgDX;            // Exponential moving average of per-turn X heading
	char           avgDY;            // Exponential moving average of per-turn Y heading

	// === Helpers ===
	bool IsConfirmed(int currentTurn) const
	{
		return (int)lastSeenTurn == currentTurn;
	}

	bool IsExpired(int currentTurn) const
	{
		return (currentTurn - (int)lastSeenTurn) > AI_FOG_GHOST_EXPIRY_TURNS;
	}

	bool IsGhost(int currentTurn) const
	{
		return !IsConfirmed(currentTurn) && !IsExpired(currentTurn);
	}

	int GetUncertaintyRadius(int currentTurn) const
	{
		int turnsMissing = currentTurn - (int)lastSeenTurn;
		return turnsMissing * (int)movementPoints;
	}
};

/// Manager for tracking enemy unit sightings, fog-of-war ghosts,
/// and directional cone search. One instance per player.
class CvUnitSightingManager
{
public:
	CvUnitSightingManager();

	void Init(CvPlayer* pPlayer);

	// === Core Operations ===
	void OnUnitSeen(CvUnit* pUnit);
	void OnUnitMoved(CvUnit* pUnit, CvPlot* pFrom, CvPlot* pTo);
	void OnUnitDestroyed(PlayerTypes eOwner, int iUnitId);
	void UpdateGhosts(int currentTurn);
	void CleanupExpired(int currentTurn);

	// === Queries ===
	UnitSighting*       GetSighting(PlayerTypes eOwner, int iUnitId);
	const UnitSighting* GetSighting(PlayerTypes eOwner, int iUnitId) const;
	int  GetHostileSightingsNearPlot(CvPlot* pPlot, int iRadius, std::vector<UnitSighting*>& results);
	int  GetGhostsInSearchCone(int centerX, int centerY, int dirX, int dirY, int maxDist, std::vector<UnitSighting*>& results);
	int  CountSiegeUnitsNearCity(const CvCity* pCity, int iRadius, bool bAllowImminentCheck = true) const;
	bool IsPlotInSearchCone(const UnitSighting* pGhost, int plotX, int plotY, int currentTurn) const;
	bool CouldGhostThreatenCity(const UnitSighting* pGhost, CvCity* pCity, int currentTurn) const;

	/// City-aware intent inference: uses movement direction relative to a specific
	/// city to distinguish approach from retreat.
	UnitPredictedIntent InferUnitIntentNearCity(const UnitSighting* pSighting, int iCityX, int iCityY, int iCurrentTurn, bool bMovedThisTurn) const;

	// === Multi-Unit Pattern Detection (query-time aggregation) ===
	int CountUnitsConvergingOnCity(const CvCity* pCity, int iRadius, PlayerTypes eEnemy = NO_PLAYER) const;
	bool IsCoordinatedAttackOnCity(const CvCity* pCity, PlayerTypes eEnemy = NO_PLAYER) const;

	// === Serialization ===
	void Read(FDataStream& kStream);
	void Write(FDataStream& kStream) const;

	int GetNumSightings() const { return (int)m_Sightings.size(); }
	const std::vector<UnitSighting>& GetSightings() const { return m_Sightings; }

private:
	UnitSighting* GetOrCreateSighting(CvUnit* pUnit);
	UnitPredictedIntent InferUnitIntent(const UnitSighting* pSighting, CvUnit* pUnit) const;
	unsigned char ComputeFlags(CvUnit* pUnit) const;

	CvPlayer*                   m_pPlayer;
	std::vector<UnitSighting>   m_Sightings;
	std::map<std::pair<PlayerTypes, int>, int> m_SightingIndex;  // Key = owner/unitId -> index in m_Sightings

	std::pair<PlayerTypes, int> MakeKey(PlayerTypes eOwner, int iUnitId) const
	{
		return std::make_pair(eOwner, iUnitId);
	}
};

#endif
