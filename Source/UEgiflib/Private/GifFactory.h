// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Factories/TextureFactory.h"

THIRD_PARTY_INCLUDES_START
#include "giflib/lib/gif_lib.h"
THIRD_PARTY_INCLUDES_END

#include "GifFactory.generated.h"

/**
 * 
 */
UCLASS()
class UGifFactory : public UTextureFactory
{
	GENERATED_UCLASS_BODY()
	
	
public:

	virtual UObject* FactoryCreateBinary(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, const TCHAR* Type, const uint8*& Buffer, const uint8* BufferEnd, FFeedbackContext* Warn) override;

private:

	static int OnReadGif(GifFileType*, GifByteType*, int);

	static TArray<uint8> GifBytes;
	static int32 GifIndex;


	
	UTexture2D* DecodeGifData(const void* Data, int32 Size, UObject* InParent, FName Name, EObjectFlags Flags);
};