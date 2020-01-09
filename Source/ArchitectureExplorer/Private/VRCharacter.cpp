// Fill out your copyright notice in the Description page of Project Settings.


#include "Public\VRCharacter.h"
#include "Camera\CameraComponent.h"
#include "Components\InputComponent.h"
#include "Components\StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework\PlayerController.h"
#include "TimerManager.h"
#include "Components\CapsuleComponent.h"
#include "NavigationSystem.h"
#include "Components\PostProcessComponent.h"
#include "Materials\MaterialInstanceDynamic.h"
#include "MotionControllerComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Components\SplineComponent.h"
#include "Components\SplineMeshComponent.h"

#include "Public/HandController.h"

// Sets default values
AVRCharacter::AVRCharacter()
{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	VRRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VRRoot"));
	VRRoot->SetupAttachment(GetRootComponent());

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(VRRoot);


	TeleportPath = CreateDefaultSubobject<USplineComponent>(TEXT("TeleportPath"));
	TeleportPath->SetupAttachment(VRRoot);

	DestinationMarker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DestinationMarker"));
	DestinationMarker->SetupAttachment(GetRootComponent());

	PostProcessComponent = CreateDefaultSubobject<UPostProcessComponent>(TEXT("PostProcessComponent"));
	PostProcessComponent->SetupAttachment(GetRootComponent());
}

// Called when the game starts or when spawned
void AVRCharacter::BeginPlay()
{
	Super::BeginPlay();
	
	DestinationMarker->SetVisibility(false);

	if (BlinkerMaterialBase)
	{
		BlinkerMaterialInstance = UMaterialInstanceDynamic::Create(BlinkerMaterialBase, this);
		PostProcessComponent->AddOrUpdateBlendable(BlinkerMaterialInstance);
	}
	
	LeftController = GetWorld()->SpawnActor<AHandController>(HandControllerClass);
	if (LeftController != nullptr)
	{
		LeftController->AttachToComponent(VRRoot, FAttachmentTransformRules::KeepRelativeTransform);
		LeftController->SetOwner(this);
		LeftController->SetHand(EControllerHand::Left);
	}

	RightController = GetWorld()->SpawnActor<AHandController>(HandControllerClass);
	if (RightController != nullptr)
	{
		RightController->AttachToComponent(VRRoot, FAttachmentTransformRules::KeepRelativeTransform);
		RightController->SetOwner(this);
		RightController->SetHand(EControllerHand::Right);
	}
}

// Called every frame
void AVRCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	FVector NewCameraOffset = Camera->GetComponentLocation() - GetActorLocation();
	NewCameraOffset.Z = 0;
	AddActorWorldOffset(NewCameraOffset);
	VRRoot->AddWorldOffset(-NewCameraOffset);
	
	UpdateDestinationMarker();
	UpdateBlinkers();
}

bool AVRCharacter::FindTeleportDestination(TArray<FVector> &OutPath, FVector& OutLocation)
{
	FVector Start = RightController->GetActorLocation();
	FVector Look = RightController->GetActorForwardVector();

	
	FPredictProjectilePathParams Params(
		TeleportProjectileRadius, 
		Start, 
		Look * TeleportProjectileSpeed, 
		TeleportSimulationTime, 
		ECollisionChannel::ECC_Visibility, 
		this
	);
	Params.bTraceComplex = true; // We set this to true because we don't have simple collisions on the floor and this leads to us not being able to teleport to some areas, usually you don't need this
	FPredictProjectilePathResult Result;
	bool bHit = UGameplayStatics::PredictProjectilePath(this, Params, Result);

	if (!bHit) return false;

	for (FPredictProjectilePathPointData PointData : Result.PathData)
	{
		OutPath.Add(PointData.Location);
	}

	//UNavigationSystemV1* NavSystem = UNavigationSystemV1::GetNavigationSystem(GetWorld());
	FNavLocation NavLocation;
	bool bOnNavMesh = UNavigationSystemV1::GetNavigationSystem(GetWorld())->ProjectPointToNavigation(Result.HitResult.Location, NavLocation, TeleportProjectionExtent);

	if (!bOnNavMesh) return false;

	OutLocation = NavLocation.Location;
	return true;
}

void AVRCharacter::UpdateDestinationMarker()
{
	TArray<FVector> Path;
	FVector Location;
	bool bHasDestination = FindTeleportDestination(Path, Location);

	if (bHasDestination)
	{
		DestinationMarker->SetVisibility(true);
		DestinationMarker->SetWorldLocation(Location);

		DrawTeleportPath(Path);
	}
	else
	{
		DestinationMarker->SetVisibility(false);

		TArray<FVector> EmptyPath;
		DrawTeleportPath(EmptyPath); // Draw no path (sets visibility to false and won't go in the 2nd loop)
	}
}

void AVRCharacter::UpdateBlinkers()
{
	if (!RadiusVsVelocity) return;

	float Speed = GetVelocity().Size();
	float Radius = RadiusVsVelocity->GetFloatValue(Speed);

	if (BlinkerMaterialInstance)
	{
		BlinkerMaterialInstance->SetScalarParameterValue(TEXT("Radius"), Radius);
	}	

	FVector2D Center = GetBlinkerCenter();
	BlinkerMaterialInstance->SetVectorParameterValue(TEXT("Center"), FLinearColor(Center.X, Center.Y, 0)); ;
}

void AVRCharacter::DrawTeleportPath(const TArray<FVector>& Path)
{
	UpdateSpline(Path);

	for (USplineMeshComponent* SplineMesh : TeleportPathMeshPool) // Setting everything to invisible first, this solves the problem of if you aren't pointing at anything and the spline mesh is still there..
	{ // .. this makes only the things usable by the pool visible
		SplineMesh->SetVisibility(false);
	}

	int32 SegmentNum = Path.Num() - 1;
	for (int32 i = 0; i < SegmentNum; ++i) // Object pool
	{
		if (TeleportPathMeshPool.Num() <= i)
		{
			USplineMeshComponent* SplineMesh = NewObject<USplineMeshComponent>(this);
			SplineMesh->SetMobility(EComponentMobility::Movable);
			SplineMesh->AttachToComponent(TeleportPath, FAttachmentTransformRules::KeepRelativeTransform); // Could just parent to RightController
			SplineMesh->RegisterComponent(); // We register components created dynamically, we don't in constructor because it's done automatically there.
			SplineMesh->SetStaticMesh(TeleportArchMesh);
			SplineMesh->SetMaterial(0, TeleportArcMaterial);

			TeleportPathMeshPool.Add(SplineMesh);
		}
		

		USplineMeshComponent* SplineMesh = TeleportPathMeshPool[i];
		SplineMesh->SetVisibility(true); // Setting the meshes usable by the pool to be visible

		FVector StartPos, StartTangent, EndPos, EndTangent;
		TeleportPath->GetLocalLocationAndTangentAtSplinePoint(i, StartPos, StartTangent);
		TeleportPath->GetLocalLocationAndTangentAtSplinePoint(i+1, EndPos, EndTangent);
		SplineMesh->SetStartAndEnd(StartPos, StartTangent, EndPos, EndTangent);
	}

}

void AVRCharacter::UpdateSpline(const TArray<FVector>& Path)
{
	TeleportPath->ClearSplinePoints(false);
	
	for (int32 i = 0; i < Path.Num(); ++i) // We use i as an input key (identifier)
	{
		TeleportPath->AddSplinePoint(Path[i], ESplineCoordinateSpace::World, false);
		/** Use: */
		//FVector LocalPosition = TeleportPath->GetComponentTransform().InverseTransformPosition(Path[i]);
		//FSplinePoint Point(i, Path[i], ESplinePointType::Curve); // ESplinePointType can be linear, curve, etc
		//TeleportPath->AddPoint(Point, false);
		
		/** Or use this better: TeleportPath->AddSplineWorldPoint(Path[i]); */
	}

	TeleportPath->UpdateSpline();
}

FVector2D AVRCharacter::GetBlinkerCenter()
{
	FVector MovementDirection = GetVelocity().GetSafeNormal();
	if (MovementDirection.IsNearlyZero())
	{
		return FVector2D(0.5, 0.5);
	}

	FVector WorldStationaryLocation;
	if (FVector::DotProduct(Camera->GetForwardVector(), MovementDirection) > 0)
	{
		WorldStationaryLocation = Camera->GetComponentLocation() + MovementDirection * 1000;
	}
	else
	{
		WorldStationaryLocation = Camera->GetComponentLocation() - MovementDirection * 1000;
	}

	APlayerController* PC = Cast<APlayerController>(GetController());
	if (PC == nullptr)
	{
		return FVector2D(0.5, 0.5);
	}

	FVector2D ScreenStationaryLocation;
	PC->ProjectWorldLocationToScreen(WorldStationaryLocation, ScreenStationaryLocation);

	int32 SizeX, SizeY;
	PC->GetViewportSize(SizeX, SizeY);
	ScreenStationaryLocation.X /= SizeX;
	ScreenStationaryLocation.Y /= SizeY;

	return ScreenStationaryLocation;
}

// Called to bind functionality to input
void AVRCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	PlayerInputComponent->BindAxis(TEXT("Forward"), this, &AVRCharacter::MoveForward);
	PlayerInputComponent->BindAxis(TEXT("Right"), this, &AVRCharacter::MoveRight);
	PlayerInputComponent->BindAction(TEXT("Teleport"), EInputEvent::IE_Pressed, this, &AVRCharacter::BeginTeleport);

	PlayerInputComponent->BindAction(TEXT("GripLeft"), EInputEvent::IE_Pressed, this, &AVRCharacter::GripLeft);
	PlayerInputComponent->BindAction(TEXT("GripLeft"), EInputEvent::IE_Released, this, &AVRCharacter::ReleaseLeft);

	PlayerInputComponent->BindAction(TEXT("GripRight"), EInputEvent::IE_Pressed, this, &AVRCharacter::GripRight);
	PlayerInputComponent->BindAction(TEXT("GripRight"), EInputEvent::IE_Released, this, &AVRCharacter::ReleaseRight);
}

void AVRCharacter::MoveForward(float throttle)
{
	AddMovementInput(Camera->GetForwardVector(), throttle);
}

void AVRCharacter::MoveRight(float throttle)
{
	AddMovementInput(Camera->GetRightVector(), throttle);
}

void AVRCharacter::BeginTeleport()
{
	StartFade(0, 1);

	FTimerHandle TimerHandle_FadeTimer;
	GetWorldTimerManager().SetTimer(TimerHandle_FadeTimer, this, &AVRCharacter::FinishTeleport, TeleportFadeTime, false);
}

void AVRCharacter::FinishTeleport()
{
	FVector PlayerHeight = FVector(0,0,GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
	SetActorLocation(DestinationMarker->GetComponentLocation() + PlayerHeight);
	
	StartFade(1, 0);
}

void AVRCharacter::StartFade(float FromAlpha, float ToAlpha)
{
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (PC)
	{
		PC->PlayerCameraManager->StartCameraFade(FromAlpha, ToAlpha, TeleportFadeTime, FLinearColor::Black);
	}
}

