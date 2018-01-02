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

	virtual UObject* FactoryCreateBinary(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, const TCHAR* Type, const uint8*& Buffer, const uint8* BufferEnd, class FFeedbackContext* Warn) override;

private:

	bool DecodeGifDataToSprites(const void* Data, int32 Size, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, const TCHAR* Type, const uint8*& Buffer, const uint8* BufferEnd, class FFeedbackContext* Warn, class UPaperFlipbookFactory* FlipbookFactory);

	class UTexture2D* CreateTextureFromRawData(const TArray<uint8>& InRawData, const GifWord& InWidth, const GifWord& InHeight, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, class FFeedbackContext* Warn);

	class UPaperSprite* CreatePaperSprite(class UTexture2D* InitialTexture, const FVector2D& Pivot, UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, class FFeedbackContext* Warn);

	class UPaperFlipbook* CreateFlipbook(UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, class FFeedbackContext* Warn, class UPaperFlipbookFactory* FlipbookFactory);

	static int OnReadGif(GifFileType* FileType, GifByteType* ByteType, int Length);

	// * Begin - Redefine private functions of UTextureFactory
	static bool IsImportResolutionValid(int32 Width, int32 Height, bool bAllowNonPowerOfTwo, class FFeedbackContext* Warn);
	// * End - Redfine private functions of UTextureFactory

	static TArray<uint8> GifBytes;
	static int32 GifIndex;
};