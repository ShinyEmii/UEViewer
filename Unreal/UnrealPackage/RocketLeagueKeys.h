#pragma once

#include "Core.h"

#if ROCKET_LEAGUE

struct FRocketLeagueKey
{
	byte Key[32];
	char Line[256];
};

class FRocketLeagueKeyManager
{
public:
	static const uint32 CRC32_POLY = 0x04C11DB7;

	static uint32 ComputeCRC32(const byte* Data, int Length, uint32 CRC = 0x0087636B)
	{
		static bool bInit = false;
		static uint32 Table[256];
		if (!bInit)
		{
			for (uint32 i = 0; i < 256; i++)
			{
				uint32 c = i << 24;
				for (int j = 8; j != 0; j--)
				{
					c = (c & 0x80000000) ? ((c << 1) ^ CRC32_POLY) : (c << 1);
				}
				Table[i] = c;
			}
			bInit = true;
		}

		CRC = ~CRC;
		for (int i = 0; i < Length; ++i)
		{
			CRC = (CRC << 8) ^ Table[(CRC >> 24) ^ Data[i]];
		}
		return ~CRC;
	}

	static bool Base64Decode(const char* Input, byte* Output, int OutputLen)
	{
		static const int b64_index[256] = {
			-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
			-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
			-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
			52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
			-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
			15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
			-1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
			41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
			-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
			-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
			-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
			-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
			-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
			-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
			-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
			-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
		};

		int len = (int)strlen(Input);
		int val = 0, valb = -8;
		int outPos = 0;

		for (int i = 0; i < len; i++)
		{
			unsigned char c = (unsigned char)Input[i];
			if (c == '=')
				break;

			int d = b64_index[c];
			if (d == -1)
				continue;

			val = (val << 6) + d;
			valb += 6;

			if (valb >= 0)
			{
				if (outPos < OutputLen)
					Output[outPos++] = (byte)((val >> valb) & 0xFF);

				valb -= 8;
			}
		}
		return (outPos == OutputLen);
	}

	static void Base64Encode(const byte* Input, int InputLen, char* Output, int OutputBufLen)
	{
		static const char b64_chars[] =
			"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
			"abcdefghijklmnopqrstuvwxyz"
			"0123456789+/";

		int outPos = 0;
		int i = 0;

		while (i + 3 <= InputLen && outPos + 4 < OutputBufLen)
		{
			uint32 chunk = (Input[i] << 16) | (Input[i + 1] << 8) | Input[i + 2];

			Output[outPos++] = b64_chars[(chunk >> 18) & 0x3F];
			Output[outPos++] = b64_chars[(chunk >> 12) & 0x3F];
			Output[outPos++] = b64_chars[(chunk >> 6) & 0x3F];
			Output[outPos++] = b64_chars[chunk & 0x3F];

			i += 3;
		}

		int remaining = InputLen - i;
		if (remaining > 0 && outPos + 4 < OutputBufLen)
		{
			uint32 chunk = Input[i] << 16;
			if (remaining == 2)
				chunk |= Input[i + 1] << 8;

			Output[outPos++] = b64_chars[(chunk >> 18) & 0x3F];
			Output[outPos++] = b64_chars[(chunk >> 12) & 0x3F];
			Output[outPos++] = (remaining == 2) ? b64_chars[(chunk >> 6) & 0x3F] : '=';
			Output[outPos++] = '=';
		}

		Output[outPos] = '\0';
	}

	// Just in case someone wants to add a way to pull keys fron ContentConfig in Psynet config
	static bool FromContentKey(const byte* ContentKey, byte* OutAESKey)
	{
		for (int i = 0; i < 32; i += 4)
		{
			uint32 val = ComputeCRC32(ContentKey + i, 4, 0x0087636B);
			memcpy(OutAESKey + i, &val, 4);
		}
		return true;
	}

	static TArray<FRocketLeagueKey>& GetKeys()
	{
		static TArray<FRocketLeagueKey> Keys;
		static bool bLoaded = false;
		if (!bLoaded)
		{
			bLoaded = true;
			LoadKeysFromLocalFile(Keys, "aes.txt");
		}
		return Keys;
	}

	static void AddKey(TArray<FRocketLeagueKey>& Keys, const byte* KeyBytes)
	{
		for (int i = 0; i < Keys.Num(); i++)
		{
			if (memcmp(Keys[i].Key, KeyBytes, 32) == 0)
				return;
		}
		FRocketLeagueKey k;
		memcpy(k.Key, KeyBytes, 32);
		Base64Encode(k.Key, 32, k.Line, sizeof(k.Line));
		Keys.Add(k);
	}

	static void AddBase64Key(TArray<FRocketLeagueKey>& Keys, const char* B64)
	{
		byte decoded[32];
		if (Base64Decode(B64, decoded, 32))
		{
			AddKey(Keys, decoded);
		}
	}

	static void AddContentKey(TArray<FRocketLeagueKey>& Keys, const char* ContentB64)
	{
		byte contentDecoded[32];
		if (Base64Decode(ContentB64, contentDecoded, 32))
		{
			byte aesKey[32];
			FromContentKey(contentDecoded, aesKey);
			AddKey(Keys, aesKey);
		}
	}

	static void LoadKeysFromLocalFile(TArray<FRocketLeagueKey>& Keys, const char* Filename)
	{
		FILE* f = fopen(Filename, "r");
		if (!f) return;

		char line[256];
		while (fgets(line, sizeof(line), f))
		{
			// Trim whitespace
			char* p = line;
			while (*p == ' ' || *p == '\t') p++;
			int slen = (int)strlen(p);
			while (slen > 0 && (p[slen-1] == '\r' || p[slen-1] == '\n' || p[slen-1] == ' '))
				p[--slen] = '\0';
			if (slen == 44) // base64-encoded 32 bytes
			{
				AddBase64Key(Keys, p);
			}
		}
		fclose(f);
	}

	// TestData is data to test if package was properly decrypted
	// Either incrementing values or fully 0s
	static bool VerifyDecryptedPackageData(const byte* DecryptedHeader, int HeaderSize, int TestDataOffset)
	{
		if (TestDataOffset < 0 || TestDataOffset >= HeaderSize - 1)
			return false;

		bool bFullZero = true;
		bool bCounting = true;

		for (int i = TestDataOffset; i < HeaderSize; ++i)
		{
			if (DecryptedHeader[i] != 0) {
				bFullZero = false;
				break;
			}
		}

		for (int i = TestDataOffset + 1; i < HeaderSize; ++i)
		{
			if (DecryptedHeader[i] != (byte)((DecryptedHeader[i - 1] + 1) % 255)) {
				bCounting = false;
				break;
			}
		}
		return bCounting || bFullZero;
	}
};

#endif // ROCKET_LEAGUE
