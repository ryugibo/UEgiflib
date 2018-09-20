// Copyright 2018 Ryugibo, Inc. All Rights Reserved.

#include "GifFactory.h"

#include "UEgiflib.h"

#include "AssetToolsModule.h"
#include "AssetRegistryModule.h"

#include "ObjectTools.h"
#include "PackageTools.h"

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

#include "gif_lib_private.h"

#define LOCTEXT_NAMESPACE "GifFactories"

int32 UGifFactory::GifIndex;
int32 UGifFactory::GifLength;

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
	GifLength = BufferEnd - Buffer;
	GifIndex = 0;

	/** Texture Packing Test */
	UPaperFlipbookFactory* FlipbookFactory = NewObject<UPaperFlipbookFactory>();

	if (!DecodeGifDataToSpritesPackedTexture(InParent, Name, Flags, Context, Type, Warn, (void *)Buffer, FlipbookFactory))
	{
		UE_LOG(LogGiflib, Error, TEXT("Failed DecodeGifDataToSpritesPackedTexture"));
		return nullptr;
	}
	/*
	if (!DecodeGifDataToSprites(InParent, Name, Flags, Context, Type, Warn, (void *)Buffer, FlipbookFactory))
	{
		UE_LOG(LogGiflib, Error, TEXT("Failed DecodeGifDataToSprites"));
		return nullptr;
	}
	*/
	UPaperFlipbook* Flipbook = CreateFlipbook(InParent, Name, Flags, Context, Warn, FlipbookFactory);

	return Flipbook;
}

bool UGifFactory::DecodeGifDataToSpritesPackedTexture
(
	UObject*				InParent,
	FName					Name,
	EObjectFlags			Flags,
	UObject*				Context,
	const TCHAR*			Type,
	FFeedbackContext*		Warn,
	void*					Data,
	UPaperFlipbookFactory*	FlipbookFactory
)
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
		UE_LOG(LogGiflib, Error, TEXT("DGifSlurp Failed"));
		return false;
	}

	const int32 ImageCount = FileType->ImageCount;
	
	const GifWord CanvasWidth = FileType->SWidth;
	const GifWord CanvasHeight = FileType->SHeight;

	int32 RowCount = 0;
	float RowFloat = FMath::Sqrt(ImageCount);
	RowCount = ((RowFloat - (int32)RowFloat) > 0) ? (RowFloat + 1) : (RowFloat);

	const uint32 TextureSizeX = CanvasWidth * RowCount;
	const uint32 TextureSizeY = CanvasHeight * RowCount;

	TArray<uint8> FullImage;
	FullImage.AddUninitialized(TextureSizeX * TextureSizeY * 4);

	int LastDisposalMode = 0;

	TArray<FSpriteInfo> SpriteInfos;
	SpriteInfos.AddUninitialized(ImageCount);

	FSpriteInfo LastSpriteInfo;
	for (int image_i = 0; image_i < ImageCount; image_i++)
	{
		SavedImage& Frame = FileType->SavedImages[image_i];

		const GifWord ImageLeft = Frame.ImageDesc.Left;
		const GifWord ImageTop = Frame.ImageDesc.Top;
		const GifWord ImageHeight = Frame.ImageDesc.Height;
		const GifWord ImageWidth = Frame.ImageDesc.Width;

		TArray<uint8> CurrentImage;
		CurrentImage.AddUninitialized(CanvasWidth * CanvasHeight * 4);

		bool HasGCB = false;
		GraphicsControlBlock GCB = { 0, false, 0, -1 };
		for (int ext_i = 0; ext_i < Frame.ExtensionBlockCount; ext_i++)
		{
			if (GIF_OK == DGifExtensionToGCB(Frame.ExtensionBlocks[ext_i].ByteCount, Frame.ExtensionBlocks[ext_i].Bytes, &GCB))
			{
				HasGCB = true;
			}
		}

		bool bIsImageExist = false;
		switch (LastDisposalMode)
		{
		case DISPOSE_DO_NOT:
			if (image_i != 0 && Frame.RasterBits[0] == '\0')
			{
				SpriteInfos[image_i] = LastSpriteInfo;
				bIsImageExist = true;
			}
			break;
		case DISPOSE_BACKGROUND:
			// TODO:
			break;
		case DISPOSE_PREVIOUS:
			// TODO:
			break;
		}

		FString SourceName = FString::Printf(TEXT("%s_%d"), *Name.ToString(), image_i);
		if (!bIsImageExist)
		{
			ColorMapObject* CMO = Frame.ImageDesc.ColorMap ? Frame.ImageDesc.ColorMap : FileType->SColorMap;
			GifColorType* ColorMap = CMO->Colors;
			uint32 Raster_i = 0;
			uint32 Blank_i = 0;

			if (Frame.ImageDesc.Interlace)
			{
				int InterlacedOffset[] = { 0, 4, 2, 1 };
				int InterlacedJumps[] = { 8, 8, 4, 2 };

				for (int pass_i = 0; pass_i < 4; pass_i++)
				{
					for (int height_i = InterlacedOffset[pass_i]; height_i < CanvasHeight; height_i += InterlacedJumps[pass_i])
					{
						for (int width_i = 0; width_i < CanvasWidth; width_i++)
						{
							if ((width_i < ImageLeft || height_i < ImageTop) ||
								(width_i >= (ImageLeft + ImageWidth) || height_i >= (ImageTop + ImageHeight)))
							{
								CurrentImage[(Raster_i + Blank_i) * 4 + 0] = ColorMap[FileType->SBackGroundColor].Blue;
								CurrentImage[(Raster_i + Blank_i) * 4 + 1] = ColorMap[FileType->SBackGroundColor].Green;
								CurrentImage[(Raster_i + Blank_i) * 4 + 2] = ColorMap[FileType->SBackGroundColor].Red;
								CurrentImage[(Raster_i + Blank_i) * 4 + 3] = (HasGCB && (int)FileType->SBackGroundColor == GCB.TransparentColor) ? 0 : 255;

								Blank_i++;

								continue;
							}
							unsigned char& Bit = Frame.RasterBits[Raster_i];
							if (Bit >= CMO->ColorCount)
							{
								break;
							}
							CurrentImage[(Raster_i + Blank_i) * 4 + 0] = ColorMap[Bit].Blue;
							CurrentImage[(Raster_i + Blank_i) * 4 + 1] = ColorMap[Bit].Green;
							CurrentImage[(Raster_i + Blank_i) * 4 + 2] = ColorMap[Bit].Red;
							CurrentImage[(Raster_i + Blank_i) * 4 + 3] = (HasGCB && (int)Bit == GCB.TransparentColor) ? 0 : 255;

							Raster_i++;
						}
					}
				}
			}
			else
			{
				for (int height_i = 0; height_i < CanvasHeight; height_i++)
				{
					for (int width_i = 0; width_i < CanvasWidth; width_i++)
					{
						if ((width_i < ImageLeft || height_i < ImageTop) || 
							(width_i >= (ImageLeft + ImageWidth) || height_i >= (ImageTop + ImageHeight)))
						{
							CurrentImage[(Raster_i + Blank_i) * 4 + 0] = ColorMap[FileType->SBackGroundColor].Blue;
							CurrentImage[(Raster_i + Blank_i) * 4 + 1] = ColorMap[FileType->SBackGroundColor].Green;
							CurrentImage[(Raster_i + Blank_i) * 4 + 2] = ColorMap[FileType->SBackGroundColor].Red;
							CurrentImage[(Raster_i + Blank_i) * 4 + 3] = (HasGCB && (int)FileType->SBackGroundColor == GCB.TransparentColor) ? 0 : 255;

							Blank_i++;

							continue;
						}
						unsigned char& Bit = Frame.RasterBits[Raster_i];
						if (Bit >= CMO->ColorCount)
						{
							break;
						}

						CurrentImage[(Raster_i + Blank_i) * 4 + 0] = ColorMap[Bit].Blue;
						CurrentImage[(Raster_i + Blank_i) * 4 + 1] = ColorMap[Bit].Green;
						CurrentImage[(Raster_i + Blank_i) * 4 + 2] = ColorMap[Bit].Red;
						CurrentImage[(Raster_i + Blank_i) * 4 + 3] = (HasGCB && (int)Bit == GCB.TransparentColor) ? 0 : 255;

						Raster_i++;
					}
				}
			}

			FIntPoint Offset((image_i % RowCount) * CanvasWidth, (image_i / RowCount) * CanvasHeight);
			for (int height_i = 0; height_i < CanvasHeight; height_i++)
			{
				for (int width_i = 0; width_i < CanvasWidth; width_i++)
				{
					const uint32 From = 4 * (height_i * CanvasWidth + width_i);

					uint32 To = (Offset.X + width_i + ((Offset.Y + height_i) * TextureSizeX)) * 4;

					FullImage[To + 0] = CurrentImage[From + 0];
					FullImage[To + 1] = CurrentImage[From + 1];
					FullImage[To + 2] = CurrentImage[From + 2];
					FullImage[To + 3] = CurrentImage[From + 3];
				}
			}

			SpriteInfos[image_i].Offset = Offset;
			SpriteInfos[image_i].Dimension = FIntPoint(CanvasWidth, CanvasHeight);
			SpriteInfos[image_i].Frame = HasGCB ? (GCB.DelayTime) : 1;
		}

		LastSpriteInfo = SpriteInfos[image_i];

		if (HasGCB)
		{
			LastDisposalMode = GCB.DisposalMode;
		}
		else
		{
			LastDisposalMode = DISPOSAL_UNSPECIFIED;
		}
	}

	UTexture2D* ResultTexture = CreateTextureFromRawData(InParent, Name, Flags, Context, Warn, FullImage, TextureSizeX, TextureSizeY);

	for (int32 image_i = 0; image_i < SpriteInfos.Num(); image_i++)
	{
		FSpriteInfo Info = SpriteInfos[image_i];
		FString SourceName = FString::Printf(TEXT("%s_%d"), *Name.ToString(), image_i);
		UPaperSprite* NewSprite = CreatePaperSprite(InParent, *SourceName, Flags, Context, Warn, ResultTexture, Info.Offset, Info.Dimension);

		FPaperFlipbookKeyFrame* KeyFrame = new (FlipbookFactory->KeyFrames) FPaperFlipbookKeyFrame();
		KeyFrame->Sprite = NewSprite;
		KeyFrame->FrameRun = Info.Frame;
	}

	if (GIF_OK != DGifCloseFile(FileType, &ErrorCode))
	{
		UE_LOG(LogGiflib, Error, TEXT("DGifCloseFile Failed : %d"), ErrorCode);
		return false;
	}

	return true;
}

bool UGifFactory::DecodeGifDataToSprites
(
	UObject*				InParent,
	FName					Name,
	EObjectFlags			Flags,
	UObject*				Context,
	const TCHAR*			Type,
	FFeedbackContext*		Warn,
	void*					Data,
	UPaperFlipbookFactory*	FlipbookFactory
)
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
		UE_LOG(LogGiflib, Error, TEXT("DGifSlurp Failed"));
		return false;
	}

	const GifWord CanvasWidth = FileType->SWidth;
	const GifWord CanvasHeight = FileType->SHeight;

	UTexture2D* LastTexture = nullptr;
	int LastDisposalMode = 0;
	for (int i = 0; i < FileType->ImageCount; i++)
	{
		SavedImage& Frame = FileType->SavedImages[i];

		const GifWord ImageLeft = Frame.ImageDesc.Left;
		const GifWord ImageTop = Frame.ImageDesc.Top;
		const GifWord ImageHeight = Frame.ImageDesc.Height;
		const GifWord ImageWidth = Frame.ImageDesc.Width;

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

		UTexture2D* NewTexture = nullptr;
		switch (LastDisposalMode)
		{
		case DISPOSE_DO_NOT:
			if (LastTexture != nullptr && Frame.RasterBits[0] == '\0')
			{
				NewTexture = LastTexture;
			}
			break;
		case DISPOSE_BACKGROUND:
			// TODO:
			break;
		case DISPOSE_PREVIOUS:
			// TODO:
			break;
		}

		FString SourceName = FString::Printf(TEXT("%s_%d"), *Name.ToString(), i);
		if (NewTexture == nullptr)
		{
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

			NewTexture = CreateTextureFromRawData(InParent, *SourceName, Flags, Context, Warn, Image, ImageWidth, ImageHeight);
		}

		UPaperSprite* NewSprite = CreatePaperSprite(InParent, *SourceName, Flags, Context, Warn, NewTexture);

		FPaperFlipbookKeyFrame* KeyFrame = new (FlipbookFactory->KeyFrames) FPaperFlipbookKeyFrame();
		KeyFrame->Sprite = NewSprite;
		KeyFrame->FrameRun = HasGCB ? (GCB.DelayTime) : 1;

		// Update Last Image Infos
		LastTexture = NewTexture;
		if (HasGCB)
		{
			LastDisposalMode = GCB.DisposalMode;
		}
		else
		{
			LastDisposalMode = DISPOSAL_UNSPECIFIED;
		}
	}

	if (GIF_OK != DGifCloseFile(FileType, &ErrorCode))
	{
		UE_LOG(LogGiflib, Error, TEXT("DGifCloseFile Failed : %d"), ErrorCode);
		return false;
	}
	return true;
}

UTexture2D* UGifFactory::CreateTextureFromRawData
(
	UObject*				InParent,
	FName					Name,
	EObjectFlags			Flags,
	UObject*				Context,
	FFeedbackContext*		Warn,
 	const TArray<uint8>&	InRawData,
 	const int32&			Width,
 	const int32&			Height
)
{
	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));

	TSharedPtr<IImageWrapper> BmpImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::BMP);
	if (!BmpImageWrapper.IsValid())
	{
		return nullptr;
	}
	if (!BmpImageWrapper->SetRaw(InRawData.GetData(), InRawData.Num(), Width, Height, ERGBFormat::BGRA, 8))
	{
		return nullptr;
	}
	// Check the resolution of the imported texture to ensure validity
	if (!IsImportResolutionValid(BmpImageWrapper->GetWidth(), BmpImageWrapper->GetHeight(), /*bAllowNonPowerOfTwo*/ true, Warn))
	{
		return nullptr;
	}


	FString TextureName = ObjectTools::SanitizeObjectName(FString::Printf(TEXT("T_%s"), *Name.ToString()));

	FString BasePackageName = FPackageName::GetLongPackagePath(InParent->GetName()) / TextureName;
	BasePackageName = PackageTools::SanitizePackageName(BasePackageName);


	FString ObjectPath = BasePackageName + TEXT(".") + TextureName;
	UTexture* ExistingTexture = LoadObject<UTexture>(NULL, *ObjectPath, nullptr, LOAD_Quiet | LOAD_NoWarn);

	UPackage* TexturePackage = nullptr;
	if (ExistingTexture == nullptr)
	{
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		FString FinalPackageName;
		AssetToolsModule.Get().CreateUniqueAssetName(BasePackageName, TEXT(""), FinalPackageName, TextureName);

		TexturePackage = CreatePackage(NULL, *FinalPackageName);
	}
	else
	{
		TexturePackage = ExistingTexture->GetOutermost();
	}

	UTexture2D* Texture = CreateTexture2D(TexturePackage, *TextureName, Flags);
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
		TexturePackage->SetDirtyFlag(true);
		CleanUp();
	}
	FAssetRegistryModule::AssetCreated(Texture);
	Texture->MarkPackageDirty();
	Texture->PostEditChange();

	return Texture;
}

UPaperSprite* UGifFactory::CreatePaperSprite
(
	UObject*			InParent,
	FName				Name,
	EObjectFlags		Flags,
	UObject*			Context,
	FFeedbackContext*	Warn,
	UTexture2D*			InitialTexture,
	const FIntPoint&	InOffset,
	const FIntPoint&	InDimension
)
{
	FString SpriteName = ObjectTools::SanitizeObjectName(FString::Printf(TEXT("S_%s"), *Name.ToString()));

	FString BasePackageName = FPackageName::GetLongPackagePath(InParent->GetName()) / SpriteName;
	BasePackageName = PackageTools::SanitizePackageName(BasePackageName);

	FString ObjectPath = BasePackageName + TEXT(".") + SpriteName;
	UPaperSprite* ExistingSprite = LoadObject<UPaperSprite>(NULL, *ObjectPath, nullptr, LOAD_Quiet | LOAD_NoWarn);

	UPackage* SpritePackage = nullptr;
	if (SpritePackage == nullptr)
	{
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		FString FinalPackageName;
		AssetToolsModule.Get().CreateUniqueAssetName(BasePackageName, TEXT(""), FinalPackageName, SpriteName);

		SpritePackage = CreatePackage(NULL, *FinalPackageName);
	}
	else
	{
		SpritePackage = ExistingSprite->GetOutermost();
	}

	UPaperSprite* NewSprite = NewObject<UPaperSprite>(SpritePackage, UPaperSprite::StaticClass(), *SpriteName, Flags | RF_Transactional);
	if (NewSprite && InitialTexture)
	{
		FSpriteAssetInitParameters SpriteInitParams;
		SpriteInitParams.SetTextureAndFill(InitialTexture);
		SpriteInitParams.Offset = InOffset;
		SpriteInitParams.Dimension = InDimension;

		const UPaperImporterSettings* ImporterSettings = GetDefault<UPaperImporterSettings>();

		ImporterSettings->ApplySettingsForSpriteInit(SpriteInitParams, ESpriteInitMaterialLightingMode::Automatic);

		NewSprite->InitializeSprite(SpriteInitParams);

		SpritePackage->SetDirtyFlag(true);

		FAssetRegistryModule::AssetCreated(NewSprite);
		NewSprite->MarkPackageDirty();
		NewSprite->PostEditChange();
	}

	return NewSprite;
}

UPaperFlipbook* UGifFactory::CreateFlipbook
(
	UObject*				InParent,
	FName					Name,
	EObjectFlags			Flags,
	UObject*				Context,
	FFeedbackContext*		Warn,
	UPaperFlipbookFactory*	FlipbookFactory
)
{
	FString FlipbookName = FString::Printf(TEXT("%s"), *Name.ToString());
	UPaperFlipbook* NewFlipbook = nullptr;

	NewFlipbook = Cast<UPaperFlipbook>(FlipbookFactory->FactoryCreateNew(UPaperFlipbook::StaticClass(), InParent, *FlipbookName, Flags, Context, Warn));

	FScopedFlipbookMutator EditFlipbook(NewFlipbook);
	EditFlipbook.FramesPerSecond = 100.0f;

	return NewFlipbook;
}

int UGifFactory::OnReadGif
(
	GifFileType*	FileType,
	GifByteType*	ByteType,
	int				Length
)
{
	if (FileType == nullptr || ByteType == nullptr)
	{
		return 0;
	}

	int32 size = 0;
	for (int i = 0; i < Length; i++)
	{
		const int32 DataIndex = GifIndex + i;
		if (DataIndex >= GifLength)
		{
			break;
		}
		ByteType[i] = ((uint8*)FileType->UserData)[DataIndex];
		size++;
	}
	GifIndex += size;
	return size;
}

bool UGifFactory::IsImportResolutionValid
(
	int32				Width,
	int32				Height,
	bool				bAllowNonPowerOfTwo,
	FFeedbackContext*	Warn
)
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
		Warn->Log(ELogVerbosity::Error, *(NSLOCTEXT("UnrealEd", "Warning_TextureNotAPowerOfTwo", "Cannot import texture with non-power of two dimensions").ToString()));
		bValid = false;
	}

	return bValid;
}

#undef LOCTEXT_NAMESPACE

