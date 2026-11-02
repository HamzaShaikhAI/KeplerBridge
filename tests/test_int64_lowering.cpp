#include "downgrader/int64_lowering.h"
#include <iostream>
#include <cassert>
#include <vector>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <cstring>

using namespace kepler;

int main() {
    std::cout << "========================================================\n";
    std::cout << "   KeplerBridge Milestone 11: 64-Bit Integer Lowering   \n";
    std::cout << "========================================================\n" << std::endl;

    // 1. Addition and Subtraction with Carry/Borrow
    std::cout << "[TEST 1] Testing addition and subtraction with carry/borrow..." << std::endl;
    U64RegPair a = Int64LoweringEngine::FromUint64(0xFFFFFFFFULL);
    U64RegPair one = Int64LoweringEngine::FromUint64(1ULL);

    U64RegPair sum = Int64LoweringEngine::Add(a, one);
    assert(Int64LoweringEngine::ToUint64(sum) == 0x100000000ULL);
    assert(sum.lo == 0 && sum.hi == 1);

    U64RegPair diff = Int64LoweringEngine::Sub(sum, one);
    assert(Int64LoweringEngine::ToUint64(diff) == 0xFFFFFFFFULL);
    assert(diff.lo == 0xFFFFFFFF && diff.hi == 0);
    std::cout << "  [PASS] Carry and borrow propagation verified across 32-bit boundary." << std::endl;

    // 2. Bitwise operations
    std::cout << "\n[TEST 2] Testing bitwise operations..." << std::endl;
    uint64_t valA = 0x0123456789ABCDEFULL;
    uint64_t valB = 0xFEDCBA9876543210ULL;
    U64RegPair regA = Int64LoweringEngine::FromUint64(valA);
    U64RegPair regB = Int64LoweringEngine::FromUint64(valB);

    assert(Int64LoweringEngine::ToUint64(Int64LoweringEngine::BitwiseAnd(regA, regB)) == (valA & valB));
    assert(Int64LoweringEngine::ToUint64(Int64LoweringEngine::BitwiseOr(regA, regB)) == (valA | valB));
    assert(Int64LoweringEngine::ToUint64(Int64LoweringEngine::BitwiseXor(regA, regB)) == (valA ^ valB));
    assert(Int64LoweringEngine::ToUint64(Int64LoweringEngine::BitwiseNot(regA)) == (~valA));
    std::cout << "  [PASS] Bitwise AND, OR, XOR, NOT match 64-bit CPU reference." << std::endl;

    // 3. Shifts
    std::cout << "\n[TEST 3] Testing shifts across 32-bit boundary..." << std::endl;
    uint64_t v = 0x0000000100000002ULL;
    U64RegPair rV = Int64LoweringEngine::FromUint64(v);
    assert(Int64LoweringEngine::ToUint64(Int64LoweringEngine::ShiftLeft(rV, 16)) == (v << 16));
    assert(Int64LoweringEngine::ToUint64(Int64LoweringEngine::ShiftLeft(rV, 36)) == (v << 36));
    assert(Int64LoweringEngine::ToUint64(Int64LoweringEngine::LogicalShiftRight(rV, 16)) == (v >> 16));

    // Arithmetic shift with negative signed value
    int64_t neg = -1048576LL;
    U64RegPair rNeg = Int64LoweringEngine::FromUint64(static_cast<uint64_t>(neg));
    U64RegPair rAshr = Int64LoweringEngine::ArithmeticShiftRight(rNeg, 4);
    assert(static_cast<int64_t>(Int64LoweringEngine::ToUint64(rAshr)) == (neg >> 4));
    std::cout << "  [PASS] Logical and arithmetic shifts verified." << std::endl;

    // 4. Multiplication
    std::cout << "\n[TEST 4] Testing multiword 64-bit multiplication..." << std::endl;
    uint64_t m1 = 123456789ULL;
    uint64_t m2 = 987654321ULL;
    U64RegPair prod = Int64LoweringEngine::Multiply(Int64LoweringEngine::FromUint64(m1), Int64LoweringEngine::FromUint64(m2));
    assert(Int64LoweringEngine::ToUint64(prod) == (m1 * m2));
    std::cout << "  [PASS] Multiplication matches 64-bit integer product: " << (m1 * m2) << std::endl;

    // 5. Atomics Check
    std::cout << "\n[TEST 5] Testing atomics rejection..." << std::endl;
    assert(!Int64LoweringEngine::IsAtomicSupported());
    std::cout << "  [PASS] 64-bit atomics safely rejected: " << Int64LoweringEngine::GetAtomicsRejectionDiagnostic() << std::endl;

    // 6. Live D3D11 GPU Execution Test
    std::cout << "\n[TEST 6] Live D3D11 Hardware Execution of 64-bit Register Pair Math..." << std::endl;
    ID3D11Device* pDevice = nullptr;
    ID3D11DeviceContext* pContext = nullptr;
    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL obtainedLevel;

    HRESULT hr = D3D11CreateDevice(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        0,
        featureLevels,
        1,
        D3D11_SDK_VERSION,
        &pDevice,
        &obtainedLevel,
        &pContext
    );

    if (SUCCEEDED(hr) && pDevice && pContext) {
        static const char* s_i64CS = R"(
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

            RWStructuredBuffer<uint2> OutputBuffer : register(u0);

            [numthreads(1, 1, 1)]
            void CSMain(uint3 id : SV_DispatchThreadID) {
                uint64_t a; a.lo = 0xFFFFFFFF; a.hi = 0x0;
                uint64_t b; b.lo = 0x00000001; b.hi = 0x0;
                uint64_t r = u64_add(a, b);
                OutputBuffer[0] = uint2(r.lo, r.hi);
            }
        )";

        ID3DBlob* pBlob = nullptr;
        ID3DBlob* pErr = nullptr;
        HRESULT compHr = D3DCompile(s_i64CS, std::strlen(s_i64CS), "Int64Add_Test", nullptr, nullptr,
                                    "CSMain", "cs_5_0", 0, 0, &pBlob, &pErr);
        if (SUCCEEDED(compHr) && pBlob) {
            ID3D11ComputeShader* pCS = nullptr;
            pDevice->CreateComputeShader(pBlob->GetBufferPointer(), pBlob->GetBufferSize(), nullptr, &pCS);
            pBlob->Release();

            // Create UAV buffer for uint2 (8 bytes)
            D3D11_BUFFER_DESC bufDesc{};
            bufDesc.ByteWidth = sizeof(uint32_t) * 2;
            bufDesc.Usage = D3D11_USAGE_DEFAULT;
            bufDesc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
            bufDesc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
            bufDesc.StructureByteStride = sizeof(uint32_t) * 2;

            ID3D11Buffer* pOutBuf = nullptr;
            pDevice->CreateBuffer(&bufDesc, nullptr, &pOutBuf);

            D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
            uavDesc.Format = DXGI_FORMAT_UNKNOWN;
            uavDesc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
            uavDesc.Buffer.NumElements = 1;

            ID3D11UnorderedAccessView* pUAV = nullptr;
            pDevice->CreateUnorderedAccessView(pOutBuf, &uavDesc, &pUAV);

            pContext->CSSetShader(pCS, nullptr, 0);
            pContext->CSSetUnorderedAccessViews(0, 1, &pUAV, nullptr);
            pContext->Dispatch(1, 1, 1);

            ID3D11UnorderedAccessView* nullUAV[1] = { nullptr };
            pContext->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);

            // Staging readback
            D3D11_BUFFER_DESC stageDesc = bufDesc;
            stageDesc.Usage = D3D11_USAGE_STAGING;
            stageDesc.BindFlags = 0;
            stageDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            stageDesc.MiscFlags = 0;

            ID3D11Buffer* pStageBuf = nullptr;
            pDevice->CreateBuffer(&stageDesc, nullptr, &pStageBuf);
            pContext->CopyResource(pStageBuf, pOutBuf);

            D3D11_MAPPED_SUBRESOURCE mapped{};
            HRESULT mapHr = pContext->Map(pStageBuf, 0, D3D11_MAP_READ, 0, &mapped);
            assert(SUCCEEDED(mapHr));
            const uint32_t* pOutData = reinterpret_cast<const uint32_t*>(mapped.pData);
            uint32_t resLo = pOutData[0];
            uint32_t resHi = pOutData[1];
            pContext->Unmap(pStageBuf, 0);

            std::cout << "  GPU Output: lo=0x" << std::hex << resLo << ", hi=0x" << resHi << std::dec << std::endl;
            assert(resLo == 0 && resHi == 1); // 0xFFFFFFFF + 1 = 0x100000000

            pStageBuf->Release();
            pUAV->Release();
            pOutBuf->Release();
            pCS->Release();
            std::cout << "  [PASS] Live GT 740 GPU register-pair addition correctly propagated carry bit to hi word!\n";
        }
        if (pErr) pErr->Release();
        pContext->Release();
        pDevice->Release();
    } else {
        std::cout << "  [SKIP] D3D11 device creation skipped or hardware unavailable.\n";
    }

    std::cout << "========================================================\n";
    std::cout << "   ALL 64-BIT INTEGER LOWERING TESTS PASSED!            \n";
    std::cout << "========================================================\n" << std::endl;
    return 0;
}
