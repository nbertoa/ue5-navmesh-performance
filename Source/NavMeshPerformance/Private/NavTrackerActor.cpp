#include "NavTrackerActor.h"

DEFINE_LOG_CATEGORY(LogNavTrackerActor);

ANavTrackerActor::ANavTrackerActor()
{
	PrimaryActorTick.bCanEverTick = true;
}

void ANavTrackerActor::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogNavTrackerActor,
	       Log,
	       TEXT("[%s] NavTrackerActor started at location: %s"),
	       *GetName(),
	       *GetActorLocation().ToString());
}

void ANavTrackerActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Move the actor continuously across the NavMesh surface.
	// This forces the NavTargetTrackerComponent to recalculate
	// a full path every frame from a different origin point.
	FVector NewLocation = GetActorLocation() + MovementDirection * MovementSpeed * DeltaTime;

	// Flip direction every 50000 units to keep the actor within bounds.
	if (NewLocation.X > 50000.0f || NewLocation.X < -50000.0f)
	{
		MovementDirection *= -1.0f;
		UE_LOG(LogNavTrackerActor,
		       Verbose,
		       TEXT("[%s] Direction flipped at: %s"),
		       *GetName(),
		       *NewLocation.ToString());
	}

	SetActorLocation(NewLocation);
}
