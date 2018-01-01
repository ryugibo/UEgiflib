// Fill out your copyright notice in the Description page of Project Settings.

#include "GifFactory.h"
#include "PaperFlipbook.h"
#include "UEgiflib.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Engine/Texture2D.h"
#include "PaperImporterSettings.h"

THIRD_PARTY_INCLUDES_START
#include "giflib/lib/gif_lib_private.h"
THIRD_PARTY_INCLUDES_END

#define LOCTEXT_NAMESPACE "GifFactories"


TArray<uint8> UGifFactory::GifBytes;
int32 UGifFactory::GifIndex;

UGifFactory::UGifFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UTexture2D::StaticClass();

	Formats.Empty();
	Formats.Add(TEXT("gif;Texture"));
	/*
	SupportedClass = UPaperFlipbook::StaticClass();

	Formats.Empty();
	Formats.Add(TEXT("gif;Flipbook"));
	*/
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

	UTexture2D* Texture = DecodeGifData(Buffer, Length, InParent, Name, Flags);


	return Texture;
}

UTexture2D* UGifFactory::DecodeGifData(const void* Data, int32 Size, UObject* InParent, FName Name, EObjectFlags Flags)
{
	int ErrorCode;

	GifFileType* FileType = DGifOpen((void *)Data, UGifFactory::OnReadGif, &ErrorCode);
	if (DGifSlurp(FileType) == GIF_ERROR)
	{
		return nullptr;
	}
	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	UTexture2D* Texture = nullptr;
	for (int i = 0; i < FileType->ImageCount; i++)
	{
		SavedImage& Frame = FileType->SavedImages[i];

		TArray<uint8> Image;
		Image.AddUninitialized(Frame.ImageDesc.Width * Frame.ImageDesc.Height * 4);

		bool HasGCB = false;
		GraphicsControlBlock GCB;
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
				for (int height_i = InterlacedOffset[pass_i]; height_i < Frame.ImageDesc.Height; height_i += InterlacedJumps[pass_i])
				{
					for (int width_i = 0; width_i < Frame.ImageDesc.Width; width_i++)
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
			for (int height_i = 0; height_i < Frame.ImageDesc.Height; height_i++)
			{
				for (int width_i = 0; width_i < Frame.ImageDesc.Width; width_i++)
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

		TSharedPtr<IImageWrapper> BmpImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::BMP);
		if (BmpImageWrapper.IsValid() && BmpImageWrapper->SetRaw(Image.GetData(), Frame.ImageDesc.Width * Frame.ImageDesc.Height * 4, Frame.ImageDesc.Width, Frame.ImageDesc.Height, ERGBFormat::BGRA, 24))
		{
			// Check the resolution of the imported texture to ensure validity
			/*
			if (!IsImportResolutionValid(BmpImageWrapper->GetWidth(), BmpImageWrapper->GetHeight(), bAllowNonPowerOfTwo, Warn))
			{
				return nullptr;
			}
			*/

			FString NewName = FString::Printf(TEXT("%2d"), i);
			UTexture2D* NewTexture = CreateTexture2D(InParent, *NewName, Flags);
			if (NewTexture)
			{
				// Set texture properties.
				NewTexture->Source.Init(
					BmpImageWrapper->GetWidth(),
					BmpImageWrapper->GetHeight(),
					/*NumSlices=*/ 1,
					/*NumMips=*/ 1,
					TSF_BGRA8
				);
				GetDefault<UPaperImporterSettings>()->ApplyTextureSettings(NewTexture);

				const TArray<uint8>* RawBMP = nullptr;
				if (BmpImageWrapper->GetRaw(BmpImageWrapper->GetFormat(), BmpImageWrapper->GetBitDepth(), RawBMP))
				{
					uint8* MipData = NewTexture->Source.LockMip(0);
					FMemory::Memcpy(MipData, RawBMP->GetData(), RawBMP->Num());
					NewTexture->Source.UnlockMip(0);
				}
				CleanUp();
			}
			Texture = NewTexture;
			Texture->MarkPackageDirty();
			Texture->PostEditChange();
			//return Texture;
		}
	}

	return Texture;
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

#undef LOCTEXT_NAMESPACE