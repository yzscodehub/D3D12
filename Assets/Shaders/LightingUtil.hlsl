#define MaxLights 16

struct Light
{
    float3 strength;        // 光强度
    float falloffStart;     // 衰减   point/spot light only
    float3 direction;       // 方向   directional/spot light only
    float falloffEnd;       // 衰减   point/spot light only
    float3 position;        // 位置   point/spot light only
    float spotPower;        // spot light only
};

struct Material
{
    float4 diffuseAlbedo;   // 漫反射反照率
    float3 fresnelR0;       // 材质属性(菲涅尔)
    float snininess;        // 光泽度,光泽度与粗糙度是一对性质相反的属性 snininess = 1 - rougness
};

// 实现一种线性衰减因子的计算方法，可用于点光源和聚光灯
float CalcAttenuation(float d, float falloffStart, float falloffEnd)
{
    return saturate((falloffEnd - d) / (falloffEnd - falloffStart));
}

// 代替菲涅尔方程的石里克近似。此函数基于光向量L与表面法线n之间的夹角，并根据菲涅尔效应近似地计算出以n为法线的表面所反射光的百分比。
// R0 = ((n-1)/(n+1))^2,式中的n为折射率
float3 SchlickFresnel(float3 R0, float3 normal, float3 lightVec)
{
    float cosIncidentAngle = saturate(dot(normal, lightVec));
    float f0 = 1.0f - cosIncidentAngle;
    float3 reflectPercent = R0 + (1.0f - R0) * (f0 * f0 * f0 * f0 * f0);
    return reflectPercent;
}

// 计算反射到观察者眼中的光量，该值为漫反射光量与镜面光量的总和
float3 BlinnPhong(float3 lightStrength, float3 lightVec, float3 normal, float3 toEye, Material mat)
{
    const float m = mat.snininess * 256.0f;
    float3 halfVec = normalize(toEye + lightVec);   // 半程向量
    
    float roughnessFactor = (m + 8.0f) * pow(max(dot(halfVec, normal), 0.0f), m) / 8.0f;    // 粗糙度因子
    float3 fresnelFector = SchlickFresnel(mat.fresnelR0, halfVec, lightVec);                // 衰减因子
    
    float3 specAlbebo = roughnessFactor * fresnelFector;
    // 尽管我们进行的是LDR(Low dynamic range，低动态范围)，但spec(镜面反射)公式得到的结果仍会超出[0,1],因此现将其按比例缩小一些
    specAlbebo = specAlbebo / (specAlbebo + 1.0f);
    
    return (mat.diffuseAlbedo.rgb + specAlbebo) * lightStrength;
}

// 方向光
float3 ComputeDirectionalLight(Light light, Material mat, float3 normal, float3 toEye)
{
    // 光向量与光线传播的方向刚好相反
    float3 lightVec = -light.direction;
    
    // 通过朗伯余弦定律按比例降低光强度
    float ndotl = max(dot(normal, lightVec), 0.0f);
    float3 lightStrength = light.strength * ndotl;
    
    return BlinnPhong(lightStrength, lightVec, normal, toEye, mat);
}

// 点光源
float3 ComputePointLight(Light light, Material mat, float3 pos, float3 normal, float3 toEye)
{
    // 从表面指向光源的向量
    float3 lightVec = light.position - pos;
    
    // 从表面到光源的距离
    float d = length(lightVec);
    
    // 范围检测
    if (d > light.falloffEnd)
        return 0.0f;
    
    lightVec /= d;
    
    // 通过朗伯余弦定律按比例降低光强度
    float ndotl = max(dot(normal, lightVec), 0.0f);
    float3 lightStrength = light.strength * ndotl;
    
    // 根据距离计算光的衰减
    float att = CalcAttenuation(d, light.falloffStart, light.falloffEnd);
    lightStrength *= att;
    
    return BlinnPhong(lightStrength, lightVec, normal, toEye, mat);
}

// 聚光灯
float3 ComputeSpotLight(Light light, Material mat, float3 pos, float3 normal, float3 toEye)
{
    // 从表面指向光源的向量
    float3 lightVec = light.position - pos;
    
    // 从表面到光源的距离
    float d = length(lightVec);
    
    // 范围检测
    if (d > light.falloffEnd)
        return 0.0f;
    
    lightVec /= d;
    
     // 通过朗伯余弦定律按比例降低光强度
    float ndotl = max(dot(normal, lightVec), 0.0f);
    float3 lightStrength = light.strength * ndotl;
    
    // 根据距离计算光的衰减
    float att = CalcAttenuation(d, light.falloffStart, light.falloffEnd);
    lightStrength *= att;
    
    // 根据聚光灯照明模型对光强进行缩放处理
    float spotFactor = pow(max(dot(-lightVec, light.direction), 0.0f), light.spotPower);
    lightStrength *= spotFactor;
    
    return BlinnPhong(lightStrength, lightVec, normal, toEye, mat);
}

// 叠加多种关照
float4 ComputeLighting(Light lights[MaxLights], Material mat,
                       float3 pos, float3 normal, float3 toEye, 
                       float3 shadowFactor)
{
    float3 result = 0.0f;
    
    int i = 0;

#if(NUM_DIR_LIGHTS > 0)
    for(i = 0; i < NUM_DIR_LIGHTS; ++i)
    {
        result += shadowFactor[i] * ComputeDirectionalLight(lights[i], mat, normal, toEye);
    }
#endif
    
#if(NUM_POINT_LIGHTS > 0)
    for(i = NUM_DIR_LIGHTS; i < NUM_DIR_LIGHTS + NUM_POINT_LIGHTS; ++i)
    {
        result += ComputePointLight(lights[i], mat, pos, normal, toEye);
    }
#endif
    
#if(NUM_SPOT_LIGHTS > 0)
    for(i = NUM_DIR_LIGHTS + NUM_POINT_LIGHTS; i < NUM_DIR_LIGHTS + NUM_POINT_LIGHTS + NUM_SPOT_LIGHTS; ++i)
    {
        result += ComputeSpotLight(lights[i], mat, pos, normal, toEye);
    }
#endif
    
    return float4(result, 0.0f);
}