//#version 420 // Keep it for editor detection

#if HAS_BINDING_LAYOUT
#define LAYOUT_BINDING(x) layout(binding = x)
#define LAYOUT_BINDING_FORMAT(format, x) layout(format, binding = x)
#define LAYOUT_LOCATION(x) layout(location = x)
#define LAYOUT_LOCATION_INDEXED(x, y) layout(location = x, index = y)
#else
#define LAYOUT_BINDING(x)
#define LAYOUT_BINDING_FORMAT(format, x) layout(format)
#define LAYOUT_LOCATION(x)
#define LAYOUT_LOCATION_INDEXED(x, y)
#endif

#if HAS_ENHANCED_LAYOUTS
#define INOUT_LOCATION(x) layout(location = x)
#else
#define INOUT_LOCATION(x)
#endif

//////////////////////////////////////////////////////////////////////
// Common Interface Definition
//////////////////////////////////////////////////////////////////////

#ifdef VERTEX_SHADER

#if !pGL_ES
out gl_PerVertex {
    vec4 gl_Position;
    float gl_PointSize;
#if !pGL_ES
    float gl_ClipDistance[1];
#endif
};
#endif

#endif



#ifdef GEOMETRY_SHADER

#if !pGL_ES
in gl_PerVertex {
    vec4 gl_Position;
    float gl_PointSize;
#if !pGL_ES
    float gl_ClipDistance[1];
#endif
} gl_in[];

out gl_PerVertex {
    vec4 gl_Position;
    float gl_PointSize;
#if !pGL_ES
    float gl_ClipDistance[1];
#endif
};
#endif

#endif

//////////////////////////////////////////////////////////////////////
// Constant Buffer Definition
//////////////////////////////////////////////////////////////////////
// Performance note, some drivers (nouveau) will validate all Constant Buffers
// even if only one was updated.

#if defined(VERTEX_SHADER) || defined(GEOMETRY_SHADER)
LAYOUT_BINDING_FORMAT(std140, 1) uniform cb20
{
    vec2  VertexScale;
    vec2  VertexOffset;

    vec2  TextureScale;
    vec2  TextureOffset;

    vec2  PointSize;
    uint  MaxDepth;
    uint  pad_cb20;
};
#endif

#if defined(VERTEX_SHADER) || defined(FRAGMENT_SHADER)
LAYOUT_BINDING_FORMAT(std140, 0) uniform cb21
{
    vec3 FogColor;
    float AREF;

    vec4 WH;

    vec2 TA;
    float MaxDepthPS;
    float Af;

    uvec4 MskFix;

    uvec4 FbMask;

    vec4 HalfTexel;

    vec4 MinMax;

    ivec4 ChannelShuffle;

    vec2 TC_OffsetHack;
    vec2 STScale;

    mat4 DitherMatrix;
};
#endif

//layout(std140, binding = 22) uniform cb22
//{
//    vec4 rt_size;
//};

//////////////////////////////////////////////////////////////////////
// Default Sampler
//////////////////////////////////////////////////////////////////////
#ifdef FRAGMENT_SHADER

LAYOUT_BINDING(0) uniform sampler2D TextureSampler;

#endif
