#version 460 core

layout(push_constant) uniform ForwardPushConstants
{
    mat4 model;
    vec4 tint;
} pc;

layout(std140, set = 0, binding = 0) uniform FrameGlobals
{
    mat4 viewProj;
    vec4 cameraPosition;
    vec4 sunDirection;
    uvec4 lightGrid;
    vec4 frameParams;
} fg;

layout(set = 0, binding = 1) uniform sampler2D albedoTexture;

struct GpuLight
{
    vec4 positionRadius;
    vec4 colorIntensity;
};

struct TileMeta
{
    uint offset;
    uint count;
};

layout(std430, set = 0, binding = 2) readonly buffer LightBuffer
{
    GpuLight lights[];
};

layout(std430, set = 0, binding = 3) readonly buffer TileMetaBuffer
{
    TileMeta tiles[];
};

layout(std430, set = 0, binding = 4) readonly buffer TileIndexBuffer
{
    uint tileLightIndices[];
};

struct ShadowCascade
{
    mat4 worldToShadow;
    vec4 atlasRect;
    vec4 params;
};

layout(std140, set = 0, binding = 5) uniform ShadowGlobals
{
    ShadowCascade cascades[3];
    vec4 cameraForward;
    vec4 atlasTexelSize;
} shadowData;

layout(set = 0, binding = 6) uniform sampler2DShadow shadowAtlas;

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragUV;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 0) out vec4 outColor;

float SampleShadowCascade(uint cascadeIndex, vec3 worldPos, vec3 normal, vec3 sunDir)
{
    ShadowCascade cascade = shadowData.cascades[cascadeIndex];
    vec3 offsetWorld = worldPos + normal*cascade.params.w;
    vec4 shadowPos4 = cascade.worldToShadow*vec4(offsetWorld, 1.0);
    vec3 shadowPos = shadowPos4.xyz/max(shadowPos4.w, 0.0001);
    if (shadowPos.x <= 0.0 || shadowPos.x >= 1.0 || shadowPos.y <= 0.0 || shadowPos.y >= 1.0)
    {
        return 1.0;
    }

    vec2 atlasUv = cascade.atlasRect.xy + shadowPos.xy*cascade.atlasRect.zw;
    float ndl = max(dot(normal, sunDir), 0.0);
    float compareDepth = shadowPos.z - cascade.params.z*(1.0 - ndl);
    vec2 texel = shadowData.atlasTexelSize.xy;

    float visibility = 0.0;
    visibility += texture(shadowAtlas, vec3(atlasUv + texel*vec2(-0.5, -0.5), compareDepth));
    visibility += texture(shadowAtlas, vec3(atlasUv + texel*vec2(0.5, -0.5), compareDepth));
    visibility += texture(shadowAtlas, vec3(atlasUv + texel*vec2(-0.5, 0.5), compareDepth));
    visibility += texture(shadowAtlas, vec3(atlasUv + texel*vec2(0.5, 0.5), compareDepth));
    return visibility*0.25;
}

float EvaluateSunShadow(vec3 worldPos, vec3 normal, vec3 sunDir)
{
    uint cascadeCount = uint(shadowData.atlasTexelSize.z + 0.5);
    if (cascadeCount == 0u)
    {
        return 1.0;
    }

    vec3 cameraForward = normalize(shadowData.cameraForward.xyz);
    float viewDepth = dot(worldPos - fg.cameraPosition.xyz, cameraForward);
    if (viewDepth <= 0.0)
    {
        return 1.0;
    }

    uint cascadeIndex = cascadeCount - 1u;
    for (uint i = 0u; i < cascadeCount; ++i)
    {
        if (viewDepth <= shadowData.cascades[i].params.x)
        {
            cascadeIndex = i;
            break;
        }
    }

    float shadowA = SampleShadowCascade(cascadeIndex, worldPos, normal, sunDir);
    if ((cascadeIndex + 1u) >= cascadeCount)
    {
        return shadowA;
    }

    float blendStart = shadowData.cascades[cascadeIndex].params.y;
    float splitEnd = shadowData.cascades[cascadeIndex].params.x;
    if (viewDepth <= blendStart)
    {
        return shadowA;
    }

    float blendRange = max(splitEnd - blendStart, 0.0001);
    float t = clamp((viewDepth - blendStart)/blendRange, 0.0, 1.0);
    float shadowB = SampleShadowCascade(cascadeIndex + 1u, worldPos, normal, sunDir);
    return mix(shadowA, shadowB, t);
}

void main()
{
    vec3 sunDir = normalize(fg.sunDirection.xyz);
    vec3 N = normalize(fragNormal);

    const float checkerCellSize = 1.15;
    vec2 checkerCoord = floor(fragWorldPos.xz/checkerCellSize);
    float checker = abs(mod(checkerCoord.x + checkerCoord.y, 2.0));
    vec3 checkerA = vec3(0.11, 0.12, 0.13);
    vec3 checkerB = vec3(0.18, 0.19, 0.20);
    vec3 groundColor = mix(checkerA, checkerB, checker);

    float isGround = (fragWorldPos.y < -0.005) ? 1.0 : 0.0;
    vec3 meshColor = texture(albedoTexture, fragUV).rgb;
    vec3 baseAlbedo = mix(meshColor, groundColor, isGround);

    float ndlSun = max(dot(N, sunDir), 0.0);
    float sunShadow = EvaluateSunShadow(fragWorldPos, N, sunDir);
    vec3 directSun = vec3(1.0, 0.94, 0.86) * (1.05 * ndlSun * sunShadow);

    float hemi = clamp(N.y*0.5 + 0.5, 0.0, 1.0);
    vec3 ambientSky = mix(vec3(0.05, 0.055, 0.06), vec3(0.18, 0.24, 0.34), hemi);
    vec3 bounce = mix(vec3(0.015), groundColor * 0.26, clamp(N.y, 0.0, 1.0));
    vec3 gi = ambientSky + bounce;

    uint lightCount = fg.lightGrid.x;
    uint tileCountX = max(fg.lightGrid.y, 1u);
    uint tileCountY = max(fg.lightGrid.z, 1u);
    uint tileSize = max(fg.lightGrid.w, 1u);
    uvec2 pixel = uvec2(gl_FragCoord.xy);
    uint tileX = min(pixel.x / tileSize, tileCountX - 1u);
    uint tileY = min(pixel.y / tileSize, tileCountY - 1u);
    uint tileIndex = tileY * tileCountX + tileX;
    TileMeta tile = tiles[tileIndex];

    vec3 pointAccum = vec3(0.0);
    for (uint i = 0u; i < tile.count; ++i)
    {
        uint lightIndex = tileLightIndices[tile.offset + i];
        if (lightIndex >= lightCount)
        {
            continue;
        }

        GpuLight light = lights[lightIndex];
        vec3 toLight = light.positionRadius.xyz - fragWorldPos;
        float distanceSquared = dot(toLight, toLight);
        float radius = light.positionRadius.w;
        float radiusSquared = radius*radius;
        if (distanceSquared >= radiusSquared)
        {
            continue;
        }

        float distanceToLight = sqrt(max(distanceSquared, 0.00001));
        vec3 lightDir = toLight / distanceToLight;
        float ndl = max(dot(N, lightDir), 0.0);
        float attenuation = 1.0 - (distanceToLight / radius);
        attenuation = attenuation*attenuation;
        pointAccum += light.colorIntensity.rgb * (light.colorIntensity.a * ndl * attenuation);
    }

    vec3 lit = baseAlbedo * (gi + directSun + pointAccum);
    lit *= pc.tint.rgb;
    outColor = vec4(lit, pc.tint.a);
}
