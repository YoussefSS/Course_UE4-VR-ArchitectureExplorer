// Fill out your copyright notice in the Description page of Project Settings.


#include "Public/HandController.h"
#include "Haptics\HapticFeedbackEffect_Base.h"
#include "GameFramework\Pawn.h"
#include "GameFramework\PlayerController.h"

// Sets default values
AHandController::AHandController()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;


	MotionController = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("MotionController"));
	SetRootComponent(MotionController);


	
}

// Called when the game starts or when spawned
void AHandController::BeginPlay()
{
	Super::BeginPlay();

	MotionController->bDisplayDeviceModel = true; // Shows the default controller mesh
	OnActorBeginOverlap.AddDynamic(this, &AHandController::ActorBeginOverlap);
	OnActorEndOverlap.AddDynamic(this, &AHandController::ActorEndOverlap);
}

// Called every frame
void AHandController::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

void AHandController::ActorBeginOverlap(AActor* OverlappedActor, AActor* OtherActor)
{
	bool bNewCanClimb = CanClimb();
	if (!bCanClimb && bNewCanClimb)
	{
		APawn* Pawn = Cast<APawn>(GetAttachParentActor());
		if (Pawn)
		{
			APlayerController* Controller = Cast<APlayerController>(Pawn->GetController());
			if (Controller)
			{
				if (HapticEffect)
				{
					Controller->PlayHapticEffect(HapticEffect, MotionController->GetTrackingSource());
				}
			}
		}
	}
	bCanClimb = bNewCanClimb;

}

void AHandController::ActorEndOverlap(AActor* OverlappedActor, AActor* OtherActor)
{
	bCanClimb = CanClimb();

}

bool AHandController::CanClimb() const
{
	TArray<AActor*> OverlappingActors;
	GetOverlappingActors(OverlappingActors);
	for (AActor* OverlappingActor : OverlappingActors)
	{
		if (OverlappingActor->ActorHasTag(TEXT("Climbable")))
		{
			return true;
		}
	}

	return false;
}
