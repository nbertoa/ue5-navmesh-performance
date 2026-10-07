# NavMesh Performance — Unreal Engine 5.6 C++

A deliberately minimal C++ stress test comparing three pathfinding architectures in Unreal Engine: synchronous pathfinding in Tick, timer-driven pathfinding with a cache, and asynchronous pathfinding with a cache. The goal is to understand architectural trade-offs and Game Thread impact, not to present a hardware-normalized benchmark.

---

## The Problem

Calling `FindPathToActorSynchronously` inside `TickComponent` performs a synchronous navigation query every frame. At high instance counts, that work compounds quickly:

- 289 actor instances pathfinding synchronously every Tick
- ~38 ms of observed Game Thread work in the captured synchronous scenario, measured with Unreal Insights
- Scenario-level framerate drop from approximately 120 FPS to 12 FPS

These numbers should **not** be treated as directly convertible metrics. A complete 38 ms frame would correspond to roughly 26 FPS, while 12 FPS corresponds to roughly 83 ms per frame. The ~38 ms measurement therefore represents an observed Game Thread cost in the capture, not the complete end-to-end frame time.

The useful conclusion is that synchronous pathfinding was a major Game Thread bottleneck in this stress scenario; the experiment did not isolate every contributor to total frame time.

---

## Methodology and Scope

Known test conditions:

- **Engine:** Unreal Engine 5.6
- **Actors:** 289 `ANavTrackerActor` instances
- **Target:** one actor tagged `NavTarget`
- **Profiler:** Unreal Insights

Detailed hardware, build configuration, capture duration, warm-up, and repeated-run metadata were not recorded with the original experiment. For that reason, this project should be read as an architectural stress test rather than a controlled cross-hardware benchmark.

The three modes also do **not** execute an identical amount of navigation work. In particular, Timer + Cache intentionally lowers query frequency from potentially once per frame per component to 5 Hz per component.

---

## The Three Pathfinding Modes

The `UNavTargetTrackerComponent` exposes an `EPathfindingMode` enum that can be switched at runtime in the Editor Details panel — no recompile needed.

### Synchronous Baseline

Calls `FindPathToActorSynchronously` every Tick. This is deliberately expensive: each component can issue one synchronous NavMesh query per frame, making it a useful baseline for demonstrating how repeated synchronous work can consume the Game Thread budget.

### Timer + Cache

Uses an `FTimerHandle` to trigger a synchronous path recalculation at 5 Hz (every 0.2 s). The result is stored in a `TArray<FVector> CachedPathPoints`, and Tick only consumes the cached path.

At 120 FPS, the synchronous mode can theoretically request around 120 path calculations per second per component, while Timer + Cache requests 5. Across 289 components, that is up to roughly 34,680 versus 1,445 requests per second.

The performance benefit therefore comes partly from doing much less pathfinding work. The trade-off is path freshness: an actor can follow cached navigation data for roughly 0.2 seconds before the next synchronous refresh.

### Async + Cache

Uses `FindPathAsync` and receives the result through an `FNavPathQueryDelegate` callback. Unreal queues asynchronous pathfinding work for processing outside the calling Game Thread, while Tick consumes the latest cached result.

Async changes the scheduling model; it does not make pathfinding free. Results arrive later, the cached path can temporarily represent older state, request frequency still matters, and component lifetime/cancellation must be handled safely. The implementation tracks the current request ID and cancels pending work during teardown.

The practical value of this mode is decoupling path computation from the caller rather than claiming that the underlying navigation computation becomes cheaper.

---

## What the Experiment Demonstrates

The three modes illustrate different engineering trade-offs:

- **Synchronous Tick** — maximum update frequency, but repeated synchronous work can dominate the Game Thread.
- **Timer + Cache** — much lower query frequency in exchange for less-fresh paths.
- **Async + Cache** — decouples query completion from the caller in exchange for latency and additional lifecycle complexity.

The broader lesson is not “never use Tick.” Tick is often the correct place to consume continuously changing state. The problem is performing expensive synchronous work every frame when the gameplay requirement does not actually demand frame-level recomputation.

---

## Key Classes

### `UNavTargetTrackerComponent`

The core of the demo. An `ActorComponent` that pathfinds toward any `AActor` tagged `NavTarget` in the level.

- `PathfindingMode` — enum switch between `Synchronous`, `TimerCache`, and `Async`
- `CachedPathPoints` — `TArray<FVector>` written by the timer or async callback and consumed by Tick
- `AsyncPathRequestId` — tracks the current async request
- `OnPathFound` — delegate callback that receives an async result and updates the cache
- `EndPlay` — clears the timer and cancels any pending request
- `LogNavTargetTracker` — custom log category for filtered Output Log inspection

### `ANavTrackerActor`

A simple Actor that moves continuously across the NavMesh surface and owns the `UNavTargetTrackerComponent`. Constant movement forces path queries to start from a changing origin.

---

## Tech Stack

| | |
|---|---|
| **Engine** | Unreal Engine 5.6 |
| **Language** | C++ |
| **Modules** | NavigationSystem, AIModule |
| **Profiling** | Unreal Insights |

---

## Project Structure

```text
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

1. Clone the repository.
2. Open `NavMeshPerformance.uproject` with Unreal Engine 5.6.
3. Build the C++ project from Rider or Visual Studio.
4. Open `DemoLevel`.
5. Press **P** to verify NavMesh coverage.
6. Press **Play** with `PathfindingMode = Synchronous`.
7. Select all `BP_NavTrackerActor` instances in the Outliner and switch `PathfindingMode` in Details.
8. Compare behavior while profiling with Unreal Insights.

To inspect the Game Thread scenario, open **Tools → Run Unreal Insights** before Play and filter for the relevant navigation/component activity.

---

## About

**Nicolás Bertoa** — Senior R&D Prototyping Engineer with 15+ years of experience in technical R&D, focused on Unreal Engine, C++, prototyping, and real-time systems.

🌐 [Portfolio](https://nbertoa.com/) · 🎬 [Demo Reels](https://nbertoa.com/demo-reels/) · 💻 [GitHub](https://github.com/nbertoa)
