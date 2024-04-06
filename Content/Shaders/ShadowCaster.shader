---
defines:
- EVSM

includes:
- Shaders/Constants.glsl
- Shaders/Math.glsl
- Shaders/Lighting.glsl

glslCommon: |
  #version 460
  #extension GL_ARB_separate_shader_objects : enable
   
glslVertex: |
  layout(set = 0, binding = 0) uniform FrameData
  {
      mat4 view;
      mat4 projection;
      mat4 invProjection;
      vec4 cameraPosition;
      ivec2 viewportSize;
      vec2 cameraZNearZFar;
      float currentTime;
      float deltaTime;
  } frame;
  
  layout(set = 0, binding = 1) uniform PreviousFrameData
  {
      mat4 view;
      mat4 projection;
      mat4 invProjection;
      vec4 cameraPosition;
      ivec2 viewportSize;
      vec2 cameraZNearZFar;
      float currentTime;
      float deltaTime;
  } previousFrame;
  
  struct PerInstanceData
  {
      mat4 model;
  };
  
  layout(std430, push_constant) uniform Constants
  {
    mat4 lightMatrix;
  } PushConstants;
  
  layout(std140, set = 1, binding = 0) readonly buffer PerInstanceDataSSBO
  {
      PerInstanceData instance[];
  } data;
  
  layout(location=DefaultPositionBinding) in vec3 inPosition;
  
  void main() 
  {
      gl_Position = PushConstants.lightMatrix * data.instance[gl_InstanceIndex].model * vec4(inPosition, 1.0);
  }
  
glslFragment: |

  #if defined(EVSM)
    layout(location=0) out vec4 outDepth;
  #else
    layout(location=0) out float outDepth;
  #endif
  
  void main() 
  {
    #if defined(EVSM)
      outDepth.x = exp(EVSM_C1 * gl_FragCoord.z);
      outDepth.y = outDepth.x * outDepth.x;
      outDepth.z = -exp(-EVSM_C2 * gl_FragCoord.z);
      outDepth.w = outDepth.z * outDepth.z;
    #else 
      outDepth = gl_FragCoord.z;
    #endif
  }
