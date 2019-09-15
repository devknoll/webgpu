/*
 * MACHINE GENERATED, DO NOT EDIT
 * GENERATED BY nwgpu v0.0.1
 */
#include "index.h"


#include "GPU.h"
#include "GPUDevice.h"
#include "GPUAdapter.h"
#include "GPUQueue.h"
#include "GPUFence.h"
#include "GPUBuffer.h"

#include "BackendBinding.h"
#include "TerribleCommandBuffer.h"
#include <shaderc/shaderc.hpp>

static dawn_native::BackendType backendType = dawn_native::BackendType::D3D12;

DawnDevice device;
DawnQueue queue;
DawnSwapChain swapchain;
DawnRenderPipeline pipeline;

DawnTextureFormat swapChainFormat;

enum class CmdBufType {
  None,
  Terrible
};

static CmdBufType cmdBufType = CmdBufType::None;
static std::unique_ptr<dawn_native::Instance> instance;
static BackendBinding* binding = nullptr;

static GLFWwindow* window = nullptr;

static dawn_wire::WireServer* wireServer = nullptr;
static dawn_wire::WireClient* wireClient = nullptr;
static TerribleCommandBuffer* c2sBuf = nullptr;
static TerribleCommandBuffer* s2cBuf = nullptr;

enum class SingleShaderStage {
  Vertex,
  Fragment,
  Compute
};

void onDeviceError(DawnErrorType errorType, const char* message, void*) {
  switch (errorType) {
    case DAWN_ERROR_TYPE_VALIDATION:
      std::cout << "Validation ";
    break;
    case DAWN_ERROR_TYPE_OUT_OF_MEMORY:
      std::cout << "Out of memory ";
    break;
    case DAWN_ERROR_TYPE_UNKNOWN:
      std::cout << "Unknown ";
    break;
    case DAWN_ERROR_TYPE_DEVICE_LOST:
      std::cout << "Device lost ";
    break;
    default:
      return;
  }
  std::cout << "error: " << message << std::endl;
};

void onGLFWError(int code, const char* message) {
  std::cout << "GLFW error: " << code << " - " << message << std::endl;
};

dawn::Device CreateCppDawnDevice() {
  glfwSetErrorCallback(onGLFWError);

  if (!glfwInit()) return dawn::Device();

  if (backendType == dawn_native::BackendType::OpenGL) {
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 4);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  } else {
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  }

  window = glfwCreateWindow(640, 480, "NWGPU", nullptr, nullptr);
  if (!window) return dawn::Device();

  instance = std::make_unique<dawn_native::Instance>();

  if (backendType == dawn_native::BackendType::OpenGL) {
#if defined(DAWN_ENABLE_BACKEND_OPENGL)
  glfwMakeContextCurrent(window);
  dawn_native::opengl::AdapterDiscoveryOptions adapterOptions;
  adapterOptions.getProc = reinterpret_cast<void* (*)(const char*)>(glfwGetProcAddress);
  instance->DiscoverAdapters(&adapterOptions);
#endif
  } else {
    instance->DiscoverDefaultAdapters();
  }

  // Get an adapter for the backend to use, and create the device.
  dawn_native::Adapter backendAdapter;
  {
    std::vector<dawn_native::Adapter> adapters = instance->GetAdapters();
    auto adapterIt = std::find_if(
      adapters.begin(),
      adapters.end(),
      [](const dawn_native::Adapter adapter) -> bool {
      return adapter.GetBackendType() == backendType;
      }
    );
    if (adapterIt == adapters.end()) {
      std::cout << "No compatible adapter found!" << std::endl;
      return dawn::Device();
    }
    backendAdapter = *adapterIt;
  }

  DawnDevice backendDevice = backendAdapter.CreateDevice();
  DawnProcTable backendProcs = dawn_native::GetProcs();

  binding = CreateBinding(backendType, window, backendDevice);
  if (binding == nullptr) return dawn::Device();

  // Choose whether to use the backend procs and devices directly, or set up the wire.
  DawnDevice cDevice = nullptr;
  DawnProcTable procs;

  switch (cmdBufType) {
    case CmdBufType::None:
      procs = backendProcs;
      cDevice = backendDevice;
    break;
    case CmdBufType::Terrible:
    {
      c2sBuf = new TerribleCommandBuffer();
      s2cBuf = new TerribleCommandBuffer();

      dawn_wire::WireServerDescriptor serverDesc = {};
      serverDesc.device = backendDevice;
      serverDesc.procs = &backendProcs;
      serverDesc.serializer = s2cBuf;

      wireServer = new dawn_wire::WireServer(serverDesc);
      c2sBuf->SetHandler(wireServer);

      dawn_wire::WireClientDescriptor clientDesc = {};
      clientDesc.serializer = c2sBuf;

      wireClient = new dawn_wire::WireClient(clientDesc);
      DawnDevice clientDevice = wireClient->GetDevice();
      DawnProcTable clientProcs = wireClient->GetProcs();
      s2cBuf->SetHandler(wireClient);

      procs = clientProcs;
      cDevice = clientDevice;
    }
    break;
  };

  dawnSetProcs(&procs);
  procs.deviceSetUncapturedErrorCallback(cDevice, onDeviceError, nullptr);
  return dawn::Device::Acquire(cDevice);
}

dawn::ShaderModule CreateShaderModuleFromResult(const dawn::Device& device, const shaderc::SpvCompilationResult& result) {
  // result.cend and result.cbegin return pointers to uint32_t.
  const uint32_t* resultBegin = result.cbegin();
  const uint32_t* resultEnd = result.cend();
  // So this size is in units of sizeof(uint32_t).
  ptrdiff_t resultSize = resultEnd - resultBegin;
  // SetSource takes data as uint32_t*.

  dawn::ShaderModuleDescriptor descriptor;
  descriptor.codeSize = static_cast<uint32_t>(resultSize);
  descriptor.code = result.cbegin();
  return device.CreateShaderModule(&descriptor);
}

shaderc_shader_kind ShadercShaderKind(SingleShaderStage stage) {
  switch (stage) {
    case SingleShaderStage::Vertex:
      return shaderc_glsl_vertex_shader;
    case SingleShaderStage::Fragment:
      return shaderc_glsl_fragment_shader;
    case SingleShaderStage::Compute:
      return shaderc_glsl_compute_shader;
    default:
      return shaderc_glsl_compute_shader;
  }
}

dawn::ShaderModule CreateShaderModule(const dawn::Device& device, SingleShaderStage stage, const char* source) {
  shaderc_shader_kind kind = ShadercShaderKind(stage);
  shaderc::Compiler compiler;
  auto result = compiler.CompileGlslToSpv(source, strlen(source), kind, "myshader?");
  if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
    std::cerr << result.GetErrorMessage();
    return {};
  }
  return CreateShaderModuleFromResult(device, result);
}

void DoFlush() {
  if (cmdBufType == CmdBufType::Terrible) {
    c2sBuf->Flush();
    s2cBuf->Flush();
  }
  glfwPollEvents();
}

static Napi::Value onFrame(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  DawnTexture backbuffer = dawnSwapChainGetNextTexture(swapchain);
  DawnTextureView backbufferView = dawnTextureCreateView(backbuffer, nullptr);
  DawnRenderPassDescriptor renderpassInfo;
  DawnRenderPassColorAttachmentDescriptor colorAttachment;
  DawnRenderPassColorAttachmentDescriptor* colorAttachments = {&colorAttachment};
  {
    colorAttachment.attachment = backbufferView;
    colorAttachment.resolveTarget = nullptr;
    colorAttachment.clearColor = { 0.0f, 0.0f, 0.0f, 0.0f };
    colorAttachment.loadOp = DAWN_LOAD_OP_CLEAR;
    colorAttachment.storeOp = DAWN_STORE_OP_STORE;
    renderpassInfo.colorAttachmentCount = 1;
    renderpassInfo.colorAttachments = &colorAttachments;
    renderpassInfo.depthStencilAttachment = nullptr;
  }
  DawnCommandBuffer commands;
  {
    DawnCommandEncoder encoder = dawnDeviceCreateCommandEncoder(device, nullptr);

    DawnRenderPassEncoder pass = dawnCommandEncoderBeginRenderPass(encoder, &renderpassInfo);
    dawnRenderPassEncoderSetPipeline(pass, pipeline);
    dawnRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    dawnRenderPassEncoderEndPass(pass);
    dawnRenderPassEncoderRelease(pass);

    commands = dawnCommandEncoderFinish(encoder, nullptr);
    dawnCommandEncoderRelease(encoder);
  }

  dawnQueueSubmit(queue, 1, &commands);
  dawnCommandBufferRelease(commands);
  dawnSwapChainPresent(swapchain, backbuffer);
  dawnTextureViewRelease(backbufferView);

  DoFlush();
  return env.Undefined();
}

Napi::Value InitDawn(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  device = CreateCppDawnDevice().Release();
  queue = dawnDeviceCreateQueue(device);

  {
    DawnSwapChainDescriptor descriptor;
    descriptor.nextInChain = nullptr;
    descriptor.implementation = binding->GetSwapChainImplementation();
    swapchain = dawnDeviceCreateSwapChain(device, &descriptor);
  }

  DoFlush();
  swapChainFormat = static_cast<DawnTextureFormat>(binding->GetPreferredSwapChainTextureFormat());

  dawnSwapChainConfigure(swapchain, swapChainFormat, DAWN_TEXTURE_USAGE_OUTPUT_ATTACHMENT, 640, 480);

  const char* vs =
    "#version 450\n"
    "const vec2 pos[3] = vec2[3](vec2(0.0f, 0.5f), vec2(-0.5f, -0.5f), vec2(0.5f, -0.5f));\n"
    "void main() {\n"
    "  vec2 fpos = pos[gl_VertexIndex];\n"
    "  fpos.y *= -1.0;\n"
    "  gl_Position = vec4(fpos, 0.0, 1.0);\n"
    "}\n";

  const char* fs =
    "#version 450\n"
    "layout(location = 0) out vec4 fragColor;"
    "void main() {\n"
    "  fragColor = vec4(1.0, 0.0, 0.0, 1.0);\n"
    "}\n";

  DawnShaderModule vsModule = CreateShaderModule(dawn::Device(device), SingleShaderStage::Vertex, vs).Release();

  DawnShaderModule fsModule = CreateShaderModule(device, SingleShaderStage::Fragment, fs).Release();

  {
    DawnBuffer buffer;
    DawnBufferDescriptor descriptor;
    descriptor.size = 128u;
    descriptor.usage = static_cast<DawnBufferUsage>(0x0001);
    buffer = dawnDeviceCreateBuffer(device, &descriptor);
  }

  {
    DawnRenderPipelineDescriptor descriptor;
    descriptor.nextInChain = nullptr;

    descriptor.vertexStage.nextInChain = nullptr;
    descriptor.vertexStage.module = vsModule;
    descriptor.vertexStage.entryPoint = "main";

    DawnPipelineStageDescriptor fragmentStage;
    fragmentStage.nextInChain = nullptr;
    fragmentStage.module = fsModule;
    fragmentStage.entryPoint = "main";
    descriptor.fragmentStage = &fragmentStage;

    descriptor.sampleCount = 1;

    DawnBlendDescriptor blendDescriptor;
    blendDescriptor.operation = DAWN_BLEND_OPERATION_ADD;
    blendDescriptor.srcFactor = DAWN_BLEND_FACTOR_ONE;
    blendDescriptor.dstFactor = DAWN_BLEND_FACTOR_ONE;
    DawnColorStateDescriptor colorStateDescriptor;
    colorStateDescriptor.nextInChain = nullptr;
    colorStateDescriptor.format = swapChainFormat;
    colorStateDescriptor.alphaBlend = blendDescriptor;
    colorStateDescriptor.colorBlend = blendDescriptor;
    colorStateDescriptor.writeMask = DAWN_COLOR_WRITE_MASK_ALL;

    descriptor.colorStateCount = 1;
    DawnColorStateDescriptor* colorStatesPtr[] = {&colorStateDescriptor};
    descriptor.colorStates = colorStatesPtr;

    DawnPipelineLayoutDescriptor pl;
    pl.nextInChain = nullptr;
    pl.bindGroupLayoutCount = 0;
    pl.bindGroupLayouts = nullptr;
    descriptor.layout = dawnDeviceCreatePipelineLayout(device, &pl);

    DawnVertexInputDescriptor vertexInput;
    vertexInput.nextInChain = nullptr;
    vertexInput.indexFormat = DAWN_INDEX_FORMAT_UINT32;
    vertexInput.bufferCount = 0;
    vertexInput.buffers = nullptr;
    descriptor.vertexInput = &vertexInput;

    DawnRasterizationStateDescriptor rasterizationState;
    rasterizationState.nextInChain = nullptr;
    rasterizationState.frontFace = DAWN_FRONT_FACE_CCW;
    rasterizationState.cullMode = DAWN_CULL_MODE_NONE;
    rasterizationState.depthBias = 0;
    rasterizationState.depthBiasSlopeScale = 0.0;
    rasterizationState.depthBiasClamp = 0.0;
    descriptor.rasterizationState = &rasterizationState;

    descriptor.primitiveTopology = DAWN_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    descriptor.sampleMask = 0xFFFFFFFF;
    descriptor.alphaToCoverageEnabled = false;

    descriptor.depthStencilState = nullptr;

    pipeline = dawnDeviceCreateRenderPipeline(device, &descriptor);
  }

  dawnShaderModuleRelease(vsModule);
  dawnShaderModuleRelease(fsModule);

  return env.Undefined();
}

Napi::Object Init(Napi::Env env, Napi::Object exports) {

  GPU::Initialize(env, exports);
  GPUAdapter::Initialize(env, exports);
  GPUDevice::Initialize(env, exports);
  GPUQueue::Initialize(env, exports);
  GPUFence::Initialize(env, exports);
  GPUBuffer::Initialize(env, exports);

  exports["InitDawn"] = Napi::Function::New(env, InitDawn, "InitDawn");
  exports["onFrame"] = Napi::Function::New(env, onFrame, "onFrame");

  
  return exports;
}

NODE_API_MODULE(addon, Init)
