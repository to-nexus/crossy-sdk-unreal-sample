#pragma once

#include "CoreMinimal.h"

/**
 * Minimal ERC-20 calldata encoder for the sample app.
 *
 * Only supports the two methods actually invoked by the sample panel:
 *   • balanceOf(address)       — selector 0x70a08231
 *   • transfer(address, uint256) — selector 0xa9059cbb
 *
 * Design notes:
 *   • Amount encoding uses a simple base-10 string → base-16 string pipeline
 *     so we avoid any dependency on a big-integer library. The sample test
 *     amounts (sub-token units) fit well within this approach.
 *   • All returned strings start with "0x" and use lowercase hex characters.
 *   • Validation is minimal; callers are expected to feed already-normalized
 *     hex addresses ("0x" + 40 hex chars). A malformed address produces an
 *     empty result and a warning log.
 *
 * External dApp teams moving toward production should replace this with a
 * proper ABI encoder (e.g. from a dedicated web3 library).
 */
namespace DappErc20Codec
{
	/**
	 * Build `balanceOf(address)` calldata.
	 * @return "0x70a08231" + 32-byte padded address, or an empty string on failure.
	 */
	FString EncodeBalanceOf(const FString& HolderAddress);

	/**
	 * Build `transfer(address,uint256)` calldata.
	 * @param ToAddress           "0x"-prefixed 20-byte address
	 * @param DecimalAmount       human-readable amount, e.g. "1.5"
	 * @param TokenDecimals       token decimals (usually 18)
	 * @return "0xa9059cbb" + padded address + padded uint256, empty on failure
	 */
	FString EncodeTransfer(const FString& ToAddress, const FString& DecimalAmount, int32 TokenDecimals);

	/**
	 * Parse an `eth_call` result (hex-encoded uint256) into a decimal string,
	 * rescaled by TokenDecimals. Returns "0" on failure.
	 */
	FString DecodeUint256BalanceHex(const FString& HexResult, int32 TokenDecimals);
}
