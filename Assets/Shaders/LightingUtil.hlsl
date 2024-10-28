#define MaxLights 16

struct Light
{
    float3 strength;        // ��ǿ��
    float falloffStart;     // ˥��   point/spot light only
    float3 direction;       // ����   directional/spot light only
    float falloffEnd;       // ˥��   point/spot light only
    float3 position;        // λ��   point/spot light only
    float spotPower;        // spot light only
};

struct Material
{
    float4 diffuseAlbedo;   // �����䷴����
    float3 fresnelR0;       // ��������(������)
    float snininess;        // �����,�������ֲڶ���һ�������෴������ snininess = 1 - rougness
};

// ʵ��һ������˥�����ӵļ��㷽���������ڵ��Դ�;۹��
float CalcAttenuation(float d, float falloffStart, float falloffEnd)
{
    return saturate((falloffEnd - d) / (falloffEnd - falloffStart));
}

// ������������̵�ʯ��˽��ơ��˺������ڹ�����L����淨��n֮��ļнǣ������ݷ�����ЧӦ���Ƶؼ������nΪ���ߵı����������İٷֱȡ�
// R0 = ((n-1)/(n+1))^2,ʽ�е�nΪ������
float3 SchlickFresnel(float3 R0, float3 normal, float3 lightVec)
{
    float cosIncidentAngle = saturate(dot(normal, lightVec));
    float f0 = 1.0f - cosIncidentAngle;
    float3 reflectPercent = R0 + (1.0f - R0) * (f0 * f0 * f0 * f0 * f0);
    return reflectPercent;
}

// ���㷴�䵽�۲������еĹ�������ֵΪ����������뾵��������ܺ�
float3 BlinnPhong(float3 lightStrength, float3 lightVec, float3 normal, float3 toEye, Material mat)
{
    const float m = mat.snininess * 256.0f;
    float3 halfVec = normalize(toEye + lightVec);   // �������
    
    float roughnessFactor = (m + 8.0f) * pow(max(dot(halfVec, normal), 0.0f), m) / 8.0f;    // �ֲڶ�����
    float3 fresnelFector = SchlickFresnel(mat.fresnelR0, halfVec, lightVec);                // ˥������
    
    float3 specAlbebo = roughnessFactor * fresnelFector;
    // �������ǽ��е���LDR(Low dynamic range���Ͷ�̬��Χ)����spec(���淴��)��ʽ�õ��Ľ���Իᳬ��[0,1],����ֽ��䰴������СһЩ
    specAlbebo = specAlbebo / (specAlbebo + 1.0f);
    
    return (mat.diffuseAlbedo.rgb + specAlbebo) * lightStrength;
}

// �����
float3 ComputeDirectionalLight(Light light, Material mat, float3 normal, float3 toEye)
{
    // ����������ߴ����ķ���պ��෴
    float3 lightVec = -light.direction;
    
    // ͨ���ʲ����Ҷ��ɰ��������͹�ǿ��
    float ndotl = max(dot(normal, lightVec), 0.0f);
    float3 lightStrength = light.strength * ndotl;
    
    return BlinnPhong(lightStrength, lightVec, normal, toEye, mat);
}

// ���Դ
float3 ComputePointLight(Light light, Material mat, float3 pos, float3 normal, float3 toEye)
{
    // �ӱ���ָ���Դ������
    float3 lightVec = light.position - pos;
    
    // �ӱ��浽��Դ�ľ���
    float d = length(lightVec);
    
    // ��Χ���
    if (d > light.falloffEnd)
        return 0.0f;
    
    lightVec /= d;
    
    // ͨ���ʲ����Ҷ��ɰ��������͹�ǿ��
    float ndotl = max(dot(normal, lightVec), 0.0f);
    float3 lightStrength = light.strength * ndotl;
    
    // ���ݾ��������˥��
    float att = CalcAttenuation(d, light.falloffStart, light.falloffEnd);
    lightStrength *= att;
    
    return BlinnPhong(lightStrength, lightVec, normal, toEye, mat);
}

// �۹��
float3 ComputeSpotLight(Light light, Material mat, float3 pos, float3 normal, float3 toEye)
{
    // �ӱ���ָ���Դ������
    float3 lightVec = light.position - pos;
    
    // �ӱ��浽��Դ�ľ���
    float d = length(lightVec);
    
    // ��Χ���
    if (d > light.falloffEnd)
        return 0.0f;
    
    lightVec /= d;
    
     // ͨ���ʲ����Ҷ��ɰ��������͹�ǿ��
    float ndotl = max(dot(normal, lightVec), 0.0f);
    float3 lightStrength = light.strength * ndotl;
    
    // ���ݾ��������˥��
    float att = CalcAttenuation(d, light.falloffStart, light.falloffEnd);
    lightStrength *= att;
    
    // ���ݾ۹������ģ�ͶԹ�ǿ�������Ŵ���
    float spotFactor = pow(max(dot(-lightVec, light.direction), 0.0f), light.spotPower);
    lightStrength *= spotFactor;
    
    return BlinnPhong(lightStrength, lightVec, normal, toEye, mat);
}

// ���Ӷ��ֹ���
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