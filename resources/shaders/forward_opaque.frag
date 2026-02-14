#version 460 core

layout(push_constant) uniform ForwardPushConstants
{
    mat4 mvp;
    vec4 tint;
    vec4 cameraPosition;
    uvec4 lightGrid;
} pc;

layout(set = 0, binding = 0) uniform sampler2D albedoTexture;

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

layout(std430, set = 0, binding = 1) readonly buffer LightBuffer
{
    GpuLight lights[];
};

layout(std430, set = 0, binding = 2) readonly buffer TileMetaBuffer
{
    TileMeta tiles[];
};

layout(std430, set = 0, binding = 3) readonly buffer TileIndexBuffer
{
    uint tileLightIndices[];
};

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragUV;
layout(location = 2) in vec3 fragWorldPos;
layout(location = 0) out vec4 outColor;

vec3 EvaluateSky(vec3 viewDir, vec3 sunDir)
{
    float t = clamp(viewDir.y*0.5 + 0.5, 0.0, 1.0);
    vec3 horizon = vec3(0.42, 0.52, 0.66);
    vec3 zenith = vec3(0.06, 0.13, 0.24);
    vec3 sky = mix(horizon, zenith, t);
    float sunAmount = max(dot(viewDir, sunDir), 0.0);
    sky += vec3(1.0, 0.88, 0.70) * pow(sunAmount, 192.0) * 4.0;
    sky += vec3(1.0, 0.93, 0.82) * pow(sunAmount, 48.0) * 0.18;
    return sky;
}

void main()
{
    vec3 sunDir = normalize(vec3(0.35, 0.82, 0.28));
    bool isSky = fragUV.x < -9000.0;
    if (isSky)
    {
        vec3 viewDir = normalize(fragWorldPos - pc.cameraPosition.xyz);
        vec3 skyColor = EvaluateSky(viewDir, sunDir);
        outColor = vec4(skyColor, 1.0);
        return;
    }

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
    vec3 directSun = vec3(1.0, 0.94, 0.86) * (1.05 * ndlSun);

    float hemi = clamp(N.y*0.5 + 0.5, 0.0, 1.0);
    vec3 ambientSky = mix(vec3(0.05, 0.055, 0.06), vec3(0.18, 0.24, 0.34), hemi);
    vec3 bounce = mix(vec3(0.015), groundColor * 0.26, clamp(N.y, 0.0, 1.0));
    vec3 gi = ambientSky + bounce;

    uint lightCount = pc.lightGrid.x;
    uint tileCountX = max(pc.lightGrid.y, 1u);
    uint tileCountY = max(pc.lightGrid.z, 1u);
    uint tileSize = max(pc.lightGrid.w, 1u);
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
