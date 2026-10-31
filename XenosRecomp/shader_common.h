#ifndef SHADER_COMMON_H_INCLUDED
#define SHADER_COMMON_H_INCLUDED

#define SPEC_CONSTANT_R11G11B10_NORMAL  (1 << 0)
#define SPEC_CONSTANT_ALPHA_TEST        (1 << 1)

#ifdef UNLEASHED_RECOMP
    #define SPEC_CONSTANT_BICUBIC_GI_FILTER (1 << 2)
    #define SPEC_CONSTANT_ALPHA_TO_COVERAGE (1 << 3)
    #define SPEC_CONSTANT_REVERSE_Z         (1 << 4)
#endif

#if !defined(__cplusplus) || defined(__INTELLISENSE__)

#define FLT_MIN asfloat(0xff7fffff)
#define FLT_MAX asfloat(0x7f7fffff)

#ifdef __spirv__

struct PushConstants
{
    uint64_t VertexShaderConstants;
    uint64_t PixelShaderConstants;
    uint64_t SharedConstants;
};

[[vk::push_constant]] ConstantBuffer<PushConstants> g_PushConstants;

#define g_Booleans                 vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 256)
#define g_SwappedTexcoords         vk::RawBufferLoad<uint>(g_PushConstants.SharedConstants + 260)
#define g_HalfPixelOffset          vk::RawBufferLoad<float2>(g_PushConstants.SharedConstants + 264)
#define g_AlphaThreshold           vk::RawBufferLoad<float>(g_PushConstants.SharedConstants + 272)

[[vk::constant_id(0)]] const uint g_SpecConstants = 0;

#define g_SpecConstants() g_SpecConstants

#else

#define DEFINE_SHARED_CONSTANTS() \
    uint g_Booleans : packoffset(c16.x); \
    uint g_SwappedTexcoords : packoffset(c16.y); \
    float2 g_HalfPixelOffset : packoffset(c16.z); \
    float g_AlphaThreshold : packoffset(c17.x);

uint g_SpecConstants();

#endif

Texture2D<float4> g_Texture2DDescriptorHeap[] : register(t0, space0);
Texture3D<float4> g_Texture3DDescriptorHeap[] : register(t0, space1);
TextureCube<float4> g_TextureCubeDescriptorHeap[] : register(t0, space2);
SamplerState g_SamplerDescriptorHeap[] : register(s0, space3);
ByteAddressBuffer g_BufferDescriptorHeap[] : register(t0, space4);

uint2 getTexture2DDimensions(Texture2D<float4> texture)
{
    uint2 dimensions;
    texture.GetDimensions(dimensions.x, dimensions.y);
    return dimensions;
}

float4 tfetch2D(uint resourceDescriptorIndex, uint samplerDescriptorIndex, float2 texCoord, float2 offset)
{
    Texture2D<float4> texture = g_Texture2DDescriptorHeap[resourceDescriptorIndex];
#if __SHADER_TARGET_STAGE == __SHADER_STAGE_VERTEX
    return texture.SampleLevel(g_SamplerDescriptorHeap[samplerDescriptorIndex], texCoord + offset / getTexture2DDimensions(texture), 0);
#else
    return texture.Sample(g_SamplerDescriptorHeap[samplerDescriptorIndex], texCoord + offset / getTexture2DDimensions(texture));
#endif
}

float2 getWeights2D(uint resourceDescriptorIndex, uint samplerDescriptorIndex, float2 texCoord, float2 offset)
{
    Texture2D<float4> texture = g_Texture2DDescriptorHeap[resourceDescriptorIndex];
    return select(isnan(texCoord), 0.0, frac(texCoord * getTexture2DDimensions(texture) + offset - 0.5));
}

float w0(float a)
{
    return (1.0f / 6.0f) * (a * (a * (-a + 3.0f) - 3.0f) + 1.0f);
}

float w1(float a)
{
    return (1.0f / 6.0f) * (a * a * (3.0f * a - 6.0f) + 4.0f);
}

float w2(float a)
{
    return (1.0f / 6.0f) * (a * (a * (-3.0f * a + 3.0f) + 3.0f) + 1.0f);
}

float w3(float a)
{
    return (1.0f / 6.0f) * (a * a * a);
}

float g0(float a)
{
    return w0(a) + w1(a);
}

float g1(float a)
{
    return w2(a) + w3(a);
}

float h0(float a)
{
    return -1.0f + w1(a) / (w0(a) + w1(a)) + 0.5f;
}

float h1(float a)
{
    return 1.0f + w3(a) / (w2(a) + w3(a)) + 0.5f;
}

float4 tfetch2DBicubic(uint resourceDescriptorIndex, uint samplerDescriptorIndex, float2 texCoord, float2 offset)
{
    Texture2D<float4> texture = g_Texture2DDescriptorHeap[resourceDescriptorIndex];
    SamplerState samplerState = g_SamplerDescriptorHeap[samplerDescriptorIndex];
    uint2 dimensions = getTexture2DDimensions(texture);
    
    float x = texCoord.x * dimensions.x + offset.x;
    float y = texCoord.y * dimensions.y + offset.y;

    x -= 0.5f;
    y -= 0.5f;
    float px = floor(x);
    float py = floor(y);
    float fx = x - px;
    float fy = y - py;

    float g0x = g0(fx);
    float g1x = g1(fx);
    float h0x = h0(fx);
    float h1x = h1(fx);
    float h0y = h0(fy);
    float h1y = h1(fy);

    float4 r =
        g0(fy) * (g0x * texture.Sample(samplerState, float2(px + h0x, py + h0y) / float2(dimensions)) +
            g1x * texture.Sample(samplerState, float2(px + h1x, py + h0y) / float2(dimensions))) +
        g1(fy) * (g0x * texture.Sample(samplerState, float2(px + h0x, py + h1y) / float2(dimensions)) +
            g1x * texture.Sample(samplerState, float2(px + h1x, py + h1y) / float2(dimensions)));

    return r;
}

float4 tfetch3D(uint resourceDescriptorIndex, uint samplerDescriptorIndex, float3 texCoord)
{
    return g_Texture3DDescriptorHeap[resourceDescriptorIndex].Sample(g_SamplerDescriptorHeap[samplerDescriptorIndex], texCoord);
}

struct CubeMapData
{
    float3 cubeMapDirections[2];
    uint cubeMapIndex;
};

float4 tfetchCube(uint resourceDescriptorIndex, uint samplerDescriptorIndex, float3 texCoord, inout CubeMapData cubeMapData)
{
    return g_TextureCubeDescriptorHeap[resourceDescriptorIndex].Sample(g_SamplerDescriptorHeap[samplerDescriptorIndex], cubeMapData.cubeMapDirections[texCoord.z]);
}

#if 0
float4 tfetchRawFloat3(uint constIndex, uint strideWords, uint offsetWords, uint elementIndex)
{
    // 1. Grab the buffer from the descriptor heap using the Fetch Constant index (e.g., 31)
    ByteAddressBuffer buffer = g_BufferDescriptorHeap[constIndex];

    // 2. Calculate the byte offset. 
    // The Xenos instruction stores stride and offset in 32-bit Words, but ByteAddressBuffer requires Bytes.
    uint byteOffset = (elementIndex * strideWords + offsetWords) * 4;

    // 3. Load 3 consecutive 32-bit values (12 bytes total)
    uint3 rawData = buffer.Load3(byteOffset);

    // 4. Cast the raw bits to floats and pad the W component to 1.0 (standard vector behavior)
    return float4(asfloat(rawData.x), asfloat(rawData.y), asfloat(rawData.z), 1.0);
}
#endif
float4 tfetch_8_8_8_8(uint c, uint s, uint o, uint e) {
    uint v = g_BufferDescriptorHeap[c].Load((e * s + o) * 4);
    return float4(v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, v >> 24) / 255.0;
}
float4 tfetch_2_10_10_10(uint c, uint s, uint o, uint e) {
    uint v = g_BufferDescriptorHeap[c].Load((e * s + o) * 4);
    return float4(v & 0x3FF, (v >> 10) & 0x3FF, (v >> 20) & 0x3FF, v >> 30) / float4(1023.0, 1023.0, 1023.0, 3.0);
}
float4 tfetch_10_11_11(uint c, uint s, uint o, uint e) {
    uint v = g_BufferDescriptorHeap[c].Load((e * s + o) * 4);
    return float4(v & 0x7FF, (v >> 11) & 0x7FF, (v >> 22) & 0x3FF, 1.0) / float4(2047.0, 2047.0, 1023.0, 1.0);
}
float4 tfetch_11_11_10(uint c, uint s, uint o, uint e) {
    uint v = g_BufferDescriptorHeap[c].Load((e * s + o) * 4);
    return float4(v & 0x3FF, (v >> 10) & 0x7FF, (v >> 21) & 0x7FF, 1.0) / float4(1023.0, 2047.0, 2047.0, 1.0);
}
float4 tfetch_16_16_16_16(uint c, uint s, uint o, uint e) {
    uint2 v = g_BufferDescriptorHeap[c].Load2((e * s + o) * 4);
    return float4(v.x & 0xFFFF, v.x >> 16, v.y & 0xFFFF, v.y >> 16) / 65535.0;
}
float4 tfetch_16_16_16_16_FLOAT(uint c, uint s, uint o, uint e) {
    uint2 v = g_BufferDescriptorHeap[c].Load2((e * s + o) * 4);
    return float4(f16tof32(v.x & 0xFFFF), f16tof32(v.x >> 16), f16tof32(v.y & 0xFFFF), f16tof32(v.y >> 16));
}
float4 tfetch_32(uint c, uint s, uint o, uint e) {
    return float4(g_BufferDescriptorHeap[c].Load((e * s + o) * 4), 0.0, 0.0, 1.0);
}
float4 tfetch_32_32(uint c, uint s, uint o, uint e) {
    return float4(g_BufferDescriptorHeap[c].Load2((e * s + o) * 4), 0.0, 1.0);
}
float4 tfetch_32_32_32_32(uint c, uint s, uint o, uint e) {
    return float4(g_BufferDescriptorHeap[c].Load4((e * s + o) * 4));
}
float4 tfetch_32_FLOAT(uint c, uint s, uint o, uint e) {
    return float4(asfloat(g_BufferDescriptorHeap[c].Load((e * s + o) * 4)), 0.0, 0.0, 1.0);
}
float4 tfetch_32_32_FLOAT(uint c, uint s, uint o, uint e) {
    return float4(asfloat(g_BufferDescriptorHeap[c].Load2((e * s + o) * 4)), 0.0, 1.0);
}
float4 tfetch_32_32_32_FLOAT(uint c, uint s, uint o, uint e) {
    return float4(asfloat(g_BufferDescriptorHeap[c].Load3((e * s + o) * 4)), 1.0);
}
float4 tfetch_32_32_32_32_FLOAT(uint c, uint s, uint o, uint e) {
    return asfloat(g_BufferDescriptorHeap[c].Load4((e * s + o) * 4));
}

float4 tfetchR11G11B10(uint4 value)
{
    if (g_SpecConstants() & SPEC_CONSTANT_R11G11B10_NORMAL)
    {
        return float4(
            (value.x & 0x00000400 ? -1.0 : 0.0) + ((value.x & 0x3FF) / 1024.0),
            (value.x & 0x00200000 ? -1.0 : 0.0) + (((value.x >> 11) & 0x3FF) / 1024.0),
            (value.x & 0x80000000 ? -1.0 : 0.0) + (((value.x >> 22) & 0x1FF) / 512.0),
            0.0);
    }
    else
    {
        return asfloat(value);
    }
}

float4 tfetchTexcoord(uint swappedTexcoords, float4 value, uint semanticIndex)
{
    return (swappedTexcoords & (1ull << semanticIndex)) != 0 ? value.yxwz : value;
}

float4 cube(float4 value, inout CubeMapData cubeMapData)
{
    uint index = cubeMapData.cubeMapIndex;
    cubeMapData.cubeMapDirections[index] = value.xyz;
    ++cubeMapData.cubeMapIndex;
    
    return float4(0.0, 0.0, 0.0, index);
}

float4 dst(float4 src0, float4 src1)
{
    float4 dest;
    dest.x = 1.0;
    dest.y = src0.y * src1.y;
    dest.z = src0.z;
    dest.w = src1.w;
    return dest;
}

float4 max4(float4 src0)
{
    return max(max(src0.x, src0.y), max(src0.z, src0.w));
}

float2 getPixelCoord(uint resourceDescriptorIndex, float2 texCoord)
{
    return getTexture2DDimensions(g_Texture2DDescriptorHeap[resourceDescriptorIndex]) * texCoord;
}

float computeMipLevel(float2 pixelCoord)
{
    float2 dx = ddx(pixelCoord);
    float2 dy = ddy(pixelCoord);
    float deltaMaxSqr = max(dot(dx, dx), dot(dy, dy));
    return max(0.0, 0.5 * log2(deltaMaxSqr));
}

#endif

#endif
