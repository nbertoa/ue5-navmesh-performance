#include "NavTargetTrackerComponent.h"

#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "NavMesh/NavMeshPath.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "AI/Navigation/NavigationTypes.h"

DEFINE_LOG_CATEGORY(LogNavTargetTracker);

UNavTargetTrackerComponent::UNavTargetTrackerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UNavTargetTrackerComponent::BeginPlay()
{
	Super::BeginPlay();

	// Cache the navigation system once — shared across all three pathfinding modes.
	NavSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!IsValid(NavSystem))
	{
		UE_LOG(LogNavTargetTracker,
		       Error,
		       TEXT("[%s] NavSystem not found. Component will not function."),
		       *GetOwner()->GetName());
		return;
	}

	// Find the nav target by tag.
	TArray<AActor*> FoundActors;
	UGameplayStatics::GetAllActorsWithTag(GetWorld(),
	                                      NavTargetTrackerConstants::NavTargetTag,
	                                      FoundActors);

	if (FoundActors.IsEmpty())
	{
		UE_LOG(LogNavTargetTracker,
		       Warning,
		       TEXT("[%s] No actor with tag 'NavTarget' found in level."),
		       *GetOwner()->GetName());
		return;
	}

	NavTarget = FoundActors[0];
	UE_LOG(LogNavTargetTracker,
	       Log,
	       TEXT("[%s] Nav target set to: %s"),
	       *GetOwner()->GetName(),
	       *NavTarget->GetName());

	// Start the timer unconditionally — RecalculatePath guards against inactive modes.
	// This avoids restarting the timer every time PathfindingMode changes at runtime.
	GetWorld()->GetTimerManager().SetTimer(PathUpdateTimerHandle,
	                                       this,
	                                       &UNavTargetTrackerComponent::RecalculatePath,
	                                       NavTargetTrackerConstants::PathUpdateInterval,
	                                       true // looping
	);
}

void UNavTargetTrackerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	// Clear the timer to prevent dangling callbacks after the component is destroyed.
	if (GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(PathUpdateTimerHandle);
	}

	// Cancel any in-flight async request — the callback must not fire after EndPlay.
	if (IsValid(NavSystem) && AsyncPathRequestId != FAIRequestID::InvalidRequest)
	{
		NavSystem->AbortAsyncFindPathRequest(AsyncPathRequestId);
		AsyncPathRequestId = FAIRequestID::InvalidRequest;
	}
}

void UNavTargetTrackerComponent::TickComponent(float DeltaTime,
                                               ELevelTick TickType,
                                               FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime,
	                     TickType,
	                     ThisTickFunction);

	if (!IsValid(NavSystem) || !IsValid(NavTarget) || !IsValid(GetOwner()))
	{
		UE_LOG(LogNavTargetTracker,
		       Warning,
		       TEXT("Early return — invalid references"));
		return;
	}

	switch (PathfindingMode)
	{
	case EPathfindingMode::Synchronous:
		{
			// BUGGY: Blocks the Game Thread for the full NavMesh query — every frame.
			// At 120 FPS with hundreds of instances this causes severe frame drops.
			UNavigationPath* Path = NavSystem->FindPathToActorSynchronously(GetWorld(),
			                                                                GetOwner()->GetActorLocation(),
			                                                                NavTarget);

			if (!IsValid(Path))
			{
				UE_LOG(LogNavTargetTracker,
				       Warning,
				       TEXT("[%s] [Synchronous] Path could not be calculated."),
				       *GetOwner()->GetName());
				return;
			}

			UE_LOG(LogNavTargetTracker,
			       Verbose,
			       TEXT("[%s] [Synchronous] Path recalculated. Points: %d"),
			       *GetOwner()->GetName(),
			       Path->PathPoints.Num());
			break;
		}

	case EPathfindingMode::TimerCache:
		{
			// OPTIMIZED: Read the cache written by RecalculatePath at 5Hz.
			// No NavMesh query here — negligible cost per frame.
			if (CachedPathPoints.IsEmpty())
			{
				return;
			}

			UE_LOG(LogNavTargetTracker,
			       Verbose,
			       TEXT("[%s] [TimerCache] Using cached path. Points: %d"),
			       *GetOwner()->GetName(),
			       CachedPathPoints.Num());
			break;
		}

	case EPathfindingMode::Async:
		{
			// BEST: Launch a background path request if none is in flight.
			// The Game Thread is never blocked — OnPathFound writes the cache
			// when the background thread completes the query.
			if (AsyncPathRequestId == FAIRequestID::InvalidRequest)
			{
				RequestAsyncPath();
			}

			// Read the cache written by OnPathFound — same pattern as TimerCache.
			if (CachedPathPoints.IsEmpty())
			{
				return;
			}

			UE_LOG(LogNavTargetTracker,
			       Verbose,
			       TEXT("[%s] [Async] Using cached path. Points: %d"),
			       *GetOwner()->GetName(),
			       CachedPathPoints.Num());
			break;
		}
	}
}

void UNavTargetTrackerComponent::RecalculatePath()
{
	// Guard: only runs in TimerCache mode. Timer fires regardless of active mode
	// to avoid restart overhead when switching modes at runtime.
	if (PathfindingMode != EPathfindingMode::TimerCache)
	{
		return;
	}

	if (!IsValid(NavSystem) || !IsValid(NavTarget) || !IsValid(GetOwner()))
	{
		return;
	}

	UNavigationPath* Path = NavSystem->FindPathToActorSynchronously(GetWorld(),
	                                                                GetOwner()->GetActorLocation(),
	                                                                NavTarget);

	if (!IsValid(Path))
	{
		UE_LOG(LogNavTargetTracker,
		       Warning,
		       TEXT("[%s] [TimerCache] Path recalculation failed."),
		       *GetOwner()->GetName());
		return;
	}

	CachedPathPoints = Path->PathPoints;

	UE_LOG(LogNavTargetTracker,
	       Verbose,
	       TEXT("[%s] [TimerCache] Path cached. Points: %d"),
	       *GetOwner()->GetName(),
	       CachedPathPoints.Num());
}

void UNavTargetTrackerComponent::RequestAsyncPath()
{
	if (!IsValid(NavSystem) || !IsValid(NavTarget) || !IsValid(GetOwner()))
	{
		return;
	}

	// Cancel any previously in-flight request before launching a new one.
	// Without this, a slow previous query could overwrite a more recent cache
	// with a stale result computed from an older origin position.
	if (AsyncPathRequestId != FAIRequestID::InvalidRequest)
	{
		NavSystem->AbortAsyncFindPathRequest(AsyncPathRequestId);
		AsyncPathRequestId = FAIRequestID::InvalidRequest;
	}

	// Build the query: from current owner location toward NavTarget's location.
	const FNavAgentProperties& AgentProps = FNavAgentProperties::DefaultProperties;
	const ANavigationData* NavData = NavSystem->GetNavDataForProps(AgentProps,
	                                                               GetOwner()->GetActorLocation());

	if (!NavData)
	{
		UE_LOG(LogNavTargetTracker,
		       Warning,
		       TEXT("[%s] [Async] No NavData found for agent properties."),
		       *GetOwner()->GetName());
		return;
	}

	FPathFindingQuery Query(GetOwner(),
	                        *NavData,
	                        GetOwner()->GetActorLocation(),
	                        NavTarget->GetActorLocation());

	// Launch the async request. OnPathFound will be called on the Game Thread
	// when the background navigation thread completes the query.
	AsyncPathRequestId = NavSystem->FindPathAsync(AgentProps,
	                                              Query,
	                                              FNavPathQueryDelegate::CreateUObject(this,
		                                              &UNavTargetTrackerComponent::OnPathFound));

	UE_LOG(LogNavTargetTracker,
	       Verbose,
	       TEXT("[%s] [Async] Path request launched. RequestId: %d"),
	       *GetOwner()->GetName(),
	       AsyncPathRequestId);
}

void UNavTargetTrackerComponent::OnPathFound(uint32 RequestId,
                                             ENavigationQueryResult::Type ResultType,
                                             FNavPathSharedPtr FoundPath)
{
	// Mark the request as completed — Tick can now launch a new one next frame.
	AsyncPathRequestId = FAIRequestID::InvalidRequest;

	// Validate that the component and owner are still alive.
	// The callback fires on the Game Thread but the actor may have been destroyed
	// between the request being launched and the result arriving.
	if (!IsValid(this) || !IsValid(GetOwner()))
	{
		return;
	}

	if (ResultType != ENavigationQueryResult::Success || !FoundPath.IsValid())
	{
		UE_LOG(LogNavTargetTracker,
		       Warning,
		       TEXT("[%s] [Async] Path query failed. Result: %d"),
		       *GetOwner()->GetName(),
		       static_cast<int32>(ResultType));
		return;
	}

	// Extract path points from the raw FNavPath into our cached TArray<FVector>.
	// FNavPath::GetPathPoints returns TArray<FNavPathPoint>, each with a Location field.
	const TArray<FNavPathPoint>& NavPoints = FoundPath->GetPathPoints();
	CachedPathPoints.Reset(NavPoints.Num());
	for (const FNavPathPoint& Point : NavPoints)
	{
		CachedPathPoints.Add(Point.Location);
	}

	UE_LOG(LogNavTargetTracker,
	       Verbose,
	       TEXT("[%s] [Async] Path received and cached. Points: %d"),
	       *GetOwner()->GetName(),
	       CachedPathPoints.Num());
}
