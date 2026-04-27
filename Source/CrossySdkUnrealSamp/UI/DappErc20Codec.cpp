#include "UI/DappErc20Codec.h"

#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogDappErc20, Log, All);

namespace
{
	// Strip a leading "0x" prefix if present.
	FString StripHexPrefix(const FString& Hex)
	{
		return Hex.StartsWith(TEXT("0x"), ESearchCase::IgnoreCase) ? Hex.Mid(2) : Hex;
	}

	bool IsHexAddressValid(const FString& Address)
	{
		const FString Body = StripHexPrefix(Address);
		if (Body.Len() != 40) { return false; }
		for (TCHAR Ch : Body)
		{
			if (!FChar::IsHexDigit(Ch)) { return false; }
		}
		return true;
	}

	FString PadLeftToLength(const FString& In, int32 TargetLen, TCHAR Pad = '0')
	{
		if (In.Len() >= TargetLen) { return In; }
		return FString::ChrN(TargetLen - In.Len(), Pad) + In;
	}

	// Decimal-string integer multiplied by 10^N, returned as decimal string.
	FString MultiplyDecByPowerOfTen(const FString& Dec, int32 N)
	{
		if (N <= 0) { return Dec; }
		return Dec + FString::ChrN(N, TEXT('0'));
	}

	// Adds two non-negative decimal strings. Handles arbitrary length via schoolbook add.
	FString AddDec(const FString& A, const FString& B)
	{
		FString Out;
		int32 I = A.Len() - 1;
		int32 J = B.Len() - 1;
		int32 Carry = 0;
		while (I >= 0 || J >= 0 || Carry > 0)
		{
			int32 Sum = Carry;
			if (I >= 0) { Sum += A[I] - TEXT('0'); --I; }
			if (J >= 0) { Sum += B[J] - TEXT('0'); --J; }
			Carry = Sum / 10;
			Out.AppendChar(TEXT('0') + (Sum % 10));
		}
		// Reverse in place.
		for (int32 K = 0, L = Out.Len() - 1; K < L; ++K, --L)
		{
			Swap(Out[K], Out[L]);
		}
		return Out;
	}

	// Multiplies a non-negative decimal string by a single digit (0-9).
	FString MulDecByDigit(const FString& Dec, int32 Digit)
	{
		if (Digit == 0) { return TEXT("0"); }
		FString Out;
		int32 Carry = 0;
		for (int32 I = Dec.Len() - 1; I >= 0; --I)
		{
			const int32 D = Dec[I] - TEXT('0');
			const int32 M = D * Digit + Carry;
			Carry = M / 10;
			Out.AppendChar(TEXT('0') + (M % 10));
		}
		while (Carry > 0)
		{
			Out.AppendChar(TEXT('0') + (Carry % 10));
			Carry /= 10;
		}
		for (int32 K = 0, L = Out.Len() - 1; K < L; ++K, --L)
		{
			Swap(Out[K], Out[L]);
		}
		return Out;
	}

	// Convert "12.345" + decimals=18 → integer amount in smallest unit, as decimal string.
	bool DecimalToIntegerUnits(const FString& InDecimal, int32 TokenDecimals, FString& OutInt)
	{
		FString Trimmed = InDecimal;
		Trimmed.TrimStartAndEndInline();
		if (Trimmed.IsEmpty()) { return false; }

		FString IntPart, FracPart;
		int32 DotIdx = INDEX_NONE;
		if (Trimmed.FindChar(TEXT('.'), DotIdx))
		{
			IntPart  = Trimmed.Left(DotIdx);
			FracPart = Trimmed.Mid(DotIdx + 1);
		}
		else
		{
			IntPart  = Trimmed;
			FracPart = FString();
		}

		if (IntPart.IsEmpty()) { IntPart = TEXT("0"); }
		for (TCHAR C : IntPart)  { if (!FChar::IsDigit(C)) { return false; } }
		for (TCHAR C : FracPart) { if (!FChar::IsDigit(C)) { return false; } }

		if (FracPart.Len() > TokenDecimals)
		{
			// Too many fractional digits — drop the overflow (truncate, no rounding).
			FracPart = FracPart.Left(TokenDecimals);
		}
		const int32 PadZeros = TokenDecimals - FracPart.Len();

		// Result = IntPart * 10^decimals + FracPart * 10^(decimals - fracPartLen)
		const FString Scaled   = MultiplyDecByPowerOfTen(IntPart, TokenDecimals);
		const FString FracFull = FracPart.IsEmpty()
			? FString(TEXT("0"))
			: FracPart + FString::ChrN(PadZeros, TEXT('0'));

		OutInt = AddDec(Scaled, FracFull);
		// Strip leading zeros (but keep at least one digit).
		int32 Cut = 0;
		while (Cut < OutInt.Len() - 1 && OutInt[Cut] == TEXT('0')) { ++Cut; }
		OutInt.RightChopInline(Cut, false);
		return true;
	}

	// Divides a non-negative decimal string by 16; returns quotient + remainder (0-15).
	FString DivDecBy16(const FString& Dec, int32& OutRemainder)
	{
		FString Q;
		int32 Rem = 0;
		for (int32 I = 0; I < Dec.Len(); ++I)
		{
			const int32 D = Dec[I] - TEXT('0');
			const int32 Cur = Rem * 10 + D;
			Q.AppendChar(TEXT('0') + (Cur / 16));
			Rem = Cur % 16;
		}
		int32 Cut = 0;
		while (Cut < Q.Len() - 1 && Q[Cut] == TEXT('0')) { ++Cut; }
		Q.RightChopInline(Cut, false);
		OutRemainder = Rem;
		return Q;
	}

	// Convert non-negative decimal string to hex string (lowercase, no "0x").
	FString DecToHex(const FString& Dec)
	{
		FString Cur = Dec;
		if (Cur == TEXT("0")) { return TEXT("0"); }

		FString Hex;
		while (!(Cur.Len() == 1 && Cur[0] == TEXT('0')))
		{
			int32 Rem = 0;
			Cur = DivDecBy16(Cur, Rem);
			Hex.AppendChar(Rem < 10 ? (TEXT('0') + Rem) : (TEXT('a') + (Rem - 10)));
		}
		for (int32 K = 0, L = Hex.Len() - 1; K < L; ++K, --L)
		{
			Swap(Hex[K], Hex[L]);
		}
		return Hex;
	}

	// Convert hex string (no "0x") to decimal string.
	FString HexToDec(const FString& Hex)
	{
		FString Out(TEXT("0"));
		for (TCHAR Ch : Hex)
		{
			if (!FChar::IsHexDigit(Ch)) { return TEXT("0"); }
			const int32 Digit = FChar::IsDigit(Ch)
				? (Ch - TEXT('0'))
				: (FChar::ToLower(Ch) - TEXT('a') + 10);
			Out = MulDecByDigit(Out, 16);
			Out = AddDec(Out, FString::FromInt(Digit));
		}
		return Out;
	}

	// Insert a decimal point so that InDecString has `TokenDecimals` fractional digits.
	FString RescaleDecimal(const FString& IntegerDec, int32 TokenDecimals)
	{
		if (TokenDecimals <= 0) { return IntegerDec; }

		FString Padded = IntegerDec;
		if (Padded.Len() <= TokenDecimals)
		{
			Padded = FString::ChrN(TokenDecimals - Padded.Len() + 1, TEXT('0')) + Padded;
		}
		const int32 DotPos = Padded.Len() - TokenDecimals;
		FString Whole = Padded.Left(DotPos);
		FString Frac  = Padded.Mid(DotPos);

		// Trim trailing zeros from fractional part, and remove trailing dot.
		int32 TrimAt = Frac.Len();
		while (TrimAt > 0 && Frac[TrimAt - 1] == TEXT('0')) { --TrimAt; }
		Frac = Frac.Left(TrimAt);

		return Frac.IsEmpty() ? Whole : (Whole + TEXT(".") + Frac);
	}
}

FString DappErc20Codec::EncodeBalanceOf(const FString& HolderAddress)
{
	if (!IsHexAddressValid(HolderAddress))
	{
		UE_LOG(LogDappErc20, Warning, TEXT("EncodeBalanceOf: invalid address '%s'"), *HolderAddress);
		return FString();
	}
	const FString Padded = PadLeftToLength(StripHexPrefix(HolderAddress).ToLower(), 64);
	return FString(TEXT("0x70a08231")) + Padded;
}

FString DappErc20Codec::EncodeTransfer(const FString& ToAddress, const FString& DecimalAmount, int32 TokenDecimals)
{
	if (!IsHexAddressValid(ToAddress))
	{
		UE_LOG(LogDappErc20, Warning, TEXT("EncodeTransfer: invalid address '%s'"), *ToAddress);
		return FString();
	}
	FString IntegerUnits;
	if (!DecimalToIntegerUnits(DecimalAmount, TokenDecimals, IntegerUnits))
	{
		UE_LOG(LogDappErc20, Warning, TEXT("EncodeTransfer: invalid decimal amount '%s'"), *DecimalAmount);
		return FString();
	}
	const FString AmountHex  = PadLeftToLength(DecToHex(IntegerUnits), 64);
	const FString AddressHex = PadLeftToLength(StripHexPrefix(ToAddress).ToLower(), 64);
	return FString(TEXT("0xa9059cbb")) + AddressHex + AmountHex;
}

FString DappErc20Codec::DecodeUint256BalanceHex(const FString& HexResult, int32 TokenDecimals)
{
	FString Body = StripHexPrefix(HexResult);
	// Strip leading zeros but keep at least one digit.
	int32 Cut = 0;
	while (Cut < Body.Len() - 1 && Body[Cut] == TEXT('0')) { ++Cut; }
	Body.RightChopInline(Cut, false);
	if (Body.IsEmpty() || Body == TEXT("0"))
	{
		return TEXT("0");
	}
	const FString Integer = HexToDec(Body);
	return RescaleDecimal(Integer, TokenDecimals);
}
