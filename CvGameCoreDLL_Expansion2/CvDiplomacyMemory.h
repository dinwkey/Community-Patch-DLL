/*	-------------------------------------------------------------------------------------------------------
	© 1991-2012 Take-Two Interactive Software and its subsidiaries.  Developed by Firaxis Games.
	Sid Meier's Civilization V, Civ, Civilization, 2K Games, Firaxis Games, Take-Two Interactive Software
	and their respective logos are all trademarks of Take-Two interactive Software, Inc.
	All other marks and trademarks are the property of their respective owners.
	All rights reserved.
	------------------------------------------------------------------------------------------------------- */
#pragma once

#ifndef CV_DIPLOMACY_MEMORY_H
#define CV_DIPLOMACY_MEMORY_H

class CvDiplomacyAI;
class FDataStream;

static const int AI_MEMORY_DEPTH = 10;              // Turns of history to retain

// Snapshot flags (bitfield stored in TurnSnapshot::flags)
enum SnapshotFlags
{
	SNAPSHOT_FLAG_NONE                 = 0x00,
	SNAPSHOT_FLAG_AT_WAR               = 0x01,  // Currently in any war
	SNAPSHOT_FLAG_WINNING_WAR          = 0x02,  // Winning at least one war
	SNAPSHOT_FLAG_LOSING_WAR           = 0x04,  // Losing at least one war
	SNAPSHOT_FLAG_BUILDING_WONDER      = 0x08,
	SNAPSHOT_FLAG_RECENTLY_DENOUNCED   = 0x10,
	SNAPSHOT_FLAG_RECENTLY_BACKSTABBED = 0x20,
};

/// Per-civ state snapshot captured each turn.
/// Size: ~312 bytes with 43 major civs (VP default), ~445 bytes with 62.
struct TurnSnapshot
{
	// === Validity & Timestamp ===
	short turn;                                       // 0 = invalid/empty slot

	// === Per-Civ Arrays (sized by MAX_MAJOR_CIVS) ===
	char          warState[MAX_MAJOR_CIVS];           // WarStateTypes enum
	char          approach[MAX_MAJOR_CIVS];           // CivApproachTypes enum
	unsigned char theirMilitaryNearUs[MAX_MAJOR_CIVS]; // Aggressive-score near our borders
	unsigned char theirMilitaryStrength[MAX_MAJOR_CIVS]; // Military might scaled 0-255
	unsigned char proximity[MAX_MAJOR_CIVS];          // PlayerProximityTypes enum
	unsigned char siegeUnitsNearUs[MAX_MAJOR_CIVS];   // Siege units = attack signal
	unsigned char navalUnitsNearUs[MAX_MAJOR_CIVS];   // Coastal threat

	// === Our State (Scalars) ===
	unsigned char militaryRank;                       // Our rank 1..N (1 = strongest)
	unsigned char numCities;                          // Our city count (capped 255)
	short         goldPerTurn;                        // Our GPT (can be negative)
	unsigned char numUnitsNearBorders;                // Total enemy aggression near us
	unsigned char numWars;                            // How many wars we're fighting
	unsigned char flags;                              // SnapshotFlags bitfield
	unsigned char padding;                            // Alignment / reserved

	bool IsValid() const { return turn > 0; }
};

/// Circular buffer storing AI_MEMORY_DEPTH snapshots for one player.
struct CivMemory
{
	TurnSnapshot history[AI_MEMORY_DEPTH];            // Ring buffer
	unsigned char currentIndex;                       // Newest slot (0 .. AI_MEMORY_DEPTH-1)
	unsigned char validCount;                         // How many slots are populated (0 .. AI_MEMORY_DEPTH)

	/// Get snapshot from N turns ago (0 = current, 9 = oldest).
	/// Returns NULL if insufficient history.
	TurnSnapshot* GetTurnsAgo(int iTurnsAgo)
	{
		if (iTurnsAgo < 0 || iTurnsAgo >= (int)validCount || iTurnsAgo >= AI_MEMORY_DEPTH)
			return NULL;

		int index = ((int)currentIndex - iTurnsAgo + AI_MEMORY_DEPTH) % AI_MEMORY_DEPTH;
		TurnSnapshot* pSnapshot = &history[index];
		return pSnapshot->IsValid() ? pSnapshot : NULL;
	}

	const TurnSnapshot* GetTurnsAgo(int iTurnsAgo) const
	{
		if (iTurnsAgo < 0 || iTurnsAgo >= (int)validCount || iTurnsAgo >= AI_MEMORY_DEPTH)
			return NULL;

		int index = ((int)currentIndex - iTurnsAgo + AI_MEMORY_DEPTH) % AI_MEMORY_DEPTH;
		const TurnSnapshot* pSnapshot = &history[index];
		return pSnapshot->IsValid() ? pSnapshot : NULL;
	}

	/// Shortcut for GetTurnsAgo(0).
	TurnSnapshot* GetCurrent() { return GetTurnsAgo(0); }
	const TurnSnapshot* GetCurrent() const { return GetTurnsAgo(0); }

	/// Advance ring buffer and return a zeroed slot for the new turn.
	TurnSnapshot* AdvanceAndGetNew()
	{
		currentIndex = (unsigned char)((currentIndex + 1) % AI_MEMORY_DEPTH);
		if (validCount < AI_MEMORY_DEPTH)
			validCount++;

		memset(&history[currentIndex], 0, sizeof(TurnSnapshot));
		return &history[currentIndex];
	}
};

class CvDiplomacyMemory
{
public:
	CvDiplomacyMemory();

	void Init(CvDiplomacyAI* pDiplomacyAI);

	void ReadMemorySystem(FDataStream& kStream);
	void WriteMemorySystem(FDataStream& kStream) const;
	void CaptureMemorySnapshot();

	bool IsPlayerBuildingUpNearUs(PlayerTypes ePlayer) const;
	bool IsSiegeWarningActive(PlayerTypes ePlayer) const;
	bool IsPlayerCreepingCloser(PlayerTypes ePlayer) const;
	bool HasApproachChangedRecently(PlayerTypes ePlayer, int iWithinTurns) const;
	bool HasTurnedHostileRecently(PlayerTypes ePlayer, int iWithinTurns) const;
	bool AmIOverextended() const;
	int GetCoalitionThreatScore() const;
	int GetHistoricalThreat(PlayerTypes ePlayer, int iTurnsAgo) const;
	bool IsThreatRising(PlayerTypes ePlayer) const;
	bool IsAttackLikelyImminent(PlayerTypes ePlayer) const;

	CivMemory& GetMemory() { return m_Memory; }
	const CivMemory& GetMemory() const { return m_Memory; }

private:
	CvDiplomacyAI* m_pDiplomacyAI;
	CivMemory m_Memory;
	mutable bool m_bEvaluatingAttackLikelyImminent;
};

#endif
