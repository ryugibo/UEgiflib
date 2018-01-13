// Fill out your copyright notice in the Description page of Project Settings.

#include "GifReimportFactory.h"





bool UGifReimportFactory::CanReimport(UObject* Obj, TArray<FString>& OutFilenames)
{
	return false;
}

void UGifReimportFactory::SetReimportPaths(UObject* Obj, const TArray<FString>& NewReimportPaths)
{

}

EReimportResult::Type UGifReimportFactory::Reimport(UObject* Obj)
{
	return EReimportResult::Failed;
}

int32 UGifReimportFactory::GetPriority() const
{
	return ImportPriority;
}