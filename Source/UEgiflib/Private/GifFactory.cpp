// Fill out your copyright notice in the Description page of Project Settings.

#include "GifFactory.h"

#include "UEgiflib.h"

#include "Engine/Texture2D.h"

#include "IImageWrapper.h"
#include "IImageWrapperModule.h"

#include "Misc/FeedbackContext.h"
#include "Misc/MessageDialog.h"

#include "PaperFlipbook.h"
#include "PaperFlipbookFactory.h"
#include "PaperFlipbookHelpers.h"
#include "PaperImporterSettings.h"
#include "PaperSprite.h"

#include "RHIDefinitions.h"

#include "SpriteEditorOnlyTypes.h"

THIRD_PARTY_INCLUDES_START
#include "giflib/lib/gif_lib_private.h"
THIRD_PARTY_INCLUDES_END

#define LOCTEXT_NAMESPACE "GifFactories"

TArray<uint8> UGifFactory::GifBytes;
int32 UGifFactory::GifIndex;

UGifFactory::UGifFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UPaperFlipbook::StaticClass();

	Formats.Empty();
	Formats.Add(TEXT("gif;Flipbook"));
}

UObject* UGifFactory::FactoryCreateBinary
(
	UClass*				Class,
	UObject*			InParent,
	FName				Name,
	EObjectFlags		Flags,
	UObject*			Context,
	const TCHAR*		Type,
	const uint8*&		Buffer,
	const uint8*		BufferEnd,
	FFeedbackContext*	Warn
)
{
	const int32 Length = BufferEnd - Buffer;
	GifIndex = 0;
	GifBytes.AddUninitialized(Length);	// Allocate Empty Space
	FMemory::Memcpy(GifBytes.GetData(), Buffer, Length);

	TArray<UPaperSprite*> Sprites;
	if (!DecodeGifDataToSprites(Buffer, Length, InParent, Name, Flags, Context, Type, Buffer, BufferEnd, Warn, &Sprites))
	{
		UE_LOG(LogGiflib, Error, TEXT("Failed DecodeGifDataToSprites"));
		return nullptr;
	}

	UPaperFlipbook* Flipbook = CreateFlipbook(InParent, Name, Flags, Context, Warn, Sprites);

	return Flipbook;
}

bool UGifFactory::DecodeGifDataToSprites(const void* Data, int32 Size, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, const TCHAR* Type, const uint8*& Buffer, const uint8*		BufferEnd, FFeedbackContext* Warn, TArray<class UPaperSprite*>* OutSprites)
{
	int ErrorCode;
	GifFileType* FileType = DGifOpen((void *)Data, UGifFactory::OnReadGif, &ErrorCode);
	if (FileType == nullptr)
	{
		UE_LOG(LogGiflib, Error, TEXT("DGifOpen Failed : %d"), ErrorCode);
		return false;
	}
	if (DGifSlurp(FileType) == GIF_ERROR)
	{
		return false;
	}

	for (int i = 0; i < FileType->ImageCount; i++)
	{
		SavedImage& Frame = FileType->SavedImages[i];

		GifWord ImageLeft, ImageTop, ImageHeight, ImageWidth;

		ImageHeight = Frame.ImageDesc.Height;
		ImageWidth = Frame.ImageDesc.Width;
		ImageLeft = Frame.ImageDesc.Left;
		ImageTop = Frame.ImageDesc.Top;

		TArray<uint8> Image;
		Image.AddUninitialized(ImageWidth * ImageHeight * 4);

		bool HasGCB = false;
		GraphicsControlBlock GCB = { 0, false, 0, -1 };
		for (int ext_i = 0; ext_i < Frame.ExtensionBlockCount; ext_i++)
		{
			if (GIF_OK == DGifExtensionToGCB(Frame.ExtensionBlocks[ext_i].ByteCount, Frame.ExtensionBlocks[ext_i].Bytes, &GCB))
			{
				HasGCB = true;
			}
		}
		ColorMapObject* CMO = Frame.ImageDesc.ColorMap ? Frame.ImageDesc.ColorMap : FileType->SColorMap;
		GifColorType* ColorMap = CMO->Colors;
		uint32 Raster_i = 0;

		if (Frame.ImageDesc.Interlace)
		{
			int InterlacedOffset[] = { 0, 4, 2, 1 };
			int InterlacedJumps[] = { 8, 8, 4, 2 };

			for (int pass_i = 0; pass_i < 4; pass_i++)
			{
				for (int height_i = InterlacedOffset[pass_i]; height_i < ImageHeight; height_i += InterlacedJumps[pass_i])
				{
					for (int width_i = 0; width_i < ImageWidth; width_i++)
					{
						unsigned char& Bit = Frame.RasterBits[Raster_i];
						if (Bit >= CMO->ColorCount)
						{
							break;
						}
						Image[Raster_i * 4 + 0] = ColorMap[Bit].Blue;
						Image[Raster_i * 4 + 1] = ColorMap[Bit].Green;
						Image[Raster_i * 4 + 2] = ColorMap[Bit].Red;
						Image[Raster_i * 4 + 3] = (HasGCB && (int)Bit == GCB.TransparentColor) ? 0 : 255;

						Raster_i++;
					}
				}
			}
		}
		else
		{
			for (int height_i = 0; height_i < ImageHeight; height_i++)
			{
				for (int width_i = 0; width_i < ImageWidth; width_i++)
				{
					unsigned char& Bit = Frame.RasterBits[Raster_i];
					if (Bit >= CMO->ColorCount)
					{
						break;
					}
					Image[Raster_i * 4 + 0] = ColorMap[Bit].Blue;
					Image[Raster_i * 4 + 1] = ColorMap[Bit].Green;
					Image[Raster_i * 4 + 2] = ColorMap[Bit].Red;
					Image[Raster_i * 4 + 3] = (HasGCB && (int)Bit == GCB.TransparentColor) ? 0 : 255;

					Raster_i++;
				}
			}
		}

		FString SourceName = FString::Printf(TEXT("%s_%d"), *Name.ToString(), i);
		UTexture2D* NewTexture = CreateTextureFromRawData(Image, ImageWidth, ImageHeight, InParent, *SourceName, Flags, Context, Warn);

		UPaperSprite* NewSprite = CreatePaperSprite(NewTexture, ImageLeft, ImageTop, UPaperSprite::StaticClass(), InParent, *SourceName, Flags, Context, Warn);

		if (OutSprites) OutSprites->Add(NewSprite);
	}

	return true;
}

UTexture2D* UGifFactory::CreateTextureFromRawData(const TArray<uint8>& InRawData, const GifWord& Width, const GifWord& Height, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));

	TSharedPtr<IImageWrapper> BmpImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::BMP);
	if (!BmpImageWrapper.IsValid())
	{
		return nullptr;
	}
	if (!BmpImageWrapper->SetRaw(InRawData.GetData(), InRawData.Num(), Width, Height, ERGBFormat::BGRA, 24))
	{
		return nullptr;
	}
	// Check the resolution of the imported texture to ensure validity
	if (!IsImportResolutionValid(BmpImageWrapper->GetWidth(), BmpImageWrapper->GetHeight(), /*bAllowNonPowerOfTwo*/ true, Warn))
	{
		return nullptr;
	}

	FString NewName = FString::Printf(TEXT("T_%s"), *Name.ToString());
	UTexture2D* Texture = CreateTexture2D(InParent, *NewName, Flags);
	if (Texture)
	{
		// Set texture properties.
		Texture->Source.Init(
			BmpImageWrapper->GetWidth(),
			BmpImageWrapper->GetHeight(),
			/*NumSlices=*/ 1,
			/*NumMips=*/ 1,
			TSF_BGRA8
		);
		GetDefault<UPaperImporterSettings>()->ApplyTextureSettings(Texture);

		const TArray<uint8>* RawBMP = nullptr;
		if (BmpImageWrapper->GetRaw(BmpImageWrapper->GetFormat(), BmpImageWrapper->GetBitDepth(), RawBMP))
		{
			uint8* MipData = Texture->Source.LockMip(0);
			FMemory::Memcpy(MipData, RawBMP->GetData(), RawBMP->Num());
			Texture->Source.UnlockMip(0);
		}
		CleanUp();
	}
	Texture->MarkPackageDirty();
	Texture->PostEditChange();

	return Texture;
}

UPaperSprite* UGifFactory::CreatePaperSprite(class UTexture2D* InitialTexture, const GifWord& InLeft, const GifWord& InTop, UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, class FFeedbackContext* Warn)
{
	UPaperSprite* NewSprite = NewObject<UPaperSprite>(InParent, Class, Name, Flags | RF_Transactional);
	if (NewSprite && InitialTexture)
	{
		FSpriteAssetInitParameters SpriteInitParams;
		SpriteInitParams.SetTextureAndFill(InitialTexture);

		const UPaperImporterSettings* ImporterSettings = GetDefault<UPaperImporterSettings>();

		ImporterSettings->ApplySettingsForSpriteInit(SpriteInitParams, ESpriteInitMaterialLightingMode::Automatic);

		NewSprite->InitializeSprite(SpriteInitParams);

		NewSprite->SetPivotMode(ESpritePivotMode::Custom, FVector2D(-InLeft, -InTop));
	}

	return NewSprite;
}

UPaperFlipbook* UGifFactory::CreateFlipbook(UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn, const TArray<UPaperSprite*>& AllSprites)
{
	if (AllSprites.Num() == 0)
	{
		return nullptr;
	}

	FString NewName = FString::Printf(TEXT("F_%s"), *Name.ToString());
	
	UPaperFlipbookFactory* FlipbookFactory = NewObject<UPaperFlipbookFactory>();
	for (UPaperSprite* Sprite : AllSprites)
	{
		if (Sprite != nullptr)
		{
			FPaperFlipbookKeyFrame* KeyFrame = new (FlipbookFactory->KeyFrames) FPaperFlipbookKeyFrame();
			KeyFrame->Sprite = Sprite;
			KeyFrame->FrameRun = 6;
		}
	}

	UPaperFlipbook* NewFlipBook = nullptr;

	NewFlipBook = Cast<UPaperFlipbook>(FlipbookFactory->FactoryCreateNew(UPaperFlipbook::StaticClass(), InParent, *NewName, Flags, Context, Warn));

	return NewFlipBook;
}

int UGifFactory::OnReadGif(GifFileType* FileType, GifByteType* ByteType, int Length)
{
	if (ByteType == nullptr)
	{
		return 0;
	}
	if (GifIndex >= GifBytes.Num())
	{
		return 0;
	}
	if (Length >= GifBytes.Num())
	{
		return 0;
	}

	const int32 TargetIndex = GifIndex + Length;

	int32 size = 0;
	for (int i = 0; i < GifBytes.Num() && i < Length; i++)
	{
		ByteType[i] = GifBytes[GifIndex + i];
		size++;
	}
	ByteType[size] = '\0';
	GifIndex += size;
	return size;
}

bool UGifFactory::IsImportResolutionValid(int32 Width, int32 Height, bool bAllowNonPowerOfTwo, FFeedbackContext* Warn)
{
	// Calculate the maximum supported resolution utilizing the global max texture mip count
	// (Note, have to subtract 1 because 1x1 is a valid mip-size; this means a GMaxTextureMipCount of 4 means a max resolution of 8x8, not 2^4 = 16x16)
	const int32 MaximumSupportedResolution = 1 << (MAX_TEXTURE_MIP_COUNT - 1);

	bool bValid = true;

	// Check if the texture is above the supported resolution and prompt the user if they wish to continue if it is
	if (Width > MaximumSupportedResolution || Height > MaximumSupportedResolution)
	{
		if (EAppReturnType::Yes != FMessageDialog::Open(EAppMsgType::YesNo, FText::Format(
			NSLOCTEXT("UnrealEd", "Warning_LargeTextureImport", "Attempting to import {0} x {1} texture, proceed?\nLargest supported texture size: {2} x {3}"),
			FText::AsNumber(Width), FText::AsNumber(Height), FText::AsNumber(MaximumSupportedResolution), FText::AsNumber(MaximumSupportedResolution))))
		{
			bValid = false;
		}
	}

	const bool bIsPowerOfTwo = FMath::IsPowerOfTwo(Width) && FMath::IsPowerOfTwo(Height);
	// Check if the texture dimensions are powers of two
	if (!bAllowNonPowerOfTwo && !bIsPowerOfTwo)
	{
		Warn->Logf(ELogVerbosity::Error, *NSLOCTEXT("UnrealEd", "Warning_TextureNotAPowerOfTwo", "Cannot import texture with non-power of two dimensions").ToString());
		bValid = false;
	}

	return bValid;
}
#undef LOCTEXT_NAMESPACE