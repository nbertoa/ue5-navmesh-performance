#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NavigationSystem.h"
#include "AITypes.h"
#include "NavMesh/NavMeshPath.h"
#include "NavTargetTrackerComponent.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogNavTargetTracker,
                            Log,
                            All);

namespace NavTargetTrackerConstants
{
	/** Tag used to identify the navigation target actor in the level. */
	static const FName NavTargetTag = FName("NavTarget");

	/** Interval in seconds between path recalculations in TimerCache mode. */
	static constexpr float PathUpdateInterval = 0.2f;
}

/**
 * EPathfindingMode
 *
 * Controls how UNavTargetTrackerComponent calculates the path to its target.
 * Switch between modes at runtime in the Editor Details panel to compare performance live.
 */
UENUM(BlueprintType)
enum class EPathfindingMode : uint8
{
	/**
	 * Calls FindPathToActorSynchronously every Tick.
	 * Blocks the Game Thread for the full duration of the NavMesh query — every frame.
	 * At high instance counts causes severe frame drops. This is the BUGGY baseline.
	 */
	Synchronous UMETA(DisplayName = "Synchronous (Buggy)"),

	/**
	 * Uses an FTimerHandle to recalculate the path synchronously at 5Hz.
	 * Tick reads the cached result — no NavMesh query per frame.
	 * First optimization: reduces Game Thread stalls from 120x/sec to 5x/sec.
	 */
	TimerCache UMETA(DisplayName = "Timer + Cache"),

	/**
	 * Launches FindPathToActorAsynchronously and receives the result via delegate.
	 * The NavMesh query runs on a background thread — the Game Thread is never blocked.
	 * Tick reads the cached result exactly as in TimerCache mode.
	 * Best optimization: zero Game Thread cost for path recalculation.
	 */
	Async UMETA(DisplayName = "Async")
};

/**
 * UNavTargetTrackerComponent
 *
 * Demonstrates three approaches to NavMesh pathfinding with measurable performance impact.
 *
 * Synchronous mode: FindPathToActorSynchronously in Tick — blocks Game Thread every frame.
 * TimerCache mode:  FTimerHandle at 5Hz + synchronous query + cached result.
 * Async mode:       FindPathToActorAsynchronously — query on background thread, result via delegate.
 *
 * In all three modes, the path result is available via CachedPathPoints.
 * Toggle PathfindingMode at runtime in the Editor to compare performance live with Unreal Insights.
 */
UCLASS(ClassGroup=(Custom),
	meta=(BlueprintSpawnableComponent))
class NAVMESHPERFORMANCE_API UNavTargetTrackerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNavTargetTrackerComponent();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	virtual void TickComponent(float DeltaTime,
	                           ELevelTick TickType,
	                           FActorComponentTickFunction* ThisTickFunction) override;

private:
	/**
	 * Selects the pathfinding strategy used at runtime.
	 * Switch between modes in the Editor Details panel — no recompile needed.
	 */
	UPROPERTY(EditAnywhere,
		BlueprintReadWrite,
		Category = "Nav Target Tracker",
		meta=(AllowPrivateAccess="true"))
	EPathfindingMode PathfindingMode = EPathfindingMode::Synchronous;

	/** The target actor we are pathfinding toward. Set in BeginPlay via tag lookup. */
	UPROPERTY()
	TObjectPtr<AActor> NavTarget;

	/** Navigation system reference cached on BeginPlay. */
	UPROPERTY()
	TObjectPtr<UNavigationSystemV1> NavSystem;

	/**
	 * Cached path points from the last recalculation.
	 * Written by RecalculatePath (TimerCache) or OnPathFound (Async).
	 * Read every Tick in TimerCache and Async modes — no NavMesh query involved.
	 */
	UPROPERTY()
	TArray<FVector> CachedPathPoints;

	/** Timer handle that triggers RecalculatePath at PathUpdateInterval in TimerCache mode. */
	FTimerHandle PathUpdateTimerHandle;

	/**
	 * ID of the last async path request.
	 * Stored so we can cancel an in-flight request before launching a new one,
	 * preventing stale results from overwriting a more recent cache.
	 */
	uint32 AsyncPathRequestId = FAIRequestID::InvalidRequest;

	/**
	 * Synchronous path recalculation called by the timer in TimerCache mode.
	 * Writes the result to CachedPathPoints. Never called from Tick directly.
	 */
	void RecalculatePath();

	/**
	 * Launches an async path request to NavTarget.
	 * Cancels any previously in-flight request before launching a new one.
	 * Called from Tick in Async mode.
	 */
	void RequestAsyncPath();

	/**
	 * Delegate callback invoked by the NavSystem when an async path query completes.
	 * Writes the result to CachedPathPoints if the path is valid and the component
	 * is still alive. Runs on the Game Thread — safe to write UPROPERTY members.
	 *
	 * @param PathParams   The original query parameters.
	 * @param PathResult   The computed path result, including success/failure status.
	 */
	void OnPathFound(uint32 RequestId,
	                 ENavigationQueryResult::Type ResultType,
	                 FNavPathSharedPtr FoundPath);
};
