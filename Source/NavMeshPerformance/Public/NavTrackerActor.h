#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NavTrackerActor.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogNavTrackerActor,
                            Log,
                            All);

/**
 * ANavTrackerActor
 *
 * Actor that moves continuously across the NavMesh surface,
 * forcing the NavTargetTrackerComponent to recalculate a full
 * path every frame via FindPathToActorSynchronously.
 */
UCLASS()
class NAVMESHPERFORMANCE_API ANavTrackerActor : public AActor
{
	GENERATED_BODY()

public:
	ANavTrackerActor();

protected:
	virtual void BeginPlay() override;

public:
	virtual void Tick(float DeltaTime) override;

private:
	/** Movement speed in units per second. */
	UPROPERTY(EditAnywhere,
		BlueprintReadOnly,
		Category = "Nav Tracker",
		meta=(AllowPrivateAccess="true"))
	float MovementSpeed = 500.0f;

	/** Current movement direction. Flips when reaching bounds. */
	FVector MovementDirection = FVector(1.0f,
	                                    0.0f,
	                                    0.0f);
};
