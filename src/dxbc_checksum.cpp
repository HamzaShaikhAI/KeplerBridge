#include "downgrader/dxbc_checksum.h"
#include <cstring>

namespace kepler {

namespace {

typedef uint32_t UINT4;

struct MD5Context {
    UINT4 i[2];               // Number of bits handled mod 2^64
    UINT4 buf[4];             // Scratch buffer
    uint8_t in[64];           // Input buffer
    uint8_t digest[16];       // Actual digest
};

static const uint8_t MD5_PADDING[64] = {
    0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

#define MD5_F(x, y, z) (((x) & (y)) | ((~x) & (z)))
#define MD5_G(x, y, z) (((x) & (z)) | ((y) & (~z)))
#define MD5_H(x, y, z) ((x) ^ (y) ^ (z))
#define MD5_I(x, y, z) ((y) ^ ((x) | (~z)))

#define ROTATE_LEFT(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

#define MD5_FF(a, b, c, d, x, s, ac) { \
    (a) += MD5_F((b), (c), (d)) + (x) + (UINT4)(ac); \
    (a) = ROTATE_LEFT((a), (s)); \
    (a) += (b); \
}

#define MD5_GG(a, b, c, d, x, s, ac) { \
    (a) += MD5_G((b), (c), (d)) + (x) + (UINT4)(ac); \
    (a) = ROTATE_LEFT((a), (s)); \
    (a) += (b); \
}

#define MD5_HH(a, b, c, d, x, s, ac) { \
    (a) += MD5_H((b), (c), (d)) + (x) + (UINT4)(ac); \
    (a) = ROTATE_LEFT((a), (s)); \
    (a) += (b); \
}

#define MD5_II(a, b, c, d, x, s, ac) { \
    (a) += MD5_I((b), (c), (d)) + (x) + (UINT4)(ac); \
    (a) = ROTATE_LEFT((a), (s)); \
    (a) += (b); \
}

static void MD5Transform(UINT4* buf, const UINT4* in) {
    UINT4 a = buf[0], b = buf[1], c = buf[2], d = buf[3];

    // Round 1
    MD5_FF(a, b, c, d, in[ 0],  7, 0xd76aa478);
    MD5_FF(d, a, b, c, in[ 1], 12, 0xe8c7b756);
    MD5_FF(c, d, a, b, in[ 2], 17, 0x242070db);
    MD5_FF(b, c, d, a, in[ 3], 22, 0xc1bdceee);
    MD5_FF(a, b, c, d, in[ 4],  7, 0xf57c0faf);
    MD5_FF(d, a, b, c, in[ 5], 12, 0x4787c62a);
    MD5_FF(c, d, a, b, in[ 6], 17, 0xa8304613);
    MD5_FF(b, c, d, a, in[ 7], 22, 0xfd469501);
    MD5_FF(a, b, c, d, in[ 8],  7, 0x698098d8);
    MD5_FF(d, a, b, c, in[ 9], 12, 0x8b44f7af);
    MD5_FF(c, d, a, b, in[10], 17, 0xffff5bb1);
    MD5_FF(b, c, d, a, in[11], 22, 0x895cd7be);
    MD5_FF(a, b, c, d, in[12],  7, 0x6b901122);
    MD5_FF(d, a, b, c, in[13], 12, 0xfd987193);
    MD5_FF(c, d, a, b, in[14], 17, 0xa679438e);
    MD5_FF(b, c, d, a, in[15], 22, 0x49b40821);

    // Round 2
    MD5_GG(a, b, c, d, in[ 1],  5, 0xf61e2562);
    MD5_GG(d, a, b, c, in[ 6],  9, 0xc040b340);
    MD5_GG(c, d, a, b, in[11], 14, 0x265e5a51);
    MD5_GG(b, c, d, a, in[ 0], 20, 0xe9b6c7aa);
    MD5_GG(a, b, c, d, in[ 5],  5, 0xd62f105d);
    MD5_GG(d, a, b, c, in[10],  9, 0x02441453);
    MD5_GG(c, d, a, b, in[15], 14, 0xd8a1e681);
    MD5_GG(b, c, d, a, in[ 4], 20, 0xe7d3fbc8);
    MD5_GG(a, b, c, d, in[ 9],  5, 0x21e1cde6);
    MD5_GG(d, a, b, c, in[14],  9, 0xc33707d6);
    MD5_GG(c, d, a, b, in[ 3], 14, 0xf4d50d87);
    MD5_GG(b, c, d, a, in[ 8], 20, 0x455a14ed);
    MD5_GG(a, b, c, d, in[13],  5, 0xa9e3e905);
    MD5_GG(d, a, b, c, in[ 2],  9, 0xfcefa3f8);
    MD5_GG(c, d, a, b, in[ 7], 14, 0x676f02d9);
    MD5_GG(b, c, d, a, in[12], 20, 0x8d2a4c8a);

    // Round 3
    MD5_HH(a, b, c, d, in[ 5],  4, 0xfffa3942);
    MD5_HH(d, a, b, c, in[ 8], 11, 0x8771f681);
    MD5_HH(c, d, a, b, in[11], 16, 0x6d9d6122);
    MD5_HH(b, c, d, a, in[14], 23, 0xfde5380c);
    MD5_HH(a, b, c, d, in[ 1],  4, 0xa4beea44);
    MD5_HH(d, a, b, c, in[ 4], 11, 0x4bdecfa9);
    MD5_HH(c, d, a, b, in[ 7], 16, 0xf6bb4b60);
    MD5_HH(b, c, d, a, in[10], 23, 0xbebfbc70);
    MD5_HH(a, b, c, d, in[13],  4, 0x289b7ec6);
    MD5_HH(d, a, b, c, in[ 0], 11, 0xeaa127fa);
    MD5_HH(c, d, a, b, in[ 3], 16, 0xd4ef3085);
    MD5_HH(b, c, d, a, in[ 6], 23, 0x04881d05);
    MD5_HH(a, b, c, d, in[ 9],  4, 0xd9d4d039);
    MD5_HH(d, a, b, c, in[12], 11, 0xe6db99e5);
    MD5_HH(c, d, a, b, in[15], 16, 0x1fa27cf8);
    MD5_HH(b, c, d, a, in[ 2], 23, 0xc4ac5665);

    // Round 4
    MD5_II(a, b, c, d, in[ 0],  6, 0xf4292244);
    MD5_II(d, a, b, c, in[ 7], 10, 0x432aff97);
    MD5_II(c, d, a, b, in[14], 15, 0xab9423a7);
    MD5_II(b, c, d, a, in[ 5], 21, 0xfc93a039);
    MD5_II(a, b, c, d, in[12],  6, 0x655b59c3);
    MD5_II(d, a, b, c, in[ 3], 10, 0x8f0ccc92);
    MD5_II(c, d, a, b, in[10], 15, 0xffeff47d);
    MD5_II(b, c, d, a, in[ 1], 21, 0x85845dd1);
    MD5_II(a, b, c, d, in[ 8],  6, 0x6fa87e4f);
    MD5_II(d, a, b, c, in[15], 10, 0xfe2ce6e0);
    MD5_II(c, d, a, b, in[ 6], 15, 0xa3014314);
    MD5_II(b, c, d, a, in[13], 21, 0x4e0811a1);
    MD5_II(a, b, c, d, in[ 4],  6, 0xf7537e82);
    MD5_II(d, a, b, c, in[11], 10, 0xbd3af235);
    MD5_II(c, d, a, b, in[ 2], 15, 0x2ad7d2bb);
    MD5_II(b, c, d, a, in[ 9], 21, 0xeb86d391);

    buf[0] += a;
    buf[1] += b;
    buf[2] += c;
    buf[3] += d;
}

static void MD5Init(MD5Context* ctx) {
    ctx->i[0] = ctx->i[1] = 0;
    ctx->buf[0] = 0x67452301;
    ctx->buf[1] = 0xefcdab89;
    ctx->buf[2] = 0x98badcfe;
    ctx->buf[3] = 0x10325476;
}

static void MD5Update(MD5Context* ctx, const uint8_t* inBuf, uint32_t inLen) {
    uint32_t inIdx = (ctx->i[0] >> 3) & 0x3F;
    if ((ctx->i[0] += ((UINT4)inLen << 3)) < ((UINT4)inLen << 3)) {
        ctx->i[1]++;
    }
    ctx->i[1] += ((UINT4)inLen >> 29);

    uint32_t fill = 64 - inIdx;
    if (inLen >= fill) {
        memcpy(&ctx->in[inIdx], inBuf, fill);
        MD5Transform(ctx->buf, reinterpret_cast<const UINT4*>(ctx->in));
        inBuf += fill;
        inLen -= fill;
        while (inLen >= 64) {
            MD5Transform(ctx->buf, reinterpret_cast<const UINT4*>(inBuf));
            inBuf += 64;
            inLen -= 64;
        }
        inIdx = 0;
    }
    if (inLen > 0) {
        memcpy(&ctx->in[inIdx], inBuf, inLen);
    }
}

} // anonymous namespace

bool CalculateDXBCChecksum(const void* pData, size_t size, uint32_t outHash[4]) {
    constexpr size_t HASH_OFFSET = 0x14; // 20 bytes
    if (!pData || size <= HASH_OFFSET || !outHash) {
        return false;
    }

    const uint8_t* pRaw = reinterpret_cast<const uint8_t*>(pData);
    size_t hashableSize = size - HASH_OFFSET;
    pRaw += HASH_OFFSET;

    MD5Context md5Ctx;
    MD5Init(&md5Ctx);

    uint32_t numberOfBits = static_cast<uint32_t>(hashableSize * 8);
    uint32_t fullChunksSize = static_cast<uint32_t>(hashableSize & ~0x3F); // multiple of 64

    if (fullChunksSize > 0) {
        MD5Update(&md5Ctx, pRaw, fullChunksSize);
    }

    uint32_t lastChunkSize = static_cast<uint32_t>(hashableSize - fullChunksSize);
    uint32_t paddingSize = 64 - lastChunkSize;
    const uint8_t* pLastChunkData = pRaw + fullChunksSize;

    if (lastChunkSize >= 56) {
        MD5Update(&md5Ctx, pLastChunkData, lastChunkSize);
        MD5Update(&md5Ctx, MD5_PADDING, paddingSize);

        UINT4 in[16];
        memset(in, 0, sizeof(in));
        in[0] = numberOfBits;
        in[15] = (numberOfBits >> 2) | 1;

        MD5Transform(md5Ctx.buf, in);
    } else {
        MD5Update(&md5Ctx, reinterpret_cast<const uint8_t*>(&numberOfBits), 4);
        if (lastChunkSize > 0) {
            MD5Update(&md5Ctx, pLastChunkData, lastChunkSize);
        }

        lastChunkSize += sizeof(uint32_t);
        paddingSize -= sizeof(uint32_t);

        memcpy(&md5Ctx.in[lastChunkSize], MD5_PADDING, paddingSize);
        reinterpret_cast<UINT4*>(md5Ctx.in)[15] = (numberOfBits >> 2) | 1;

        UINT4 in[16];
        memcpy(in, md5Ctx.in, 64);
        MD5Transform(md5Ctx.buf, in);
    }

    memcpy(outHash, md5Ctx.buf, 4 * sizeof(uint32_t));
    return true;
}

} // namespace kepler
