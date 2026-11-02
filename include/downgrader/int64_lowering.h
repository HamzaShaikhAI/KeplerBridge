#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kepler {

struct U64RegPair {
    uint32_t lo = 0;
    uint32_t hi = 0;
};

class Int64LoweringEngine {
public:
    static Int64LoweringEngine& Instance();

    // Pure reference math
    static U64RegPair FromUint64(uint64_t val);
    static uint64_t ToUint64(const U64RegPair& reg);

    static U64RegPair Add(const U64RegPair& a, const U64RegPair& b);
    static U64RegPair Sub(const U64RegPair& a, const U64RegPair& b);

    static U64RegPair BitwiseAnd(const U64RegPair& a, const U64RegPair& b);
    static U64RegPair BitwiseOr(const U64RegPair& a, const U64RegPair& b);
    static U64RegPair BitwiseXor(const U64RegPair& a, const U64RegPair& b);
    static U64RegPair BitwiseNot(const U64RegPair& a);

    static U64RegPair ShiftLeft(const U64RegPair& a, uint32_t shiftCount);
    static U64RegPair LogicalShiftRight(const U64RegPair& a, uint32_t shiftCount);
    static U64RegPair ArithmeticShiftRight(const U64RegPair& a, uint32_t shiftCount);

    static bool CompareEqual(const U64RegPair& a, const U64RegPair& b);
    static bool CompareUnsignedLess(const U64RegPair& a, const U64RegPair& b);
    static bool CompareSignedLess(const U64RegPair& a, const U64RegPair& b);

    static U64RegPair Multiply(const U64RegPair& a, const U64RegPair& b);

    // Atomics check
    static bool IsAtomicSupported() { return false; }
    static std::string GetAtomicsRejectionDiagnostic() {
        return "64-bit integer atomics (InterlockedAdd64, InterlockedCompareExchange64) are unsupported on Direct3D 11 FL 11_0.";
    }

    // HLSL helper library for D3D11 CS 5.0 shader generation
    static std::string GenerateHlslInt64Library();

private:
    Int64LoweringEngine() = default;
    ~Int64LoweringEngine() = default;
};

} // namespace kepler
