// Copyright 2018 Ryugibo, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/TextureFactory.h"

#include "giflib/gif_lib.h"

#include "GifFactory.generated.h"

struct FSpriteInfo
{
	FIntPoint Offset;

	FIntPoint Dimension;

	int32 Frame;
};

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

	bool DecodeGifDataToSpritesPackedTexture(UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, const TCHAR* Type, class FFeedbackContext* Warn, void* Data, class UPaperFlipbookFactory* FlipbookFactory);

	bool DecodeGifDataToSprites(UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, const TCHAR* Type, class FFeedbackContext* Warn, void* Data, class UPaperFlipbookFactory* FlipbookFactory);

	class UTexture2D* CreateTextureFromRawData(UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, class FFeedbackContext* Warn, const TArray<uint8>& InRawData, const GifWord& InWidth, const GifWord& InHeight);

	class UPaperSprite* CreatePaperSprite(UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, class FFeedbackContext* Warn, class UTexture2D* InitialTexture, const FIntPoint& InOffset = FIntPoint::ZeroValue, const FIntPoint& InDimension = FIntPoint::ZeroValue);

	class UPaperFlipbook* CreateFlipbook(UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, class FFeedbackContext* Warn, class UPaperFlipbookFactory* FlipbookFactory);

	static int OnReadGif(struct GifFileType* FileType, GifByteType* ByteType, int Length);

	// * Begin - Redefine private functions of UTextureFactory
	static bool IsImportResolutionValid(int32 Width, int32 Height, bool bAllowNonPowerOfTwo, class FFeedbackContext* Warn);
	// * End - Redfine private functions of UTextureFactory

	static int32 GifIndex;
	static int32 GifLength;
};
