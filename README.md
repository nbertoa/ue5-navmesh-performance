# NavMesh Performance — Unreal Engine 5.6 C++

A C++ prototype built to demonstrate and measure the performance impact of synchronous NavMesh pathfinding called every Tick, and the step-by-step optimizations that eliminate the Game Thread stall. The scenario is intentionally minimal — hundreds of actors continuously pathfinding toward a single target — so the performance difference remains the subject under study, not the game design.

---

## The Problem

Calling `FindPathToActorSynchronously` inside `TickComponent` blocks the Game Thread until the full path across the NavMesh is calculated — every single frame. At high instance counts this compounds rapidly:

- 289 actor instances pathfinding synchronously every Tick
- Game Thread stall of ~38ms per frame measured with Unreal Insights
- Framerate drop from 120 FPS to 12 FPS

---

## The Three Pathfinding Modes

The `UNavTargetTrackerComponent` exposes an `EPathfindingMode` enum that can be switched at runtime in the Editor Details panel — no recompile needed.

### Synchronous (Buggy Baseline)

Calls `FindPathToActorSynchronously` every Tick. Blocks the Game Thread for the full duration of the NavMesh query on every frame. At high instance counts causes severe frame drops. This is the baseline that demonstrates the problem.

### Timer + Cache

Uses an `FTimerHandle` to trigger a synchronous path recalculation at 5Hz (every 0.2s) instead of every frame. The result is stored in a `TArray<FVector> CachedPathPoints`. Tick reads from this cache without touching the NavMesh at all. Reduces Game Thread stalls from 120x/sec to 5x/sec.

### Async

Launches `FindPathAsync` and receives the result via a delegate callback (`FNavPathQueryDelegate`). The NavMesh query runs on a background navigation thread — the Game Thread is never blocked. Tick reads `CachedPathPoints` exactly as in Timer + Cache mode, but the cache is written by the async callback instead of a timer-triggered synchronous call. Any in-flight request is cancelled before a new one is launched to prevent stale results from overwriting a more recent cache.

---

## Key Classes

### `UNavTargetTrackerComponent`

The core of the demo. An `ActorComponent` that pathfinds toward any `AActor` tagged `NavTarget` in the level.

- `PathfindingMode` — enum switch between `Synchronous`, `TimerCache`, and `Async`
- `CachedPathPoints` — `TArray<FVector>` written by the timer or async callback, read by Tick
- `AsyncPathRequestId` — tracks the in-flight async request to enable cancellation before re-querying
- `OnPathFound` — delegate callback invoked on the Game Thread when the async query completes
- `EndPlay` clears the timer and cancels any pending async request to prevent dangling callbacks
- Custom log category `LogNavTargetTracker` for filtered Output Log inspection per mode

### `ANavTrackerActor`

A simple Actor that moves continuously across the NavMesh surface and owns the `UNavTargetTrackerComponent`. Constant movement forces the pathfinder to recalculate from a changing origin on every query, preventing the engine from serving cached results.

---

## Tech Stack

|                |                                    |
|----------------|------------------------------------|
| **Engine**     | Unreal Engine 5.6                  |
| **Language**   | C++                                |
| **Modules**    | NavigationSystem, AIModule         |

---

## Project Structure

```
NavMeshPerformance/
├── Config/
├── Content/
│   └── Maps/
│       └── DemoLevel.umap       # Large NavMesh, 289 NavTrackerActors, one NavTarget
├── Source/
│   └── NavMeshPerformance/
│       ├── NavTargetTrackerComponent   # Core component — three pathfinding modes
│       ├── NavTrackerActor             # Moving actor that owns the component
│       └── NavMeshPerformance.Build.cs
└── NavMeshPerformance.uproject
```

---

## Getting Started

1. Clone the repository
2. Open `NavMeshPerformance.uproject` with Unreal Engine 5.6
3. Build the C++ project from Rider or Visual Studio
4. Open `DemoLevel`
5. Press **P** to verify the NavMesh coverage
6. Press **Play** — observe FPS with `PathfindingMode = Synchronous`
7. Select all `BP_NavTrackerActor` instances in the Outliner, switch `PathfindingMode` in Details
8. Observe the FPS recovery across all three modes

To profile the Game Thread stall, open **Tools → Run Unreal Insights** before pressing Play and filter by `NavTargetTracker`.

---

## About

**Nicolás Bertoa** — Unreal Engine developer with 15+ years of professional experience, focused on C++ and gameplay systems.

🌐 [Portfolio](https://nbertoa.wordpress.com) | 🎬 [Demo Reels](https://nbertoa.wordpress.com/demo-reels/)
