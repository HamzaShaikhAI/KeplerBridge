#include "downgrader/int64_lowering.h"
#include <sstream>

namespace kepler {

Int64LoweringEngine& Int64LoweringEngine::Instance() {
    static Int64LoweringEngine s_instance;
    return s_instance;
}

U64RegPair Int64LoweringEngine::FromUint64(uint64_t val) {
    U64RegPair r;
    r.lo = static_cast<uint32_t>(val & 0xFFFFFFFFULL);
    r.hi = static_cast<uint32_t>((val >> 32) & 0xFFFFFFFFULL);
    return r;
}

uint64_t Int64LoweringEngine::ToUint64(const U64RegPair& reg) {
    return (static_cast<uint64_t>(reg.hi) << 32) | static_cast<uint64_t>(reg.lo);
}

U64RegPair Int64LoweringEngine::Add(const U64RegPair& a, const U64RegPair& b) {
    U64RegPair r;
    r.lo = a.lo + b.lo;
    uint32_t carry = (r.lo < a.lo) ? 1 : 0;
    r.hi = a.hi + b.hi + carry;
    return r;
}

U64RegPair Int64LoweringEngine::Sub(const U64RegPair& a, const U64RegPair& b) {
    U64RegPair r;
    uint32_t borrow = (a.lo < b.lo) ? 1 : 0;
    r.lo = a.lo - b.lo;
    r.hi = a.hi - b.hi - borrow;
    return r;
}

U64RegPair Int64LoweringEngine::BitwiseAnd(const U64RegPair& a, const U64RegPair& b) {
    return { a.lo & b.lo, a.hi & b.hi };
}

U64RegPair Int64LoweringEngine::BitwiseOr(const U64RegPair& a, const U64RegPair& b) {
    return { a.lo | b.lo, a.hi | b.hi };
}

U64RegPair Int64LoweringEngine::BitwiseXor(const U64RegPair& a, const U64RegPair& b) {
    return { a.lo ^ b.lo, a.hi ^ b.hi };
}

U64RegPair Int64LoweringEngine::BitwiseNot(const U64RegPair& a) {
    return { ~a.lo, ~a.hi };
}

U64RegPair Int64LoweringEngine::ShiftLeft(const U64RegPair& a, uint32_t count) {
    U64RegPair r{};
    if (count == 0) return a;
    if (count < 32) {
        r.hi = (a.hi << count) | (a.lo >> (32 - count));
        r.lo = (a.lo << count);
    } else if (count < 64) {
        r.hi = (a.lo << (count - 32));
        r.lo = 0;
    }
    return r;
}

U64RegPair Int64LoweringEngine::LogicalShiftRight(const U64RegPair& a, uint32_t count) {
    U64RegPair r{};
    if (count == 0) return a;
    if (count < 32) {
        r.lo = (a.lo >> count) | (a.hi << (32 - count));
        r.hi = (a.hi >> count);
    } else if (count < 64) {
        r.lo = (a.hi >> (count - 32));
        r.hi = 0;
    }
    return r;
}

U64RegPair Int64LoweringEngine::ArithmeticShiftRight(const U64RegPair& a, uint32_t count) {
    U64RegPair r{};
    int32_t signedHi = static_cast<int32_t>(a.hi);
    if (count == 0) return a;
    if (count < 32) {
        r.lo = (a.lo >> count) | (a.hi << (32 - count));
        r.hi = static_cast<uint32_t>(signedHi >> count);
    } else if (count < 64) {
        r.lo = static_cast<uint32_t>(signedHi >> (count - 32));
        r.hi = (signedHi < 0) ? 0xFFFFFFFFU : 0;
    } else {
        uint32_t fill = (signedHi < 0) ? 0xFFFFFFFFU : 0;
        r.lo = fill;
        r.hi = fill;
    }
    return r;
}

bool Int64LoweringEngine::CompareEqual(const U64RegPair& a, const U64RegPair& b) {
    return (a.lo == b.lo && a.hi == b.hi);
}

bool Int64LoweringEngine::CompareUnsignedLess(const U64RegPair& a, const U64RegPair& b) {
    if (a.hi < b.hi) return true;
    if (a.hi == b.hi) return a.lo < b.lo;
    return false;
}

bool Int64LoweringEngine::CompareSignedLess(const U64RegPair& a, const U64RegPair& b) {
    int32_t sHiA = static_cast<int32_t>(a.hi);
    int32_t sHiB = static_cast<int32_t>(b.hi);
    if (sHiA < sHiB) return true;
    if (sHiA == sHiB) return a.lo < b.lo;
    return false;
}

U64RegPair Int64LoweringEngine::Multiply(const U64RegPair& a, const U64RegPair& b) {
    // 32x32 -> 64 expansion: (a.lo * b.lo) + (a.lo * b.hi + a.hi * b.lo) * 2^32
    uint64_t p0 = static_cast<uint64_t>(a.lo) * static_cast<uint64_t>(b.lo);
    uint32_t lo = static_cast<uint32_t>(p0 & 0xFFFFFFFFULL);
    uint32_t carry = static_cast<uint32_t>(p0 >> 32);

    uint32_t p1 = a.lo * b.hi;
    uint32_t p2 = a.hi * b.lo;
    uint32_t hi = carry + p1 + p2;

    return { lo, hi };
}

std::string Int64LoweringEngine::GenerateHlslInt64Library() {
    return R"(
        // KeplerBridge HLSL 64-bit Integer Emulation Library
        struct uint64_t {
            uint lo;
            uint hi;
        };

        uint64_t u64_add(uint64_t a, uint64_t b) {
            uint64_t r;
            r.lo = a.lo + b.lo;
            uint carry = (r.lo < a.lo) ? 1 : 0;
            r.hi = a.hi + b.hi + carry;
            return r;
        }

        uint64_t u64_sub(uint64_t a, uint64_t b) {
            uint64_t r;
            uint borrow = (a.lo < b.lo) ? 1 : 0;
            r.lo = a.lo - b.lo;
            r.hi = a.hi - b.hi - borrow;
            return r;
        }

        uint64_t u64_shl(uint64_t a, uint count) {
            uint64_t r;
            if (count == 0) return a;
            if (count < 32) {
                r.hi = (a.hi << count) | (a.lo >> (32 - count));
                r.lo = (a.lo << count);
            } else {
                r.hi = (a.lo << (count - 32));
                r.lo = 0;
            }
            return r;
        }

        uint64_t u64_mul(uint64_t a, uint64_t b) {
            uint a_lo16 = a.lo & 0xFFFF;
            uint a_hi16 = a.lo >> 16;
            uint b_lo16 = b.lo & 0xFFFF;
            uint b_hi16 = b.lo >> 16;

            uint p0 = a_lo16 * b_lo16;
            uint p1 = a_hi16 * b_lo16;
            uint p2 = a_lo16 * b_hi16;
            uint p3 = a_hi16 * b_hi16;

            uint mid = p1 + p2;
            uint carry = (mid < p1) ? 0x10000 : 0;
            uint lo = p0 + (mid << 16);
            if (lo < p0) carry++;

            uint hi = p3 + (mid >> 16) + carry + (a.lo * b.hi) + (a.hi * b.lo);
            uint64_t r;
            r.lo = lo;
            r.hi = hi;
            return r;
        }
    )";
}

} // namespace kepler
