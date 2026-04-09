
#include "../../include/druid.h"
#include <math.h>

// count is always a multiple of SIMD_WIDTH (4), no scalar remainder needed

#if defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64)
    #define DRUID_SSE2 1
    #include <xmmintrin.h>
    #include <emmintrin.h>
#endif

#if defined(__FMA__)
    #define DRUID_FMA 1
#endif

#if defined(__AVX2__)
    #define DRUID_AVX2 1
    #include <immintrin.h>
#endif

//=====================================================================================================================
// Binary array ops
//=====================================================================================================================

void simdAdd(const f32 *a, const f32 *b, f32 *out, u32 count)
{
#ifdef DRUID_SSE2
    for (u32 i = 0; i < count; i += 4)
        _mm_storeu_ps(out + i, _mm_add_ps(_mm_loadu_ps(a + i), _mm_loadu_ps(b + i)));
#else
    for (u32 i = 0; i < count; i++) out[i] = a[i] + b[i];
#endif
}

void simdSub(const f32 *a, const f32 *b, f32 *out, u32 count)
{
#ifdef DRUID_SSE2
    for (u32 i = 0; i < count; i += 4)
        _mm_storeu_ps(out + i, _mm_sub_ps(_mm_loadu_ps(a + i), _mm_loadu_ps(b + i)));
#else
    for (u32 i = 0; i < count; i++) out[i] = a[i] - b[i];
#endif
}

void simdMul(const f32 *a, const f32 *b, f32 *out, u32 count)
{
#ifdef DRUID_SSE2
    for (u32 i = 0; i < count; i += 4)
        _mm_storeu_ps(out + i, _mm_mul_ps(_mm_loadu_ps(a + i), _mm_loadu_ps(b + i)));
#else
    for (u32 i = 0; i < count; i++) out[i] = a[i] * b[i];
#endif
}

void simdDiv(const f32 *a, const f32 *b, f32 *out, u32 count)
{
#ifdef DRUID_SSE2
    for (u32 i = 0; i < count; i += 4)
        _mm_storeu_ps(out + i, _mm_div_ps(_mm_loadu_ps(a + i), _mm_loadu_ps(b + i)));
#else
    for (u32 i = 0; i < count; i++) out[i] = a[i] / b[i];
#endif
}

void simdMadd(const f32 *a, const f32 *b, const f32 *c, f32 *out, u32 count)
{
#ifdef DRUID_SSE2
    for (u32 i = 0; i < count; i += 4)
    {
        __m128 va = _mm_loadu_ps(a + i);
        __m128 vb = _mm_loadu_ps(b + i);
        __m128 vc = _mm_loadu_ps(c + i);
#ifdef DRUID_FMA
        _mm_storeu_ps(out + i, _mm_fmadd_ps(va, vb, vc));
#else
        _mm_storeu_ps(out + i, _mm_add_ps(_mm_mul_ps(va, vb), vc));
#endif
    }
#else
    for (u32 i = 0; i < count; i++) out[i] = a[i] * b[i] + c[i];
#endif
}

void simdMin(const f32 *a, const f32 *b, f32 *out, u32 count)
{
#ifdef DRUID_SSE2
    for (u32 i = 0; i < count; i += 4)
        _mm_storeu_ps(out + i, _mm_min_ps(_mm_loadu_ps(a + i), _mm_loadu_ps(b + i)));
#else
    for (u32 i = 0; i < count; i++) out[i] = a[i] < b[i] ? a[i] : b[i];
#endif
}

void simdMax(const f32 *a, const f32 *b, f32 *out, u32 count)
{
#ifdef DRUID_SSE2
    for (u32 i = 0; i < count; i += 4)
        _mm_storeu_ps(out + i, _mm_max_ps(_mm_loadu_ps(a + i), _mm_loadu_ps(b + i)));
#else
    for (u32 i = 0; i < count; i++) out[i] = a[i] > b[i] ? a[i] : b[i];
#endif
}

//=====================================================================================================================
// Unary array ops
//=====================================================================================================================

void simdNeg(const f32 *a, f32 *out, u32 count)
{
#ifdef DRUID_SSE2
    __m128 zero = _mm_setzero_ps();
    for (u32 i = 0; i < count; i += 4)
        _mm_storeu_ps(out + i, _mm_sub_ps(zero, _mm_loadu_ps(a + i)));
#else
    for (u32 i = 0; i < count; i++) out[i] = -a[i];
#endif
}

void simdAbs(const f32 *a, f32 *out, u32 count)
{
#ifdef DRUID_SSE2
    __m128 signMask = _mm_set1_ps(-0.0f);
    for (u32 i = 0; i < count; i += 4)
    {
        __m128 va = _mm_loadu_ps(a + i);
        _mm_storeu_ps(out + i, _mm_andnot_ps(signMask, va));
    }
#else
    for (u32 i = 0; i < count; i++) out[i] = fabsf(a[i]);
#endif
}

void simdSqrt(const f32 *a, f32 *out, u32 count)
{
#ifdef DRUID_SSE2
    for (u32 i = 0; i < count; i += 4)
        _mm_storeu_ps(out + i, _mm_sqrt_ps(_mm_loadu_ps(a + i)));
#else
    for (u32 i = 0; i < count; i++) out[i] = sqrtf(a[i]);
#endif
}

void simdRsqrt(const f32 *a, f32 *out, u32 count)
{
#ifdef DRUID_SSE2
    __m128 half = _mm_set1_ps(0.5f);
    __m128 three = _mm_set1_ps(3.0f);
    for (u32 i = 0; i < count; i += 4)
    {
        __m128 va = _mm_loadu_ps(a + i);
        __m128 est = _mm_rsqrt_ps(va);
        // Newton-Raphson refinement
        __m128 ae2 = _mm_mul_ps(va, _mm_mul_ps(est, est));
        est = _mm_mul_ps(_mm_mul_ps(est, _mm_sub_ps(three, ae2)), half);
        _mm_storeu_ps(out + i, est);
    }
#else
    for (u32 i = 0; i < count; i++) out[i] = 1.0f / sqrtf(a[i]);
#endif
}

void simdRcp(const f32 *a, f32 *out, u32 count)
{
#ifdef DRUID_SSE2
    __m128 two = _mm_set1_ps(2.0f);
    for (u32 i = 0; i < count; i += 4)
    {
        __m128 va = _mm_loadu_ps(a + i);
        __m128 est = _mm_rcp_ps(va);
        // Newton-Raphson refinement
        est = _mm_mul_ps(est, _mm_sub_ps(two, _mm_mul_ps(va, est)));
        _mm_storeu_ps(out + i, est);
    }
#else
    for (u32 i = 0; i < count; i++) out[i] = 1.0f / a[i];
#endif
}

//=====================================================================================================================
// Scalar broadcast ops
//=====================================================================================================================

void simdAddScalar(const f32 *a, f32 s, f32 *out, u32 count)
{
#ifdef DRUID_SSE2
    __m128 vs = _mm_set1_ps(s);
    for (u32 i = 0; i < count; i += 4)
    {
        _mm_storeu_ps(out + i, _mm_add_ps(_mm_loadu_ps(a + i), vs));
    }
#else
    for (u32 i = 0; i < count; i++) out[i] = a[i] + s;
#endif
}

void simdMulScalar(const f32 *a, f32 s, f32 *out, u32 count)
{
#ifdef DRUID_SSE2
    __m128 vs = _mm_set1_ps(s);
    for (u32 i = 0; i < count; i += 4)
    {
        _mm_storeu_ps(out + i, _mm_mul_ps(_mm_loadu_ps(a + i), vs));
    }
#else
    for (u32 i = 0; i < count; i++) out[i] = a[i] * s;
#endif
}

void simdClampScalar(const f32 *a, f32 lo, f32 hi, f32 *out, u32 count)
{
#ifdef DRUID_SSE2
    __m128 vlo = _mm_set1_ps(lo);
    __m128 vhi = _mm_set1_ps(hi);
    for (u32 i = 0; i < count; i += 4)
    {
        __m128 va = _mm_loadu_ps(a + i);
        _mm_storeu_ps(out + i, _mm_min_ps(_mm_max_ps(va, vlo), vhi));
    }
#else
    for (u32 i = 0; i < count; i++)
    {
        f32 v = a[i];
        out[i] = v < lo ? lo : (v > hi ? hi : v);
    }
#endif
}

void simdLerp(const f32 *a, const f32 *b, f32 t, f32 *out, u32 count)
{
#ifdef DRUID_SSE2
    __m128 vt = _mm_set1_ps(t);
    for (u32 i = 0; i < count; i += 4)
    {
        __m128 va = _mm_loadu_ps(a + i);
        __m128 vb = _mm_loadu_ps(b + i);
        __m128 diff = _mm_sub_ps(vb, va);
        _mm_storeu_ps(out + i, _mm_add_ps(va, _mm_mul_ps(diff, vt)));
    }
#else
    for (u32 i = 0; i < count; i++) out[i] = a[i] + t * (b[i] - a[i]);
#endif
}

//=====================================================================================================================
// SoA Vec3 ops
//=====================================================================================================================

void simdDot3(const f32 *ax, const f32 *ay, const f32 *az,
              const f32 *bx, const f32 *by, const f32 *bz, f32 *out, u32 count)
{
#ifdef DRUID_AVX2
    u32 i;
    for (i = 0; i + 8 <= count; i += 8)
    {
        __m256 axx = _mm256_loadu_ps(ax + i);
        __m256 ayy = _mm256_loadu_ps(ay + i);
        __m256 azz = _mm256_loadu_ps(az + i);
        __m256 bxx = _mm256_loadu_ps(bx + i);
        __m256 byy = _mm256_loadu_ps(by + i);
        __m256 bzz = _mm256_loadu_ps(bz + i);

        __m256 xx = _mm256_mul_ps(axx, bxx);
        __m256 yy = _mm256_mul_ps(ayy, byy);
        __m256 zz = _mm256_mul_ps(azz, bzz);

        _mm256_storeu_ps(out + i, _mm256_add_ps(_mm256_add_ps(xx, yy), zz));
    }
    // Scalar remainder
    while (i < count)
    {
        out[i] = ax[i] * bx[i] + ay[i] * by[i] + az[i] * bz[i];
        i++;
    }
#elif defined(DRUID_SSE2)
    for (u32 i = 0; i < count; i += 4)
    {
        __m128 xx = _mm_mul_ps(_mm_loadu_ps(ax + i), _mm_loadu_ps(bx + i));
        __m128 yy = _mm_mul_ps(_mm_loadu_ps(ay + i), _mm_loadu_ps(by + i));
        __m128 zz = _mm_mul_ps(_mm_loadu_ps(az + i), _mm_loadu_ps(bz + i));
        _mm_storeu_ps(out + i, _mm_add_ps(_mm_add_ps(xx, yy), zz));
    }
#else
    for (u32 i = 0; i < count; i++)
        out[i] = ax[i] * bx[i] + ay[i] * by[i] + az[i] * bz[i];
#endif
}

void simdCross3(const f32 *ax, const f32 *ay, const f32 *az,
                const f32 *bx, const f32 *by, const f32 *bz,
                f32 *outX, f32 *outY, f32 *outZ, u32 count)
{
#ifdef DRUID_AVX2
    u32 i;
    for (i = 0; i + 8 <= count; i += 8)
    {
        __m256 vax = _mm256_loadu_ps(ax + i), vay = _mm256_loadu_ps(ay + i), vaz = _mm256_loadu_ps(az + i);
        __m256 vbx = _mm256_loadu_ps(bx + i), vby = _mm256_loadu_ps(by + i), vbz = _mm256_loadu_ps(bz + i);
        _mm256_storeu_ps(outX + i, _mm256_sub_ps(_mm256_mul_ps(vay, vbz), _mm256_mul_ps(vaz, vby)));
        _mm256_storeu_ps(outY + i, _mm256_sub_ps(_mm256_mul_ps(vaz, vbx), _mm256_mul_ps(vax, vbz)));
        _mm256_storeu_ps(outZ + i, _mm256_sub_ps(_mm256_mul_ps(vax, vby), _mm256_mul_ps(vay, vbx)));
    }
    // Scalar remainder
    while (i < count)
    {
        outX[i] = ay[i] * bz[i] - az[i] * by[i];
        outY[i] = az[i] * bx[i] - ax[i] * bz[i];
        outZ[i] = ax[i] * by[i] - ay[i] * bx[i];
        i++;
    }
#elif defined(DRUID_SSE2)
    for (u32 i = 0; i < count; i += 4)
    {
        __m128 vax = _mm_loadu_ps(ax + i), vay = _mm_loadu_ps(ay + i), vaz = _mm_loadu_ps(az + i);
        __m128 vbx = _mm_loadu_ps(bx + i), vby = _mm_loadu_ps(by + i), vbz = _mm_loadu_ps(bz + i);
        _mm_storeu_ps(outX + i, _mm_sub_ps(_mm_mul_ps(vay, vbz), _mm_mul_ps(vaz, vby)));
        _mm_storeu_ps(outY + i, _mm_sub_ps(_mm_mul_ps(vaz, vbx), _mm_mul_ps(vax, vbz)));
        _mm_storeu_ps(outZ + i, _mm_sub_ps(_mm_mul_ps(vax, vby), _mm_mul_ps(vay, vbx)));
    }
#else
    for (u32 i = 0; i < count; i++)
    {
        outX[i] = ay[i] * bz[i] - az[i] * by[i];
        outY[i] = az[i] * bx[i] - ax[i] * bz[i];
        outZ[i] = ax[i] * by[i] - ay[i] * bx[i];
    }
#endif
}

void simdLengthSq3(const f32 *ax, const f32 *ay, const f32 *az, f32 *out, u32 count)
{
#ifdef DRUID_AVX2
    u32 i;
    for (i = 0; i + 8 <= count; i += 8)
    {
        __m256 vx = _mm256_loadu_ps(ax + i);
        __m256 vy = _mm256_loadu_ps(ay + i);
        __m256 vz = _mm256_loadu_ps(az + i);
        __m256 sq = _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(vx, vx), _mm256_mul_ps(vy, vy)),
                                  _mm256_mul_ps(vz, vz));
        _mm256_storeu_ps(out + i, sq);
    }
    // Scalar remainder
    while (i < count)
    {
        out[i] = ax[i] * ax[i] + ay[i] * ay[i] + az[i] * az[i];
        i++;
    }
#elif defined(DRUID_SSE2)
    for (u32 i = 0; i < count; i += 4)
    {
        __m128 vx = _mm_loadu_ps(ax + i);
        __m128 vy = _mm_loadu_ps(ay + i);
        __m128 vz = _mm_loadu_ps(az + i);
        __m128 sq = _mm_add_ps(_mm_add_ps(_mm_mul_ps(vx, vx), _mm_mul_ps(vy, vy)),
                               _mm_mul_ps(vz, vz));
        _mm_storeu_ps(out + i, sq);
    }
#else
    for (u32 i = 0; i < count; i++)
        out[i] = ax[i] * ax[i] + ay[i] * ay[i] + az[i] * az[i];
#endif
}

void simdLength3(const f32 *ax, const f32 *ay, const f32 *az, f32 *out, u32 count)
{
#ifdef DRUID_AVX2
    u32 i;
    for (i = 0; i + 8 <= count; i += 8)
    {
        __m256 vx = _mm256_loadu_ps(ax + i);
        __m256 vy = _mm256_loadu_ps(ay + i);
        __m256 vz = _mm256_loadu_ps(az + i);
        __m256 sq = _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(vx, vx), _mm256_mul_ps(vy, vy)),
                                  _mm256_mul_ps(vz, vz));
        _mm256_storeu_ps(out + i, _mm256_sqrt_ps(sq));
    }
    // Scalar remainder
    while (i < count)
    {
        out[i] = sqrtf(ax[i] * ax[i] + ay[i] * ay[i] + az[i] * az[i]);
        i++;
    }
#elif defined(DRUID_SSE2)
    for (u32 i = 0; i < count; i += 4)
    {
        __m128 vx = _mm_loadu_ps(ax + i);
        __m128 vy = _mm_loadu_ps(ay + i);
        __m128 vz = _mm_loadu_ps(az + i);
        __m128 sq = _mm_add_ps(_mm_add_ps(_mm_mul_ps(vx, vx), _mm_mul_ps(vy, vy)),
                               _mm_mul_ps(vz, vz));
        _mm_storeu_ps(out + i, _mm_sqrt_ps(sq));
    }
#else
    for (u32 i = 0; i < count; i++)
        out[i] = sqrtf(ax[i] * ax[i] + ay[i] * ay[i] + az[i] * az[i]);
#endif
}

void simdNormalize3(const f32 *inX, const f32 *inY, const f32 *inZ,
                    f32 *outX, f32 *outY, f32 *outZ, u32 count)
{
#ifdef DRUID_AVX2
    __m256 eps = _mm256_set1_ps(1e-8f);
    __m256 half = _mm256_set1_ps(0.5f);
    __m256 three = _mm256_set1_ps(3.0f);
    u32 i;
    for (i = 0; i + 8 <= count; i += 8)
    {
        __m256 vx = _mm256_loadu_ps(inX + i);
        __m256 vy = _mm256_loadu_ps(inY + i);
        __m256 vz = _mm256_loadu_ps(inZ + i);
        __m256 sq = _mm256_add_ps(_mm256_add_ps(_mm256_mul_ps(vx, vx), _mm256_mul_ps(vy, vy)),
                                  _mm256_mul_ps(vz, vz));

        // rsqrt + Newton-Raphson, masked by epsilon
        __m256 mask = _mm256_cmp_ps(sq, eps, _CMP_GT_OQ);
        __m256 est = _mm256_rsqrt_ps(sq);
        __m256 ae2 = _mm256_mul_ps(sq, _mm256_mul_ps(est, est));
        __m256 inv = _mm256_mul_ps(_mm256_mul_ps(est, _mm256_sub_ps(three, ae2)), half);
        inv = _mm256_and_ps(inv, mask);

        _mm256_storeu_ps(outX + i, _mm256_mul_ps(vx, inv));
        _mm256_storeu_ps(outY + i, _mm256_mul_ps(vy, inv));
        _mm256_storeu_ps(outZ + i, _mm256_mul_ps(vz, inv));
    }
    // Scalar remainder
    while (i < count)
    {
        f32 sq = inX[i] * inX[i] + inY[i] * inY[i] + inZ[i] * inZ[i];
        if (sq > 1e-8f)
        {
            f32 inv = 1.0f / sqrtf(sq);
            outX[i] = inX[i] * inv;
            outY[i] = inY[i] * inv;
            outZ[i] = inZ[i] * inv;
        }
        else
        {
            outX[i] = 0.0f;
            outY[i] = 0.0f;
            outZ[i] = 0.0f;
        }
        i++;
    }
#elif defined(DRUID_SSE2)
    __m128 eps = _mm_set1_ps(1e-8f);
    __m128 half = _mm_set1_ps(0.5f);
    __m128 three = _mm_set1_ps(3.0f);
    for (u32 i = 0; i < count; i += 4)
    {
        __m128 vx = _mm_loadu_ps(inX + i);
        __m128 vy = _mm_loadu_ps(inY + i);
        __m128 vz = _mm_loadu_ps(inZ + i);
        __m128 sq = _mm_add_ps(_mm_add_ps(_mm_mul_ps(vx, vx), _mm_mul_ps(vy, vy)),
                               _mm_mul_ps(vz, vz));

        // rsqrt + Newton-Raphson, masked by epsilon
        __m128 mask = _mm_cmpgt_ps(sq, eps);
        __m128 est = _mm_rsqrt_ps(sq);
        __m128 ae2 = _mm_mul_ps(sq, _mm_mul_ps(est, est));
        __m128 inv = _mm_mul_ps(_mm_mul_ps(est, _mm_sub_ps(three, ae2)), half);
        inv = _mm_and_ps(inv, mask);

        _mm_storeu_ps(outX + i, _mm_mul_ps(vx, inv));
        _mm_storeu_ps(outY + i, _mm_mul_ps(vy, inv));
        _mm_storeu_ps(outZ + i, _mm_mul_ps(vz, inv));
    }
#else
    for (u32 i = 0; i < count; i++)
    {
        f32 sq = inX[i] * inX[i] + inY[i] * inY[i] + inZ[i] * inZ[i];
        if (sq > 1e-8f)
        {
            f32 inv = 1.0f / sqrtf(sq);
            outX[i] = inX[i] * inv;
            outY[i] = inY[i] * inv;
            outZ[i] = inZ[i] * inv;
        }
        else
        {
            outX[i] = 0.0f;
            outY[i] = 0.0f;
            outZ[i] = 0.0f;
        }
    }
#endif
}

//=====================================================================================================================
// Reductions
//=====================================================================================================================

f32 simdSum(const f32 *a, u32 count)
{
#ifdef DRUID_SSE2
    __m128 acc = _mm_setzero_ps();
    for (u32 i = 0; i < count; i += 4)
        acc = _mm_add_ps(acc, _mm_loadu_ps(a + i));

    // horizontal sum
    __m128 shuf = _mm_shuffle_ps(acc, acc, _MM_SHUFFLE(2, 3, 0, 1));
    acc = _mm_add_ps(acc, shuf);
    shuf = _mm_shuffle_ps(acc, acc, _MM_SHUFFLE(0, 1, 2, 3));
    acc = _mm_add_ps(acc, shuf);

    f32 result;
    _mm_store_ss(&result, acc);
    return result;
#else
    f32 result = 0.0f;
    for (u32 i = 0; i < count; i++) result += a[i];
    return result;
#endif
}

f32 simdMinReduce(const f32 *a, u32 count)
{
    if (count == 0) return 0.0f;

#ifdef DRUID_SSE2
    __m128 acc = _mm_set1_ps(a[0]);
    for (u32 i = 0; i < count; i += 4)
        acc = _mm_min_ps(acc, _mm_loadu_ps(a + i));

    __m128 shuf = _mm_shuffle_ps(acc, acc, _MM_SHUFFLE(2, 3, 0, 1));
    acc = _mm_min_ps(acc, shuf);
    shuf = _mm_shuffle_ps(acc, acc, _MM_SHUFFLE(0, 1, 2, 3));
    acc = _mm_min_ps(acc, shuf);

    f32 result;
    _mm_store_ss(&result, acc);
    return result;
#else
    f32 result = a[0];
    for (u32 i = 1; i < count; i++) if (a[i] < result) result = a[i];
    return result;
#endif
}

f32 simdMaxReduce(const f32 *a, u32 count)
{
    if (count == 0) return 0.0f;

#ifdef DRUID_SSE2
    __m128 acc = _mm_set1_ps(a[0]);
    for (u32 i = 0; i < count; i += 4)
        acc = _mm_max_ps(acc, _mm_loadu_ps(a + i));

    __m128 shuf = _mm_shuffle_ps(acc, acc, _MM_SHUFFLE(2, 3, 0, 1));
    acc = _mm_max_ps(acc, shuf);
    shuf = _mm_shuffle_ps(acc, acc, _MM_SHUFFLE(0, 1, 2, 3));
    acc = _mm_max_ps(acc, shuf);

    f32 result;
    _mm_store_ss(&result, acc);
    return result;
#else
    f32 result = a[0];
    for (u32 i = 1; i < count; i++) if (a[i] > result) result = a[i];
    return result;
#endif
}
