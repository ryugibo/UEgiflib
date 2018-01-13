// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GifFactory.h"
#include "EditorReimportHandler.h"
#include "GifReimportFactory.generated.h"

/**
 * 
 */
UCLASS()
class UGifReimportFactory : public UGifFactory, public FReimportHandler
{
	GENERATED_BODY()

	//~ Begin FReimportHandler Interface
	virtual bool CanReimport(UObject* Obj, TArray<FString>& OutFilenames) override;
	virtual void SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths) override;
	virtual EReimportResult::Type Reimport(UObject* Obj) override;
	virtual int32 GetPriority() const override;
	//~ End FReimportHandler Interface
};
