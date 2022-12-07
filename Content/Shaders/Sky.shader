--- 
includes :
  - Shaders/Lighting.glsl
defines : ~

depthAttachment :
- UNDEFINED

glslCommon: |
  #version 450


glslVertex: |
  layout(location=0) in vec3 inPosition;
  layout(location=1) in vec3 inNormal;
  layout(location=2) in vec2 inTexcoord;
  layout(location=3) in vec4 inColor;
  
  layout(location=0) out vec2 fragTexcoord;
  
  void main() 
  {
      gl_Position = vec4(inPosition, 1);
      fragTexcoord = inTexcoord;
  }
  
glslFragment: |
  layout(set = 0, binding = 0) uniform FrameData
  {
      mat4 view;
      mat4 projection;
      mat4 invProjection;
      vec4 cameraPosition;
      ivec2 viewportSize;
      float currentTime;
      float deltaTime;
  } frame;
  
  layout(set=1, binding=0) uniform PostProcessDataUBO
  {
    vec4 param;
  } data;
  
  layout(location = 0) in vec2 fragTexcoord;
  layout(location = 0) out vec4 outColor;
  
  #define INTEGRAL_STEPS 32
  #define INTEGRAL_STEPS_2 32
  
  const float R = 6371000.0f; // Earth radius in m
  const float AtmosphereR = 100000.0f; // Atmosphere radius
  
  float Density(float height)
  {
    // https://www.aboutdc.ru/page/1759.php
    // p = p0 · exp(–M·g·h/R·T)
    float p0 = 1013; // Presure at height = 0;
    float M = 28.98f; // Molar mass of gas
    float g = 9.81f; // Gravity
    float R = 8.314f; // Universal Gas const
    float T = 288.15f; // Temperature in K
    
    return p0 * exp((-M * g * height)/(R*T));
  }
  
  float S(float C, vec3 point) 
  {      
      float height = length(point) - R;
      return C * Density(height);
  }
  
  vec3 Extinction(vec3 point) 
  {
      // TODO: Find constants
      const vec3 Absorption = vec3(0.1f, 0.1f, 0.1f);
      const vec3 Diffusion = vec3(0.2f, 0.2f, 0.2f);
      
      return vec3(S(Absorption.x, point) + S(Diffusion.x, point),
                  S(Absorption.y, point) + S(Diffusion.y, point),
                  S(Absorption.z, point) + S(Diffusion.z, point));
  }
  
  vec3 Transfer(vec3 a, vec3 b)
  {
     vec3 step = (b-a) / INTEGRAL_STEPS;
     vec3 res = vec3(0.0f);
     vec3 fa = Extinction(a);
     
     for(uint i = 0; i < INTEGRAL_STEPS - 1; i++)
     {
         vec3 end = a + step * (i + 1);
         vec3 fb = Extinction(end);
         
         res += 0.5 * step * (fa + fb);
         
         fa = fb;
     }
     
     return exp(-res);
  }
  
  float Density(vec3 a, vec3 b, float H0)
  {
    float res = 0.0f;
    
    float heightA = length(a) - R;
    float heightB = length(b) - R;
    float stepH = (heightB - heightA) / INTEGRAL_STEPS;
    
    float step = length(b - a) / INTEGRAL_STEPS;
    
    /*for(uint i = 0; i < INTEGRAL_STEPS; i++)
    {
        res += exp(-(heightA + stepH * i)/H0) * stepH;
    }*/
    
    for(uint i = 0; i < INTEGRAL_STEPS; i++)
    {
        res += exp(-(heightA + stepH * i)/H0) * step;
    }
    
    return res;
  }
  
  float PhaseR(float cosAngle)
  {
    return ((3.0f * PI) / 16.0f) * (1.0f + cosAngle * cosAngle);
  }
  
  float PhaseMi(float cosAngle)
  {
    const float g = 9.81f;
    const float g2 = g*g;
    
    const float derived1 = 1 + g2 - 2*g * cosAngle;
    const float derived2 = (2 + g2) * sqrt(derived1 * derived1 * derived1);
    
    return ((3.0f * PI) / 8.0f) * ((g2 * (1 + cosAngle * cosAngle)) / derived2);
  }
  
  vec3 SkyLighting(vec3 origin, vec3 destination, vec3 lightDirection)
  {
     const float LightIntensity = 1.0f;
     const float Angle = dot(normalize(destination - origin), -lightDirection);     
           
     const vec3 step = (destination - origin) / INTEGRAL_STEPS_2;     
     
     // Constants for PhaseR
     const dvec3 B0R = dvec3(5.8, 13.5, 33.1) * dvec3(0.000001);
     const float H0R = 8000.0f;

     // Constants for PhaseMi
     const dvec3 B0Mi = dvec3(3.0, 3.0, 3.0) * dvec3(0.000001);
     const float H0Mi = 1200.0f;

     const float dh = (length(destination) - length(origin)) / INTEGRAL_STEPS_2;     
     const float dStep = length(step);
     
     vec3 resR = vec3(0.0f);
     vec3 resMi = vec3(0.0f);

     for(uint i = 0; i < INTEGRAL_STEPS_2 - 1; i++)
     {
         const vec3 point = origin + step * (i + 1);
         const float h = length(point) - R;
         
         const vec3 toLight = point - lightDirection * AtmosphereR;
         const float densityR = Density(point, origin, H0R) + Density(toLight, point, H0R);
         const float densityMi = Density(point, origin, H0Mi) + Density(toLight, point, H0Mi);

         resR += exp(-vec3(B0R * densityR)) * vec3(B0R * dStep * exp(-h/H0R));
         resMi += exp(-vec3(B0Mi * densityMi)) * vec3(B0Mi * dStep * exp(-h/H0Mi));
     }
     
    const vec3 final = LightIntensity * (resR * PhaseR(Angle) + resMi * PhaseMi(Angle));
    return final;
  }
  
  void main() 
  {
    // Calculate view direction
    vec4 dirViewSpace = vec4(0);
    dirViewSpace.xyz = normalize(ScreenToView(fragTexcoord.xy, -1.0f, frame.invProjection).xyz);    
    dirViewSpace.x *= -1;
    dirViewSpace = dirViewSpace * frame.view;

    // View position
    vec3 origin = vec3(0, R + 1000, 0) + frame.cameraPosition.xyz;
    vec3 rayDst = origin + dirViewSpace.xyz * AtmosphereR;    
    
    vec4 sunDirection = normalize(vec4(0,0, -1.0f, 0.0f));
    
    outColor.xyz = SkyLighting(origin, rayDst, sunDirection.xyz);
  }
