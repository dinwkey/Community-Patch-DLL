# AI Deep Reasoning Architecture

**Purpose:** Educational guide explaining advanced AI reasoning approaches, multi-turn memory systems, and potential LLM integration for strategic decision-making in Civ5 VP/CP.

**Last Updated:** February 2026  
**Status:** Architecture Exploration & Planning

**Related:** [AI_SYSTEMS_REVIEW.md](AI_SYSTEMS_REVIEW.md) — Issues backlog and improvement tracking

---

## Executive Summary

The current VP/CP AI makes decisions based on **immediate game state** without projecting future outcomes or remembering past patterns. This document explores architectural approaches to enable deeper strategic reasoning:

| Approach | Complexity | Memory Impact | Latency | Value |
|----------|------------|---------------|---------|-------|
| Extended Memory (5-turn) | Low | ~15-50 MB | None | High |
| Out-of-Process 64-bit | Medium | Unlimited | 0.1-5 ms | Medium |
| LLM Integration | High | External | 5-10 sec | Experimental |

**Recommended path:** Implement in phases, starting with lean in-process memory.

---

## 1. Current AI Limitations

### 1.1 Reactive Decision-Making

The AI evaluates **current turn state only**:

```
┌─────────────────────────────────────────────────────────────────┐
│  CURRENT AI DECISION FLOW (Simplified)                          │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  Current State ──► Score Functions ──► Best Action              │
│       │                  │                   │                  │
│       │                  │                   │                  │
│   • My units         • Approach calc     • Declare war?         │
│   • Enemy units      • Threat assess     • Make peace?          │
│   • Resources        • Target scoring    • Attack target?       │
│   • Relationships                                               │
│                                                                 │
│  ❌ No memory of past turns                                     │
│  ❌ No projection of future states                              │
│  ❌ No modeling of opponent intentions                          │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

<!-- Future: Add PNG diagram to docs/images/ai-current-flow.png -->

### 1.2 Missing Capabilities

| Capability | Current State | Impact |
|------------|---------------|--------|
| **Turn history** | None | Can't detect patterns (buildup, betrayal) |
| **Future projection** | None | Can't anticipate consequences |
| **Opponent modeling** | Partial (approach scores) | Can't predict enemy moves |
| **Multi-step planning** | None | Each decision is isolated |
| **Learning from outcomes** | None | Repeats same mistakes |

### 1.3 Example: The "Provocation While Weak" Problem

**Scenario observed:** Russia (AI) is at war with another AI, has exposed cities, yet sends "troops near my border" warning to a hostile neighbor — effectively inviting attack.

**Why it happens:**
1. `DoAggressiveMilitaryStatement()` checks if player has hostile approach
2. Fires warning without checking own defensive posture
3. No awareness that provoking a hostile neighbor while weak is suicidal

**Root cause:** No multi-factor reasoning that weighs "benefit of warning" vs "risk of provoking retaliation when I can't defend."

---

## 2. Extended Memory System

### 2.1 Design Goals

Enable the AI to:
- Detect military buildups over 3-5 turns
- Remember broken promises and betrayals
- Track opponent war outcomes (who's winning/losing)
- Notice resource/gold trends

### 2.2 Memory Budget Analysis (32-bit Constraint)

**Civ5 is a 32-bit process with ~3.2 GB ceiling.**

| Component | Typical Usage |
|-----------|---------------|
| Base game + VP DLL | 800 MB - 1.2 GB |
| Map data (Giant 180×94) | 200-400 MB |
| 63 civ AI state | 400-600 MB |
| UI/textures | 300-500 MB |
| **Typical total** | **2.0 - 2.8 GB** |
| **Available headroom** | **400-800 MB** |

**Memory system options:**

| Implementation | Size | Fits in 32-bit? |
|----------------|------|-----------------|
| Lean (5 turns, essential only) | 15-20 MB | ✅ Yes |
| Medium (10 turns, richer data) | 50-80 MB | ✅ Yes |
| Rich (10 turns, full state) | 150+ MB | ⚠️ Risky |

### 2.3 Data Structure Design (Pseudocode)

```cpp
// Compact per-civ snapshot (~64 bytes per civ per turn)
struct TurnSnapshot
{
    int16   turn;                           // 2 bytes
    int8    warState[MAX_MAJOR_CIVS];       // 36 bytes (enum: peace/war/nearly_won/etc)
    int8    approach[MAX_MAJOR_CIVS];       // 36 bytes (enum: friendly/hostile/etc)
    uint8   numCities;                      // 1 byte
    uint8   militaryRank;                   // 1 byte (1-36 relative strength)
    int16   goldPerTurn;                    // 2 bytes
    uint8   numUnitsNearMe;                 // 1 byte (visible military near my borders)
    uint8   flags;                          // 1 byte (bitflags: atWar, makingWonder, etc)
    // Padding to 64 bytes for alignment
};

// Per-civ circular buffer
struct CivMemory
{
    TurnSnapshot history[5];    // 5 turns × 64 bytes = 320 bytes
    uint8        currentIndex;  // Which slot is "newest"
    
    // Helper: Get snapshot from N turns ago (0 = current, 4 = oldest)
    TurnSnapshot* GetTurnsAgo(int n);
    
    // Helper: Detect military buildup pattern
    bool IsPlayerBuildingUp(PlayerTypes target);
};

// Global memory store
struct AIMemorySystem
{
    CivMemory playerMemory[MAX_MAJOR_CIVS];  // 36 × 320 = 11.5 KB
    
    // Event log (recent notable events)
    RecentEvent eventLog[100];               // ~5 KB for war declarations, betrayals, etc
    
    // Total: ~20 KB base + overhead ≈ 50 KB with safety margin
};
```

### 2.4 Pattern Detection Examples

```cpp
// Detect if a player is massing troops (3+ turn trend)
bool CivMemory::IsPlayerBuildingUp(PlayerTypes eObserver, PlayerTypes eTarget)
{
    int trend = 0;
    for (int i = 0; i < 4; i++)  // Compare last 4 transitions
    {
        TurnSnapshot* older = GetTurnsAgo(i + 1);
        TurnSnapshot* newer = GetTurnsAgo(i);
        
        if (IsValid(older) && IsValid(newer))
        {
            if (newer->numUnitsNearMe > older->numUnitsNearMe)
                trend++;
            else if (newer->numUnitsNearMe < older->numUnitsNearMe)
                trend--;
        }
    }
    return trend >= 2;  // Consistent increase over 2+ transitions
}

// Detect if player broke a promise recently
bool CivMemory::BrokePromiseRecently(PlayerTypes eTarget, int withinTurns)
{
    for (int i = 0; i < eventLog.size(); i++)
    {
        if (eventLog[i].type == EVENT_PROMISE_BROKEN &&
            eventLog[i].player == eTarget &&
            (currentTurn - eventLog[i].turn) <= withinTurns)
        {
            return true;
        }
    }
    return false;
}
```

### 2.5 Integration Points

| Function | How Memory Helps |
|----------|------------------|
| `DoUpdateWarTargets()` | Detect buildup → preemptive strike consideration |
| `SelectBestApproachTowardsMajorCiv()` | Factor in betrayal history, trend analysis |
| `DoAggressiveMilitaryStatement()` | Check if we're in a weak trend before provoking |
| `GetWarProjection()` | Use past war outcomes to predict future success |

---

## 3. Out-of-Process Architecture

### 3.1 Why Out-of-Process?

| Constraint | In-Process (32-bit) | Out-of-Process (64-bit) |
|------------|---------------------|-------------------------|
| Memory limit | ~3.2 GB total | Unlimited |
| LLM inference | ❌ Impossible | ✅ Possible |
| Rich history | ⚠️ Limited | ✅ Full state |
| Latency | Zero | 0.1-5 ms IPC |
| Complexity | Low | Medium |

**Recommendation:** Use in-process for lean memory, out-of-process only when LLM is needed.

### 3.2 IPC Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                    HYBRID ARCHITECTURE                          │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌──────────────────┐         ┌──────────────────────────────┐ │
│  │  Civ5 Process    │         │  Companion Process           │ │
│  │  (32-bit)        │         │  (64-bit Python/C++)         │ │
│  │                  │         │                              │ │
│  │  CvDiplomacyAI   │◄──IPC──►│  Rich History Store          │ │
│  │  CvMilitaryAI    │ (named  │  Pattern Analysis            │ │
│  │  CvTacticalAI    │  pipe)  │  LLM Inference (optional)    │ │
│  │                  │         │                              │ │
│  │  Lean Memory     │         │  Full State Snapshots        │ │
│  │  (20 KB)         │         │  (100+ MB)                   │ │
│  └──────────────────┘         └──────────────────────────────┘ │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

<!-- Future: Add PNG diagram to docs/images/ai-hybrid-architecture.png -->

### 3.3 IPC Protocol Design (Pseudocode)

```cpp
// Message envelope
struct IPCMessage
{
    uint32  messageType;    // cyclic_history / strategic_query / pattern_check
    uint32  payloadSize;
    uint8   payload[];      // Serialized data (JSON or binary)
};

// Query types
enum QueryType
{
    QUERY_SHOULD_DECLARE_WAR,       // Is now a good time to attack X?
    QUERY_WILL_PLAYER_ATTACK_ME,    // Predict if X will attack me soon
    QUERY_BEST_PEACE_TIMING,        // When should I sue for peace?
    QUERY_ALLIANCE_VALUE,           // Should I ally with X against Y?
};

// Request format (JSON for flexibility)
{
    "query_type": "SHOULD_DECLARE_WAR",
    "my_player": 3,
    "target_player": 7,
    "context": {
        "my_military_strength": 1250,
        "target_military_strength": 890,
        "target_current_wars": [2, 5],
        "target_war_states": ["NEARLY_WON", "STALEMATE"],
        "my_approach": "HOSTILE",
        "proximity": "NEIGHBORS",
        "turns_since_last_war": 15
    }
}

// Response format
{
    "recommendation": "WAIT",
    "confidence": 0.72,
    "reasoning": "Target is about to win war against player 2. Wait 3-5 turns until they redeploy.",
    "suggested_wait_turns": 4
}
```

### 3.4 Latency Analysis

| Operation | Time | Blocking? |
|-----------|------|-----------|
| Named pipe round-trip | 0.1-0.5 ms | Yes, negligible |
| Serialize 1 KB state | 0.05 ms | Yes, negligible |
| Pattern analysis (no LLM) | 1-5 ms | Yes, acceptable |
| LLM inference (7B model) | 5-10 sec | ⚠️ Only for rare decisions |

**Key insight:** IPC latency is negligible. LLM inference is the bottleneck — use sparingly.

---

## 4. LLM Integration Approach

### 4.1 Why LLM for Civ5 AI?

Traditional AI improvements (better heuristics, lookup tables) hit diminishing returns. An LLM could provide:

- **Nuanced reasoning** about multi-factor tradeoffs
- **Pattern recognition** from game state history
- **Natural language explanations** for debugging/tuning
- **Transfer learning** from Civ5 strategy discussions

### 4.2 Model Selection

| Model Size | VRAM | Inference Time | Quality | Feasibility |
|------------|------|----------------|---------|-------------|
| 3B params | 4 GB | 2-3 sec | Limited | ✅ Easy |
| 7B params | 6 GB | 4-6 sec | Good | ✅ Recommended |
| 13B params | 10 GB | 8-12 sec | Better | ⚠️ Tight on 8GB |
| 70B params | 40+ GB | 30+ sec | Best | ❌ Not feasible |

**Recommendation:** 7B-8B parameter model, quantized to 4-bit (Q4_K_M), fits in 6 GB VRAM.

### 4.3 Distillation Approach

Rather than using a general-purpose LLM, train a **Civ5-specific distilled model**:

1. **Collect training data** from VP/CP games:
   - Game state snapshots at decision points
   - Outcomes (war results, city captures, victory)
   - Human player decisions (from multiplayer replays)

2. **Generate reasoning traces** using a large teacher model (GPT-4, Claude):
   - Feed game state + outcome
   - Ask teacher to explain optimal decision
   - Capture chain-of-thought reasoning

3. **Fine-tune small model** on reasoning traces:
   - 7B base model (Mistral, Llama 2)
   - LoRA or full fine-tune on ~10K examples
   - Optimize for Civ5 domain vocabulary

4. **Quantize and deploy**:
   - 4-bit quantization (GGUF format)
   - llama.cpp or vLLM inference server
   - Local deployment, no internet required

### 4.4 Strategic Query Design

**When to query LLM (rare, high-stakes decisions):**
- Before declaring war
- When considering peace offers
- Major diplomatic shifts (ally → enemy)
- Victory path pivots

**When NOT to query LLM (use heuristics):**
- Every-turn tactical moves
- Production choices
- Trade route selection
- Minor diplomatic responses

### 4.5 Training Data Collection

```cpp
// Add to CvDiplomacyAI for data collection
struct StrategicDecisionLog
{
    int         turn;
    PlayerTypes decisionMaker;
    PlayerTypes targetPlayer;
    string      decisionType;       // "DECLARE_WAR", "MAKE_PEACE", etc
    
    // Context snapshot
    int         myMilitaryStrength;
    int         targetMilitaryStrength;
    int         myNumCities;
    int         targetNumCities;
    vector<int> myCurrentWars;
    vector<int> targetCurrentWars;
    
    // Outcome (filled in later)
    int         turnsUntilResolution;
    string      outcome;            // "WON", "LOST", "STALEMATE", "ONGOING"
    int         citiesCaptured;
    int         citiesLost;
};

// Log decisions for training data
void LogStrategicDecision(StrategicDecisionLog& log)
{
    // Write to CSV/JSON file for later training
    // Format: one row per decision with full context
}
```

---

## 5. Implementation Roadmap

### Phase 1: Lean In-Process Memory (Recommended Start)

**Effort:** 2-3 weeks  
**Risk:** Low  
**Value:** High

| Task | Description |
|------|-------------|
| Add `AIMemorySystem` struct | Global singleton, initialized on game start |
| Hook turn-end snapshot | Capture state for each major civ |
| Integrate in `DoUpdateWarTargets()` | Use buildup detection |
| Integrate in `SelectBestApproach...()` | Factor in betrayal history |
| Add logging for validation | Verify memory is populated correctly |

### Phase 2: IPC Bridge Infrastructure

**Effort:** 3-4 weeks  
**Risk:** Medium  
**Value:** Medium (enables Phase 3)

| Task | Description |
|------|-------------|
| Create named pipe server in DLL | Win32 `CreateNamedPipe` |
| Create Python companion process | Listens on pipe, stores rich history |
| Define message protocol | JSON or protobuf serialization |
| Add query interface in DLL | `QueryCompanion(type, context)` |
| Implement pattern analysis in Python | Trend detection, prediction |

### Phase 3: LLM Integration (Experimental)

**Effort:** 2-3 months  
**Risk:** High  
**Value:** Experimental

| Task | Description |
|------|-------------|
| Implement data collection logging | Capture decision contexts + outcomes |
| Play 100+ games for training data | Mix of human and AI games |
| Generate reasoning traces | Use GPT-4/Claude as teacher |
| Fine-tune 7B model | LoRA fine-tune on reasoning traces |
| Deploy inference server | llama.cpp with 4-bit quantized model |
| Connect to IPC bridge | Route strategic queries to LLM |
| Tune and validate | Adjust prompts, thresholds |

---

## 6. Reference Hardware Configuration

### 6.1 Test System Specifications

| Component | Specification |
|-----------|---------------|
| **CPU** | Intel Core i7-12850HX (16 cores, 24 threads) |
| **RAM** | 64 GB DDR5 (~45 GB free during gaming) |
| **Dedicated GPU** | NVIDIA RTX A2000 8GB GDDR6 |
| **Integrated GPU** | Intel UHD Graphics (32 EUs) |
| **Storage** | NVMe SSD |

### 6.2 Recommended Configuration

**Split GPU workloads:**
- **Intel UHD:** Runs Civ5 (DX11) at medium settings
- **RTX A2000:** Dedicated to LLM inference (8 GB VRAM)

**Why split?**
- Civ5 is a 2010 game — Intel UHD handles it fine
- LLM inference needs dedicated VRAM (no sharing with game)
- Avoids VRAM contention and stuttering

### 6.3 Civ5 on Intel UHD Graphics

**Expected performance at 1080p:**

| Quality Preset | FPS Estimate | Playable? |
|----------------|--------------|-----------|
| Low | 45-60 | ✅ Smooth |
| Medium | 25-35 | ✅ Playable |
| High | 15-25 | ⚠️ Sluggish late-game |

**Recommended settings for Intel UHD:**

| Setting | Value |
|---------|-------|
| Resolution | 1080p or 900p |
| Leader Scene Quality | Low |
| Texture Quality | Medium |
| Shadow Quality | Low or Off |
| Fog of War | Low |
| Anti-Aliasing | Off |
| VSync | On |

### 6.4 Forcing Civ5 to Use Intel GPU

**Windows Graphics Settings:**
1. Settings → System → Display → Graphics
2. Add `CivilizationV_DX11.exe`
3. Set to **Power Saving** (Intel)

**NVIDIA Control Panel:**
1. Manage 3D Settings → Program Settings
2. Add Civilization V
3. Set "Preferred graphics processor" to **Integrated Graphics**

### 6.5 LLM Inference Server Resource Usage

| Model | VRAM | RAM | Inference Time |
|-------|------|-----|----------------|
| 7B Q4_K_M | ~5-6 GB | 2-4 GB | 4-6 sec |
| 8B Q4_K_M | ~6-7 GB | 2-4 GB | 5-8 sec |
| 13B Q4_K_M | ~9-10 GB | 4-6 GB | ❌ Won't fit |

**Verdict:** 7B-8B model fits comfortably in RTX A2000 8GB with headroom.

---

## 7. Open Questions & Future Research

### 7.1 Unanswered Questions

1. **How to handle LLM latency?**
   - Pre-compute decisions at turn start while human is reviewing?
   - Background thread with cached recommendations?

2. **Training data quality:**
   - Are AI vs AI games sufficient, or do we need human games?
   - How many examples needed for useful fine-tuning?

3. **Evaluation metrics:**
   - How to measure if LLM decisions are "better"?
   - Elo rating system for AI variants?

4. **Multiplayer compatibility:**
   - LLM inference takes 5-10 seconds — acceptable in multiplayer?
   - Need deterministic fallback for sync?

### 7.2 Alternative Approaches Not Explored

| Approach | Pros | Cons |
|----------|------|------|
| Monte Carlo Tree Search | Theoretically optimal | Civ5 state space too large |
| Neural network policy | Fast inference | Needs massive training data |
| Rule-based expert system | Interpretable | Brittle, hard to tune |
| Genetic algorithms | Finds novel strategies | Slow evolution, hard to explain |

---

## 8. References & Related Work

### 8.1 Internal Documentation

- [AI_SYSTEMS_REVIEW.md](AI_SYSTEMS_REVIEW.md) — Issues backlog and improvement tracking
- `CvDiplomacyAI.cpp` — Core diplomatic decision-making
- `CvMilitaryAI.cpp` — Military threat assessment and targeting
- `CvGrandStrategyAI.cpp` — Long-term victory planning

### 8.2 External References

- [llama.cpp](https://github.com/ggerganov/llama.cpp) — Efficient LLM inference
- [LoRA Fine-tuning](https://arxiv.org/abs/2106.09685) — Parameter-efficient training
- [Civ5 AI Analysis (CivFanatics)](https://forums.civfanatics.com/) — Community AI discussions

---

*Document created: February 2026*  
*Purpose: Architecture exploration for enhanced AI reasoning in Vox Populi*
