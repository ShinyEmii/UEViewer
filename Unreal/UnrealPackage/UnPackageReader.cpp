#include "Core.h"

#include "UnCore.h"
#include "UnObject.h"
#include "UnPackage.h"

#include "UnPackageUE3Reader.h"

byte GForceCompMethod = 0;		// COMPRESS_...

/*-----------------------------------------------------------------------------
	Lineage2 file reader
-----------------------------------------------------------------------------*/

#if LINEAGE2 || EXTEEL

#define LINEAGE_HEADER_SIZE		28

class FFileReaderLineage : public FReaderWrapper
{
	DECLARE_ARCHIVE(FFileReaderLineage, FReaderWrapper);
public:
	FFileReaderLineage(FArchive *File, int Key)
	:	FReaderWrapper(File, LINEAGE_HEADER_SIZE)
	,	XorKey(Key)
	{
		Game = GAME_Lineage2;
		Seek(0);		// skip header
	}

	virtual void Serialize(void *data, int size)
	{
		Reader->Serialize(data, size);
		if (XorKey)
		{
			int i;
			byte *p;
			for (i = 0, p = (byte*)data; i < size; i++, p++)
				*p ^= XorKey;
		}
	}

protected:
	byte		XorKey;
};

#endif // LINEAGE2 || EXTEEL


/*-----------------------------------------------------------------------------
	Battle Territory Online
-----------------------------------------------------------------------------*/

#if BATTLE_TERR

class FFileReaderBattleTerr : public FReaderWrapper
{
	DECLARE_ARCHIVE(FFileReaderBattleTerr, FReaderWrapper);
public:
	FFileReaderBattleTerr(FArchive *File)
	:	FReaderWrapper(File)
	{
		Game = GAME_BattleTerr;
	}

	virtual void Serialize(void *data, int size)
	{
		Reader->Serialize(data, size);

		int i;
		byte *p;
		for (i = 0, p = (byte*)data; i < size; i++, p++)
		{
			byte b = *p;
			int shift;
			byte v;
			for (shift = 1, v = b & (b - 1); v; v = v & (v - 1))	// shift = number of identity bits in 'v' (but b=0 -> shift=1)
				shift++;
			b = ROL8(b, shift);
			*p = b;
		}
	}
};

#endif // BATTLE_TERR


/*-----------------------------------------------------------------------------
	America's Army 2
-----------------------------------------------------------------------------*/

#if AA2

class FFileReaderAA2 : public FReaderWrapper
{
	DECLARE_ARCHIVE(FFileReaderAA2, FReaderWrapper);
public:
	FFileReaderAA2(FArchive *File)
	:	FReaderWrapper(File)
	{}

	virtual void Serialize(void *data, int size)
	{
		int StartPos = Reader->Tell();
		Reader->Serialize(data, size);

		int i;
		byte *p;
		for (i = 0, p = (byte*)data; i < size; i++, p++)
		{
			byte b = *p;
		#if 0
			// used with ArraysAGPCount != 0
			int shift;
			byte v;
			for (shift = 1, v = b & (b - 1); v; v = v & (v - 1))	// shift = number of identity bits in 'v' (but b=0 -> shift=1)
				shift++;
			b = ROR8(b, shift);
		#else
			// used with ArraysAGPCount == 0
			int PosXor = StartPos + i;
			PosXor = (PosXor >> 8) ^ PosXor;
			b ^= (PosXor & 0xFF);
			if (PosXor & 2)
			{
				b = ROL8(b, 1);
			}
		#endif
			*p = b;
		}
	}
};

#endif // AA2


/*-----------------------------------------------------------------------------
	Blade & Soul
-----------------------------------------------------------------------------*/

#if BLADENSOUL

class FFileReaderBnS : public FReaderWrapper
{
	DECLARE_ARCHIVE(FFileReaderBnS, FReaderWrapper);
public:
	FFileReaderBnS(FArchive *File)
	:	FReaderWrapper(File)
	{
		Game = GAME_BladeNSoul;
	}

	virtual void Serialize(void *data, int size)
	{
		int Pos = Reader->Tell();
		Reader->Serialize(data, size);

		// Note: similar code exists in DecryptBladeAndSoul()
		int i;
		byte *p;
		static const char *key = "qiffjdlerdoqymvketdcl0er2subioxq";
		for (i = 0, p = (byte*)data; i < size; i++, p++, Pos++)
		{
			*p ^= key[Pos % 32];
		}
	}
};


static void DecodeBnSPointer(int32 &Value, uint32 Code1, uint32 Code2, int32 Index)
{
	uint32 tmp1 = ROR32(Value, (Index + Code2) & 0x1F);
	uint32 tmp2 = ROR32(Code1, Index % 32);
	Value = tmp2 ^ tmp1;
}


void PatchBnSExports(FObjectExport *Exp, const FPackageFileSummary &Summary)
{
	unsigned Code1 = ((Summary.HeadersSize & 0xFF) << 24) |
					 ((Summary.NameCount   & 0xFF) << 16) |
					 ((Summary.NameOffset  & 0xFF) << 8)  |
					 ((Summary.ExportCount & 0xFF));
	unsigned Code2 = (Summary.ExportOffset + Summary.ImportCount + Summary.ImportOffset) & 0x1F;

	for (int i = 0; i < Summary.ExportCount; i++, Exp++)
	{
		DecodeBnSPointer(Exp->SerialSize,   Code1, Code2, i);
		DecodeBnSPointer(Exp->SerialOffset, Code1, Code2, i);
	}
}

#endif // BLADENSOUL

/*-----------------------------------------------------------------------------
	Dungeon Defenders
-----------------------------------------------------------------------------*/

#if DUNDEF

void PatchDunDefExports(FObjectExport *Exp, const FPackageFileSummary &Summary)
{
	// Dungeon Defenders has nullified ExportOffset entries starting from some version.
	// Let's recover them.
	int CurrentOffset = Summary.HeadersSize;
	for (int i = 0; i < Summary.ExportCount; i++, Exp++)
	{
		if (Exp->SerialOffset == 0)
			Exp->SerialOffset = CurrentOffset;
		CurrentOffset = Exp->SerialOffset + Exp->SerialSize;
	}
}

#endif // DUNDEF


/*-----------------------------------------------------------------------------
	Nurien
-----------------------------------------------------------------------------*/

#if NURIEN

class FFileReaderNurien : public FReaderWrapper
{
	DECLARE_ARCHIVE(FFileReaderNurien, FReaderWrapper);
public:
	int			Threshold;

	FFileReaderNurien(FArchive *File)
	:	FReaderWrapper(File)
	,	Threshold(0x7FFFFFFF)
	{}

	virtual void Serialize(void *data, int size)
	{
		int Pos = Reader->Tell();
		Reader->Serialize(data, size);

		// only first Threshold bytes are compressed (package headers)
		if (Pos >= Threshold) return;

		int i;
		byte *p;
		static const byte key[] = {
			0xFE, 0xF2, 0x35, 0x2E, 0x12, 0xFF, 0x47, 0x8A,
			0xE1, 0x2D, 0x53, 0xE2, 0x21, 0xA3, 0x74, 0xA8
		};
		for (i = 0, p = (byte*)data; i < size; i++, p++, Pos++)
		{
			if (Pos >= Threshold) return;
			*p ^= key[Pos & 0xF];
		}
	}

	virtual void SetStartingPosition(int pos)
	{
		Threshold = pos;
	}
};

#endif // NURIEN

/*-----------------------------------------------------------------------------
	Rocket League
-----------------------------------------------------------------------------*/

#if ROCKET_LEAGUE

#include "RocketLeagueKeys.h"

struct FRLEncryptedChunk
{
	int64 CompressedOffset;
	int32 CompressedSize;
	byte  Nonce[12];
};

class FFileReaderRocketLeague : public FReaderWrapper
{
	DECLARE_ARCHIVE(FFileReaderRocketLeague, FReaderWrapper);
public:
	int                     EncryptionStart;
	int                     EncryptionEnd;
	byte                    Key[32];
	bool                    bIsFullEncrypted;
	byte                    HeaderNonce[12];
	TArray<FRLEncryptedChunk> Chunks;

	FFileReaderRocketLeague(FArchive *File)
	:	FReaderWrapper(File)
	,	EncryptionStart(0)
	,	EncryptionEnd(0)
	,	bIsFullEncrypted(false)
	{
		memset(Key, 0, sizeof(Key));
		memset(HeaderNonce, 0, sizeof(HeaderNonce));
	}

	virtual void Serialize(void *data, int size)
	{
		int Pos = Reader->Tell();
		Reader->Serialize(data, size);

		// 1. Header decryption
		if (Pos < EncryptionEnd && Pos + size > EncryptionStart)
		{
			int StartOffset     = max(0, Pos - EncryptionStart);
			int EndOffset       = min(EncryptionEnd, Pos + size) - EncryptionStart;
			int CopySize        = EndOffset - StartOffset;
			int CopyOffset      = max(0, EncryptionStart - Pos);

			int BlockStartOffset = StartOffset & ~15;
			int BlockEndOffset   = Align(EndOffset, 16);
			int EncryptedSize    = BlockEndOffset - BlockStartOffset;
			int EncryptedOffset  = StartOffset - BlockStartOffset;

			byte *EncryptedBuffer = (byte*)(appMalloc(EncryptedSize));
			Reader->Seek(EncryptionStart + BlockStartOffset);
			Reader->Serialize(EncryptedBuffer, EncryptedSize);

			if (bIsFullEncrypted)
			{
				uint32 initialCounter = BlockStartOffset / 16;
				appDecryptAES_CTR(EncryptedBuffer, EncryptedSize, Key, HeaderNonce, initialCounter);
			}
			else
			{
				appDecryptAES(EncryptedBuffer, EncryptedSize, (const char*)Key, 32);
			}

			memcpy(OffsetPointer(data, CopyOffset), &EncryptedBuffer[EncryptedOffset], CopySize);
			appFree(EncryptedBuffer);
			Reader->Seek(Pos + size);
		}

		// 2. Chunks decryption (for 0x0800 full-encrypted packages)
		if (bIsFullEncrypted && Chunks.Num() > 0)
		{
			for (int c = 0; c < Chunks.Num(); c++)
			{
				const FRLEncryptedChunk& chunk = Chunks[c];
				int chunkStart = (int)chunk.CompressedOffset;
				int chunkEnd = chunkStart + chunk.CompressedSize;

				if (Pos < chunkEnd && Pos + size > chunkStart)
				{
					int StartOffset     = max(0, Pos - chunkStart);
					int EndOffset       = min(chunkEnd, Pos + size) - chunkStart;
					int CopySize        = EndOffset - StartOffset;
					int CopyOffset      = max(0, chunkStart - Pos);

					int BlockStartOffset = StartOffset & ~15;
					int BlockEndOffset   = min(Align(EndOffset, 16), chunk.CompressedSize); // clamp to chunk bounds
					int EncryptedSize     = BlockEndOffset - BlockStartOffset;
					int EncryptedOffset   = StartOffset - BlockStartOffset;

					byte *EncryptedBuffer = (byte*)(appMalloc(EncryptedSize));
					Reader->Seek(chunkStart + BlockStartOffset);
					Reader->Serialize(EncryptedBuffer, EncryptedSize);

					uint32 initialCounter = BlockStartOffset / 16;
					appDecryptAES_CTR(EncryptedBuffer, EncryptedSize, Key, chunk.Nonce, initialCounter);

					memcpy(OffsetPointer(data, CopyOffset), &EncryptedBuffer[EncryptedOffset], CopySize);
					appFree(EncryptedBuffer);
					Reader->Seek(Pos + size);
				}
			}
		}
	}
};

#endif // ROCKET_LEAGUE

/*-----------------------------------------------------------------------------
	Top-level code
-----------------------------------------------------------------------------*/

/*static*/ FArchive* UnPackage::CreateLoader(const char* filename, FArchive* baseLoader)
{
	guard(UnPackage::CreateLoader);

	// setup FArchive
	FArchive* Loader = (baseLoader) ? baseLoader : new FFileReader(filename, EFileArchiveOptions::OpenWarning);
	if (!Loader)
	{
		return NULL;
	}

	// Verify the file size first, taking into account that it might be too large to open (requires 64-bit size support).
	int64 FileSize = Loader->GetFileSize64();
	if (FileSize < 16 || FileSize >= MAX_FILE_SIZE_32)
	{
		if (FileSize > 1024)
		{
			appPrintf("WARNING: package file %s is too large (%d Mb), ignoring\n", filename, int32(FileSize >> 20));
		}
		// The file is too small, possibly invalid one.
		if (!baseLoader)
			delete Loader;
		return NULL;
	}

	// Pick 32-bit integer from archive to determine its type
	uint32 checkDword;
	*Loader << checkDword;
	// Seek back to file start
	Loader->Seek(0);

#if LINEAGE2 || EXTEEL
	if (checkDword == ('L' | ('i' << 16)))	// unicode string "Lineage2Ver111"
	{
		// this is a Lineage2 package
		Loader->Seek(LINEAGE_HEADER_SIZE);
		// here is a encrypted by 'xor' standard FPackageFileSummary
		// to get encryption key, can check 1st byte
		byte b;
		*Loader << b;
		// for Ver111 XorKey==0xAC for Lineage or ==0x42 for Exteel, for Ver121 computed from filename
		byte XorKey = b ^ (PACKAGE_FILE_TAG & 0xFF);
		// replace Loader
		Loader = new FFileReaderLineage(Loader, XorKey);
		return Loader;
	}
#endif // LINEAGE2 || EXTEEL

#if BATTLE_TERR
	if (checkDword == 0x342B9CFC)
	{
		// replace Loader
		Loader = new FFileReaderBattleTerr(Loader);
		return Loader;
	}
#endif // BATTLE_TERR

#if NURIEN
	if (checkDword == 0xB01F713F)
	{
		// replace loader
		Loader = new FFileReaderNurien(Loader);
		return Loader;
	}
#endif // NURIEN

#if BLADENSOUL
	if (checkDword == 0xF84CEAB0)
	{
		if (!GForceGame) GForceGame = GAME_BladeNSoul;
		Loader = new FFileReaderBnS(Loader);
		return Loader;
	}
#endif // BLADENSOUL

#if UNREAL3
	// Code for loading UE3 "fully compressed packages"

	uint32 checkDword1, checkDword2;
	*Loader << checkDword1;
	if (checkDword1 == PACKAGE_FILE_TAG_REV)
	{
		Loader->ReverseBytes = true;
		if (GForcePlatform == PLATFORM_UNKNOWN)
			Loader->Platform = PLATFORM_XBOX360;			// default platform for "ReverseBytes" mode is PLATFORM_XBOX360
	}
	else if (checkDword1 != PACKAGE_FILE_TAG)
	{
		// fully compressed package always starts with package tag
		Loader->Seek(0);
		return Loader;
	}
	// Read 2nd dword after changing byte order in Loader
	*Loader << checkDword2;
	Loader->Seek(0);

	// Check if this is a fully compressed package. UE3 by itself checks if there's .uncompressed_size with text contents
	// file exists next to the package file.
	if (checkDword2 == PACKAGE_FILE_TAG || checkDword2 == 0x20000 || checkDword2 == 0x10000)	// seen 0x10000 in Enslaved/PS3
	{
		//!! NOTES:
		//!! - MKvsDC/X360 Core.u and Engine.u uses LZO instead of LZX (LZO and LZX are not auto-detected with COMPRESS_FIND)
		guard(ReadFullyCompressedHeader);
		// this is a fully compressed package
		FCompressedChunkHeader H;
		*Loader << H;
		TArray<FCompressedChunk> Chunks;
		FCompressedChunk *Chunk = new (Chunks) FCompressedChunk;
		Chunk->UncompressedOffset = 0;
		Chunk->UncompressedSize   = H.Sum.UncompressedSize;
		Chunk->CompressedOffset   = 0;
		Chunk->CompressedSize     = H.Sum.CompressedSize;
		byte CompMethod = GForceCompMethod;
		if (!CompMethod)
			CompMethod = (Loader->Platform == PLATFORM_XBOX360) ? COMPRESS_LZX : COMPRESS_FIND;
		FUE3ArchiveReader* UE3Loader = new FUE3ArchiveReader(Loader, CompMethod, Chunks);
		UE3Loader->IsFullyCompressed = true;
		Loader = UE3Loader;
		unguard;
	}
#endif // UNREAL3

	return Loader;

	unguardf("%s", filename);
}

void UnPackage::ReplaceLoader()
{
	guard(UnPackage::ReplaceLoader);

	// Current FArchive position is after FPackageFileSummary

#if BIOSHOCK
	if ((Game == GAME_Bioshock) && (Summary.PackageFlags & 0x20000))
	{
		// Bioshock has a special flag indicating compression. Compression table follows the package summary.
		// Read compression tables.
		int NumChunks, i;
		TArray<FCompressedChunk> Chunks;
		*this << NumChunks;
		Chunks.Empty(NumChunks);
		int UncompOffset = Tell() - 4;				//?? there should be a flag signalling presence of compression structures, because of "Tell()-4"
		for (i = 0; i < NumChunks; i++)
		{
			int Offset;
			*this << Offset;
			FCompressedChunk *Chunk = new (Chunks) FCompressedChunk;
			Chunk->UncompressedOffset = UncompOffset;
			Chunk->UncompressedSize   = 32768;
			Chunk->CompressedOffset   = Offset;
			Chunk->CompressedSize     = 0;			//?? not used
			UncompOffset             += 32768;
		}
		// Replace Loader for reading compressed Bioshock archives.
		Loader = new FUE3ArchiveReader(Loader, COMPRESS_ZLIB, Chunks);
		Loader->SetupFrom(*this);
		return;
	}
#endif // BIOSHOCK

#if AA2
	if (Game == GAME_AA2)
	{
		// America's Army 2 has encryption after FPackageFileSummary
		if (ArLicenseeVer >= 19)
		{
			int IsEncrypted;
			*this << IsEncrypted;
			if (IsEncrypted) Loader = new FFileReaderAA2(Loader);
		}
		return;
	}
#endif // AA2

#if ROCKET_LEAGUE
	if (Game == GAME_RocketLeague && (Summary.PackageFlags & PKG_Cooked))
	{
		// Rocket League has an encrypted header after FPackageFileSummary containing the name/import/export tables and a compression table.
		TArray<FString> AdditionalPackagesToCook;
		*this << AdditionalPackagesToCook;

		// Array of unknown structs (TextureAllocations)
		int32 NumUnknownStructs;
		*this << NumUnknownStructs;
		for (int i = 0; i < NumUnknownStructs; i++)
		{
			this->Seek(this->Tell() + sizeof(int32)*5); // skip 5 int32 values
			TArray<int32> unknownArray;
			*this << unknownArray;
		}

		// Info related to encrypted buffer
		int32 TestDataSize, CompressedChunkInfoOffset, LastBlockSize;
		*this << TestDataSize << CompressedChunkInfoOffset << LastBlockSize;

		int32 testA = 0, testB = 0, testC = 0;
		if (Summary.LicenseeVersion >= 33)
		{
			*this << testA << testB << testC;
		}

		bool bIsFullEncrypted = (Summary.PackageFlags & 0x0800) != 0;

		int HeaderStart = this->Tell();

		int EncryptedHeaderSize = Summary.HeadersSize - (LastBlockSize + HeaderStart);
		if (EncryptedHeaderSize <= 0)
			appError("Invalid Rocket League encrypted header size in %s", *GetFilename());

		int TestDataOffset = Summary.HeadersSize - HeaderStart - TestDataSize;

		byte* RawHeader = (byte*)appMalloc(EncryptedHeaderSize);
		Loader->Seek(HeaderStart);
		Loader->Serialize(RawHeader, EncryptedHeaderSize);

		byte FoundKey[32];
		memset(FoundKey, 0, sizeof(FoundKey));
		bool bKeyFound = false;

		byte HeaderNonce[12];
		memcpy(HeaderNonce, &testA, 4);
		memcpy(HeaderNonce + 4, &testB, 4);
		memcpy(HeaderNonce + 8, &testC, 4);

		TArray<FRocketLeagueKey>& KeyPool = FRocketLeagueKeyManager::GetKeys();
		byte* TestDecrypted = (byte*)appMalloc(EncryptedHeaderSize);

		for (int k = 0; k < KeyPool.Num(); k++)
		{
			memcpy(TestDecrypted, RawHeader, EncryptedHeaderSize);
			if (bIsFullEncrypted)
			{
				appDecryptAES_CTR(TestDecrypted, EncryptedHeaderSize, KeyPool[k].Key, HeaderNonce, 0);
			}
			else
			{
				int AlignedEncSize = EncryptedHeaderSize & ~15;
				appDecryptAES(TestDecrypted, AlignedEncSize, (const char*)KeyPool[k].Key, 32);
			}

			if (FRocketLeagueKeyManager::VerifyDecryptedPackageData(TestDecrypted, EncryptedHeaderSize, TestDataOffset))
			{
				memcpy(FoundKey, KeyPool[k].Key, 32);
				bKeyFound = true;
				appPrintf("Found AES key: '%s'\n", KeyPool[k].Line);
				break;
			}
		}

		appFree(TestDecrypted);
		appFree(RawHeader);

		if (!bKeyFound)
			appError("Unable to find a matching Rocket League AES key in aes.txt for %s", *GetFilename());

		// Create a reader to decrypt the rest of Rocket League's header
		FFileReaderRocketLeague* RocketReader = new FFileReaderRocketLeague(Loader);
		RocketReader->SetupFrom(*this);
		RocketReader->EncryptionStart = HeaderStart;
		RocketReader->EncryptionEnd = HeaderStart + EncryptedHeaderSize;
		RocketReader->bIsFullEncrypted = bIsFullEncrypted;
		memcpy(RocketReader->Key, FoundKey, 32);
		memcpy(RocketReader->HeaderNonce, HeaderNonce, 12);

		// Read chunk info
		RocketReader->Seek(RocketReader->EncryptionStart + CompressedChunkInfoOffset);

		int32 ChunkCount = 0;
		*RocketReader << ChunkCount;

		TArray<FCompressedChunk> Chunks;

		if (bIsFullEncrypted)
		{
			for (int i = 0; i < ChunkCount; i++)
			{
				int64 uo;
				int32 us;
				int64 co;
				int32 cs;
				byte chunkNonce[12];
				*RocketReader << uo << us << co << cs;
				RocketReader->Serialize(chunkNonce, 12);

				FCompressedChunk ch;
				ch.UncompressedOffset = (int)uo;
				ch.UncompressedSize = us;
				ch.CompressedOffset = (int)co;
				ch.CompressedSize = cs;
				Chunks.Add(ch);

				FRLEncryptedChunk encChunk;
				encChunk.CompressedOffset = co;
				encChunk.CompressedSize = cs;
				memcpy(encChunk.Nonce, chunkNonce, 12);
				RocketReader->Chunks.Add(encChunk);
			}
		}
		else
		{
			for (int i = 0; i < ChunkCount; i++)
			{
				// Even if no flag for AES CTR theres still a nonce
				if (ArLicenseeVer >= 33)
				{
					int64 uo;
					int32 us;
					int64 co;
					int32 cs;

					byte chunkNonce[12];
					*RocketReader << uo << us << co << cs;
					RocketReader->Serialize(chunkNonce, 12);

					FCompressedChunk ch;
					ch.UncompressedOffset = (int)uo;
					ch.UncompressedSize = us;
					ch.CompressedOffset = (int)co;
					ch.CompressedSize = cs;
					Chunks.Add(ch);
				}
				else
				{
					FCompressedChunk ch;
					*RocketReader << ch;
					Chunks.Add(ch);
				}
			}
		}

		if (Chunks.Num() > 0)
		{
			Loader = new FUE3ArchiveReader(RocketReader, COMPRESS_ZLIB, Chunks);
			Loader->SetupFrom(*this);
		}
		else
		{
			Loader = RocketReader;
		}

		// The decompressed chunks will overwrite past CompressedChunkInfoOffset, so don't decrypt past that anymore
		RocketReader->EncryptionEnd = RocketReader->EncryptionStart + CompressedChunkInfoOffset;
		return;
	}
#endif // ROCKET_LEAGUE

#if NURIEN
	// Nurien has encryption in header, and no encryption after
	FFileReaderNurien* NurienReader = Loader->CastTo<FFileReaderNurien>();
	if (NurienReader)
	{
		NurienReader->Threshold = Summary.HeadersSize;
		return;
	}
#endif // NURIEN

	unguard;
}
