# AI Deep Reasoning Architecture

**Purpose:** Educational guide explaining advanced AI reasoning approaches, multi-turn memory systems, and potential LLM integration for strategic decision-making in Civ5 VP/CP.

**Last Updated:** February 2026  
**Status:** Architecture Exploration & Planning

**Related:**
- [AI_SYSTEMS_REVIEW.md](AI_SYSTEMS_REVIEW.md) — Issues backlog and improvement tracking
- [EXTENDED_MEMORY_SYSTEM.md](EXTENDED_MEMORY_SYSTEM.md) — **Implementation spec for multi-turn memory** ⭐

---

## Executive Summary

The current VP/CP AI makes decisions based on **immediate game state** without projecting future outcomes or remembering past patterns. This document explores architectural approaches to enable deeper strategic reasoning:

| Approach | Complexity | Memory/Cost | Latency | In-Process? | Value |
|----------|------------|-------------|---------|-------------|-------|
| [Extended Memory](EXTENDED_MEMORY_SYSTEM.md) | Low | ~300 KB | None | ✅ Yes | High |
| Traditional ML (XGBoost) | Medium | ~10-50 MB | 0.01-0.1 ms | ✅ Yes | High |
| Vox Deorum (LLM) | Low | API key | 1-5 sec | ❌ No | ⭐ High |
| Copilot Bridge (free LLM) | Medium | Free tier | 1-3 sec | ❌ No | Medium |
| Self-Hosted LLM | High | 6-8 GB VRAM | 5-10 sec | ❌ No | Medium |

**Recommended path:** 
- **Phase 1-2:** Memory → ML (in-process improvements)
- **LLM integration:** Use **[Vox Deorum](https://github.com/CIVITAS-John/vox-deorum)** — production-ready, built for VP/CP

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

> **📋 Full implementation specification:** [EXTENDED_MEMORY_SYSTEM.md](EXTENDED_MEMORY_SYSTEM.md)
>
> This section provides an overview. See the linked document for complete data structures, code examples, and implementation steps.

### 2.1 Design Goals

Enable the AI to:
- Detect military buildups over 3-5 turns
- Remember broken promises and betrayals
- Track opponent war outcomes (who's winning/losing)
- Notice resource/gold trends

### 2.2 Memory Budget Analysis (32-bit Process on 64-bit Windows)

**Civ5 is a 32-bit process but is Large Address Aware (LAA).** On 64-bit Windows, this allows access to the full 4 GB virtual address space.

| Component | Typical Usage |
|-----------|---------------|
| Base game + VP DLL | 800 MB - 1.2 GB |
| Map data (Giant 180×94) | 200-400 MB |
| 63 civ AI state | 400-600 MB |
| UI/textures | 300-500 MB |
| **Typical total** | **2.0 - 2.8 GB** |
| **LAA ceiling (64-bit Windows)** | **4 GB** |
| **Available headroom** | **1.2 - 2.0 GB** |

**Memory system options:**

| Implementation | Size | Fits? |
|----------------|------|-------|
| Lean (5 turns, essential only) | 15-20 MB | ✅ Easily |
| Medium (10 turns, richer data) | 50-80 MB | ✅ Yes |
| Rich (10 turns, full state) | 150-200 MB | ✅ Yes |

**Note:** With LAA on 64-bit Windows, even the "rich" memory implementation fits comfortably. The out-of-process architecture is primarily needed for LLM inference (which requires 64-bit process), not for memory constraints.

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

| Constraint | In-Process (32-bit LAA) | Out-of-Process (64-bit) |
|------------|-------------------------|-------------------------|
| Memory limit | 4 GB (sufficient) | Unlimited |
| LLM inference | ❌ Impossible (needs 64-bit) | ✅ Possible |
| Rich history | ✅ Fits with LAA | ✅ Full state |
| Latency | Zero | 0.1-5 ms IPC |
| Complexity | Low | Medium |

**Recommendation:** Use in-process for memory system (LAA provides enough headroom). Out-of-process is only needed for LLM inference, which requires 64-bit libraries.

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

### 4.5 Vox Deorum: Production-Ready LLM Integration

**[Vox Deorum](https://github.com/CIVITAS-John/vox-deorum)** is a mature, production-ready LLM integration specifically built for Vox Populi. It provides a complete architecture with game state tools, session replay, and multi-model support.

**Architecture:**

```
┌─────────────────────────────────────────────────────────────────┐
│  VOX DEORUM ARCHITECTURE                                        │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  Civ 5 ↔ Community Patch DLL ↔ Bridge Service ↔ MCP Server     │
│           (Named Pipe)         (REST/SSE)       (MCP/HTTP)      │
│                                                      │          │
│                                                      ▼          │
│                                               Vox Agents → LLM  │
│                                                                 │
│  Components:                                                    │
│   • civ5-dll    - Modified game DLL for IPC                     │
│   • bridge-service - REST API & game communication              │
│   • mcp-server  - Game state tools via Model Context Protocol   │
│   • vox-agents  - LLM decision engine                           │
│   • civ5-mod    - Lua integration scripts                       │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

<!-- Future: Add PNG diagram to docs/images/vox-deorum-arch.png -->

**Key Features:**
- **LLM-enhanced AI Opponent** — Play against LLM-powered civilizations
- **Chat with LLM Spokespersons** — Interactive diplomacy with AI players
- **Session Replay** — Review and debug AI decisions with [Vox Deorum Replayer](https://github.com/CIVITAS-John/vox-deorum-replay)
- **Any LLM** — Supports GPT, Claude, local models via LiteLLM
- **MCP (Model Context Protocol)** — Standardized tool interface for game state

**Installation:**
1. Download installer from [releases page](https://github.com/CIVITAS-John/vox-deorum/releases)
2. Run setup wizard (handles everything automatically)
3. Launch via "Vox Deorum" in Start Menu or `scripts\vox-deorum.cmd`

**Prerequisites:**
- Windows 10/11
- Civilization V (with both expansion packs)
- API key from LLM provider (or use local models)

**Why Vox Deorum over DIY:**

| Factor | DIY (WinHTTP) | Vox Deorum |
|--------|---------------|------------|
| Setup time | Days-weeks | Minutes |
| Game state access | Manual extraction | MCP tools built-in |
| Session debugging | None | Full replay system |
| DLL modifications | DIY | Pre-modified included |
| Active development | N/A | ✅ (v0.6.3, 33 releases) |
| Community support | None | GitHub issues, docs |

**Recommendation:** For serious LLM integration, **start with Vox Deorum**. Only build custom solutions if you have specific requirements it doesn't meet.

### 4.6 Alternative: Copilot Bridge (Free-Tier Cloud LLM)

For users who want LLM capabilities without API costs, **[VSCode Copilot Bridge](https://github.com/larsbaunwall/vscode-copilot-bridge)** provides access to Copilot's models via localhost HTTP using VS Code's official Language Model API (`vscode.lm`).

**Key characteristics:**
- Uses **only** the public VS Code Language Model API — NOT a reverse-engineered proxy
- Requires active GitHub Copilot subscription
- Local-first: all traffic stays on-device, no telemetry collected
- Subject to GitHub ToS and Acceptable Use Policy

**Architecture:**

```
┌─────────────────────────────────────────────────────────────────┐
│  COPILOT BRIDGE ARCHITECTURE                                    │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  Civ5 (32-bit) ──HTTP POST──► 127.0.0.1:<port> ──► Copilot Bridge
│       │                            │                   │        │
│  Game state JSON              Polka server          VSCode      │
│  "Should I attack?"          (in VS Code)       vscode.lm API   │
│       │                            │                   │        │
│       ◄────────SSE Response────────┘◄──────────────────┘        │
│  "Wait 3 turns,                                                 │
│   target is winning war"                                        │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

<!-- Future: Add PNG diagram to docs/images/copilot-bridge.png -->

**OpenAI-compatible endpoints:**

| Endpoint | Purpose |
|----------|---------|
| `/v1/chat/completions` | Chat completions (OpenAI-style) |
| `/v1/models` | List available models |
| `/health` | Health check, diagnostics |

**Configuration options:**

| Setting | Default | Description |
|---------|---------|-------------|
| `bridge.enabled` | `false` | Auto-start with VS Code |
| `bridge.port` | `0` (ephemeral) | Server port (check status for assigned port) |
| `bridge.token` | `""` | **Required** bearer token for auth |
| `bridge.historyWindow` | `3` | Retained conversation turns |
| `bridge.maxConcurrent` | `1` | Max parallel requests |
| `bridge.verbose` | `false` | Debug logging |

**Security model:**
- 🔒 **Localhost-only** — binds to `127.0.0.1`, cannot be exposed to network (non-configurable)
- 🔑 **Mandatory bearer token** — requests without valid `Authorization: Bearer <token>` get `401 Unauthorized`
- 📊 **No telemetry** — zero data transmitted to author or third parties

**Scope and limitations:**

| ✅ Supported | ❌ Not Supported |
|--------------|------------------|
| Local, single-user loopback | Multi-user / shared deployments |
| Testing local agents / CLI | CI/CD automation |
| Educational / experimental | Public or commercial API hosting |
| Personal experimentation | Server-side deployments |

**Critical constraints:**
- **VS Code must stay open** — bridge runs only while editor is active
- **Requires Copilot subscription** — uses your personal Copilot session
- **Concurrency limited** — default 1 request at a time (tunable but affects editor responsiveness)
- **Ephemeral port** — check "Copilot Bridge: Status" command for assigned port

**Free-tier model selection:**

| Model | Speed | Reasoning | Best For |
|-------|-------|-----------|----------|
| **GPT-4o** | Fast (1-2s) | Excellent | Complex strategic decisions |
| **GPT-5-mini** | Very Fast (<1s) | Good | Frequent queries |
| **Grok Code Fast 1** | Very Fast (<1s) | Good (code-aware) | Code-aware decisions |

> ℹ️ The bridge auto-discovers models via `vscode.lm.selectChatModels()` — any model registered with VS Code's Language Model API is available.

**Rate limit budget (free tier ~50 req/hr):**

| Approach | Queries/Turn | 500-turn Game | Fits Limit? |
|----------|--------------|---------------|-------------|
| Every decision | 5-10 | 2500-5000 | ❌ Way over |
| Major decisions only | 0.1-0.2 | 50-100 | ⚠️ Borderline |
| Cached + filtered | 0.05 | 25 | ✅ Safe |

**Quick start:**

```bash
# 1. Install extension from VS Marketplace (thinkability.copilot-bridge)
# 2. Set token in VS Code settings: Copilot Bridge > Token
# 3. Command Palette > "Copilot Bridge: Enable"
# 4. Check status: "Copilot Bridge: Status" (shows port)

# Test with curl:
export PORT=<port-from-status>
export BRIDGE_TOKEN="<your-token>"

curl -H "Authorization: Bearer $BRIDGE_TOKEN" \
  http://127.0.0.1:$PORT/v1/models

curl -N \
  -H "Authorization: Bearer $BRIDGE_TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"model":"gpt-4o-copilot","messages":[{"role":"user","content":"hello"}]}' \
  http://127.0.0.1:$PORT/v1/chat/completions
```

**When to use Copilot Bridge:**
- Already have Copilot subscription
- Don't want to pay for API keys
- Simpler setup than full Vox Deorum
- VSCode is always open during play
- Want zero telemetry/data collection

### 4.7 Hybrid: Vox Deorum + Copilot Bridge

Combine Vox Deorum's mature architecture with Copilot Bridge's free models by routing Vox Deorum's LLM requests through Copilot.

**Architecture:**

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  VOX DEORUM + COPILOT BRIDGE HYBRID                                         │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  Civ 5 ↔ CP DLL ↔ Bridge Service ↔ MCP Server ↔ Vox Agents                 │
│           (Named Pipe)   (REST/SSE)    (MCP)         │                      │
│                                                      ▼                      │
│                                              LiteLLM Proxy                  │
│                                                      │                      │
│                                                      ▼                      │
│                                              Copilot Bridge ──► VSCode      │
│                                          (127.0.0.1:<port>)    vscode.lm    │
│                                                                             │
│  Benefits:                                                                  │
│   ✅ Vox Deorum's mature MCP architecture                                   │
│   ✅ Game state tools already implemented                                   │
│   ✅ Session replay for debugging                                           │
│   ✅ Free-tier Copilot models (no API key costs)                            │
│   ✅ Codebase awareness from Copilot                                        │
│   ✅ Zero telemetry — all local                                             │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

<!-- Future: Add PNG diagram to docs/images/vox-deorum-copilot-hybrid.png -->

**How it works:**

Copilot Bridge already exposes **OpenAI-compatible endpoints** out of the box:
- `/v1/chat/completions` — Chat completions API
- `/v1/models` — List available models
- `/health` — Health check

This means **no modifications required** — just configure Vox Deorum's LiteLLM to point to Copilot Bridge:

```python
# Configure LiteLLM to use Copilot Bridge
# Note: port is ephemeral — check "Copilot Bridge: Status" for assigned port
import litellm

BRIDGE_PORT = 12345  # Replace with actual port from status
BRIDGE_TOKEN = "your-secret-token"  # Same token configured in VS Code settings

litellm.api_base = f"http://127.0.0.1:{BRIDGE_PORT}/v1"
litellm.api_key = BRIDGE_TOKEN  # Required — requests without token get 401
```

**Setup steps:**

1. Install Copilot Bridge extension (requires VSCode with Copilot signed in)
2. Set `bridge.token` in VS Code settings (mandatory for auth)
3. Run "Copilot Bridge: Enable" from Command Palette
4. Note the port from "Copilot Bridge: Status"
5. Install Vox Deorum normally
6. Configure Vox Deorum's LiteLLM to use `http://127.0.0.1:<port>/v1` as API base
7. Set the same bearer token as API key
8. Play with free Copilot models + full Vox Deorum features

**Considerations:**

| Factor | Notes |
|--------|-------|
| Tool calling | MCP tools may need prompt-based fallback if Copilot doesn't support function calling |
| Rate limits | Free tier ~50/hr — use tiered strategy, cache responses |
| VSCode requirement | Must keep VSCode open during play |
| Streaming | Copilot Bridge supports SSE streaming for real-time responses |

**When to use hybrid:**
- Want Vox Deorum features (replay, MCP tools)
- Don't want to pay for API keys
- Have Copilot subscription (free or paid)

### 4.8 LLM Approach Comparison

| Factor | Vox Deorum (Direct) | Copilot Bridge (DIY) | Hybrid |
|--------|---------------------|---------------------|--------|
| **Maturity** | ⭐ Production-ready | Medium (DIY) | Good (both mature) |
| **Setup complexity** | Low (installer) | Medium | Medium |
| **Cost** | API key required | Free (Copilot sub) | Free (Copilot sub) |
| **Game state tools** | ✅ MCP included | ❌ DIY | ✅ MCP included |
| **Session replay** | ✅ Yes | ❌ No | ✅ Yes |
| **Model flexibility** | Any LLM | Copilot only | Copilot only |
| **Offline capable** | ✅ With local models | ❌ No | ❌ No |
| **VSCode required** | ❌ No | ✅ Yes | ✅ Yes |
| **Rate limits** | API-based | ~50/hr free | ~50/hr free |

**Recommendations:**

| Scenario | Best Choice |
|----------|-------------|
| Serious LLM integration, have API budget | **Vox Deorum** |
| Want free models, simple one-off queries | **Copilot Bridge** |
| Want Vox Deorum features + free models | **Hybrid** (if you enjoy tinkering) |
| Offline/privacy required | **Vox Deorum + local model** |
| Just exploring, minimal setup | **Copilot Bridge** |

### 4.9 Training Data Collection

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

## 5. Traditional Machine Learning Approaches

### 5.1 Why ML Before LLM?

Traditional ML models are **orders of magnitude lighter** than LLMs and can run entirely in-process:

| Approach | Model Size | Inference Time | GPU Required? | In-Process? |
|----------|------------|----------------|---------------|-------------|
| **LLM (7B)** | 4-6 GB | 5-10 sec | Yes (VRAM) | ❌ No (64-bit) |
| **Gradient Boosted Trees** | 1-50 MB | 0.01-0.1 ms | No | ✅ Yes |
| **Small Neural Net (MLP)** | 1-10 MB | 0.1-1 ms | No | ✅ Yes |
| **Random Forest** | 10-100 MB | 0.1-1 ms | No | ✅ Yes |
| **Logistic Regression** | <1 MB | <0.01 ms | No | ✅ Yes |

**Key insight:** Civ5 game state is **tabular data** (numbers: strength, gold, cities, relationships). This is exactly what gradient boosted trees excel at.

### 5.2 Recommended: Gradient Boosted Trees (XGBoost/LightGBM)

**Why this approach:**
- State-of-the-art for tabular data classification
- Fast inference (microseconds)
- Interpretable feature importance (explains decisions)
- Battle-tested in production systems
- Small model size (5-50 MB)

**Limitations:**
- Doesn't handle sequences natively (need feature engineering for history)
- Requires labeled training data

### 5.3 Feature Engineering

The game state must be converted to numeric features for ML:

```cpp
// Features for war decision classification
struct WarDecisionFeatures
{
    // === Strength Ratios ===
    float myMilitaryStrength;
    float targetMilitaryStrength;
    float strengthRatio;              // my / target
    float strengthDifference;         // my - target
    
    // === Situational Awareness ===
    float targetCurrentWarCount;      // How many wars is target in?
    float targetWarStateAvg;          // Avg war state (-2=losing, +2=winning)
    float myCurrentWarCount;
    float myWarStateAvg;
    
    // === Geographic ===
    float proximity;                  // 1=neighbor, 2=close, 3=far, 4=distant
    float sharedBorderTiles;
    float distanceToNearestCity;
    
    // === Historical (from Memory System) ===
    float turnsKnown;
    float turnsAtWar;
    float turnsAtPeace;
    float recentBetrayals;            // Broken promises in last 30 turns
    float militaryTrend;              // +1=building up, -1=declining
    float attacksOnMeRecently;
    
    // === Economic ===
    float myGoldPerTurn;
    float targetGoldPerTurn;
    float goldRatio;
    float myTechEra;
    float targetTechEra;
    float techDifference;
    
    // === Diplomatic Context ===
    float myApproachToTarget;         // -2=hostile to +2=friendly
    float targetApproachToMe;
    float numMutualFriends;
    float numMutualEnemies;
    
    // ~30-40 features total
};
```

### 5.4 Training Pipeline

```
┌─────────────────────────────────────────────────────────────────┐
│  ML TRAINING PIPELINE (Offline, Python)                         │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  1. DATA COLLECTION (during gameplay)                           │
│     • Log decision points + context to CSV                      │
│     • Track outcomes 10-30 turns later                          │
│     • Collect 10K+ examples                                     │
│                                                                 │
│  2. LABELING                                                    │
│     • War declared → did we win? capture cities? lose cities?   │
│     • Peace made → was it good timing?                          │
│     • Labels: GOOD_DECISION, BAD_DECISION, NEUTRAL              │
│                                                                 │
│  3. TRAINING (Python)                                           │
│     • XGBoost or LightGBM classifier                            │
│     • Cross-validation, hyperparameter tuning                   │
│     • Export feature importance for interpretability            │
│                                                                 │
│  4. EXPORT TO C++                                               │
│     • Option A: XGBoost C API (link library)                    │
│     • Option B: Export as pure C++ if-else code                 │
│     • Option C: ONNX Runtime                                    │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

<!-- Future: Add PNG diagram to docs/images/ml-training-pipeline.png -->

### 5.5 C++ Integration Options

| Method | 32-bit? | Dependencies | Complexity | Notes |
|--------|---------|--------------|------------|-------|
| **Export as if-else** | ✅ | None | Low | Tree → C++ code generator |
| **XGBoost C API** | ✅ | libxgboost | Medium | Full model, needs linking |
| **ONNX Runtime** | ✅ | onnxruntime | Medium | Universal format |
| **Custom MLP** | ✅ | None | Medium | Hand-code ~200 lines |

**Simplest approach:** Export trained decision tree as pure C++ if-else statements:

```cpp
// Auto-generated from trained XGBoost model
float PredictWarSuccess(const WarDecisionFeatures& f)
{
    // Tree 0
    float score0 = 0.0f;
    if (f.strengthRatio < 1.2f) {
        if (f.targetCurrentWarCount < 1.5f) {
            score0 = -0.15f;  // Target not distracted, we're weaker
        } else {
            score0 = 0.05f;   // Target distracted
        }
    } else {
        if (f.proximity < 2.5f) {
            score0 = 0.25f;   // We're stronger and close
        } else {
            score0 = 0.10f;   // We're stronger but far
        }
    }
    
    // Tree 1, Tree 2, ... (50-100 trees typical)
    // ...
    
    // Sum all tree scores, apply sigmoid
    float totalScore = score0 + score1 + /* ... */;
    return 1.0f / (1.0f + exp(-totalScore));  // Probability 0-1
}
```

Zero dependencies, compiles directly into DLL, microsecond inference.

### 5.6 Alternative: Small Neural Network (MLP)

For more complex patterns, a 3-layer MLP can be hand-coded:

```cpp
// Simple MLP: 30 inputs → 64 hidden → 32 hidden → 3 outputs
class SimpleMLPClassifier
{
    float weights1[30][64];   // ~7.5 KB
    float bias1[64];
    float weights2[64][32];   // ~8 KB
    float bias2[32];
    float weights3[32][3];    // ~0.4 KB
    float bias3[3];
    // Total: ~16 KB
    
    void Forward(const float* input, float* output)
    {
        float hidden1[64], hidden2[32];
        
        // Layer 1: input → hidden1 (ReLU)
        for (int j = 0; j < 64; j++) {
            hidden1[j] = bias1[j];
            for (int i = 0; i < 30; i++)
                hidden1[j] += input[i] * weights1[i][j];
            hidden1[j] = fmax(0.0f, hidden1[j]);  // ReLU
        }
        
        // Layer 2: hidden1 → hidden2 (ReLU)
        for (int j = 0; j < 32; j++) {
            hidden2[j] = bias2[j];
            for (int i = 0; i < 64; i++)
                hidden2[j] += hidden1[i] * weights2[i][j];
            hidden2[j] = fmax(0.0f, hidden2[j]);
        }
        
        // Layer 3: hidden2 → output (softmax)
        for (int j = 0; j < 3; j++) {
            output[j] = bias3[j];
            for (int i = 0; i < 32; i++)
                output[j] += hidden2[i] * weights3[i][j];
        }
        Softmax(output, 3);
    }
};
```

Weights loaded from file at game start (~16 KB), inference in microseconds.

### 5.7 Decision Types Suitable for ML

| Decision | Features | Output | Priority |
|----------|----------|--------|----------|
| **Should declare war?** | Strength, situation, history | {YES, WAIT, NO} | ⭐ High |
| **Accept peace offer?** | War state, losses, terms | {ACCEPT, REJECT} | High |
| **Approach to player** | History, power, diplomacy | {HOSTILE...FRIENDLY} | Medium |
| **Attack this target?** | Target strength, defenses | {ATTACK, SKIP} | Medium |
| **Settle city here?** | Resources, danger, expansion | Score 0-1 | Lower |

### 5.8 Comparison: ML vs LLM vs Heuristics

| Aspect | Heuristics | ML (XGBoost) | LLM |
|--------|------------|--------------|-----|
| **Inference time** | <0.001 ms | 0.01-0.1 ms | 5-10 sec |
| **Memory** | 0 | 10-50 MB | 4-6 GB |
| **In-process?** | ✅ | ✅ | ❌ |
| **Learns from data** | ❌ | ✅ | ✅ |
| **Handles nuance** | Limited | Good | Excellent |
| **Explainable** | ✅ | ✅ (feature importance) | ⚠️ (with prompting) |
| **Training effort** | N/A | Medium (10K examples) | High (fine-tuning) |

**Recommendation:** Use ML for frequent decisions (every turn), reserve LLM for rare high-stakes decisions (war declarations, major pivots).

---

## 6. Implementation Roadmap

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

### Phase 1.5: Data Collection for ML

**Effort:** 1-2 weeks  
**Risk:** Low  
**Value:** Enables Phase 2

| Task | Description |
|------|-------------|
| Add decision logging | Log war/peace decisions + context to CSV |
| Add outcome tracking | Record results 10-30 turns after decision |
| Play test games | Collect 5K-10K decision examples |
| Export for training | Python scripts to process logs |

### Phase 2: ML Model Training & Integration

**Effort:** 3-4 weeks  
**Risk:** Medium  
**Value:** High

| Task | Description |
|------|-------------|
| Train XGBoost model | Python, cross-validation, hyperparameter tuning |
| Export to C++ | Generate if-else code or link XGBoost C API |
| Integrate in decision points | `DoUpdateWarTargets()`, approach selection |
| A/B testing | Compare ML decisions vs heuristics |
| Tune thresholds | Adjust confidence thresholds for action |

### Phase 3: IPC Bridge Infrastructure (Optional)

**Effort:** 3-4 weeks  
**Risk:** Medium  
**Value:** Medium (enables Phase 4)

| Task | Description |
|------|-------------|
| Create named pipe server in DLL | Win32 `CreateNamedPipe` |
| Create Python companion process | Listens on pipe, stores rich history |
| Define message protocol | JSON or protobuf serialization |
| Add query interface in DLL | `QueryCompanion(type, context)` |
| Implement pattern analysis in Python | Trend detection, prediction |

### Phase 4: LLM Integration (Experimental)

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

## 7. Reference Hardware Configuration

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

### 7.5 LLM Inference Server Resource Usage

| Model | VRAM | RAM | Inference Time |
|-------|------|-----|----------------|
| 7B Q4_K_M | ~5-6 GB | 2-4 GB | 4-6 sec |
| 8B Q4_K_M | ~6-7 GB | 2-4 GB | 5-8 sec |
| 13B Q4_K_M | ~9-10 GB | 4-6 GB | ❌ Won't fit |

**Verdict:** 7B-8B model fits comfortably in RTX A2000 8GB with headroom.

---

## 8. Open Questions & Future Research

### 8.1 Unanswered Questions

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

### 8.2 Alternative Approaches — Revised Assessment

The following approaches were initially dismissed but deserve more nuanced consideration:

#### 8.2.1 Monte Carlo Tree Search (MCTS)

**Initial dismissal:** "Civ5 state space too large"

**Revised assessment:**

| Factor | Analysis |
|--------|----------|
| State space | ~10^50+ states (vs Chess ~10^47, Go ~10^170) |
| Branching factor | Hundreds of actions per turn |
| Simulation speed | Can't simulate Civ5 turns fast (unlike Chess/Go) |
| Full-game MCTS | ❌ Not feasible |
| **Limited MCTS** | ✅ Viable for subproblems |

**Viable use cases:**
- **Tactical combat** — 5-10 units, 2-3 turn lookahead
- **City siege planning** — Simulate assault outcomes
- **Exploration decisions** — Where to send scouts (small state space)

```
┌─────────────────────────────────────────────────────────────────┐
│  LIMITED MCTS FOR TACTICAL COMBAT                               │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  Current State ──► Build Tree ──► Simulate ──► Best Move        │
│       │               │              │             │            │
│   5-10 units      2-3 turns      100-1000       Attack X?       │
│   Small area      lookahead      playouts       Move to Y?      │
│                                                                 │
│  Constraints:                                                   │
│   • Limit to visible battlefield only                           │
│   • Max 3 turn horizon                                          │
│   • Max 1000 simulations per decision                           │
│   • Time budget: 100-500ms                                      │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

<!-- Future: Add PNG diagram to docs/images/mcts-tactical.png -->

**Recommendation:** Consider for CvTacticalAI combat decisions only.

---

#### 8.2.2 Neural Network Policy (Imitation Learning)

**Initial dismissal:** "Needs massive training data"

**Revised assessment:**

| Factor | Analysis |
|--------|----------|
| Reinforcement learning | ❌ Impractical (millions of self-play games needed) |
| Self-play speed | 500 turns × 1-5 sec = 8-40 min/game |
| **Imitation learning** | ✅ Feasible with 10K-50K examples |
| Data sources | AI decision logs, human multiplayer replays |

**Imitation learning (behavioral cloning) is viable:**

1. **Log current AI decisions** — Free data, millions of decisions per playthrough
2. **Collect human replays** — Multiplayer games from skilled players
3. **Train small policy network** — Predict "what would a good player do?"
4. **Use as fast intuition** — Quick filter before expensive scoring

```cpp
// Imitation learning integration
float GetQuickIntuition(const GameState& state, ActionType action)
{
    // Small MLP trained on human/AI decisions
    // Returns probability this action is "good"
    float features[50];
    ExtractFeatures(state, action, features);
    return policyNetwork.Forward(features);  // 0.0 - 1.0
}

void DoUpdateWarTargets()
{
    // Use intuition as first filter
    for (each potential target)
    {
        float intuition = GetQuickIntuition(state, DECLARE_WAR, target);
        if (intuition < 0.2f)
            continue;  // Skip unlikely candidates early
        
        // Full scoring for remaining candidates
        int score = ScoreWarTarget(target);  // Expensive
    }
}
```

**Recommendation:** Add to Phase 2 as training data source alongside XGBoost.

---

#### 8.2.3 Rule-Based Expert System

**Initial dismissal:** "Brittle, hard to tune"

**Revised assessment:** This IS the current VP/CP AI. The 20K+ lines in CvDiplomacyAI.cpp are a rule-based expert system.

| Factor | Analysis |
|--------|----------|
| Current state | Already implemented (entire AI codebase) |
| Brittleness | Yes, edge cases break rules |
| Tuning difficulty | Yes, changing one rule affects others |
| Interpretability | High — every decision is explainable |

**The question isn't "should we use it?" but "how do we improve it?"**

**Improvement strategies:**

1. **Learned thresholds** — Use ML to find optimal cutoff values
   ```cpp
   // Before: hardcoded
   if (strengthRatio > 1.5f) { /* attack */ }
   
   // After: learned from data
   if (strengthRatio > g_learnedAttackThreshold) { /* attack */ }
   ```

2. **Fuzzy logic** — Replace hard cutoffs with gradual membership
   ```cpp
   // Before: binary
   bool isStrong = (strength > 1000);
   
   // After: fuzzy
   float strongness = FuzzyMembership(strength, /*low*/500, /*high*/1500);
   // Returns 0.0 at 500, 1.0 at 1500, 0.5 at 1000
   ```

3. **Modular rules** — Isolate rules for easier tuning
   ```cpp
   // Each factor contributes independently
   float warScore = 0.0f;
   warScore += g_weights.strengthFactor * EvalStrength(target);
   warScore += g_weights.proximityFactor * EvalProximity(target);
   warScore += g_weights.opportunityFactor * EvalOpportunity(target);
   // Weights can be tuned/learned independently
   ```

**Recommendation:** Don't dismiss — this is the foundation. ML/LLM enhance it, not replace it.

---

#### 8.2.4 Genetic Algorithms (Evolutionary Optimization)

**Initial dismissal:** "Slow evolution, hard to explain"

**Revised assessment:**

| Factor | Analysis |
|--------|----------|
| Evolving full AI | ❌ Impractical (50+ hours per generation) |
| **Parameter tuning** | ✅ Viable for offline optimization |
| Fitness evaluation | Win rate, score, cities captured |
| Runtime use | ❌ Not for real-time decisions |

**Viable use case: Automated hyperparameter optimization**

```
┌─────────────────────────────────────────────────────────────────┐
│  GENETIC ALGORITHM FOR PARAMETER TUNING (Offline)               │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  1. Define tunable parameters (20-50 values):                   │
│     • Aggression thresholds                                     │
│     • Strength ratio cutoffs                                    │
│     • Diplomatic weight factors                                 │
│     • War target scoring multipliers                            │
│                                                                 │
│  2. Evolution loop:                                             │
│     • Population: 50 parameter sets                             │
│     • Fitness: Play 10 games each, measure win rate             │
│     • Selection: Keep top 20%                                   │
│     • Crossover + mutation: Generate next generation            │
│     • Repeat 50-100 generations                                 │
│                                                                 │
│  3. Time estimate:                                              │
│     • 50 individuals × 10 games × 30 min = 250 hours/generation │
│     • Run on cloud/cluster over weekend                         │
│     • Or use faster proxy (shorter games, fewer civs)           │
│                                                                 │
│  4. Output: Optimized parameter values for release build        │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

<!-- Future: Add PNG diagram to docs/images/genetic-tuning.png -->

**Recommendation:** Consider for automated balance tuning, not runtime AI.

---

### 8.3 Revised Approach Summary

| Approach | Original | Revised | Best Use Case |
|----------|----------|---------|---------------|
| **MCTS** | ❌ Not feasible | ⚠️ Limited | Tactical combat (2-3 turn lookahead) |
| **Neural Policy** | ❌ Too much data | ✅ Feasible | Imitation learning from AI/human logs |
| **Expert System** | ❌ Brittle | ✅ Current foundation | Improve with learned thresholds |
| **Genetic Algorithms** | ❌ Too slow | ⚠️ Offline only | Hyperparameter optimization |

### 8.4 Updated Implementation Considerations

Based on revised assessment, the implementation roadmap could include:

| Phase | Addition | Effort | Value |
|-------|----------|--------|-------|
| Phase 1.5 | Log AI decisions for imitation learning | +1 week | High |
| Phase 2 | Train imitation policy alongside XGBoost | +2 weeks | High |
| Phase 2.5 | Limited MCTS for tactical combat | +3 weeks | Medium |
| Offline | Genetic algorithm for parameter tuning | +2 weeks setup | Medium |

---

## 9. References & Related Work

### 9.1 Internal Documentation

- [AI_SYSTEMS_REVIEW.md](AI_SYSTEMS_REVIEW.md) — Issues backlog and improvement tracking
- `CvDiplomacyAI.cpp` — Core diplomatic decision-making
- `CvMilitaryAI.cpp` — Military threat assessment and targeting
- `CvGrandStrategyAI.cpp` — Long-term victory planning
- `CvTacticalAI.cpp` — Unit-level tactical decisions (MCTS candidate)

### 9.2 External References

**LLM Integration:**
- [Vox Deorum](https://github.com/CIVITAS-John/vox-deorum) — ⭐ Production-ready LLM integration for VP/CP
- [Vox Deorum Replayer](https://github.com/CIVITAS-John/vox-deorum-replay) — Session replay viewer
- [vscode-copilot-bridge](https://github.com/larsbaunwall/vscode-copilot-bridge) — Copilot API via localhost
- [llama.cpp](https://github.com/ggerganov/llama.cpp) — Efficient local LLM inference
- [LiteLLM](https://github.com/BerriAI/litellm) — Unified LLM API proxy

**Machine Learning:**
- [XGBoost](https://xgboost.readthedocs.io/) — Gradient boosted trees library
- [LightGBM](https://lightgbm.readthedocs.io/) — Fast gradient boosting framework
- [scikit-learn](https://scikit-learn.org/) — Python ML library for training
- [LoRA Fine-tuning](https://arxiv.org/abs/2106.09685) — Parameter-efficient training

**AI Techniques:**
- [MCTS Survey](https://ieeexplore.ieee.org/document/6145622) — Monte Carlo Tree Search methods
- [Behavioral Cloning](https://arxiv.org/abs/1011.0686) — Imitation learning fundamentals
- [CMA-ES](https://en.wikipedia.org/wiki/CMA-ES) — Evolution strategy for parameter optimization

**Community:**
- [Civ5 AI Analysis (CivFanatics)](https://forums.civfanatics.com/) — Community AI discussions

---

*Document created: February 2026*  
*Purpose: Architecture exploration for enhanced AI reasoning in Vox Populi*
